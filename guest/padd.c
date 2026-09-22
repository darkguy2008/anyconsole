#include <fcntl.h>
#include <linux/uinput.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>

#define FIFO "/run/pad"
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

static struct input_absinfo range(const struct control *c) {
    if (c->direction) return (struct input_absinfo){.minimum = -1, .maximum = 1};
    return (struct input_absinfo){.value = c->button ? 0 : AXIS_CENTER, .maximum = AXIS_MAX};
}

static void emit(int fd, unsigned short type, unsigned short code, int value) {
    struct input_event events[2] = {{.type = type, .code = code, .value = value}, {.type = EV_SYN, .code = SYN_REPORT}};
    if (write(fd, events, sizeof events) < 0) perror("pad write");
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
}

static void unplug(int seat) {
    if (pad[seat] < 0) return;
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

int main(void) {
    memset(pad, -1, sizeof pad);
    mkfifo(FIFO, 0666);
    FILE *commands = fopen(FIFO, "r+");
    if (!commands) { perror(FIFO); return 1; }
    plug(0);
    char line[BUFSIZ], first[WORD_MAX + 1], second[WORD_MAX + 1];
    int value;
    while (fgets(line, sizeof line, commands)) {
        int words = sscanf(line, "%" STRINGIFY(WORD_MAX) "s %" STRINGIFY(WORD_MAX) "s %d", first, second, &value);
        if (words == 3 && seat_of(first) >= 0) drive(seat_of(first), second, value);
        else if (words == 2 && !strcmp(first, "plug") && seat_of(second) >= 0) plug(seat_of(second));
        else if (words == 2 && !strcmp(first, "unplug") && seat_of(second) >= 0) unplug(seat_of(second));
        else fprintf(stderr, "bad command: %s", line);
    }
    return 1;
}
