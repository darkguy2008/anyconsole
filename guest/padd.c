#define _GNU_SOURCE
#include <SDL2/SDL.h>
#include <errno.h>
#include <fcntl.h>
#include <glob.h>
#include <json-c/json.h>
#include <libevdev/libevdev-uinput.h>
#include <libevdev/libevdev.h>
#include <libudev.h>
#include <limits.h>
#include <linux/uinput.h>
#include <math.h>
#include <opus/opus.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/inotify.h>
#include <sys/ioctl.h>
#include <sys/pidfd.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/timerfd.h>
#include <sys/wait.h>
#include <systemd/sd-bus.h>
#include <time.h>
#include <unistd.h>
#include <zlib.h>
#include "anyconsole.h"

#define DEVICES "/dev/input"
#define UINPUT_DEVICES "/sys/devices/virtual/input/"
#define NODE_MODE (S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH)
#define CAPTURED_MAX 32
#define PHYSICAL_PADS_MAX 16
#define BLUETOOTH_DEVICES_MAX 64
#define NO_PAD (-1)
#define NO_BATTERY (-1)
#define NO_BUTTON (-1)
#define PAD_NODE "event"
#define TOUCHPAD_NODE "touch"
#define TOUCHPAD_MIDDLE 0.5
#define PAD_VENDOR 0x054c
#define PAD_PRODUCT 0x0ce6
#define PAD_VERSION 0x8111
#define INPUT_HEADER 0xa1
#define OUTPUT_HEADER 0xa2
#define REPORT_CRC_SIZE 4
#define SEQUENCE_SHIFT 4
#define SEQUENCE_MASK 0x0f
#define SUBPACKET_FLAG 0x80
#define SPEAKER_REPORT 0x36
#define SPEAKER_REPORT_SIZE 398
#define AUDIO_CONTROL_SUBPACKET (0x11 | SUBPACKET_FLAG)
#define SUBPACKET_DATA 4
#define STATE_REPORT 0x31
#define MICROPHONE_REPORT_SIZE 78
#define MUTE_STATUS_BYTE 55
#define MUTE_STATUS_BIT 0x04
#define MICROPHONE_OPUS_OFFSET 3
#define MICROPHONE_OPUS_SIZE (MICROPHONE_REPORT_SIZE - REPORT_CRC_SIZE - MICROPHONE_OPUS_OFFSET)
#define MICROPHONE_CHANNELS 1
#define AUDIO_RATE 48000
#define AUDIO_FRAME_MS 10
#define AUDIO_FRAME_SAMPLES (AUDIO_RATE * AUDIO_FRAME_MS / MSEC_PER_SEC)
#define TUNNEL_CONFIG \
    "context.spa-libs = { audio.convert.* = audioconvert/libspa-audioconvert support.* = support/libspa-support }\n" \
    "context.modules = [\n" \
    "  { name = libpipewire-module-protocol-native }\n" \
    "  { name = libpipewire-module-client-node }\n" \
    "  { name = libpipewire-module-adapter }\n" \
    "  { name = libpipewire-module-pipe-tunnel args = { tunnel.mode = source pipe.filename = \"%s\" audio.format = S16LE\n" \
    "    audio.rate = %d audio.channels = %d audio.position = [ MONO ] node.virtual = false node.name = \"%s\" node.description = \"%s\"\n" \
    "    stream.props = { api.bluez5.address = \"%s\" } } }\n" \
    "]\n"
#define AXIS_REACH (AXIS_MAX / STICK_REACH_DIVISOR)
#define SDL_AXIS_REACH (SDL_JOYSTICK_AXIS_MAX / STICK_REACH_DIVISOR)
#define TRIGGER_PRESS (AXIS_MAX / 2)
#define STICK_DEADZONE 0.08
#define TRIGGER_DEADZONE 0.04
#define DS_ACC_RES_PER_G 8192
#define DS_ACC_RANGE (4 * DS_ACC_RES_PER_G)
#define DS_GYRO_RES_PER_DEG_S 1024
#define DS_GYRO_RANGE (2048 * DS_GYRO_RES_PER_DEG_S)
#define DEGREES_PER_RADIAN (180 / M_PI)
#define SENSOR_AXES SDL_arraysize(((SDL_ControllerSensorEvent){0}).data)
#define GAIN_MAX UINT16_MAX
#define BLOW_SEAT 0
#define BLOW_AMPLITUDE 1.0
#define POINTER_TICKS_PER_SECOND 60
#define POINTER_CROSSING_SECONDS 1.5
#define SCROLL_STEPS_PER_SECOND 10
#define MOUSE_COUNTS_PER_FULL_TILT 32
#define USB_MOUSE_REPORT_MS 8
#define MOUSE_REST_MS (2 * USB_MOUSE_REPORT_MS)
#define PAIRING_SECONDS 60
#define SECONDS_PER_MINUTE 60
#define USEC_PER_SEC 1000000
#define USEC_PER_MSEC 1000
#define MSEC_PER_SEC 1000
#define NSEC_PER_USEC 1000
#define NSEC_PER_MSEC (NSEC_PER_USEC * USEC_PER_MSEC)
#define NSEC_PER_SEC (NSEC_PER_USEC * USEC_PER_SEC)
#define ADDRESS_SIZE sizeof "00:00:00:00:00:00"
#define SYSTEM_BUS "unix:path=/run/dbus/system_bus_socket"
#define BLUEZ "org.bluez"
#define BLUEZ_ROOT "/org/bluez"
#define ADAPTER_INTERFACE BLUEZ ".Adapter1"
#define DEVICE_INTERFACE BLUEZ ".Device1"
#define INPUT_INTERFACE BLUEZ ".Input1"
#define AGENT_INTERFACE BLUEZ ".Agent1"
#define AGENT_MANAGER_INTERFACE BLUEZ ".AgentManager1"
#define REJECTED BLUEZ ".Error.Rejected"
#define DEVICE_PATH_PREFIX "/dev_"
#define OBJECT_MANAGER_INTERFACE "org.freedesktop.DBus.ObjectManager"
#define PROPERTIES_INTERFACE "org.freedesktop.DBus.Properties"
#define BLUEZ_OWNER_MATCH "type='signal',sender='org.freedesktop.DBus',path='/org/freedesktop/DBus',interface='org.freedesktop.DBus',member='NameOwnerChanged',arg0='" BLUEZ "'"
#define AGENT_PATH "/anyconsole/agent"
#define AGENT_CAPABILITY "NoInputNoOutput"
#define LEGACY_PIN "0000"
#define GAMEPAD_ICON "input-gaming"
#define UEVENT_ADD "add"
#define CONTROLLERS_PARTIAL CONTROLLERS_FILE ".partial"
#define STRINGIFY_(x) #x
#define STRINGIFY(x) STRINGIFY_(x)
#define notify(...) tell_overlay("notify " __VA_ARGS__)

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
#define CONTROL_COUNT SDL_arraysize(controls)

struct binding { const char *control; unsigned short code; };

struct route { int source, sign, target; };
#define SIGNS 3
enum side { NEGATIVE_SIDE = 1, POSITIVE_SIDE = 2, BOTH_SIDES = NEGATIVE_SIDE | POSITIVE_SIDE };

static const struct binding key_bindings[] = {
    {"dpup", KEY_UP}, {"dpdown", KEY_DOWN}, {"dpleft", KEY_LEFT}, {"dpright", KEY_RIGHT}, {"a", KEY_ENTER}, {"b", KEY_ESC},
    {"x", KEY_SPACE}, {"y", KEY_TAB}, {"leftshoulder", KEY_PAGEUP}, {"rightshoulder", KEY_PAGEDOWN}, {"start", KEY_ENTER}, {"back", KEY_BACKSPACE},
};
static const struct binding pointer_bindings[] = {{"a", BTN_LEFT}, {"b", BTN_RIGHT}, {"x", BTN_MIDDLE}};
static const struct binding click_bindings[] = {{"righttrigger", BTN_LEFT}, {"lefttrigger", BTN_RIGHT}};
static const struct binding keyboard_pad_bindings[] = {
    {"dpup", KEY_UP}, {"dpdown", KEY_DOWN}, {"dpleft", KEY_LEFT}, {"dpright", KEY_RIGHT}, {"a", KEY_Z}, {"b", KEY_X},
    {"x", KEY_A}, {"y", KEY_S}, {"leftshoulder", KEY_Q}, {"rightshoulder", KEY_W}, {"start", KEY_ENTER}, {"back", KEY_RIGHTSHIFT},
};
static const struct binding mouse_pad_bindings[] = {{"a", BTN_LEFT}, {"b", BTN_RIGHT}};
static const struct { Uint8 red, green, blue; } seat_colours[PADS] = {{0, 0, 255}, {255, 0, 0}, {0, 255, 0}, {255, 0, 255}};

enum mode { ANALOG, DIGITAL, KEYS, POINTER, DESKTOP, MODE_COUNT };
static const char *const modes[] = {[ANALOG] = "analog", [DIGITAL] = "digital", [KEYS] = "keyboard", [POINTER] = "mouse", [DESKTOP] = "keyboard-mouse"};
#define MODE(mode) (1u << (mode))
enum kind { PAD, KEYBOARD, MOUSE };
static const struct { const char *name; enum mode native; unsigned modes; } kinds[] = {
    [PAD] = {"pad", ANALOG, MODE(ANALOG) | MODE(DIGITAL) | MODE(KEYS) | MODE(POINTER) | MODE(DESKTOP)},
    [KEYBOARD] = {"keyboard", KEYS, MODE(KEYS) | MODE(DIGITAL)},
    [MOUSE] = {"mouse", POINTER, MODE(POINTER) | MODE(ANALOG)},
};

enum source { COMMANDS, HOTPLUG, SETTINGS, PAIRING, SLEEP, POINTING, RESTING, BUS, BLOW, SPEAKER, FIXED_SOURCES, CAPTURED = FIXED_SOURCES, VIRTUAL_PAD, PHYSICAL_PAD, MICROPHONE, TUNNEL };
#define WATCHES_PER_PAD (TUNNEL - PHYSICAL_PAD + 1)
#define WATCHED_MAX (FIXED_SOURCES + CAPTURED_MAX + WATCHES_PER_PAD * PHYSICAL_PADS_MAX + PADS)

enum pairing { NOT_PAIRING, SEARCHING, BONDING };

struct device {
    char name[NAME_MAX + 1];
    int id, pad, guide;
    enum kind kind;
    enum mode mode;
    int raw[CONTROL_COUNT];
    const struct control *swallowed;
    double travel[3];
    struct libevdev *evdev;
};

struct watch {
    enum source source;
    struct device *input;
    SDL_JoystickID pad;
    int seat;
};

struct pad {
    SDL_GameController *controller;
    SDL_JoystickID instance;
    struct device *input;
    struct udev_device *hid;
    char address[ADDRESS_SIZE];
    int bus, battery, charging;
    double touchpad_aspect;
    float touch_x, touch_y;
    int clicked;
    SDL_JoystickPowerLevel level;
    time_t active;
    int hidraw, microphone, fifo, sequence, muted;
    pid_t tunnel;
    OpusDecoder *decoder;
};

struct bluetooth_device {
    char path[PATH_MAX], address[ADDRESS_SIZE], name[NAME_MAX + 1], icon[NAME_MAX + 1];
    int paired, trusted, seen;
};

static struct libevdev_uinput *pad[PADS], *touchpad[PADS], *motion[PADS], *keyboard;
static int emitted[PADS][CONTROL_COUNT], keymap[CONTROL_COUNT], rerouted[CONTROL_COUNT], route_count, axis_control[SDL_CONTROLLER_AXIS_MAX], button_control[SDL_CONTROLLER_BUTTON_MAX];
static struct route routes[CONTROL_COUNT * SIGNS];
static struct ff_effect *effects[PADS];
static int effect_count[PADS], gain[PADS];
static SDL_JoystickID motion_source[PADS];
static struct device device_table[INPUT_DEVICES_MAX], *remote_pads[PADS], *remote_keyboard;
static int next_id = 1, pointing, menu_open;
static double pointer_speed;
static struct pollfd watched[WATCHED_MAX];
static struct watch watches[WATCHED_MAX];
static int watched_count;
static struct pad pads[PHYSICAL_PADS_MAX];
static int pad_count, sleep_minutes, touchpad_pointer = 1, blowing, blow_sent;
static struct bluetooth_device known[BLUETOOTH_DEVICES_MAX];
static int known_count;
static char adapter[PATH_MAX], target[ADDRESS_SIZE], armed[ADDRESS_SIZE], offered[PHYSICAL_PADS_MAX][ADDRESS_SIZE];
static int offered_count;
static enum pairing pairing;
static struct udev *udev;
static struct udev_monitor *monitor;
static sd_bus *bus;

static void emit(struct libevdev_uinput *device, unsigned type, unsigned code, int value) {
    libevdev_uinput_write_event(device, type, code, value);
    libevdev_uinput_write_event(device, EV_SYN, SYN_REPORT, 0);
}

static void tell_overlay(const char *format, ...) {
    char line[PIPE_BUF];
    va_list arguments;
    va_start(arguments, format);
    int length = vsnprintf(line, sizeof line - 1, format, arguments);
    va_end(arguments);
    if (length >= (int)sizeof line - 1) length = sizeof line - 2;
    line[length++] = '\n';
    int fd = open(OVERLAY_FIFO, O_WRONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) return;
    if (write(fd, line, length) < 0) perror(OVERLAY_FIFO);
    close(fd);
}

static int routed(struct device *device) { return device->pad != NO_PAD && !menu_open; }

static void menu(struct device *device, const char *command, int value) {
    if (!routed(device) || !strcmp(command, "guide")) tell_overlay("press %d %s %d", device->id, command, value);
}

static const char *node(const char *prefix, int number) {
    static char name[NAME_MAX + 1];
    snprintf(name, sizeof name, "%s%d", prefix, number);
    return name;
}

static void publish(const char *path, const char *dev_file) {
    unsigned major, minor;
    FILE *dev = fopen(dev_file, "r");
    int read = dev && fscanf(dev, "%u:%u", &major, &minor) == 2;
    if (dev) fclose(dev);
    if (!read) perror(dev_file);
    else if (mknod(path, S_IFCHR | NODE_MODE, makedev(major, minor)) < 0) perror(path);
}

static void expose(int fd, const char *directory, const char *name, int create) {
    char sysname[UINPUT_MAX_NAME_SIZE], pattern[PATH_MAX], path[PATH_MAX], dev_file[PATH_MAX];
    glob_t found;
    if (ioctl(fd, UI_GET_SYSNAME(sizeof sysname), sysname) < 0) { perror("uinput sysname"); return; }
    snprintf(pattern, sizeof pattern, UINPUT_DEVICES "%s/" PAD_NODE "*", sysname);
    if (glob(pattern, 0, NULL, &found)) { fprintf(stderr, "%s: no device nodes\n", sysname); return; }
    for (size_t i = 0; i < found.gl_pathc; i++) {
        snprintf(dev_file, sizeof dev_file, "%s/dev", found.gl_pathv[i]);
        snprintf(path, sizeof path, "%s/%s", directory, name ? name : basename(found.gl_pathv[i]));
        if (create) publish(path, dev_file);
        else unlink(path);
    }
    globfree(&found);
}

static int is_virtual(const char *path) {
    char link[PATH_MAX], resolved[PATH_MAX];
    snprintf(link, sizeof link, "/sys/class/input/%s/device", basename(path));
    return realpath(link, resolved) && !strncmp(resolved, UINPUT_DEVICES, strlen(UINPUT_DEVICES));
}

static struct libevdev_uinput *create(struct libevdev *device, const char *directory, const char *name) {
    struct libevdev_uinput *created;
    int error = libevdev_uinput_create_from_device(device, LIBEVDEV_UINPUT_OPEN_MANAGED, &created);
    if (error) { fprintf(stderr, "%s: %s\n", libevdev_get_name(device), strerror(-error)); exit(1); }
    libevdev_free(device);
    expose(libevdev_uinput_get_fd(created), directory, name, 1);
    return created;
}

static struct libevdev_uinput *create_keyboard(void) {
    struct libevdev *device = libevdev_new();
    libevdev_set_name(device, KEYBOARD_NAME);
    for (unsigned code = KEY_ESC; code < BTN_MISC; code++) libevdev_enable_event_code(device, EV_KEY, code, NULL);
    for (unsigned code = BTN_MOUSE; code < BTN_JOYSTICK; code++) libevdev_enable_event_code(device, EV_KEY, code, NULL);
    for (unsigned code = 0; code <= REL_MAX; code++) libevdev_enable_event_code(device, EV_REL, code, NULL);
    return create(device, INPUT_NODES, NULL);
}

static struct libevdev_uinput *create_touchpad(int seat) {
    struct libevdev *device = libevdev_new();
    struct input_absinfo axis = {.maximum = SDL_JOYSTICK_AXIS_MAX};
    libevdev_set_name(device, PAD_NAME " Touchpad");
    libevdev_enable_property(device, INPUT_PROP_POINTER);
    libevdev_enable_event_code(device, EV_KEY, BTN_TOUCH, NULL);
    libevdev_enable_event_code(device, EV_ABS, ABS_X, &axis);
    libevdev_enable_event_code(device, EV_ABS, ABS_Y, &axis);
    return create(device, PAD_NODES, node(TOUCHPAD_NODE, seat));
}

static struct libevdev *pad_device(const char *name) {
    struct libevdev *device = libevdev_new();
    libevdev_set_name(device, name);
    libevdev_set_id_bustype(device, BUS_USB);
    libevdev_set_id_vendor(device, PAD_VENDOR);
    libevdev_set_id_product(device, PAD_PRODUCT);
    libevdev_set_id_version(device, PAD_VERSION);
    return device;
}

static struct libevdev_uinput *create_motion(int seat) {
    struct libevdev *device = pad_device(MOTION_NAME);
    struct input_absinfo accelerometer = {.minimum = -DS_ACC_RANGE, .maximum = DS_ACC_RANGE, .resolution = DS_ACC_RES_PER_G};
    struct input_absinfo gyroscope = {.minimum = -DS_GYRO_RANGE, .maximum = DS_GYRO_RANGE, .resolution = DS_GYRO_RES_PER_DEG_S};
    libevdev_enable_property(device, INPUT_PROP_ACCELEROMETER);
    for (unsigned axis = 0; axis < SENSOR_AXES; axis++) {
        libevdev_enable_event_code(device, EV_ABS, ABS_X + axis, &accelerometer);
        libevdev_enable_event_code(device, EV_ABS, ABS_RX + axis, &gyroscope);
    }
    libevdev_enable_event_code(device, EV_MSC, MSC_TIMESTAMP, NULL);
    return create(device, PAD_NODES, node(PAD_NODE, PADS + seat));
}

static struct watch *watch(int fd, enum source source) {
    if (fd < 0 || watched_count == WATCHED_MAX) { fprintf(stderr, "padd: cannot watch source %d\n", source); return NULL; }
    watched[watched_count] = (struct pollfd){.fd = fd, .events = POLLIN};
    watches[watched_count] = (struct watch){.source = source};
    return &watches[watched_count++];
}

static void drop(int index) {
    watched_count--;
    watched[index] = watched[watched_count];
    watched[index].revents = 0;
    watches[index] = watches[watched_count];
}

static void unwatch(int index) {
    close(watched[index].fd);
    drop(index);
}

static int is_stick(unsigned i) { return controls[i].type == EV_ABS && !controls[i].button && !controls[i].direction; }

static int rest(unsigned i) { return is_stick(i) ? AXIS_CENTER : 0; }

static int control_named(const char *name) {
    for (unsigned i = 0; name && i < CONTROL_COUNT; i++)
        if (!strcmp(controls[i].name, name)) return i;
    return -1;
}

static int control_index(unsigned short type, unsigned short code) {
    for (unsigned i = 0; i < CONTROL_COUNT; i++)
        if (controls[i].type == type && controls[i].code == code && !controls[i].direction) return i;
    return -1;
}

static int is_left_stick(unsigned i) { return is_stick(i) && (controls[i].code == ABS_X || controls[i].code == ABS_Y); }

static int stick_direction(int value) { return value < AXIS_CENTER - AXIS_REACH ? -1 : value > AXIS_CENTER + AXIS_REACH ? 1 : 0; }

static int pressed_value(unsigned i, int value) { return controls[i].button ? value > TRIGGER_PRESS : value > 0; }

static int direction_control(unsigned stick, int direction) {
    unsigned short hat = controls[stick].code == ABS_X ? ABS_HAT0X : ABS_HAT0Y;
    for (unsigned i = 0; i < CONTROL_COUNT; i++)
        if (controls[i].code == hat && controls[i].direction == direction) return i;
    return -1;
}

static void cross(struct device *device, unsigned stick, int before, int value, void (*press)(struct device *, int, int)) {
    int from = stick_direction(before), to = stick_direction(value);
    if (!is_left_stick(stick) || from == to) return;
    if (from) press(device, direction_control(stick, from), 0);
    if (to) press(device, direction_control(stick, to), 1);
}

static int mode_named(const char *name) {
    for (unsigned i = 0; i < SDL_arraysize(modes); i++)
        if (!strcmp(modes[i], name)) return i;
    return -1;
}

static double deflection(int value) { return (value - AXIS_CENTER) / (double)(AXIS_MAX - AXIS_CENTER); }

static int stronger(unsigned i, int value, int candidate) { return abs(candidate - rest(i)) > abs(value - rest(i)) ? candidate : value; }

static int axis_value(double unit) { return lround((fmax(-1, fmin(1, unit)) + 1) * AXIS_MAX / 2); }

static int converted(const struct route *route, int raw) {
    const struct control *source = &controls[route->source];
    double amount = raw;
    if (is_stick(route->source)) amount = deflection(raw);
    else if (source->button) amount = (double)raw / AXIS_MAX;
    else if (source->direction) amount = raw * source->direction;
    if (route->sign) amount = fmax(0, route->sign * amount);
    if (is_stick(route->target)) return axis_value(amount);
    if (controls[route->target].button) return lround(fmin(fabs(amount), 1) * AXIS_MAX);
    if (!is_stick(route->source)) return pressed_value(route->source, raw);
    return route->sign ? stick_direction(raw) == route->sign : stick_direction(raw) != 0;
}

static int mapped(struct device *device, unsigned i) {
    int raw = device->raw[i], side = raw < rest(i) ? NEGATIVE_SIDE : raw > rest(i) ? POSITIVE_SIDE : 0;
    int value = rerouted[i] & side ? rest(i) : raw;
    for (int r = 0; r < route_count; r++)
        if (routes[r].target == (int)i) value = stronger(i, value, converted(&routes[r], device->raw[routes[r].source]));
    return value;
}

static int effective(struct device *device, unsigned i) {
    const struct control *c = &controls[i];
    if (!routed(device) || (device->mode != ANALOG && device->mode != DIGITAL)) return rest(i);
    int value = mapped(device, i);
    if (device->mode == ANALOG) return value;
    if (c->direction) {
        int stick = control_index(EV_ABS, c->code == ABS_HAT0X ? ABS_X : ABS_Y);
        return value || stick_direction(mapped(device, stick)) == c->direction;
    }
    if (is_stick(i)) return AXIS_CENTER;
    if (c->button) return pressed_value(i, value) ? AXIS_MAX : 0;
    return value;
}

static int hat(int seat, unsigned short code) {
    int value = 0;
    for (unsigned i = 0; i < CONTROL_COUNT; i++)
        if (controls[i].code == code && controls[i].direction && emitted[seat][i]) value += controls[i].direction;
    return value;
}

static int spawn_child(char *const argv[], int quiet, pid_t *child) {
    posix_spawn_file_actions_t actions;
    pid_t pid;
    posix_spawn_file_actions_init(&actions);
    if (quiet) posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO, "/dev/null", O_WRONLY, 0);
    int failed = posix_spawnp(&pid, argv[0], &actions, NULL, argv, environ);
    posix_spawn_file_actions_destroy(&actions);
    if (failed) { fprintf(stderr, "padd: %s: %s\n", argv[0], strerror(failed)); return -1; }
    if (child) *child = pid;
    return pidfd_open(pid, 0);
}

static void reap(int pidfd, const char *name) {
    siginfo_t info = {0};
    waitid(P_PIDFD, pidfd, &info, WEXITED);
    if (info.si_status) fprintf(stderr, "padd: %s exited with %d\n", name, info.si_status);
}

static void send_blow(int on) {
    char params[PATH_MAX];
    blowing = on;
    if (watched[BLOW].fd >= 0 || blowing == blow_sent) return;
    snprintf(params, sizeof params, "{ params = [ \"%s:Ampl\" %f ] }", BLOW_NODE, blowing * BLOW_AMPLITUDE);
    watched[BLOW].fd = spawn_child((char *[]){"pw-cli", "set-param", MICROPHONE_NODE, "Props", params, NULL}, 1, NULL);
    if (watched[BLOW].fd >= 0) blow_sent = blowing;
}

static void sent_blow(int fd) {
    reap(fd, "pw-cli");
    close(fd);
    watched[BLOW].fd = -1;
    send_blow(blowing);
}

static void sync_control(int seat, unsigned i) {
    const struct control *c = &controls[i];
    int value = rest(i);
    for (int j = 0; j < INPUT_DEVICES_MAX; j++) {
        struct device *device = &device_table[j];
        if (device->id && device->pad == seat) value = stronger(i, value, effective(device, i));
    }
    if (value == emitted[seat][i]) return;
    emitted[seat][i] = value;
    if (seat == BLOW_SEAT && (c->code == BTN_THUMBL || c->code == BTN_THUMBR))
        send_blow(emitted[seat][control_index(EV_KEY, BTN_THUMBL)] && emitted[seat][control_index(EV_KEY, BTN_THUMBR)]);
    if (c->type == EV_KEY) libevdev_uinput_write_event(pad[seat], EV_KEY, c->code, value);
    else if (c->direction) libevdev_uinput_write_event(pad[seat], EV_ABS, c->code, hat(seat, c->code));
    else {
        libevdev_uinput_write_event(pad[seat], EV_ABS, c->code, value);
        if (c->button) libevdev_uinput_write_event(pad[seat], EV_KEY, c->button, value > 0);
    }
}

static void sync_pad(int seat) {
    for (unsigned i = 0; pad[seat] && i < CONTROL_COUNT; i++) sync_control(seat, i);
}

static void plug(int seat) {
    if (pad[seat]) return;
    struct libevdev *device = pad_device(PAD_NAME);
    for (const struct control *c = controls; c < controls + CONTROL_COUNT; c++) {
        if (c->type == EV_KEY) { libevdev_enable_event_code(device, EV_KEY, c->code, NULL); continue; }
        if (c->button) libevdev_enable_event_code(device, EV_KEY, c->button, NULL);
        struct input_absinfo abs = c->direction ? (struct input_absinfo){.minimum = -1, .maximum = 1}
                                                : (struct input_absinfo){.value = rest(c - controls), .maximum = AXIS_MAX};
        libevdev_enable_event_code(device, EV_ABS, c->code, &abs);
    }
    libevdev_enable_event_code(device, EV_FF, FF_RUMBLE, NULL);
    libevdev_enable_event_code(device, EV_FF, FF_GAIN, NULL);
    for (unsigned i = 0; i < CONTROL_COUNT; i++) emitted[seat][i] = rest(i);
    gain[seat] = GAIN_MAX;
    pad[seat] = create(device, PAD_NODES, node(PAD_NODE, seat));
    touchpad[seat] = create_touchpad(seat);
    motion[seat] = create_motion(seat);
    struct watch *feedback = watch(libevdev_uinput_get_fd(pad[seat]), VIRTUAL_PAD);
    if (feedback) feedback->seat = seat;
}

static void unplug(int seat) {
    if (!pad[seat]) return;
    for (int i = 0; i < watched_count; i++)
        if (watches[i].source == VIRTUAL_PAD && watches[i].seat == seat) { drop(i); break; }
    expose(libevdev_uinput_get_fd(pad[seat]), PAD_NODES, node(PAD_NODE, seat), 0);
    expose(libevdev_uinput_get_fd(touchpad[seat]), PAD_NODES, node(TOUCHPAD_NODE, seat), 0);
    expose(libevdev_uinput_get_fd(motion[seat]), PAD_NODES, node(PAD_NODE, PADS + seat), 0);
    libevdev_uinput_destroy(pad[seat]);
    libevdev_uinput_destroy(touchpad[seat]);
    libevdev_uinput_destroy(motion[seat]);
    pad[seat] = touchpad[seat] = motion[seat] = NULL;
    free(effects[seat]);
    effects[seat] = NULL;
    effect_count[seat] = 0;
    if (seat == BLOW_SEAT) send_blow(0);
}

static unsigned short bound(const struct binding *bindings, size_t count, const char *control) {
    for (size_t i = 0; i < count; i++)
        if (!strcmp(bindings[i].control, control)) return bindings[i].code;
    return 0;
}

static const char *binding_of(const struct binding *bindings, size_t count, unsigned short code) {
    for (size_t i = 0; i < count; i++)
        if (bindings[i].code == code) return bindings[i].control;
    return NULL;
}

static unsigned short emulated_code(enum mode mode, unsigned i) {
    const char *name = controls[i].name;
    unsigned short click = mode == DESKTOP ? bound(click_bindings, SDL_arraysize(click_bindings), name) : 0;
    if (keymap[i]) return keymap[i];
    if (mode == POINTER) return bound(pointer_bindings, SDL_arraysize(pointer_bindings), name);
    return click ? click : bound(key_bindings, SDL_arraysize(key_bindings), name);
}

static void press_key(unsigned short code, int pressed) {
    if (code) emit(keyboard, EV_KEY, code, pressed);
}

static void press_emulated_key(struct device *device, int i, int pressed) {
    (void)device;
    press_key(emulated_code(KEYS, i), pressed);
}

static int canvas_width(void) {
    json_object *display = json_object_from_file(DISPLAY_FILE), *canvas;
    int width = json_object_object_get_ex(display, "canvas", &canvas) ? atoi(json_object_get_string(canvas)) : 0;
    json_object_put(display);
    return width;
}

static int move_pointer(double *travel, const double *motion, const unsigned short *axes, unsigned count) {
    int moved = 0;
    for (unsigned axis = 0; axis < count; axis++) {
        travel[axis] += motion[axis];
        double whole = trunc(travel[axis]);
        travel[axis] -= whole;
        if (!whole) continue;
        libevdev_uinput_write_event(keyboard, EV_REL, axes[axis], whole);
        moved = 1;
    }
    if (moved) libevdev_uinput_write_event(keyboard, EV_SYN, SYN_REPORT, 0);
    return moved;
}

static void tick_pointer(void) {
    static const unsigned short axes[] = {REL_X, REL_Y, REL_WHEEL};
    int was_pointing = pointing;
    pointing = 0;
    for (int j = 0; j < INPUT_DEVICES_MAX; j++) {
        struct device *device = &device_table[j];
        if (!device->id || !routed(device) || device->kind != PAD || (device->mode != POINTER && device->mode != DESKTOP)) continue;
        double step[] = {deflection(device->raw[axis_control[SDL_CONTROLLER_AXIS_LEFTX]]) * pointer_speed,
                         deflection(device->raw[axis_control[SDL_CONTROLLER_AXIS_LEFTY]]) * pointer_speed,
                         -deflection(device->raw[axis_control[SDL_CONTROLLER_AXIS_RIGHTY]]) * SCROLL_STEPS_PER_SECOND / POINTER_TICKS_PER_SECOND};
        pointing |= step[0] || step[1] || step[2];
        move_pointer(device->travel, step, axes, SDL_arraysize(axes));
    }
    if (pointing == was_pointing) return;
    struct itimerspec timer = {0};
    if (pointing) timer.it_value.tv_nsec = timer.it_interval.tv_nsec = NSEC_PER_SEC / POINTER_TICKS_PER_SECOND;
    timerfd_settime(watched[POINTING].fd, 0, &timer, NULL);
}

static void start_pointer(void) {
    if (pointing) return;
    pointer_speed = (double)canvas_width() / POINTER_CROSSING_SECONDS / POINTER_TICKS_PER_SECOND;
    tick_pointer();
}

static void emulate(struct device *device, unsigned i, int before, int value) {
    if (is_stick(i) && device->mode == KEYS) cross(device, i, before, value, press_emulated_key);
    else if (is_stick(i) && value != AXIS_CENTER && controls[i].code != ABS_RX) start_pointer();
    else if (!is_stick(i) && pressed_value(i, before) != pressed_value(i, value)) press_key(emulated_code(device->mode, i), pressed_value(i, value));
}

static void drive_control(struct device *device, int i, int value) {
    if (i < 0 || device->raw[i] == value) return;
    int before = device->raw[i];
    device->raw[i] = value;
    if (!routed(device)) return;
    if (device->mode == ANALOG || device->mode == DIGITAL) sync_pad(device->pad);
    else emulate(device, i, before, value);
}

static const char *command_of(unsigned short code) {
    switch (code) {
    case KEY_UP: return "up";
    case KEY_DOWN: return "down";
    case KEY_LEFT: return "left";
    case KEY_RIGHT: return "right";
    case KEY_ENTER: case KEY_KPENTER: case KEY_X: case BTN_SOUTH: case BTN_START: return "confirm";
    case KEY_ESC: case KEY_BACKSPACE: case KEY_Z: case BTN_EAST: return "back";
    case KEY_LEFTMETA: case KEY_RIGHTMETA: case BTN_MODE: return "guide";
    default: return "other";
    }
}

static void menu_direction(struct device *device, int i, int pressed) {
    int horizontal = controls[i].code == ABS_HAT0X, negative = controls[i].direction < 0;
    menu(device, horizontal ? (negative ? "left" : "right") : (negative ? "up" : "down"), pressed);
}

static void menu_control(struct device *device, unsigned i, int before, int value) {
    if (controls[i].direction) menu_direction(device, i, value);
    else if (is_stick(i)) cross(device, i, before, value, menu_direction);
    else if (pressed_value(i, before) != pressed_value(i, value)) menu(device, command_of(controls[i].code), pressed_value(i, value));
}

static unsigned short combo_partner(const struct control *c) {
    return c->code == BTN_SELECT ? BTN_START : c->code == BTN_START ? BTN_SELECT : 0;
}

static void pad_input(struct device *device, int i, int value) {
    if (i < 0) return;
    const struct control *c = &controls[i];
    if (c->code == BTN_MODE) {
        menu(device, "guide", value);
    } else if (c == device->swallowed) {
        if (value) return;
        device->swallowed = NULL;
        menu(device, "guide", 0);
    } else if (value && combo_partner(c) && device->raw[control_index(EV_KEY, combo_partner(c))]) {
        device->swallowed = c;
        menu(device, "guide", 1);
    } else if (device->raw[i] != value) {
        menu_control(device, i, device->raw[i], value);
        drive_control(device, i, value);
    }
}

static int is_meta(struct input_event *event) { return event->type == EV_KEY && !strcmp(command_of(event->code), "guide"); }

static void arm_rest(void) {
    timerfd_settime(watched[RESTING].fd, 0, &(struct itimerspec){.it_value.tv_nsec = MOUSE_REST_MS * NSEC_PER_MSEC}, NULL);
}

static void tilt(struct device *device, unsigned short code, int delta) {
    int value = AXIS_CENTER + delta * (AXIS_MAX - AXIS_CENTER) / MOUSE_COUNTS_PER_FULL_TILT;
    drive_control(device, control_index(EV_ABS, code == REL_X ? ABS_RX : ABS_RY), value < 0 ? 0 : value > AXIS_MAX ? AXIS_MAX : value);
    arm_rest();
}

static void rest_mice(void) {
    for (int j = 0; j < INPUT_DEVICES_MAX; j++) {
        struct device *device = &device_table[j];
        if (!device->id || device->kind != MOUSE) continue;
        drive_control(device, axis_control[SDL_CONTROLLER_AXIS_RIGHTX], AXIS_CENTER);
        drive_control(device, axis_control[SDL_CONTROLLER_AXIS_RIGHTY], AXIS_CENTER);
    }
}

static void key_input(struct device *device, struct input_event *event) {
    if (event->type == EV_KEY && event->value != 2) menu(device, command_of(event->code), event->value);
    if (!routed(device) || is_meta(event)) return;
    if (device->mode == kinds[device->kind].native) {
        libevdev_uinput_write_event(keyboard, event->type, event->code, event->value);
    } else if (event->type == EV_KEY && event->value != 2) {
        const char *control = device->kind == KEYBOARD ? binding_of(keyboard_pad_bindings, SDL_arraysize(keyboard_pad_bindings), event->code)
                                                       : binding_of(mouse_pad_bindings, SDL_arraysize(mouse_pad_bindings), event->code);
        drive_control(device, control_named(control), event->value);
    } else if (event->type == EV_REL && device->kind == MOUSE && (event->code == REL_X || event->code == REL_Y)) {
        tilt(device, event->code, event->value);
    }
}

static void release_keys(struct device *device) {
    if (!device->evdev || !routed(device) || device->mode != kinds[device->kind].native) return;
    for (unsigned code = 0; code <= KEY_MAX; code++)
        if (libevdev_has_event_code(device->evdev, EV_KEY, code) && libevdev_get_event_value(device->evdev, EV_KEY, code))
            emit(keyboard, EV_KEY, code, 0);
}

static void release(struct device *device) {
    release_keys(device);
    for (unsigned i = 0; i < CONTROL_COUNT; i++) drive_control(device, i, rest(i));
    device->swallowed = NULL;
    memset(device->travel, 0, sizeof device->travel);
}

static void release_all(void) {
    for (int j = 0; j < INPUT_DEVICES_MAX; j++)
        if (device_table[j].id) release(&device_table[j]);
}

static struct device *new_device(const char *name, enum kind kind, int guide) {
    for (int j = 0; j < INPUT_DEVICES_MAX; j++) {
        struct device *device = &device_table[j];
        if (device->id) continue;
        *device = (struct device){.id = next_id++, .pad = NO_PAD, .guide = guide, .kind = kind, .mode = kinds[kind].native};
        snprintf(device->name, sizeof device->name, "%s", name);
        for (unsigned i = 0; i < CONTROL_COUNT; i++) device->raw[i] = rest(i);
        return device;
    }
    fprintf(stderr, "padd: too many devices\n");
    return NULL;
}

static void free_device(struct device *device) {
    release(device);
    device->id = 0;
}

static struct device *device_with_id(int id) {
    if (!id) return NULL;
    for (int j = 0; j < INPUT_DEVICES_MAX; j++)
        if (device_table[j].id == id) return &device_table[j];
    return NULL;
}

static struct udev_enumerate *children(struct udev_device *hid, const char *subsystem) {
    struct udev_enumerate *found = udev_enumerate_new(udev);
    udev_enumerate_add_match_subsystem(found, subsystem);
    udev_enumerate_add_match_parent(found, hid);
    udev_enumerate_scan_devices(found);
    return found;
}

static double touchpad_aspect(struct udev_device *hid) {
    struct udev_list_entry *entry;
    struct libevdev *device;
    double aspect = 1;
    if (!hid) return aspect;
    struct udev_enumerate *nodes = children(hid, "input");
    udev_list_entry_foreach(entry, udev_enumerate_get_list_entry(nodes)) {
        struct udev_device *node = udev_device_new_from_syspath(udev, udev_list_entry_get_name(entry));
        const char *devnode = udev_device_get_devnode(node);
        int fd = devnode && udev_device_get_property_value(node, "ID_INPUT_TOUCHPAD") ? open(devnode, O_RDONLY | O_NONBLOCK | O_CLOEXEC) : -1;
        if (!libevdev_new_from_fd(fd, &device)) {
            const struct input_absinfo *x = libevdev_get_abs_info(device, ABS_X), *y = libevdev_get_abs_info(device, ABS_Y);
            aspect = (double)(y->maximum - y->minimum) / (x->maximum - x->minimum);
            libevdev_free(device);
        }
        close(fd);
        udev_device_unref(node);
    }
    udev_enumerate_unref(nodes);
    return aspect;
}

static void point(struct pad *source, SDL_ControllerTouchpadEvent *event) {
    static const unsigned short axes[] = {REL_X, REL_Y};
    static double travel[2];
    static int width;
    if (event->type == SDL_CONTROLLERTOUCHPADDOWN) {
        width = canvas_width();
        travel[0] = travel[1] = 0;
    }
    if (event->type != SDL_CONTROLLERTOUCHPADMOTION) return;
    double motion[] = {(event->x - source->touch_x) * width, (event->y - source->touch_y) * source->touchpad_aspect * width};
    move_pointer(travel, motion, axes, SDL_arraysize(axes));
}

static void click(struct pad *source, int down) {
    if (down) source->clicked = source->touch_x < TOUCHPAD_MIDDLE ? BTN_LEFT : BTN_RIGHT;
    emit(keyboard, EV_KEY, source->clicked, down);
    if (!down) source->clicked = 0;
}

static void touch(struct pad *source, SDL_ControllerTouchpadEvent *event) {
    struct device *input = source->input;
    int down = event->type != SDL_CONTROLLERTOUCHPADUP;
    if (event->finger || !routed(input)) return;
    if (!source->touchpad_aspect) source->touchpad_aspect = touchpad_aspect(source->hid);
    if (touchpad_pointer) point(source, event);
    source->touch_x = event->x;
    source->touch_y = event->y;
    if (input->mode != ANALOG || !touchpad[input->pad]) return;
    if (down) {
        libevdev_uinput_write_event(touchpad[input->pad], EV_ABS, ABS_X, event->x * fmin(1, 1 / source->touchpad_aspect) * SDL_JOYSTICK_AXIS_MAX);
        libevdev_uinput_write_event(touchpad[input->pad], EV_ABS, ABS_Y, event->y * fmin(1, source->touchpad_aspect) * SDL_JOYSTICK_AXIS_MAX);
    }
    emit(touchpad[input->pad], EV_KEY, BTN_TOUCH, down);
}

static struct pad *pad_of(SDL_JoystickID instance) {
    for (int i = 0; i < pad_count; i++)
        if (pads[i].instance == instance) return &pads[i];
    return NULL;
}

static void sense(SDL_ControllerSensorEvent *event) {
    struct input_event report[SENSOR_AXES + 2];
    struct pad *source = pad_of(event->which);
    int gyroscope = event->sensor == SDL_SENSOR_GYRO, seat = source && routed(source->input) ? source->input->pad : NO_PAD;
    if (seat == NO_PAD || motion_source[seat] != event->which || !motion[seat] || (!gyroscope && event->sensor != SDL_SENSOR_ACCEL)) return;
    for (unsigned axis = 0; axis < SENSOR_AXES; axis++)
        report[axis] = (struct input_event){.type = EV_ABS, .code = (gyroscope ? ABS_RX : ABS_X) + axis,
                                            .value = gyroscope ? event->data[axis] * DEGREES_PER_RADIAN * DS_GYRO_RES_PER_DEG_S
                                                               : event->data[axis] / SDL_STANDARD_GRAVITY * DS_ACC_RES_PER_G};
    report[SENSOR_AXES] = (struct input_event){.type = EV_MSC, .code = MSC_TIMESTAMP, .value = (int)event->timestamp_us};
    report[SENSOR_AXES + 1] = (struct input_event){.type = EV_SYN, .code = SYN_REPORT};
    if (write(libevdev_uinput_get_fd(motion[seat]), report, sizeof report) < 0) perror("padd: motion");
}

static void sense_with(struct pad *controller_pad, SDL_bool enabled) {
    SDL_GameControllerSetSensorEnabled(controller_pad->controller, SDL_SENSOR_ACCEL, enabled);
    SDL_GameControllerSetSensorEnabled(controller_pad->controller, SDL_SENSOR_GYRO, enabled);
}

static time_t now(void) {
    struct timespec time;
    clock_gettime(CLOCK_MONOTONIC, &time);
    return time.tv_sec;
}

static void touched(struct pad *controller_pad) {
    int seat = controller_pad->input->pad;
    controller_pad->active = now();
    if (seat == NO_PAD || controller_pad->input->mode != ANALOG || motion_source[seat] == controller_pad->instance) return;
    struct pad *previous = pad_of(motion_source[seat]);
    if (previous) sense_with(previous, SDL_FALSE);
    sense_with(controller_pad, SDL_TRUE);
    motion_source[seat] = controller_pad->instance;
}

static struct pad *pad_with_input(struct device *device) {
    for (int i = 0; i < pad_count; i++)
        if (pads[i].input == device) return &pads[i];
    return NULL;
}

static void forget_motion(struct pad *controller_pad) {
    if (!controller_pad) return;
    for (int seat = 0; seat < PADS; seat++) {
        if (motion_source[seat] != controller_pad->instance) continue;
        motion_source[seat] = NO_PAD;
        sense_with(controller_pad, SDL_FALSE);
    }
}

static void store_effect(int seat, struct ff_effect *effect) {
    if (effect->id >= effect_count[seat]) {
        struct ff_effect *grown = realloc(effects[seat], (effect->id + 1) * sizeof *grown);
        if (!grown) { perror("padd: effects"); return; }
        memset(grown + effect_count[seat], 0, (effect->id + 1 - effect_count[seat]) * sizeof *grown);
        effects[seat] = grown;
        effect_count[seat] = effect->id + 1;
    }
    effects[seat][effect->id] = *effect;
}

static void rumble(int seat, int id, int count) {
    if (id >= effect_count[seat]) return;
    struct ff_effect *effect = &effects[seat][id];
    Uint16 strong = count ? (Uint32)effect->u.rumble.strong_magnitude * gain[seat] / GAIN_MAX : 0;
    Uint16 weak = count ? (Uint32)effect->u.rumble.weak_magnitude * gain[seat] / GAIN_MAX : 0;
    for (int i = 0; i < pad_count; i++)
        if (pads[i].input->pad == seat) SDL_GameControllerRumble(pads[i].controller, strong, weak, effect->replay.length);
}

static void read_feedback(int index) {
    int fd = watched[index].fd, seat = watches[index].seat;
    struct input_event event;
    if (read(fd, &event, sizeof event) != sizeof event) return;
    if (event.type == EV_UINPUT && event.code == UI_FF_UPLOAD) {
        struct uinput_ff_upload upload = {.request_id = event.value};
        if (ioctl(fd, UI_BEGIN_FF_UPLOAD, &upload) < 0) { perror("padd: upload"); return; }
        store_effect(seat, &upload.effect);
        ioctl(fd, UI_END_FF_UPLOAD, &upload);
    } else if (event.type == EV_UINPUT && event.code == UI_FF_ERASE) {
        struct uinput_ff_erase erase = {.request_id = event.value};
        if (ioctl(fd, UI_BEGIN_FF_ERASE, &erase) < 0) { perror("padd: erase"); return; }
        ioctl(fd, UI_END_FF_ERASE, &erase);
    } else if (event.type == EV_FF && event.code == FF_GAIN) {
        gain[seat] = event.value;
    } else if (event.type == EV_FF) {
        rumble(seat, event.code, event.value);
    }
}

static int same(const char *address, const char *other) { return *address && !strcasecmp(address, other); }

static struct pad *pad_with_address(const char *address) {
    for (int i = 0; i < pad_count; i++)
        if (same(pads[i].address, address)) return &pads[i];
    return NULL;
}

static struct bluetooth_device *known_at(const char *path) {
    for (int i = 0; i < known_count; i++)
        if (!strcmp(known[i].path, path)) return &known[i];
    return NULL;
}

static struct bluetooth_device *known_address(const char *address) {
    for (int i = 0; i < known_count; i++)
        if (same(known[i].address, address)) return &known[i];
    return NULL;
}

static void replace(char *text, char from, char to) {
    for (char *at = strchr(text, from); at; at = strchr(at, from)) *at = to;
}

static void address_of(const char *path, char *address) {
    const char *name = strrchr(path, '/');
    snprintf(address, ADDRESS_SIZE, "%s", name && !strncmp(name, DEVICE_PATH_PREFIX, strlen(DEVICE_PATH_PREFIX)) ? name + strlen(DEVICE_PATH_PREFIX) : "");
    replace(address, '_', ':');
}

static json_object *controller(const char *name, const char *address, const char *link, int battery, int charging, int paired) {
    json_object *entry = json_object_new_object();
    json_object_object_add(entry, "name", json_object_new_string(name));
    json_object_object_add(entry, "address", json_object_new_string(address));
    json_object_object_add(entry, "connection", link ? json_object_new_string(link) : NULL);
    json_object_object_add(entry, "battery", battery < 0 ? NULL : json_object_new_int(battery));
    json_object_object_add(entry, "charging", json_object_new_boolean(charging));
    json_object_object_add(entry, "paired", json_object_new_boolean(paired));
    return entry;
}

static json_object *device_entry(struct device *device) {
    json_object *entry = json_object_new_object(), *allowed = json_object_new_array();
    struct pad *controller_pad = pad_with_input(device);
    for (enum mode mode = ANALOG; mode < MODE_COUNT; mode++)
        if (kinds[device->kind].modes & MODE(mode)) json_object_array_add(allowed, json_object_new_string(modes[mode]));
    json_object_object_add(entry, "id", json_object_new_int(device->id));
    json_object_object_add(entry, "name", json_object_new_string(device->name));
    json_object_object_add(entry, "kind", json_object_new_string(kinds[device->kind].name));
    json_object_object_add(entry, "guide", json_object_new_boolean(device->guide));
    json_object_object_add(entry, "bluetooth", json_object_new_boolean(controller_pad && controller_pad->bus == BUS_BLUETOOTH));
    json_object_object_add(entry, "pad", device->pad == NO_PAD ? NULL : json_object_new_int(device->pad + 1));
    json_object_object_add(entry, "mode", json_object_new_string(modes[device->mode]));
    json_object_object_add(entry, "native", json_object_new_string(modes[kinds[device->kind].native]));
    json_object_object_add(entry, "modes", allowed);
    return entry;
}

static void write_controllers(void) {
    json_object *state = json_object_new_object(), *list = json_object_new_array(), *inputs = json_object_new_array();
    for (int i = 0; i < pad_count; i++) {
        struct bluetooth_device *device = known_address(pads[i].address);
        json_object *entry = controller(SDL_GameControllerName(pads[i].controller), pads[i].address,
                                        pads[i].bus == BUS_BLUETOOTH ? "bluetooth" : "wired", pads[i].battery, pads[i].charging, device && device->trusted);
        json_object_object_add(entry, "muted", json_object_new_boolean(pads[i].muted));
        json_object_array_add(list, entry);
    }
    for (int i = 0; i < known_count; i++)
        if (known[i].trusted && !pad_with_address(known[i].address))
            json_object_array_add(list, controller(known[i].name, known[i].address, NULL, NO_BATTERY, 0, 1));
    for (int j = 0; j < INPUT_DEVICES_MAX; j++)
        if (device_table[j].id) json_object_array_add(inputs, device_entry(&device_table[j]));
    json_object_object_add(state, "bluetooth", json_object_new_boolean(*adapter));
    json_object_object_add(state, "pairing", json_object_new_boolean(pairing != NOT_PAIRING));
    json_object_object_add(state, "controllers", list);
    json_object_object_add(state, "devices", inputs);
    if (json_object_to_file_ext(CONTROLLERS_PARTIAL, state, JSON_C_TO_STRING_PLAIN) < 0 || rename(CONTROLLERS_PARTIAL, CONTROLLERS_FILE) < 0)
        perror(CONTROLLERS_FILE);
    json_object_put(state);
}

static void announce(struct device *device) {
    write_controllers();
    tell_overlay("connect %d", device->id);
}

static int read_battery(struct pad *controller_pad) {
    struct udev_list_entry *entry;
    int battery = controller_pad->battery, charging = controller_pad->charging;
    if (!controller_pad->hid) return 0;
    struct udev_enumerate *supplies = children(controller_pad->hid, "power_supply");
    udev_list_entry_foreach(entry, udev_enumerate_get_list_entry(supplies)) {
        struct udev_device *supply = udev_device_new_from_syspath(udev, udev_list_entry_get_name(entry));
        const char *capacity = udev_device_get_sysattr_value(supply, "capacity"), *status = udev_device_get_sysattr_value(supply, "status");
        if (capacity) controller_pad->battery = atoi(capacity);
        controller_pad->charging = status && !strcmp(status, "Charging");
        udev_device_unref(supply);
    }
    udev_enumerate_unref(supplies);
    return battery != controller_pad->battery || charging != controller_pad->charging;
}

static void read_level(struct pad *controller_pad, SDL_JoystickPowerLevel level) {
    int low = level == SDL_JOYSTICK_POWER_LOW || level == SDL_JOYSTICK_POWER_EMPTY;
    if (low && controller_pad->level != SDL_JOYSTICK_POWER_LOW && controller_pad->level != SDL_JOYSTICK_POWER_EMPTY)
        notify("%s battery low", SDL_GameControllerName(controller_pad->controller));
    controller_pad->level = level;
    if (read_battery(controller_pad)) write_controllers();
}

static void identify(struct pad *controller_pad, const char *path) {
    struct stat status;
    struct udev_device *node_device = path && !stat(path, &status) ? udev_device_new_from_devnum(udev, 'c', status.st_rdev) : NULL;
    controller_pad->hid = udev_device_ref(udev_device_get_parent_with_subsystem_devtype(node_device, "hid", NULL));
    udev_device_unref(node_device);
    const char *uniq = udev_device_get_property_value(controller_pad->hid, "HID_UNIQ"), *id = udev_device_get_property_value(controller_pad->hid, "HID_ID");
    snprintf(controller_pad->address, sizeof controller_pad->address, "%s", uniq ? uniq : "");
    controller_pad->bus = id ? strtol(id, NULL, 16) : 0;
}

static void reannounce(struct pad *controller_pad) {
    struct udev_list_entry *entry;
    struct udev_enumerate *nodes = children(controller_pad->hid, "hidraw");
    udev_list_entry_foreach(entry, udev_enumerate_get_list_entry(nodes)) {
        struct udev_device *hidraw = udev_device_new_from_syspath(udev, udev_list_entry_get_name(entry));
        if (udev_device_set_sysattr_value(hidraw, "uevent", UEVENT_ADD) < 0) perror(udev_list_entry_get_name(entry));
        udev_device_unref(hidraw);
    }
    udev_enumerate_unref(nodes);
}

static void call(const char *path, const char *interface, const char *method, sd_bus_message_handler_t done, void *userdata, const char *types, ...) {
    va_list arguments;
    va_start(arguments, types);
    int error = sd_bus_call_method_asyncv(bus, NULL, BLUEZ, path, interface, method, done, userdata, types, arguments);
    va_end(arguments);
    if (error < 0) fprintf(stderr, "padd: %s: %s\n", method, strerror(-error));
}

static void power_off(struct bluetooth_device *device) {
    if (device) call(device->path, DEVICE_INTERFACE, "Disconnect", NULL, NULL, "");
}

static void schedule_sleep(void) {
    time_t current = now(), earliest = 0;
    for (int i = 0; sleep_minutes && i < pad_count; i++) {
        time_t deadline = pads[i].active + sleep_minutes * SECONDS_PER_MINUTE;
        if (pads[i].bus != BUS_BLUETOOTH) continue;
        if (deadline > current) earliest = earliest && earliest < deadline ? earliest : deadline;
        else power_off(known_address(pads[i].address));
    }
    timerfd_settime(watched[SLEEP].fd, 0, &(struct itimerspec){.it_value.tv_sec = earliest ? earliest - current : 0}, NULL);
}

static void read_sleep(void) {
    json_object *settings = json_object_from_file(SETTINGS_FILE), *minutes;
    sleep_minutes = json_object_object_get_ex(settings, "sleep", &minutes) ? json_object_get_int(minutes) : atoi(SLEEP_MINUTES);
    json_object_put(settings);
    schedule_sleep();
}

static void trust(struct bluetooth_device *device) {
    device->trusted = 1;
    call(device->path, PROPERTIES_INTERFACE, "Set", NULL, NULL, "ssv", DEVICE_INTERFACE, "Trusted", "b", 1);
}

static void stop_pairing(void) {
    if (pairing == NOT_PAIRING) return;
    if (pairing == SEARCHING && *adapter) call(adapter, ADAPTER_INTERFACE, "StopDiscovery", NULL, NULL, "");
    pairing = NOT_PAIRING;
    *target = '\0';
    timerfd_settime(watched[PAIRING].fd, 0, &(struct itimerspec){0}, NULL);
    write_controllers();
}

static int on_paired(sd_bus_message *reply, void *data, sd_bus_error *error) {
    (void)data, (void)error;
    struct bluetooth_device *device = known_address(target);
    if (!device || sd_bus_message_is_method_error(reply, NULL)) {
        notify("Could not pair %s", device ? device->name : "the controller");
        stop_pairing();
        return 0;
    }
    trust(device);
    call(device->path, DEVICE_INTERFACE, "Connect", NULL, NULL, "");
    notify("Paired %s", device->name);
    stop_pairing();
    return 0;
}

static void consider(struct bluetooth_device *device) {
    if (pairing != SEARCHING || !device || !device->seen || device->paired || strcmp(device->icon, GAMEPAD_ICON)) return;
    call(adapter, ADAPTER_INTERFACE, "StopDiscovery", NULL, NULL, "");
    pairing = BONDING;
    snprintf(target, sizeof target, "%s", device->address);
    call(device->path, DEVICE_INTERFACE, "Pair", on_paired, NULL, "");
}

static void start_pairing(void) {
    if (!*adapter) { notify("No Bluetooth adapter"); return; }
    pairing = SEARCHING;
    for (int i = 0; i < known_count; i++) known[i].seen = 0;
    call(adapter, ADAPTER_INTERFACE, "StartDiscovery", NULL, NULL, "");
    timerfd_settime(watched[PAIRING].fd, 0, &(struct itimerspec){.it_value.tv_sec = PAIRING_SECONDS}, NULL);
    write_controllers();
}

static void forget(const char *address) {
    struct bluetooth_device *device = known_address(address);
    if (device && *adapter) call(adapter, ADAPTER_INTERFACE, "RemoveDevice", NULL, NULL, "o", device->path);
}

static int offered_index(const char *address) {
    for (int i = 0; i < offered_count; i++)
        if (same(offered[i], address)) return i;
    return -1;
}

static void withdraw(const char *address) {
    int index = offered_index(address);
    if (index >= 0) memcpy(offered[index], offered[--offered_count], ADDRESS_SIZE);
}

static void adopt(struct pad *controller_pad) {
    struct bluetooth_device *device = known_address(controller_pad->address);
    if (controller_pad->bus != BUS_USB || !*adapter || (device && device->trusted) || offered_index(controller_pad->address) < 0) return;
    snprintf(armed, sizeof armed, "%s", controller_pad->address);
    reannounce(controller_pad);
    notify("Pairing %s", SDL_GameControllerName(controller_pad->controller));
}

static int reject(sd_bus_message *message, void *data, sd_bus_error *error) {
    (void)data, (void)error;
    return sd_bus_reply_method_errorf(message, REJECTED, "anyconsole is not pairing this device");
}

static int acknowledge(sd_bus_message *message, void *data, sd_bus_error *error) {
    (void)data, (void)error;
    return sd_bus_reply_method_return(message, NULL);
}

static int requested(sd_bus_message *message, char *address) {
    const char *path;
    struct bluetooth_device *device;
    if (sd_bus_message_read(message, "o", &path) <= 0) return 0;
    address_of(path, address);
    device = known_at(path);
    return (device && device->trusted) || same(address, target) || same(address, armed);
}

static int confirm(sd_bus_message *message, void *data, sd_bus_error *error) {
    char address[ADDRESS_SIZE];
    return requested(message, address) ? acknowledge(message, data, error) : reject(message, data, error);
}

static int request_pin(sd_bus_message *message, void *data, sd_bus_error *error) {
    char address[ADDRESS_SIZE];
    return requested(message, address) ? sd_bus_reply_method_return(message, "s", LEGACY_PIN) : reject(message, data, error);
}

static int authorize(sd_bus_message *message, void *data, sd_bus_error *error) {
    char address[ADDRESS_SIZE];
    struct pad *controller_pad;
    if (!requested(message, address)) {
        if (*address && offered_index(address) < 0 && offered_count < PHYSICAL_PADS_MAX)
            snprintf(offered[offered_count++], ADDRESS_SIZE, "%s", address);
        return reject(message, data, error);
    }
    struct bluetooth_device *device = known_address(address);
    if (device) trust(device);
    if (same(address, armed)) {
        controller_pad = pad_with_address(address);
        notify("Paired %s", controller_pad ? SDL_GameControllerName(controller_pad->controller) : address);
        *armed = '\0';
        withdraw(address);
        write_controllers();
    }
    return acknowledge(message, data, error);
}

static const sd_bus_vtable agent[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_METHOD("Release", "", "", acknowledge, 0),
    SD_BUS_METHOD("RequestPinCode", "o", "s", request_pin, 0),
    SD_BUS_METHOD("DisplayPinCode", "os", "", acknowledge, 0),
    SD_BUS_METHOD("RequestPasskey", "o", "u", reject, 0),
    SD_BUS_METHOD("DisplayPasskey", "ouq", "", acknowledge, 0),
    SD_BUS_METHOD("RequestConfirmation", "ou", "", confirm, 0),
    SD_BUS_METHOD("RequestAuthorization", "o", "", confirm, 0),
    SD_BUS_METHOD("AuthorizeService", "os", "", authorize, 0),
    SD_BUS_METHOD("Cancel", "", "", acknowledge, 0),
    SD_BUS_VTABLE_END,
};

static struct bluetooth_device *remember(const char *path) {
    struct bluetooth_device *device = known_at(path);
    if (device || known_count == BLUETOOTH_DEVICES_MAX) return device;
    device = &known[known_count++];
    *device = (struct bluetooth_device){0};
    snprintf(device->path, sizeof device->path, "%s", path);
    address_of(path, device->address);
    return device;
}

static int read_string(sd_bus_message *message, char *text, size_t size) {
    const char *value;
    if (sd_bus_message_read(message, "v", "s", &value) <= 0 || !strcmp(text, value)) return 0;
    snprintf(text, size, "%s", value);
    return 1;
}

static int read_flag(sd_bus_message *message, int *flag) {
    int value = 0;
    sd_bus_message_read(message, "v", "b", &value);
    if (*flag == value) return 0;
    *flag = value;
    return 1;
}

static int read_properties(sd_bus_message *message, struct bluetooth_device *device) {
    const char *name;
    int changed = 0;
    if (!device) {
        sd_bus_message_skip(message, "a{sv}");
        return 0;
    }
    sd_bus_message_enter_container(message, 'a', "{sv}");
    while (sd_bus_message_enter_container(message, 'e', "sv") > 0) {
        sd_bus_message_read(message, "s", &name);
        if (!strcmp(name, "Alias")) changed |= read_string(message, device->name, sizeof device->name);
        else if (!strcmp(name, "Icon")) read_string(message, device->icon, sizeof device->icon);
        else if (!strcmp(name, "Paired")) changed |= read_flag(message, &device->paired);
        else if (!strcmp(name, "Trusted")) changed |= read_flag(message, &device->trusted);
        else {
            device->seen |= !strcmp(name, "RSSI");
            sd_bus_message_skip(message, "v");
        }
        sd_bus_message_exit_container(message);
    }
    sd_bus_message_exit_container(message);
    consider(device);
    return changed;
}

static int read_object(sd_bus_message *message) {
    const char *path, *interface;
    int changed = 0;
    sd_bus_message_read(message, "o", &path);
    sd_bus_message_enter_container(message, 'a', "{sa{sv}}");
    while (sd_bus_message_enter_container(message, 'e', "sa{sv}") > 0) {
        sd_bus_message_read(message, "s", &interface);
        if (!strcmp(interface, DEVICE_INTERFACE)) {
            changed |= read_properties(message, remember(path));
        } else {
            if (!strcmp(interface, ADAPTER_INTERFACE) && !*adapter) {
                snprintf(adapter, sizeof adapter, "%s", path);
                for (int i = 0; i < pad_count; i++)
                    if (pads[i].bus == BUS_USB) reannounce(&pads[i]);
                changed = 1;
            }
            sd_bus_message_skip(message, "a{sv}");
        }
        sd_bus_message_exit_container(message);
    }
    sd_bus_message_exit_container(message);
    return changed;
}

static int on_objects(sd_bus_message *reply, void *data, sd_bus_error *error) {
    (void)data, (void)error;
    if (sd_bus_message_is_method_error(reply, NULL)) return 0;
    sd_bus_message_enter_container(reply, 'a', "{oa{sa{sv}}}");
    while (sd_bus_message_enter_container(reply, 'e', "oa{sa{sv}}") > 0) {
        read_object(reply);
        sd_bus_message_exit_container(reply);
    }
    write_controllers();
    return 0;
}

static int on_interfaces_added(sd_bus_message *message, void *data, sd_bus_error *error) {
    (void)data, (void)error;
    if (read_object(message)) write_controllers();
    return 0;
}

static int on_interfaces_removed(sd_bus_message *message, void *data, sd_bus_error *error) {
    (void)data, (void)error;
    const char *path, *interface;
    struct bluetooth_device *device;
    sd_bus_message_read(message, "o", &path);
    sd_bus_message_enter_container(message, 'a', "s");
    while (sd_bus_message_read(message, "s", &interface) > 0) {
        if (!strcmp(interface, DEVICE_INTERFACE) && (device = known_at(path))) *device = known[--known_count];
        if (!strcmp(interface, ADAPTER_INTERFACE) && !strcmp(path, adapter)) {
            *adapter = '\0';
            stop_pairing();
        }
    }
    write_controllers();
    return 0;
}

static int on_properties_changed(sd_bus_message *message, void *data, sd_bus_error *error) {
    (void)data, (void)error;
    const char *interface;
    struct bluetooth_device *device = known_at(sd_bus_message_get_path(message));
    if (sd_bus_message_read(message, "s", &interface) <= 0 || strcmp(interface, DEVICE_INTERFACE) || !device) return 0;
    if (read_properties(message, device)) write_controllers();
    return 0;
}

static void forget_bluez(void) {
    stop_pairing();
    known_count = 0;
    *adapter = '\0';
}

static void bluez_up(void) {
    forget_bluez();
    call(BLUEZ_ROOT, AGENT_MANAGER_INTERFACE, "RegisterAgent", NULL, NULL, "os", AGENT_PATH, AGENT_CAPABILITY);
    call(BLUEZ_ROOT, AGENT_MANAGER_INTERFACE, "RequestDefaultAgent", NULL, NULL, "o", AGENT_PATH);
    call("/", OBJECT_MANAGER_INTERFACE, "GetManagedObjects", on_objects, NULL, "");
}

static void bluez_down(void) {
    forget_bluez();
    write_controllers();
}

static int on_bluez_owner(sd_bus_message *message, void *data, sd_bus_error *error) {
    (void)data, (void)error;
    const char *name, *old_owner, *new_owner;
    if (sd_bus_message_read(message, "sss", &name, &old_owner, &new_owner) <= 0) return 0;
    if (*new_owner) bluez_up();
    else bluez_down();
    return 0;
}

static void open_bus(void) {
    if (sd_bus_new(&bus) < 0 || sd_bus_set_address(bus, SYSTEM_BUS) < 0 || sd_bus_set_bus_client(bus, 1) < 0 ||
        sd_bus_set_watch_bind(bus, 1) < 0 || sd_bus_start(bus) < 0 ||
        sd_bus_add_object_vtable(bus, NULL, AGENT_PATH, AGENT_INTERFACE, agent, NULL) < 0 ||
        sd_bus_add_match_async(bus, NULL, BLUEZ_OWNER_MATCH, on_bluez_owner, NULL, NULL) < 0 ||
        sd_bus_match_signal_async(bus, NULL, BLUEZ, NULL, OBJECT_MANAGER_INTERFACE, "InterfacesAdded", on_interfaces_added, NULL, NULL) < 0 ||
        sd_bus_match_signal_async(bus, NULL, BLUEZ, NULL, OBJECT_MANAGER_INTERFACE, "InterfacesRemoved", on_interfaces_removed, NULL, NULL) < 0 ||
        sd_bus_match_signal_async(bus, NULL, BLUEZ, NULL, PROPERTIES_INTERFACE, "PropertiesChanged", on_properties_changed, NULL, NULL) < 0) {
        fprintf(stderr, "padd: cannot use the system bus\n");
        bus = sd_bus_unref(bus);
        return;
    }
    bluez_up();
}

static int bus_timeout(void) {
    uint64_t until;
    struct timespec time;
    if (!bus || sd_bus_get_timeout(bus, &until) < 0 || until == UINT64_MAX) return -1;
    clock_gettime(CLOCK_MONOTONIC, &time);
    uint64_t current = (uint64_t)time.tv_sec * USEC_PER_SEC + time.tv_nsec / NSEC_PER_USEC;
    return until > current ? (until - current + USEC_PER_MSEC - 1) / USEC_PER_MSEC : 0;
}

static void process_bus(void) {
    int status = 0;
    while (bus && (status = sd_bus_process(bus, NULL)) > 0) {}
    if (status >= 0) return;
    bus = sd_bus_flush_close_unref(bus);
    bluez_down();
    open_bus();
}

static void show_seat(SDL_GameController *controller, int seat) {
    if (seat == NO_PAD || SDL_GameControllerGetType(controller) == SDL_CONTROLLER_TYPE_PS5) SDL_GameControllerSetLED(controller, 0, 0, 0);
    else SDL_GameControllerSetLED(controller, seat_colours[seat].red, seat_colours[seat].green, seat_colours[seat].blue);
    SDL_GameControllerSetPlayerIndex(controller, seat);
}

static void assign(struct device *device, int seat) {
    struct pad *controller_pad = pad_with_input(device);
    if (device->pad == seat) return;
    release(device);
    forget_motion(controller_pad);
    device->pad = seat;
    if (controller_pad) {
        show_seat(controller_pad->controller, seat);
        touched(controller_pad);
    }
    write_controllers();
}

static void set_mode(struct device *device, const char *name) {
    int mode = mode_named(name);
    if (mode < 0 || !(kinds[device->kind].modes & MODE(mode)) || (enum mode)mode == device->mode) return;
    release(device);
    forget_motion(pad_with_input(device));
    device->mode = mode;
    write_controllers();
}

static void add_route(const char *source, const char *target) {
    int sign = (*source == '+') - (*source == '-');
    int from = control_named(source + abs(sign)), to = control_named(target);
    if (from < 0 || to < 0 || (sign && !is_stick(from))) { fprintf(stderr, "padd: cannot map %s to %s\n", source, target); return; }
    routes[route_count++] = (struct route){from, sign, to};
    rerouted[from] |= sign < 0 ? NEGATIVE_SIDE : sign > 0 ? POSITIVE_SIDE : BOTH_SIDES;
}

static void load_maps(const char *game) {
    char path[PATH_MAX], slug[NAME_MAX + 1];
    json_object *manifest = NULL, *config = NULL, *keys, *pad_map;
    release_all();
    memset(keymap, 0, sizeof keymap);
    memset(rerouted, 0, sizeof rerouted);
    route_count = 0;
    const char *platform = strchr(game, '/');
    if (!platform) return;
    snprintf(slug, sizeof slug, "%.*s", (int)(platform - game), game);
    snprintf(path, sizeof path, GAMES_DIR "/%s/" MANIFEST, slug);
    manifest = json_object_from_file(path);
    json_object_object_get_ex(manifest, platform + 1, &config);
    if (json_object_object_get_ex(config, "keys", &keys)) {
        json_object_object_foreach(keys, control, code) {
            int i = control_named(control), value = libevdev_event_code_from_name(EV_KEY, json_object_get_string(code));
            if (i >= 0 && value >= 0) keymap[i] = value;
        }
    }
    if (json_object_object_get_ex(config, "pad", &pad_map)) {
        json_object_object_foreach(pad_map, source, target) add_route(source, json_object_get_string(target));
    }
    json_object_put(manifest);
}

static void set_menu(int open) {
    if (open) release_all();
    menu_open = open;
}

static void set_pads(int count) {
    for (int seat = 0; seat < PADS; seat++) {
        if (seat < count || !seat) plug(seat);
        else unplug(seat);
    }
}

static struct device *remote_device(struct device **slot, const char *name, enum kind kind, int guide) {
    if (*slot) return *slot;
    *slot = new_device(name, kind, guide);
    if (*slot) announce(*slot);
    return *slot;
}

static struct device *remote_pad(int number) {
    char name[NAME_MAX + 1];
    if (number < 1 || number > PADS) return NULL;
    snprintf(name, sizeof name, REMOTE_PAD_NAME " %d", number);
    return remote_device(&remote_pads[number - 1], name, PAD, 1);
}

static void detach(SDL_JoystickID instance);

static void command(char *line) {
    char words[3][WORD_MAX + 1];
    int count = sscanf(line, "%" STRINGIFY(WORD_MAX) "s %" STRINGIFY(WORD_MAX) "s %" STRINGIFY(WORD_MAX) "s", words[0], words[1], words[2]);
    struct device *device = count == 3 ? device_with_id(atoi(words[1])) : NULL, *remote;
    if (count == 1 && !strcmp(words[0], "pair")) {
        if (pairing == NOT_PAIRING) start_pairing();
        else stop_pairing();
    } else if (count == 1 && !strcmp(words[0], "batteries")) {
        int changed = 0;
        for (int i = 0; i < pad_count; i++) changed |= read_battery(&pads[i]);
        if (changed) write_controllers();
    } else if (count == 2 && !strcmp(words[0], "forget")) {
        forget(words[1]);
    } else if (count == 2 && !strcmp(words[0], "off")) {
        struct pad *controller_pad = pad_with_input(device_with_id(atoi(words[1])));
        if (controller_pad && controller_pad->bus == BUS_BLUETOOTH) {
            struct bluetooth_device *bluetooth = known_address(controller_pad->address);
            detach(controller_pad->instance);
            power_off(bluetooth);
        }
    } else if (count == 2 && !strcmp(words[0], "touchpad")) {
        touchpad_pointer = !strcmp(words[1], "pointer");
    } else if (count == 2 && !strcmp(words[0], "game")) {
        load_maps(strcmp(words[1], NO_GAME) ? words[1] : "");
    } else if (count == 2 && !strcmp(words[0], "menu")) {
        set_menu(atoi(words[1]));
    } else if (count == 2 && !strcmp(words[0], "pads")) {
        set_pads(atoi(words[1]));
    } else if (device && !strcmp(words[0], "assign") && atoi(words[2]) >= 0 && atoi(words[2]) <= PADS) {
        assign(device, atoi(words[2]) - 1);
    } else if (device && !strcmp(words[0], "preview") && atoi(words[2]) >= 0 && atoi(words[2]) <= PADS) {
        struct pad *controller_pad = pad_with_input(device);
        if (controller_pad) show_seat(controller_pad->controller, atoi(words[2]) ? atoi(words[2]) - 1 : device->pad);
    } else if (device && !strcmp(words[0], "mode")) {
        set_mode(device, words[2]);
    } else if (count == 3 && (remote = remote_pad(atoi(words[0])))) {
        pad_input(remote, control_named(words[1]), atoi(words[2]));
    } else {
        fprintf(stderr, "bad command: %s", line);
    }
}

static void inject(const char *line) {
    char type_name[WORD_MAX + 1], code_name[WORD_MAX + 1];
    int value, type, code;
    if (sscanf(line, "%" STRINGIFY(WORD_MAX) "s %" STRINGIFY(WORD_MAX) "s %d", type_name, code_name, &value) != 3 ||
        (type = libevdev_event_type_from_name(type_name)) < 0 || (code = libevdev_event_code_from_name(type, code_name)) < 0) {
        fprintf(stderr, "bad event: %s", line);
        return;
    }
    if (!remote_device(&remote_keyboard, REMOTE_KEYBOARD_NAME, KEYBOARD, 0)) return;
    key_input(remote_keyboard, &(struct input_event){.type = type, .code = code, .value = value});
    key_input(remote_keyboard, &(struct input_event){.type = EV_SYN, .code = SYN_REPORT});
}

static void read_commands(int fd) {
    static char pending[BUFSIZ];
    static size_t length;
    ssize_t count = read(fd, pending + length, sizeof pending - length);
    if (count <= 0) return;
    length += count;
    char *start = pending, line[sizeof pending + 1];
    for (char *end; (end = memchr(start, '\n', pending + length - start)); start = end + 1) {
        snprintf(line, sizeof line, "%.*s", (int)(end - start + 1), start);
        if (!strncmp(line, "EV_", strlen("EV_"))) inject(line);
        else command(line);
    }
    length -= start - pending;
    memmove(pending, start, length);
    if (length == sizeof pending) length = 0;
}

static int expired(int timer) {
    uint64_t expirations;
    return read(timer, &expirations, sizeof expirations) > 0;
}

static void capture(const char *node_name) {
    char path[PATH_MAX];
    struct libevdev *evdev = NULL;
    snprintf(path, sizeof path, DEVICES "/%s", node_name);
    if (strncmp(node_name, PAD_NODE, strlen(PAD_NODE)) || is_virtual(path)) return;
    int fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) { perror(path); return; }
    int keys = !libevdev_new_from_fd(fd, &evdev) && libevdev_has_event_code(evdev, EV_KEY, KEY_A);
    int pointer = evdev && libevdev_has_event_code(evdev, EV_REL, REL_X);
    struct device *input = keys || pointer ? new_device(libevdev_get_name(evdev), keys ? KEYBOARD : MOUSE, 0) : NULL;
    struct watch *captured = input ? watch(fd, CAPTURED) : NULL;
    if (!captured) {
        if (input) input->id = 0;
        libevdev_free(evdev);
        close(fd);
        return;
    }
    libevdev_grab(evdev, LIBEVDEV_GRAB);
    input->evdev = evdev;
    captured->input = input;
    notify("Connected %s", libevdev_get_name(evdev));
    announce(input);
}

static void pass_through(int index) {
    struct device *input = watches[index].input;
    struct input_event event;
    int status;
    while ((status = libevdev_next_event(input->evdev, LIBEVDEV_READ_FLAG_NORMAL, &event)) >= 0) {
        key_input(input, &event);
        if (status == LIBEVDEV_READ_STATUS_SYNC)
            while (libevdev_next_event(input->evdev, LIBEVDEV_READ_FLAG_SYNC, &event) == LIBEVDEV_READ_STATUS_SYNC) key_input(input, &event);
    }
    if (status == -EAGAIN) return;
    notify("Disconnected %s", libevdev_get_name(input->evdev));
    free_device(input);
    libevdev_free(input->evdev);
    unwatch(index);
    write_controllers();
}

static void capture_devices(struct udev_enumerate *inputs) {
    struct udev_list_entry *entry;
    udev_enumerate_add_match_subsystem(inputs, "input");
    udev_enumerate_scan_devices(inputs);
    udev_list_entry_foreach(entry, udev_enumerate_get_list_entry(inputs)) capture(basename(udev_list_entry_get_name(entry)));
    udev_enumerate_unref(inputs);
}

static void read_hotplug(void) {
    for (struct udev_device *device; (device = udev_monitor_receive_device(monitor)); udev_device_unref(device)) {
        const char *subsystem = udev_device_get_subsystem(device);
        if (!strcmp(subsystem, "input") && !strcmp(udev_device_get_action(device), "add")) capture(udev_device_get_sysname(device));
        if (strcmp(subsystem, "power_supply")) continue;
        struct udev_device *hid = udev_device_get_parent_with_subsystem_devtype(device, "hid", NULL);
        for (int i = 0; hid && i < pad_count; i++)
            if (pads[i].hid && !strcmp(udev_device_get_syspath(pads[i].hid), udev_device_get_syspath(hid)) && read_battery(&pads[i]))
                write_controllers();
    }
}

static void read_settings_change(int fd) {
    char buffer[BUFSIZ] __attribute__((aligned(__alignof__(struct inotify_event))));
    ssize_t count = read(fd, buffer, sizeof buffer);
    for (char *at = buffer; count > 0 && at < buffer + count;) {
        struct inotify_event *event = (struct inotify_event *)at;
        if (event->len && !strcmp(event->name, basename(SETTINGS_FILE))) read_sleep();
        at += sizeof *event + event->len;
    }
}

static double past_deadzone(double magnitude, double deadzone) {
    return magnitude > deadzone ? fmin((magnitude - deadzone) / (1 - deadzone), 1) : 0;
}

static double unit_axis(SDL_GameController *controller, SDL_GameControllerAxis axis) {
    return SDL_GameControllerGetAxis(controller, axis) / (double)SDL_JOYSTICK_AXIS_MAX;
}

static void drive_axis(struct pad *source, SDL_GameControllerAxis axis) {
    SDL_GameController *controller = source->controller;
    if (axis >= SDL_CONTROLLER_AXIS_TRIGGERLEFT) {
        pad_input(source->input, axis_control[axis], lround(past_deadzone(unit_axis(controller, axis), TRIGGER_DEADZONE) * AXIS_MAX));
        return;
    }
    SDL_GameControllerAxis horizontal = axis - axis % 2, vertical = horizontal + 1;
    double x = unit_axis(controller, horizontal), y = unit_axis(controller, vertical), magnitude = hypot(x, y);
    double gain = magnitude ? past_deadzone(magnitude, STICK_DEADZONE) / magnitude : 0;
    pad_input(source->input, axis_control[horizontal], axis_value(x * gain));
    pad_input(source->input, axis_control[vertical], axis_value(y * gain));
}

static void put_crc(unsigned char *at, unsigned char header, const unsigned char *report, size_t size) {
    uint32_t crc = crc32(crc32(0, &header, 1), report, size - REPORT_CRC_SIZE);
    for (int i = 0; i < REPORT_CRC_SIZE; i++) at[i] = crc >> (i * CHAR_BIT);
}

static int valid_crc(const unsigned char *report, size_t size) {
    unsigned char expected[REPORT_CRC_SIZE];
    put_crc(expected, INPUT_HEADER, report, size);
    return !memcmp(expected, report + size - REPORT_CRC_SIZE, REPORT_CRC_SIZE);
}

static void microphone_path(struct pad *controller_pad, char *path, const char *suffix) {
    snprintf(path, PATH_MAX, MICROPHONES_DIR "/%s.%s", controller_pad->address, suffix);
}

static void speak(void) {
    static const unsigned char audio_control[] = {0x7f, 0x46, 0x1f, 0x64, 0x28, 0x1d};
    int speaking = 0;
    for (int i = 0; i < pad_count; i++) {
        if (pads[i].microphone < 0) continue;
        unsigned char report[SPEAKER_REPORT_SIZE] = {SPEAKER_REPORT, (pads[i].sequence++ & SEQUENCE_MASK) << SEQUENCE_SHIFT, AUDIO_CONTROL_SUBPACKET, sizeof audio_control};
        memcpy(report + SUBPACKET_DATA, audio_control, sizeof audio_control);
        put_crc(report + sizeof report - REPORT_CRC_SIZE, OUTPUT_HEADER, report, sizeof report);
        if (write(pads[i].hidraw, report, sizeof report) < 0 && errno != EAGAIN) perror("padd: speaker");
        speaking = 1;
    }
    if (!speaking) timerfd_settime(watched[SPEAKER].fd, 0, &(struct itimerspec){0}, NULL);
}

static void read_state(int index) {
    unsigned char report[BUFSIZ];
    struct pad *controller_pad = pad_of(watches[index].pad);
    for (ssize_t size; (size = read(watched[index].fd, report, sizeof report)) > 0;) {
        if (!controller_pad || controller_pad->microphone < 0 || size != MICROPHONE_REPORT_SIZE || report[0] != STATE_REPORT) continue;
        int muted = !!(report[MUTE_STATUS_BYTE] & MUTE_STATUS_BIT);
        if (muted == controller_pad->muted) continue;
        controller_pad->muted = muted;
        write_controllers();
    }
}

static void start_tunnel(struct pad *controller_pad) {
    char config[PATH_MAX], fifo[PATH_MAX], name[NAME_MAX + 1];
    microphone_path(controller_pad, config, "conf");
    microphone_path(controller_pad, fifo, "fifo");
    snprintf(name, sizeof name, "dualsense_microphone.%s", controller_pad->address);
    replace(name, ':', '_');
    FILE *file = fopen(config, "w");
    if (!file || (mkfifo(fifo, S_IRUSR | S_IWUSR) < 0 && errno != EEXIST)) { perror(fifo); if (file) fclose(file); return; }
    fprintf(file, TUNNEL_CONFIG, fifo, AUDIO_RATE, MICROPHONE_CHANNELS, name, SDL_GameControllerName(controller_pad->controller), controller_pad->address);
    fclose(file);
    struct watch *exit_watch = watch(spawn_child((char *[]){"pipewire", "-c", config, NULL}, 0, &controller_pad->tunnel), TUNNEL);
    if (exit_watch) exit_watch->pad = controller_pad->instance;
}

static void reap_tunnel(int index) {
    struct pad *controller_pad = pad_of(watches[index].pad);
    reap(watched[index].fd, "microphone tunnel");
    if (controller_pad) controller_pad->tunnel = 0;
    unwatch(index);
}

static int acquired_microphone(sd_bus_message *reply, void *userdata, sd_bus_error *error) {
    struct pad *controller_pad = pad_of((SDL_JoystickID)(intptr_t)userdata);
    int fd, status;
    (void)error;
    if (!controller_pad || controller_pad->microphone >= 0) return 0;
    if (sd_bus_message_is_method_error(reply, NULL) || sd_bus_message_read(reply, "h", &fd) < 0) {
        fprintf(stderr, "padd: AcquireMicrophone: %s\n", sd_bus_message_get_error(reply) ? sd_bus_message_get_error(reply)->message : "no fd");
        return 0;
    }
    controller_pad->decoder = opus_decoder_create(AUDIO_RATE, MICROPHONE_CHANNELS, &status);
    if (!controller_pad->decoder) { fprintf(stderr, "padd: opus: %s\n", opus_strerror(status)); return 0; }
    controller_pad->microphone = fcntl(fd, F_DUPFD_CLOEXEC, 0);
    struct watch *frames = watch(controller_pad->microphone, MICROPHONE);
    if (frames) frames->pad = controller_pad->instance;
    start_tunnel(controller_pad);
    struct timespec period = {.tv_nsec = AUDIO_FRAME_MS * NSEC_PER_MSEC};
    timerfd_settime(watched[SPEAKER].fd, 0, &(struct itimerspec){.it_value = period, .it_interval = period}, NULL);
    return 0;
}

static void start_microphone(struct pad *controller_pad) {
    struct bluetooth_device *device = known_address(controller_pad->address);
    if (controller_pad->bus != BUS_BLUETOOTH || SDL_GameControllerGetType(controller_pad->controller) != SDL_CONTROLLER_TYPE_PS5 || !device || !bus) return;
    call(device->path, INPUT_INTERFACE, "AcquireMicrophone", acquired_microphone, (void *)(intptr_t)controller_pad->instance, "");
}

static void unwatch_pad(enum source source, SDL_JoystickID instance) {
    for (int i = watched_count - 1; i >= FIXED_SOURCES; i--)
        if (watches[i].source == source && watches[i].pad == instance) unwatch(i);
}

static void stop_microphone(struct pad *controller_pad) {
    char path[PATH_MAX];
    unwatch_pad(MICROPHONE, controller_pad->instance);
    if (controller_pad->fifo >= 0) close(controller_pad->fifo);
    if (controller_pad->tunnel) kill(controller_pad->tunnel, SIGTERM);
    opus_decoder_destroy(controller_pad->decoder);
    microphone_path(controller_pad, path, "conf");
    unlink(path);
    microphone_path(controller_pad, path, "fifo");
    unlink(path);
    controller_pad->microphone = controller_pad->fifo = -1;
    controller_pad->muted = 0;
    controller_pad->decoder = NULL;
}

static void hear(int index) {
    char fifo[PATH_MAX];
    unsigned char frame[MICROPHONE_REPORT_SIZE];
    opus_int16 samples[AUDIO_FRAME_SAMPLES];
    struct pad *controller_pad = pad_of(watches[index].pad);
    ssize_t size = read(watched[index].fd, frame, sizeof frame);
    if (!controller_pad || size <= 0) { if (controller_pad) stop_microphone(controller_pad); else unwatch(index); return; }
    if (size != sizeof frame || !valid_crc(frame, sizeof frame)) return;
    int count = opus_decode(controller_pad->decoder, frame + MICROPHONE_OPUS_OFFSET, MICROPHONE_OPUS_SIZE, samples, AUDIO_FRAME_SAMPLES, 0);
    if (count <= 0) return;
    if (controller_pad->muted) memset(samples, 0, count * sizeof *samples);
    if (controller_pad->fifo < 0) {
        microphone_path(controller_pad, fifo, "fifo");
        controller_pad->fifo = open(fifo, O_WRONLY | O_NONBLOCK | O_CLOEXEC);
    }
    if (controller_pad->fifo >= 0 && write(controller_pad->fifo, samples, count * sizeof *samples) < 0 && errno != EAGAIN) {
        close(controller_pad->fifo);
        controller_pad->fifo = -1;
    }
}

static void attach(int index) {
    const char *path = SDL_JoystickPathForIndex(index);
    if (path && is_virtual(path)) return;
    if (pad_count == PHYSICAL_PADS_MAX) { fprintf(stderr, "padd: too many pads\n"); return; }
    SDL_GameController *game_controller = SDL_GameControllerOpen(index);
    if (!game_controller) { fprintf(stderr, "%s: %s\n", SDL_JoystickNameForIndex(index), SDL_GetError()); return; }
    struct device *input = new_device(SDL_GameControllerName(game_controller), PAD, SDL_GameControllerHasButton(game_controller, SDL_CONTROLLER_BUTTON_GUIDE));
    if (!input) { SDL_GameControllerClose(game_controller); return; }
    show_seat(game_controller, NO_PAD);
    struct pad *controller_pad = &pads[pad_count++];
    *controller_pad = (struct pad){.controller = game_controller, .instance = SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(game_controller)),
                                   .input = input, .battery = NO_BATTERY, .level = SDL_JoystickCurrentPowerLevel(SDL_GameControllerGetJoystick(game_controller)),
                                   .microphone = -1, .fifo = -1};
    identify(controller_pad, path);
    read_battery(controller_pad);
    touched(controller_pad);
    notify("Connected %s", SDL_GameControllerName(game_controller));
    controller_pad->hidraw = open(path, O_RDWR | O_NONBLOCK | O_CLOEXEC);
    struct watch *physical = watch(controller_pad->hidraw, PHYSICAL_PAD);
    if (physical) physical->pad = controller_pad->instance;
    start_microphone(controller_pad);
    announce(input);
    schedule_sleep();
}

static void detach(SDL_JoystickID instance) {
    struct pad *controller_pad = pad_of(instance);
    if (!controller_pad) return;
    notify("Disconnected %s", SDL_GameControllerName(controller_pad->controller));
    unwatch_pad(PHYSICAL_PAD, instance);
    withdraw(controller_pad->address);
    stop_microphone(controller_pad);
    forget_motion(controller_pad);
    free_device(controller_pad->input);
    udev_device_unref(controller_pad->hid);
    SDL_GameControllerClose(controller_pad->controller);
    *controller_pad = pads[--pad_count];
    write_controllers();
}

static int guide_button(struct pad *controller_pad) {
    SDL_GameControllerButtonBind bind = SDL_GameControllerGetBindForButton(controller_pad->controller, SDL_CONTROLLER_BUTTON_GUIDE);
    return bind.bindType == SDL_CONTROLLER_BINDTYPE_BUTTON ? bind.value.button : NO_BUTTON;
}

static void press_guide_of(struct pad *controller_pad, int pressed) {
    if (pressed) adopt(controller_pad);
    pad_input(controller_pad->input, button_control[SDL_CONTROLLER_BUTTON_GUIDE], pressed);
}

static void handle(SDL_Event *event) {
    struct pad *source;
    switch (event->type) {
    case SDL_CONTROLLERDEVICEADDED: attach(event->cdevice.which); break;
    case SDL_CONTROLLERDEVICEREMOVED: detach(event->cdevice.which); break;
    case SDL_JOYBUTTONDOWN:
    case SDL_JOYBUTTONUP:
        source = pad_of(event->jbutton.which);
        if (source && event->jbutton.button == guide_button(source)) press_guide_of(source, event->jbutton.state == SDL_PRESSED);
        break;
    case SDL_CONTROLLERBUTTONDOWN:
    case SDL_CONTROLLERBUTTONUP:
        source = pad_of(event->cbutton.which);
        if (!source) break;
        if (event->cbutton.state == SDL_PRESSED) touched(source);
        if (event->cbutton.button == SDL_CONTROLLER_BUTTON_TOUCHPAD && routed(source->input) && (touchpad_pointer || source->clicked))
            click(source, event->cbutton.state == SDL_PRESSED);
        if (event->cbutton.button != SDL_CONTROLLER_BUTTON_GUIDE)
            pad_input(source->input, button_control[event->cbutton.button], event->cbutton.state == SDL_PRESSED);
        else if (guide_button(source) == NO_BUTTON) press_guide_of(source, event->cbutton.state == SDL_PRESSED);
        break;
    case SDL_CONTROLLERAXISMOTION:
        source = pad_of(event->caxis.which);
        if (source && abs(event->caxis.value) > SDL_AXIS_REACH) touched(source);
        if (source) drive_axis(source, event->caxis.axis);
        break;
    case SDL_CONTROLLERTOUCHPADDOWN:
    case SDL_CONTROLLERTOUCHPADMOTION:
    case SDL_CONTROLLERTOUCHPADUP:
        source = pad_of(event->ctouchpad.which);
        if (source) touch(source, &event->ctouchpad);
        break;
    case SDL_CONTROLLERSENSORUPDATE: sense(&event->csensor); break;
    case SDL_JOYBATTERYUPDATED:
        source = pad_of(event->jbattery.which);
        if (source) read_level(source, event->jbattery.level);
        break;
    }
}

static struct udev_monitor *watch_devices(void) {
    struct udev_monitor *created = udev_monitor_new_from_netlink(udev, "udev");
    if (!created || udev_monitor_filter_add_match_subsystem_devtype(created, "input", NULL) < 0 ||
        udev_monitor_filter_add_match_subsystem_devtype(created, "hidraw", NULL) < 0 ||
        udev_monitor_filter_add_match_subsystem_devtype(created, "power_supply", NULL) < 0 || udev_monitor_enable_receiving(created) < 0)
        return udev_monitor_unref(created);
    return created;
}

static void handle_sdl_events(void) {
    for (SDL_Event event; SDL_PollEvent(&event);) handle(&event);
}

int main(void) {
    char settings_directory[PATH_MAX];
    mkfifo(PAD_FIFO, S_IRUSR | S_IWUSR);
    setenv("SDL_JOYSTICK_DISABLE_UDEV", "1", 1);
    SDL_SetHint(SDL_HINT_GAMECONTROLLERCONFIG_FILE, CONTROLLER_MAPPINGS);
    SDL_SetHint(SDL_HINT_NO_SIGNAL_HANDLERS, "1");
    if (SDL_Init(SDL_INIT_GAMECONTROLLER) < 0) { fprintf(stderr, "SDL: %s\n", SDL_GetError()); return 1; }
    for (int seat = 0; seat < PADS; seat++) motion_source[seat] = NO_PAD;
    udev = udev_new();
    monitor = watch_devices();
    int fixed[FIXED_SOURCES] = {
        [COMMANDS] = open(PAD_FIFO, O_RDWR | O_NONBLOCK | O_CLOEXEC), [HOTPLUG] = monitor ? udev_monitor_get_fd(monitor) : -1,
        [SETTINGS] = inotify_init1(IN_NONBLOCK | IN_CLOEXEC), [PAIRING] = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC),
        [SLEEP] = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC), [POINTING] = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC),
        [RESTING] = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC), [BUS] = -1, [BLOW] = -1,
        [SPEAKER] = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC)};
    snprintf(settings_directory, sizeof settings_directory, "%.*s", (int)(strrchr(SETTINGS_FILE, '/') - SETTINGS_FILE), SETTINGS_FILE);
    for (enum source source = COMMANDS; source < FIXED_SOURCES; source++) {
        watched[source] = (struct pollfd){.fd = fixed[source], .events = POLLIN};
        watches[source].source = source;
        if (source != BUS && source != BLOW && fixed[source] < 0) { perror("padd"); return 1; }
    }
    watched_count = FIXED_SOURCES;
    if (inotify_add_watch(fixed[SETTINGS], settings_directory, IN_MOVED_TO) < 0) { perror(settings_directory); return 1; }
    for (SDL_GameControllerAxis axis = 0; axis < SDL_CONTROLLER_AXIS_MAX; axis++) axis_control[axis] = control_named(SDL_GameControllerGetStringForAxis(axis));
    for (SDL_GameControllerButton button = 0; button < SDL_CONTROLLER_BUTTON_MAX; button++)
        button_control[button] = control_named(SDL_GameControllerGetStringForButton(button));
    keyboard = create_keyboard();
    plug(0);
    read_sleep();
    open_bus();
    write_controllers();
    tell_overlay("reset");
    capture_devices(udev_enumerate_new(udev));
    for (;;) {
        handle_sdl_events();
        for (int seat = 0; seat < PADS; seat++)
            if (pad[seat]) libevdev_uinput_write_event(pad[seat], EV_SYN, SYN_REPORT, 0);
        watched[BUS] = (struct pollfd){.fd = bus ? sd_bus_get_fd(bus) : -1, .events = bus ? sd_bus_get_events(bus) : 0};
        int ready = poll(watched, watched_count, bus_timeout());
        if (ready < 0) continue;
        if (!ready || watched[BUS].revents) process_bus();
        for (int i = watched_count - 1; i >= 0; i--) {
            if (!watched[i].revents) continue;
            switch (watches[i].source) {
            case COMMANDS: read_commands(watched[i].fd); break;
            case HOTPLUG: read_hotplug(); break;
            case SETTINGS: read_settings_change(watched[i].fd); break;
            case PAIRING:
                if (!expired(watched[i].fd)) break;
                notify("No controller found");
                stop_pairing();
                break;
            case SLEEP: if (expired(watched[i].fd)) schedule_sleep(); break;
            case POINTING: if (expired(watched[i].fd)) tick_pointer(); break;
            case RESTING: if (expired(watched[i].fd)) rest_mice(); break;
            case BUS: break;
            case BLOW: sent_blow(watched[i].fd); break;
            case SPEAKER: if (expired(watched[i].fd)) speak(); break;
            case MICROPHONE: hear(i); break;
            case TUNNEL: reap_tunnel(i); break;
            case CAPTURED: pass_through(i); break;
            case PHYSICAL_PAD:
                if (watched[i].revents & (POLLERR | POLLHUP)) unwatch(i);
                else read_state(i);
                break;
            case VIRTUAL_PAD: read_feedback(i); break;
            }
        }
    }
}
