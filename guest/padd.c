#include <SDL2/SDL.h>
#include <fcntl.h>
#include <linux/uinput.h>
#include <dirent.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <unistd.h>

#define FIFO "/run/pad"
#define NODES "/run/pads"
#define SEATS 4
#define PAD_NAME "Sony Interactive Entertainment DualSense Wireless Controller"
#define PAD_VENDOR 0x054c
#define PAD_PRODUCT 0x0ce6
#define AXIS_MAX 255
#define AXIS_CENTER (AXIS_MAX / 2 + 1)
#define WORD_MAX 15
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
    KEY("cross", BTN_SOUTH), KEY("circle", BTN_EAST), KEY("triangle", BTN_NORTH), KEY("square", BTN_WEST),
    KEY("l1", BTN_TL), KEY("r1", BTN_TR), KEY("l3", BTN_THUMBL), KEY("r3", BTN_THUMBR),
    KEY("create", BTN_SELECT), KEY("options", BTN_START), KEY("ps", BTN_MODE),
    KEY("touch", BTN_TRIGGER_HAPPY1), KEY("touch-left", BTN_TRIGGER_HAPPY2),
    KEY("touch-right", BTN_TRIGGER_HAPPY3), KEY("mute", BTN_TRIGGER_HAPPY4),
    AXIS("lx", ABS_X, 0), AXIS("ly", ABS_Y, 0), AXIS("rx", ABS_RX, 0), AXIS("ry", ABS_RY, 0),
    AXIS("l2", ABS_Z, BTN_TL2), AXIS("r2", ABS_RZ, BTN_TR2),
    HAT("up", ABS_HAT0Y, -1), HAT("down", ABS_HAT0Y, 1), HAT("left", ABS_HAT0X, -1), HAT("right", ABS_HAT0X, 1),
};
#define CONTROLS (sizeof controls / sizeof *controls)

static int pad[SEATS];
static int held[SEATS][CONTROLS];
static const char *const sdl_button[SDL_CONTROLLER_BUTTON_MAX] = {
    [SDL_CONTROLLER_BUTTON_A] = "cross", [SDL_CONTROLLER_BUTTON_B] = "circle",
    [SDL_CONTROLLER_BUTTON_X] = "square", [SDL_CONTROLLER_BUTTON_Y] = "triangle",
    [SDL_CONTROLLER_BUTTON_BACK] = "create", [SDL_CONTROLLER_BUTTON_START] = "options", [SDL_CONTROLLER_BUTTON_GUIDE] = "ps",
    [SDL_CONTROLLER_BUTTON_LEFTSHOULDER] = "l1", [SDL_CONTROLLER_BUTTON_RIGHTSHOULDER] = "r1",
    [SDL_CONTROLLER_BUTTON_LEFTSTICK] = "l3", [SDL_CONTROLLER_BUTTON_RIGHTSTICK] = "r3",
    [SDL_CONTROLLER_BUTTON_DPAD_UP] = "up", [SDL_CONTROLLER_BUTTON_DPAD_DOWN] = "down",
    [SDL_CONTROLLER_BUTTON_DPAD_LEFT] = "left", [SDL_CONTROLLER_BUTTON_DPAD_RIGHT] = "right",
    [SDL_CONTROLLER_BUTTON_TOUCHPAD] = "touch", [SDL_CONTROLLER_BUTTON_MISC1] = "mute",
};
static const char *const sdl_axis[SDL_CONTROLLER_AXIS_MAX] = {
    [SDL_CONTROLLER_AXIS_LEFTX] = "lx", [SDL_CONTROLLER_AXIS_LEFTY] = "ly",
    [SDL_CONTROLLER_AXIS_RIGHTX] = "rx", [SDL_CONTROLLER_AXIS_RIGHTY] = "ry",
    [SDL_CONTROLLER_AXIS_TRIGGERLEFT] = "l2", [SDL_CONTROLLER_AXIS_TRIGGERRIGHT] = "r2",
};

static struct input_absinfo range(const struct control *c) {
    if (c->direction) return (struct input_absinfo){.minimum = -1, .maximum = 1};
    return (struct input_absinfo){.value = c->button ? 0 : AXIS_CENTER, .maximum = AXIS_MAX};
}

static void emit(int fd, unsigned short type, unsigned short code, int value) {
    struct input_event events[2] = {{.type = type, .code = code, .value = value}, {.type = EV_SYN, .code = SYN_REPORT}};
    if (write(fd, events, sizeof events) < 0) perror("pad write");
}

static void node_path(int seat, char *path, size_t size) { snprintf(path, size, NODES "/js%d", seat); }

static void expose(int seat, int fd) {
    char sysname[UINPUT_MAX_NAME_SIZE], path[PATH_MAX], id[WORD_MAX + 1] = "";
    if (ioctl(fd, UI_GET_SYSNAME(sizeof sysname), sysname) < 0) { perror("uinput sysname"); return; }
    snprintf(path, sizeof path, "/sys/devices/virtual/input/%s", sysname);
    DIR *dir = opendir(path);
    for (struct dirent *entry; dir && (entry = readdir(dir));) {
        if (strncmp(entry->d_name, "js", 2)) continue;
        snprintf(path, sizeof path, "/sys/devices/virtual/input/%s/%s/dev", sysname, entry->d_name);
        FILE *dev = fopen(path, "r");
        if (dev) { if (!fgets(id, sizeof id, dev)) id[0] = 0; fclose(dev); }
    }
    if (dir) closedir(dir);
    unsigned major, minor;
    if (sscanf(id, "%u:%u", &major, &minor) != 2) { fprintf(stderr, "seat %d: no joystick node\n", seat + 1); return; }
    node_path(seat, path, sizeof path);
    if (mknod(path, S_IFCHR | S_IRUSR, makedev(major, minor)) < 0) perror(path);
}

static void plug(int seat) {
    if (pad[seat] >= 0) return;
    int fd = open("/dev/uinput", O_WRONLY);
    if (fd < 0) { perror("/dev/uinput"); return; }
    struct uinput_setup setup = {.id = {.bustype = BUS_USB, .vendor = PAD_VENDOR, .product = PAD_PRODUCT}, .name = PAD_NAME};
    ioctl(fd, UI_SET_EVBIT, EV_KEY);
    ioctl(fd, UI_SET_EVBIT, EV_ABS);
    for (const struct control *c = controls; c < controls + CONTROLS; c++) {
        if (c->type == EV_KEY) { ioctl(fd, UI_SET_KEYBIT, c->code); continue; }
        if (c->button) ioctl(fd, UI_SET_KEYBIT, c->button);
        ioctl(fd, UI_SET_ABSBIT, c->code);
        struct uinput_abs_setup abs = {.code = c->code, .absinfo = range(c)};
        ioctl(fd, UI_ABS_SETUP, &abs);
    }
    if (ioctl(fd, UI_DEV_SETUP, &setup) < 0 || ioctl(fd, UI_DEV_CREATE) < 0) { perror("uinput create"); close(fd); return; }
    memset(held[seat], 0, sizeof held[seat]);
    pad[seat] = fd;
    expose(seat, fd);
}

static void unplug(int seat) {
    if (pad[seat] < 0) return;
    char path[PATH_MAX];
    node_path(seat, path, sizeof path);
    unlink(path);
    close(pad[seat]);
    pad[seat] = -1;
}

static int hat(int seat, unsigned short code) {
    int value = 0;
    for (unsigned i = 0; i < CONTROLS; i++)
        if (controls[i].code == code && controls[i].type == EV_ABS && held[seat][i]) value += controls[i].direction;
    return value;
}

static void drive(int seat, const char *name, int value) {
    if (pad[seat] < 0) { fprintf(stderr, "seat %d not plugged\n", seat + 1); return; }
    for (unsigned i = 0; i < CONTROLS; i++) {
        const struct control *c = &controls[i];
        if (strcmp(c->name, name)) continue;
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
    if (words == 3 && seat_of(first) >= 0) drive(seat_of(first), second, value);
    else if (words == 2 && !strcmp(first, "plug") && seat_of(second) >= 0) plug(seat_of(second));
    else if (words == 2 && !strcmp(first, "unplug") && seat_of(second) >= 0) unplug(seat_of(second));
    else fprintf(stderr, "bad command: %s", line);
}

static int read_commands(void *unused) {
    (void)unused;
    FILE *commands = fopen(FIFO, "r+");
    if (!commands) { perror(FIFO); exit(1); }
    char line[BUFSIZ];
    while (fgets(line, sizeof line, commands)) {
        SDL_Event event = {.user = {.type = SDL_USEREVENT, .data1 = strdup(line)}};
        SDL_PushEvent(&event);
    }
    exit(1);
}

static int scaled(int value, int min, int max) { return (value - min) * AXIS_MAX / (max - min); }

static void attach(int index) {
    if (SDL_JoystickGetDeviceVendor(index) == PAD_VENDOR && SDL_JoystickGetDeviceProduct(index) == PAD_PRODUCT) return;
    if (!SDL_GameControllerOpen(index)) fprintf(stderr, "%s: %s\n", SDL_JoystickNameForIndex(index), SDL_GetError());
}

int main(void) {
    memset(pad, -1, sizeof pad);
    mkfifo(FIFO, S_IRUSR | S_IWUSR);
    setenv("SDL_JOYSTICK_DISABLE_UDEV", "1", 1);
    if (SDL_Init(SDL_INIT_GAMECONTROLLER) < 0) { fprintf(stderr, "SDL: %s\n", SDL_GetError()); return 1; }
    SDL_CreateThread(read_commands, "commands", NULL);
    plug(0);
    for (SDL_Event event; SDL_WaitEvent(&event);) {
        switch (event.type) {
        case SDL_USEREVENT: command(event.user.data1); free(event.user.data1); break;
        case SDL_CONTROLLERDEVICEADDED: attach(event.cdevice.which); break;
        case SDL_CONTROLLERBUTTONDOWN:
        case SDL_CONTROLLERBUTTONUP:
            if (sdl_button[event.cbutton.button]) drive(0, sdl_button[event.cbutton.button], event.cbutton.state == SDL_PRESSED);
            break;
        case SDL_CONTROLLERAXISMOTION:
            if (!sdl_axis[event.caxis.axis]) break;
            if (event.caxis.axis >= SDL_CONTROLLER_AXIS_TRIGGERLEFT) drive(0, sdl_axis[event.caxis.axis], scaled(event.caxis.value, 0, SDL_JOYSTICK_AXIS_MAX));
            else drive(0, sdl_axis[event.caxis.axis], scaled(event.caxis.value, SDL_JOYSTICK_AXIS_MIN, SDL_JOYSTICK_AXIS_MAX));
            break;
        }
    }
    return 1;
}
