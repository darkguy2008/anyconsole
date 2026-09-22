#include <SDL2/SDL.h>
#include <fcntl.h>
#include <glob.h>
#include <limits.h>
#include <linux/uinput.h>
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
    KEY("a", BTN_SOUTH), KEY("b", BTN_EAST), KEY("y", BTN_NORTH), KEY("x", BTN_WEST),
    KEY("leftshoulder", BTN_TL), KEY("rightshoulder", BTN_TR), KEY("leftstick", BTN_THUMBL), KEY("rightstick", BTN_THUMBR),
    KEY("back", BTN_SELECT), KEY("start", BTN_START), KEY("guide", BTN_MODE),
    KEY("touchpad", BTN_TRIGGER_HAPPY1), KEY("paddle1", BTN_TRIGGER_HAPPY2),
    KEY("paddle2", BTN_TRIGGER_HAPPY3), KEY("misc1", BTN_TRIGGER_HAPPY4),
    AXIS("leftx", ABS_X, 0), AXIS("lefty", ABS_Y, 0), AXIS("rightx", ABS_RX, 0), AXIS("righty", ABS_RY, 0),
    AXIS("lefttrigger", ABS_Z, BTN_TL2), AXIS("righttrigger", ABS_RZ, BTN_TR2),
    HAT("dpup", ABS_HAT0Y, -1), HAT("dpdown", ABS_HAT0Y, 1), HAT("dpleft", ABS_HAT0X, -1), HAT("dpright", ABS_HAT0X, 1),
};

static int pad[SEATS];
static int held[SEATS][SDL_arraysize(controls)];

static void emit(int fd, unsigned short type, unsigned short code, int value) {
    struct input_event events[2] = {{.type = type, .code = code, .value = value}, {.type = EV_SYN, .code = SYN_REPORT}};
    if (write(fd, events, sizeof events) < 0) perror("pad write");
}

static void expose(int seat, int fd) {
    char sysname[UINPUT_MAX_NAME_SIZE], path[PATH_MAX];
    unsigned major, minor;
    glob_t found;
    if (ioctl(fd, UI_GET_SYSNAME(sizeof sysname), sysname) < 0) { perror("uinput sysname"); return; }
    snprintf(path, sizeof path, "/sys/devices/virtual/input/%s/js*/dev", sysname);
    if (glob(path, 0, NULL, &found)) { fprintf(stderr, "seat %d: no joystick node\n", seat + 1); return; }
    FILE *dev = fopen(found.gl_pathv[0], "r");
    globfree(&found);
    if (!dev || fscanf(dev, "%u:%u", &major, &minor) != 2) { perror(path); return; }
    fclose(dev);
    snprintf(path, sizeof path, NODES "/js%d", seat);
    if (mknod(path, S_IFCHR | S_IRUSR, makedev(major, minor)) < 0) perror(path);
}

static void plug(int seat) {
    if (pad[seat] >= 0) return;
    int fd = open("/dev/uinput", O_WRONLY);
    if (fd < 0) { perror("/dev/uinput"); return; }
    struct uinput_setup setup = {.id = {.bustype = BUS_USB, .vendor = PAD_VENDOR, .product = PAD_PRODUCT}, .name = PAD_NAME};
    ioctl(fd, UI_SET_EVBIT, EV_KEY);
    ioctl(fd, UI_SET_EVBIT, EV_ABS);
    for (const struct control *c = controls; c < controls + SDL_arraysize(controls); c++) {
        if (c->type == EV_KEY) { ioctl(fd, UI_SET_KEYBIT, c->code); continue; }
        if (c->button) ioctl(fd, UI_SET_KEYBIT, c->button);
        ioctl(fd, UI_SET_ABSBIT, c->code);
        struct uinput_abs_setup abs = {.code = c->code, .absinfo = c->direction
            ? (struct input_absinfo){.minimum = -1, .maximum = 1}
            : (struct input_absinfo){.value = c->button ? 0 : AXIS_CENTER, .maximum = AXIS_MAX}};
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
    snprintf(path, sizeof path, NODES "/js%d", seat);
    unlink(path);
    close(pad[seat]);
    pad[seat] = -1;
}

static int hat(int seat, unsigned short code) {
    int value = 0;
    for (unsigned i = 0; i < SDL_arraysize(controls); i++)
        if (controls[i].code == code && held[seat][i]) value += controls[i].direction;
    return value;
}

static void drive(int seat, const char *name, int value) {
    if (pad[seat] < 0) { fprintf(stderr, "seat %d not plugged\n", seat + 1); return; }
    for (unsigned i = 0; i < SDL_arraysize(controls); i++) {
        const struct control *c = &controls[i];
        if (strcmp(c->name, name)) continue;
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

static int scaled(int value, int min) { return (value - min) * AXIS_MAX / (SDL_JOYSTICK_AXIS_MAX - min); }

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
