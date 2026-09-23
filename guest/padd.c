#include <SDL2/SDL.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <glob.h>
#include <libevdev/libevdev-uinput.h>
#include <libevdev/libevdev.h>
#include <limits.h>
#include <linux/uinput.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/inotify.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <unistd.h>
#include "anyconsole.h"

#define DEVICES "/dev/input"
#define VIRTUAL_DEVICES "/sys/devices/virtual/"
#define NODE_MODE (S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH)
#define SEATS 4
#define CAPTURED_MAX 32
#define NO_JOYSTICK (-1)
#define PAD_NAME "Sony Interactive Entertainment DualSense Wireless Controller"
#define KEYBOARD_NAME "anyconsole keyboard"
#define PAD_VENDOR 0x054c
#define PAD_PRODUCT 0x0ce6
#define AXIS_MAX 255
#define AXIS_CENTER (AXIS_MAX / 2 + 1)
#define WORD_MAX 31
#define STRINGIFY_(x) #x
#define STRINGIFY(x) STRINGIFY_(x)

struct control {
    const char *name;
    unsigned short type, code, button;
    short direction;
};

#define KEY(name, code) {name, EV_KEY, code, 0, 0}
#define AXIS(name, code, button) {name, EV_ABS, code, button, 0}
#define HAT(name, code, direction) {name, EV_ABS, code, 0, direction}

static const struct control controls[] = {
    KEY("a", BTN_SOUTH), KEY("b", BTN_EAST), KEY("y", BTN_NORTH), KEY("x", BTN_WEST),
    KEY("leftshoulder", BTN_TL), KEY("rightshoulder", BTN_TR), KEY("leftstick", BTN_THUMBL), KEY("rightstick", BTN_THUMBR),
    KEY("back", BTN_SELECT), KEY("start", BTN_START), KEY("guide", BTN_MODE),
    KEY("touchpad", BTN_TRIGGER_HAPPY1), KEY("paddle1", BTN_TRIGGER_HAPPY2),
    KEY("paddle2", BTN_TRIGGER_HAPPY3), KEY("misc1", BTN_TRIGGER_HAPPY4),
    AXIS("leftx", ABS_X, 0), AXIS("lefty", ABS_Y, 0), AXIS("rightx", ABS_RX, 0), AXIS("righty", ABS_RY, 0),
    AXIS("lefttrigger", ABS_Z, BTN_TL2), AXIS("righttrigger", ABS_RZ, BTN_TR2),
    HAT("dpup", ABS_HAT0Y, -1), HAT("dpdown", ABS_HAT0Y, 1), HAT("dpleft", ABS_HAT0X, -1), HAT("dpright", ABS_HAT0X, 1),
};

struct captured {
    struct libevdev *device;
    char node[NAME_MAX + 1];
};

enum { HOTPLUG, FIRST_CAPTURED };

static struct libevdev_uinput *pad[SEATS], *guide, *keyboard;
static const struct control *swallowed[SEATS];
static int held[SEATS][SDL_arraysize(controls)];
static struct pollfd watched[FIRST_CAPTURED + CAPTURED_MAX];
static struct captured captured[FIRST_CAPTURED + CAPTURED_MAX];
static int watched_count;

static void emit(struct libevdev_uinput *device, unsigned type, unsigned code, int value) {
    libevdev_uinput_write_event(device, type, code, value);
    libevdev_uinput_write_event(device, EV_SYN, SYN_REPORT, 0);
}

static void expose(int fd, int seat, int create) {
    char sysname[UINPUT_MAX_NAME_SIZE], pattern[PATH_MAX], path[PATH_MAX];
    unsigned major, minor;
    glob_t found;
    if (ioctl(fd, UI_GET_SYSNAME(sizeof sysname), sysname) < 0) { perror("uinput sysname"); return; }
    snprintf(pattern, sizeof pattern, VIRTUAL_DEVICES "input/%s/*/dev", sysname);
    if (glob(pattern, 0, NULL, &found)) { fprintf(stderr, "%s: no device nodes\n", sysname); return; }
    for (size_t i = 0; i < found.gl_pathc; i++) {
        char *node = found.gl_pathv[i];
        *strrchr(node, '/') = '\0';
        node = strrchr(node, '/') + 1;
        if (!strncmp(node, "event", strlen("event"))) snprintf(path, sizeof path, INPUT_NODES "/%s", node);
        else if (!strncmp(node, "js", strlen("js")) && seat != NO_JOYSTICK) snprintf(path, sizeof path, PAD_NODES "/js%d", seat);
        else continue;
        if (!create) { unlink(path); continue; }
        strcat(found.gl_pathv[i], "/dev");
        FILE *dev = fopen(found.gl_pathv[i], "r");
        if (!dev || fscanf(dev, "%u:%u", &major, &minor) != 2) { perror(found.gl_pathv[i]); continue; }
        fclose(dev);
        if (mknod(path, S_IFCHR | NODE_MODE, makedev(major, minor)) < 0) perror(path);
    }
    globfree(&found);
}

static void announce(const char *id, const char *name, int present) {
    char path[PATH_MAX];
    snprintf(path, sizeof path, INPUTS_DIR "/%s %s", id, name);
    for (char *slash = strchr(path + strlen(INPUTS_DIR "/"), '/'); slash; slash = strchr(slash, '/')) *slash = '-';
    if (!present) { unlink(path); return; }
    int fd = open(path, O_WRONLY | O_CREAT | O_CLOEXEC, NODE_MODE);
    if (fd < 0) perror(path);
    else close(fd);
}

static int is_virtual(const char *path) {
    char link[PATH_MAX], resolved[PATH_MAX];
    snprintf(link, sizeof link, "/sys/class/input/%s/device", strrchr(path, '/') + 1);
    return realpath(link, resolved) && !strncmp(resolved, VIRTUAL_DEVICES, strlen(VIRTUAL_DEVICES));
}

static struct libevdev_uinput *create(struct libevdev *device, int seat) {
    struct libevdev_uinput *created;
    int error = libevdev_uinput_create_from_device(device, LIBEVDEV_UINPUT_OPEN_MANAGED, &created);
    if (error) { fprintf(stderr, "%s: %s\n", libevdev_get_name(device), strerror(-error)); exit(1); }
    libevdev_free(device);
    expose(libevdev_uinput_get_fd(created), seat, 1);
    return created;
}

static struct libevdev_uinput *create_guide(void) {
    struct libevdev *device = libevdev_new();
    libevdev_set_name(device, GUIDE_DEVICE);
    libevdev_enable_event_code(device, EV_KEY, BTN_MODE, NULL);
    return create(device, NO_JOYSTICK);
}

static struct libevdev_uinput *create_keyboard(void) {
    struct libevdev *device = libevdev_new();
    libevdev_set_name(device, KEYBOARD_NAME);
    for (unsigned code = KEY_ESC; code < BTN_MISC; code++) libevdev_enable_event_code(device, EV_KEY, code, NULL);
    for (unsigned code = BTN_MOUSE; code < BTN_JOYSTICK; code++) libevdev_enable_event_code(device, EV_KEY, code, NULL);
    for (unsigned code = 0; code <= REL_MAX; code++) libevdev_enable_event_code(device, EV_REL, code, NULL);
    return create(device, NO_JOYSTICK);
}

static void plug(int seat) {
    if (pad[seat]) return;
    struct libevdev *device = libevdev_new();
    libevdev_set_name(device, PAD_NAME);
    libevdev_set_id_bustype(device, BUS_USB);
    libevdev_set_id_vendor(device, PAD_VENDOR);
    libevdev_set_id_product(device, PAD_PRODUCT);
    for (const struct control *c = controls; c < controls + SDL_arraysize(controls); c++) {
        if (c->type == EV_KEY) { libevdev_enable_event_code(device, EV_KEY, c->code, NULL); continue; }
        if (c->button) libevdev_enable_event_code(device, EV_KEY, c->button, NULL);
        struct input_absinfo abs = c->direction ? (struct input_absinfo){.minimum = -1, .maximum = 1}
                                                : (struct input_absinfo){.value = c->button ? 0 : AXIS_CENTER, .maximum = AXIS_MAX};
        libevdev_enable_event_code(device, EV_ABS, c->code, &abs);
    }
    memset(held[seat], 0, sizeof held[seat]);
    pad[seat] = create(device, seat);
}

static void unplug(int seat) {
    if (!pad[seat]) return;
    expose(libevdev_uinput_get_fd(pad[seat]), seat, 0);
    libevdev_uinput_destroy(pad[seat]);
    pad[seat] = NULL;
}

static int hat(int seat, unsigned short code) {
    int value = 0;
    for (unsigned i = 0; i < SDL_arraysize(controls); i++)
        if (controls[i].code == code && held[seat][i]) value += controls[i].direction;
    return value;
}

static int holding(int seat, unsigned short code) {
    for (unsigned i = 0; i < SDL_arraysize(controls); i++)
        if (controls[i].code == code) return held[seat][i];
    return 0;
}

static void press_guide(int value) { emit(guide, EV_KEY, BTN_MODE, value); }

static unsigned short combo_partner(const struct control *c) {
    return c->code == BTN_SELECT ? BTN_START : c->code == BTN_START ? BTN_SELECT : 0;
}

static void drive(int seat, const char *name, int value) {
    if (!strcmp(name, "guide")) { press_guide(value); return; }
    if (!pad[seat]) { fprintf(stderr, "seat %d not plugged\n", seat + 1); return; }
    for (unsigned i = 0; i < SDL_arraysize(controls); i++) {
        const struct control *c = &controls[i];
        if (strcmp(c->name, name)) continue;
        if (c == swallowed[seat]) {
            if (value) return;
            swallowed[seat] = NULL;
            press_guide(0);
            return;
        }
        if (value && combo_partner(c) && holding(seat, combo_partner(c))) {
            swallowed[seat] = c;
            press_guide(1);
            return;
        }
        if (held[seat][i] == value) return;
        held[seat][i] = value;
        if (c->direction) value = hat(seat, c->code);
        emit(pad[seat], c->type, c->code, value);
        if (c->button) emit(pad[seat], EV_KEY, c->button, value > 0);
        return;
    }
    fprintf(stderr, "unknown control %s\n", name);
}

static int seat_of(const char *word) {
    int seat = atoi(word) - 1;
    return seat >= 0 && seat < SEATS ? seat : -1;
}

static void command(char *line) {
    char first[WORD_MAX + 1], second[WORD_MAX + 1];
    int value;
    int words = sscanf(line, "%" STRINGIFY(WORD_MAX) "s %" STRINGIFY(WORD_MAX) "s %d", first, second, &value);
    int seat = words >= 2 ? seat_of(words == 3 ? first : second) : -1;
    if (seat < 0) fprintf(stderr, "bad command: %s", line);
    else if (words == 3) drive(seat, second, value);
    else if (!strcmp(first, "plug")) plug(seat);
    else if (!strcmp(first, "unplug")) unplug(seat);
    else fprintf(stderr, "bad command: %s", line);
}

static void forward(unsigned type, unsigned code, int value) {
    if (type == EV_KEY && (code == KEY_LEFTMETA || code == KEY_RIGHTMETA)) press_guide(value > 0);
    else libevdev_uinput_write_event(keyboard, type, code, value);
}

static void inject(const char *line) {
    char type_name[WORD_MAX + 1], code_name[WORD_MAX + 1];
    int value, type, code;
    if (sscanf(line, "%" STRINGIFY(WORD_MAX) "s %" STRINGIFY(WORD_MAX) "s %d", type_name, code_name, &value) != 3 ||
        (type = libevdev_event_type_from_name(type_name)) < 0 || (code = libevdev_event_code_from_name(type, code_name)) < 0) {
        fprintf(stderr, "bad event: %s", line);
        return;
    }
    forward(type, code, value);
    forward(EV_SYN, SYN_REPORT, 0);
}

static int read_commands(void *unused) {
    (void)unused;
    FILE *commands = fopen(PAD_FIFO, "r+");
    if (!commands) { perror(PAD_FIFO); exit(1); }
    char line[BUFSIZ];
    while (fgets(line, sizeof line, commands)) {
        if (!strncmp(line, "EV_", strlen("EV_"))) { inject(line); continue; }
        SDL_Event event = {.user = {.type = SDL_USEREVENT, .data1 = strdup(line)}};
        SDL_PushEvent(&event);
    }
    exit(1);
}

static void capture(const char *node) {
    char path[PATH_MAX];
    struct libevdev *device;
    snprintf(path, sizeof path, DEVICES "/%s", node);
    if (strncmp(node, "event", strlen("event")) || is_virtual(path) || watched_count == (int)SDL_arraysize(watched)) return;
    int fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) { perror(path); return; }
    if (libevdev_new_from_fd(fd, &device) < 0) { close(fd); return; }
    if (!libevdev_has_event_code(device, EV_KEY, KEY_A) && !libevdev_has_event_code(device, EV_REL, REL_X)) {
        libevdev_free(device);
        close(fd);
        return;
    }
    libevdev_grab(device, LIBEVDEV_GRAB);
    watched[watched_count] = (struct pollfd){.fd = fd, .events = POLLIN};
    captured[watched_count].device = device;
    snprintf(captured[watched_count].node, sizeof captured[0].node, "%s", node);
    watched_count++;
    announce(node, libevdev_get_name(device), 1);
}

static void release(int index) {
    announce(captured[index].node, libevdev_get_name(captured[index].device), 0);
    libevdev_free(captured[index].device);
    close(watched[index].fd);
    watched_count--;
    watched[index] = watched[watched_count];
    captured[index] = captured[watched_count];
}

static void pass_through(int index) {
    struct input_event event;
    int status;
    while ((status = libevdev_next_event(captured[index].device, LIBEVDEV_READ_FLAG_NORMAL, &event)) >= 0) {
        forward(event.type, event.code, event.value);
        if (status == LIBEVDEV_READ_STATUS_SYNC)
            while (libevdev_next_event(captured[index].device, LIBEVDEV_READ_FLAG_SYNC, &event) == LIBEVDEV_READ_STATUS_SYNC)
                forward(event.type, event.code, event.value);
    }
    if (status != -EAGAIN) release(index);
}

static void read_hotplug(int fd) {
    char buffer[BUFSIZ] __attribute__((aligned(__alignof__(struct inotify_event))));
    ssize_t count = read(fd, buffer, sizeof buffer);
    for (char *at = buffer; count > 0 && at < buffer + count;) {
        struct inotify_event *event = (struct inotify_event *)at;
        if (event->len) capture(event->name);
        at += sizeof *event + event->len;
    }
}

static int capture_all(void *unused) {
    (void)unused;
    int hotplug = inotify_init1(IN_CLOEXEC);
    if (hotplug < 0 || inotify_add_watch(hotplug, DEVICES, IN_CREATE) < 0) { perror(DEVICES); exit(1); }
    watched[HOTPLUG] = (struct pollfd){.fd = hotplug, .events = POLLIN};
    watched_count = FIRST_CAPTURED;
    DIR *devices = opendir(DEVICES);
    for (struct dirent *entry; devices && (entry = readdir(devices));) capture(entry->d_name);
    if (devices) closedir(devices);
    for (;;) {
        if (poll(watched, watched_count, -1) < 0) continue;
        if (watched[HOTPLUG].revents) read_hotplug(hotplug);
        for (int i = watched_count - 1; i >= FIRST_CAPTURED; i--)
            if (watched[i].revents) pass_through(i);
    }
}

static int scaled(int value, int min) { return (value - min) * AXIS_MAX / (SDL_JOYSTICK_AXIS_MAX - min); }

static void announce_controller(SDL_GameController *controller, int present) {
    char id[NAME_MAX + 1];
    snprintf(id, sizeof id, "pad%d", SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(controller)));
    announce(id, SDL_GameControllerName(controller), present);
}

static void attach(int index) {
    const char *path = SDL_JoystickPathForIndex(index);
    if (path && is_virtual(path)) return;
    SDL_GameController *controller = SDL_GameControllerOpen(index);
    if (!controller) { fprintf(stderr, "%s: %s\n", SDL_JoystickNameForIndex(index), SDL_GetError()); return; }
    announce_controller(controller, 1);
}

static void detach(SDL_JoystickID instance) {
    SDL_GameController *controller = SDL_GameControllerFromInstanceID(instance);
    if (!controller) return;
    announce_controller(controller, 0);
    SDL_GameControllerClose(controller);
}

int main(void) {
    mkfifo(PAD_FIFO, S_IRUSR | S_IWUSR);
    setenv("SDL_JOYSTICK_DISABLE_UDEV", "1", 1);
    if (SDL_Init(SDL_INIT_GAMECONTROLLER) < 0) { fprintf(stderr, "SDL: %s\n", SDL_GetError()); return 1; }
    guide = create_guide();
    keyboard = create_keyboard();
    plug(0);
    SDL_CreateThread(read_commands, "commands", NULL);
    SDL_CreateThread(capture_all, "capture", NULL);
    for (SDL_Event event; SDL_WaitEvent(&event);) {
        switch (event.type) {
        case SDL_USEREVENT: command(event.user.data1); free(event.user.data1); break;
        case SDL_CONTROLLERDEVICEADDED: attach(event.cdevice.which); break;
        case SDL_CONTROLLERDEVICEREMOVED: detach(event.cdevice.which); break;
        case SDL_CONTROLLERBUTTONDOWN:
        case SDL_CONTROLLERBUTTONUP:
            drive(0, SDL_GameControllerGetStringForButton(event.cbutton.button), event.cbutton.state == SDL_PRESSED);
            break;
        case SDL_CONTROLLERAXISMOTION:
            drive(0, SDL_GameControllerGetStringForAxis(event.caxis.axis),
                  scaled(event.caxis.value, event.caxis.axis >= SDL_CONTROLLER_AXIS_TRIGGERLEFT ? 0 : SDL_JOYSTICK_AXIS_MIN));
            break;
        }
    }
    return 1;
}
