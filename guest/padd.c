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
#include <poll.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/inotify.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/timerfd.h>
#include <systemd/sd-bus.h>
#include <time.h>
#include <unistd.h>
#include "anyconsole.h"

#define DEVICES "/dev/input"
#define UINPUT_DEVICES "/sys/devices/virtual/input/"
#define NODE_MODE (S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH)
#define SEATS 4
#define CAPTURED_MAX 32
#define PHYSICAL_PADS_MAX 16
#define BLUETOOTH_DEVICES_MAX 64
#define NO_PAD (-1)
#define NO_BATTERY (-1)
#define NO_BUTTON (-1)
#define KEYBOARD_NAME "anyconsole keyboard"
#define PAD_NODE "event"
#define TOUCHPAD_NODE "touch"
#define PAD_VENDOR 0x054c
#define PAD_PRODUCT 0x0ce6
#define PAD_VERSION 0x8111
#define AXIS_MAX 255
#define AXIS_CENTER (AXIS_MAX / 2 + 1)
#define DS_ACC_RES_PER_G 8192
#define DS_ACC_RANGE (4 * DS_ACC_RES_PER_G)
#define DS_GYRO_RES_PER_DEG_S 1024
#define DS_GYRO_RANGE (2048 * DS_GYRO_RES_PER_DEG_S)
#define DEGREES_PER_RADIAN (180 / M_PI)
#define SENSOR_AXES SDL_arraysize(((SDL_ControllerSensorEvent){0}).data)
#define GAIN_MAX UINT16_MAX
#define PAIRING_SECONDS 60
#define SECONDS_PER_MINUTE 60
#define USEC_PER_SEC 1000000
#define USEC_PER_MSEC 1000
#define NSEC_PER_USEC 1000
#define WORD_MAX 31
#define ADDRESS_SIZE sizeof "00:00:00:00:00:00"
#define SYSTEM_BUS "unix:path=/run/dbus/system_bus_socket"
#define BLUEZ "org.bluez"
#define BLUEZ_ROOT "/org/bluez"
#define ADAPTER_INTERFACE BLUEZ ".Adapter1"
#define DEVICE_INTERFACE BLUEZ ".Device1"
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

enum source { COMMANDS, HOTPLUG, SETTINGS, PAIRING, SLEEP, BUS, FIXED_SOURCES, CAPTURED = FIXED_SOURCES, PHYSICAL_PAD, VIRTUAL_PAD };
#define WATCHED_MAX (FIXED_SOURCES + CAPTURED_MAX + PHYSICAL_PADS_MAX + SEATS)

enum pairing { NOT_PAIRING, SEARCHING, BONDING };

struct watch {
    enum source source;
    struct libevdev *device;
    SDL_JoystickID pad;
    int seat;
};

struct pad {
    SDL_GameController *controller;
    SDL_JoystickID instance;
    struct udev_device *hid;
    char address[ADDRESS_SIZE];
    int bus, battery, charging, claimed_guide;
    SDL_JoystickPowerLevel level;
    time_t active;
};

struct bluetooth_device {
    char path[PATH_MAX], address[ADDRESS_SIZE], name[NAME_MAX + 1], icon[NAME_MAX + 1];
    int paired, trusted, seen;
};

static struct libevdev_uinput *pad[SEATS], *touchpad[SEATS], *motion[SEATS], *guide, *keyboard;
static const struct control *swallowed[SEATS];
static int held[SEATS][SDL_arraysize(controls)];
static struct ff_effect *effects[SEATS];
static int effect_count[SEATS], gain[SEATS];
static struct pollfd watched[WATCHED_MAX];
static struct watch watches[WATCHED_MAX];
static int watched_count;
static struct pad pads[PHYSICAL_PADS_MAX];
static int pad_count, reach, sleep_minutes;
static SDL_JoystickID motion_source = NO_PAD;
static struct bluetooth_device known[BLUETOOTH_DEVICES_MAX];
static int known_count;
static char adapter[PATH_MAX], target[ADDRESS_SIZE], armed[ADDRESS_SIZE], offered[PHYSICAL_PADS_MAX][ADDRESS_SIZE], target_name[NAME_MAX + 1];
static int offered_count;
static enum pairing pairing;
static struct udev *udev;
static struct udev_monitor *monitor;
static sd_bus *bus;

static void emit(struct libevdev_uinput *device, unsigned type, unsigned code, int value) {
    libevdev_uinput_write_event(device, type, code, value);
    libevdev_uinput_write_event(device, EV_SYN, SYN_REPORT, 0);
}

static void notify(const char *format, ...) {
    char line[PIPE_BUF];
    va_list arguments;
    va_start(arguments, format);
    int length = vsnprintf(line, sizeof line - 1, format, arguments);
    va_end(arguments);
    if (length >= (int)sizeof line - 1) length = sizeof line - 2;
    line[length++] = '\n';
    int fd = open(NOTIFY_FIFO, O_WRONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) return;
    if (write(fd, line, length) < 0) perror(NOTIFY_FIFO);
    close(fd);
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

static void expose(int fd, const char *pad_node, int shared, int create) {
    char sysname[UINPUT_MAX_NAME_SIZE], pattern[PATH_MAX], path[PATH_MAX], dev_file[PATH_MAX];
    glob_t found;
    if (ioctl(fd, UI_GET_SYSNAME(sizeof sysname), sysname) < 0) { perror("uinput sysname"); return; }
    snprintf(pattern, sizeof pattern, UINPUT_DEVICES "%s/" PAD_NODE "*", sysname);
    if (glob(pattern, 0, NULL, &found)) { fprintf(stderr, "%s: no device nodes\n", sysname); return; }
    for (size_t i = 0; i < found.gl_pathc; i++) {
        snprintf(dev_file, sizeof dev_file, "%s/dev", found.gl_pathv[i]);
        if (shared) {
            snprintf(path, sizeof path, INPUT_NODES "/%s", basename(found.gl_pathv[i]));
            if (create) publish(path, dev_file);
            else unlink(path);
        }
        if (!pad_node) continue;
        snprintf(path, sizeof path, PAD_NODES "/%s", pad_node);
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

static struct libevdev_uinput *create(struct libevdev *device, const char *pad_node, int shared) {
    struct libevdev_uinput *created;
    int error = libevdev_uinput_create_from_device(device, LIBEVDEV_UINPUT_OPEN_MANAGED, &created);
    if (error) { fprintf(stderr, "%s: %s\n", libevdev_get_name(device), strerror(-error)); exit(1); }
    libevdev_free(device);
    expose(libevdev_uinput_get_fd(created), pad_node, shared, 1);
    return created;
}

static struct libevdev_uinput *create_guide(void) {
    struct libevdev *device = libevdev_new();
    libevdev_set_name(device, GUIDE_DEVICE);
    libevdev_enable_event_code(device, EV_KEY, BTN_MODE, NULL);
    return create(device, NULL, 1);
}

static struct libevdev_uinput *create_keyboard(void) {
    struct libevdev *device = libevdev_new();
    libevdev_set_name(device, KEYBOARD_NAME);
    for (unsigned code = KEY_ESC; code < BTN_MISC; code++) libevdev_enable_event_code(device, EV_KEY, code, NULL);
    for (unsigned code = BTN_MOUSE; code < BTN_JOYSTICK; code++) libevdev_enable_event_code(device, EV_KEY, code, NULL);
    for (unsigned code = 0; code <= REL_MAX; code++) libevdev_enable_event_code(device, EV_REL, code, NULL);
    return create(device, NULL, 1);
}

static struct libevdev_uinput *create_touchpad(int seat) {
    struct libevdev *device = libevdev_new();
    struct input_absinfo axis = {.maximum = SDL_JOYSTICK_AXIS_MAX};
    libevdev_set_name(device, PAD_NAME " Touchpad");
    libevdev_enable_property(device, INPUT_PROP_DIRECT);
    libevdev_enable_event_code(device, EV_KEY, BTN_TOUCH, NULL);
    libevdev_enable_event_code(device, EV_ABS, ABS_X, &axis);
    libevdev_enable_event_code(device, EV_ABS, ABS_Y, &axis);
    return create(device, node(TOUCHPAD_NODE, seat), 0);
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
    return create(device, node(PAD_NODE, SEATS + seat), 0);
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

static void plug(int seat) {
    if (pad[seat]) return;
    struct libevdev *device = pad_device(PAD_NAME);
    for (const struct control *c = controls; c < controls + SDL_arraysize(controls); c++) {
        if (c->type == EV_KEY) { libevdev_enable_event_code(device, EV_KEY, c->code, NULL); continue; }
        if (c->button) libevdev_enable_event_code(device, EV_KEY, c->button, NULL);
        struct input_absinfo abs = c->direction ? (struct input_absinfo){.minimum = -1, .maximum = 1}
                                                : (struct input_absinfo){.value = c->button ? 0 : AXIS_CENTER, .maximum = AXIS_MAX};
        libevdev_enable_event_code(device, EV_ABS, c->code, &abs);
    }
    libevdev_enable_event_code(device, EV_FF, FF_RUMBLE, NULL);
    libevdev_enable_event_code(device, EV_FF, FF_GAIN, NULL);
    memset(held[seat], 0, sizeof held[seat]);
    gain[seat] = GAIN_MAX;
    pad[seat] = create(device, node(PAD_NODE, seat), 1);
    touchpad[seat] = create_touchpad(seat);
    motion[seat] = create_motion(seat);
    struct watch *feedback = watch(libevdev_uinput_get_fd(pad[seat]), VIRTUAL_PAD);
    if (feedback) feedback->seat = seat;
}

static void unplug(int seat) {
    if (!pad[seat]) return;
    for (int i = 0; i < watched_count; i++)
        if (watches[i].source == VIRTUAL_PAD && watches[i].seat == seat) { drop(i); break; }
    expose(libevdev_uinput_get_fd(pad[seat]), node(PAD_NODE, seat), 1, 0);
    expose(libevdev_uinput_get_fd(touchpad[seat]), node(TOUCHPAD_NODE, seat), 0, 0);
    expose(libevdev_uinput_get_fd(motion[seat]), node(PAD_NODE, SEATS + seat), 0, 0);
    libevdev_uinput_destroy(pad[seat]);
    libevdev_uinput_destroy(touchpad[seat]);
    libevdev_uinput_destroy(motion[seat]);
    pad[seat] = touchpad[seat] = motion[seat] = NULL;
    free(effects[seat]);
    effects[seat] = NULL;
    effect_count[seat] = 0;
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

static void touch(struct libevdev_uinput *device, SDL_ControllerTouchpadEvent *event) {
    int down = event->type != SDL_CONTROLLERTOUCHPADUP;
    if (event->finger) return;
    if (down) {
        libevdev_uinput_write_event(device, EV_ABS, ABS_X, event->x * SDL_JOYSTICK_AXIS_MAX);
        libevdev_uinput_write_event(device, EV_ABS, ABS_Y, event->y * SDL_JOYSTICK_AXIS_MAX);
    }
    emit(device, EV_KEY, BTN_TOUCH, down);
}

static void sense(SDL_ControllerSensorEvent *event) {
    struct input_event report[SENSOR_AXES + 2];
    int gyroscope = event->sensor == SDL_SENSOR_GYRO;
    if (event->which != motion_source || !motion[0] || (!gyroscope && event->sensor != SDL_SENSOR_ACCEL)) return;
    for (unsigned axis = 0; axis < SENSOR_AXES; axis++)
        report[axis] = (struct input_event){.type = EV_ABS, .code = (gyroscope ? ABS_RX : ABS_X) + axis,
                                            .value = gyroscope ? event->data[axis] * DEGREES_PER_RADIAN * DS_GYRO_RES_PER_DEG_S
                                                               : event->data[axis] / SDL_STANDARD_GRAVITY * DS_ACC_RES_PER_G};
    report[SENSOR_AXES] = (struct input_event){.type = EV_MSC, .code = MSC_TIMESTAMP, .value = (int)event->timestamp_us};
    report[SENSOR_AXES + 1] = (struct input_event){.type = EV_SYN, .code = SYN_REPORT};
    if (write(libevdev_uinput_get_fd(motion[0]), report, sizeof report) < 0) perror("padd: motion");
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
    for (int i = 0; i < pad_count; i++) SDL_GameControllerRumble(pads[i].controller, strong, weak, effect->replay.length);
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

static struct pad *pad_of(SDL_JoystickID instance) {
    for (int i = 0; i < pad_count; i++)
        if (pads[i].instance == instance) return &pads[i];
    return NULL;
}

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

static void address_of(const char *path, char *address) {
    const char *name = strrchr(path, '/');
    snprintf(address, ADDRESS_SIZE, "%s", name && !strncmp(name, DEVICE_PATH_PREFIX, strlen(DEVICE_PATH_PREFIX)) ? name + strlen(DEVICE_PATH_PREFIX) : "");
    for (char *at = strchr(address, '_'); at; at = strchr(at, '_')) *at = ':';
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

static void write_controllers(void) {
    json_object *state = json_object_new_object(), *list = json_object_new_array(), *keyboards = json_object_new_array();
    for (int i = 0; i < pad_count; i++) {
        struct bluetooth_device *device = known_address(pads[i].address);
        json_object_array_add(list, controller(SDL_GameControllerName(pads[i].controller), pads[i].address,
                                               pads[i].bus == BUS_BLUETOOTH ? "bluetooth" : "wired", pads[i].battery, pads[i].charging,
                                               device && device->trusted));
    }
    for (int i = 0; i < known_count; i++)
        if (known[i].trusted && !pad_with_address(known[i].address))
            json_object_array_add(list, controller(known[i].name, known[i].address, NULL, NO_BATTERY, 0, 1));
    for (int i = 0; i < watched_count; i++)
        if (watches[i].source == CAPTURED) json_object_array_add(keyboards, json_object_new_string(libevdev_get_name(watches[i].device)));
    json_object_object_add(state, "bluetooth", json_object_new_boolean(*adapter));
    json_object_object_add(state, "pairing", json_object_new_boolean(pairing != NOT_PAIRING));
    json_object_object_add(state, "controllers", list);
    json_object_object_add(state, "keyboards", keyboards);
    if (json_object_to_file_ext(CONTROLLERS_PARTIAL, state, JSON_C_TO_STRING_PLAIN) < 0 || rename(CONTROLLERS_PARTIAL, CONTROLLERS_FILE) < 0)
        perror(CONTROLLERS_FILE);
    json_object_put(state);
}

static int read_battery(struct pad *controller_pad) {
    struct udev_list_entry *entry;
    int battery = controller_pad->battery, charging = controller_pad->charging;
    if (!controller_pad->hid) return 0;
    struct udev_enumerate *supplies = udev_enumerate_new(udev);
    udev_enumerate_add_match_subsystem(supplies, "power_supply");
    udev_enumerate_add_match_parent(supplies, controller_pad->hid);
    udev_enumerate_scan_devices(supplies);
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
    struct udev_enumerate *nodes = udev_enumerate_new(udev);
    udev_enumerate_add_match_subsystem(nodes, "hidraw");
    udev_enumerate_add_match_parent(nodes, controller_pad->hid);
    udev_enumerate_scan_devices(nodes);
    udev_list_entry_foreach(entry, udev_enumerate_get_list_entry(nodes)) {
        struct udev_device *hidraw = udev_device_new_from_syspath(udev, udev_list_entry_get_name(entry));
        if (udev_device_set_sysattr_value(hidraw, "uevent", UEVENT_ADD) < 0) perror(udev_list_entry_get_name(entry));
        udev_device_unref(hidraw);
    }
    udev_enumerate_unref(nodes);
}

static time_t now(void) {
    struct timespec time;
    clock_gettime(CLOCK_MONOTONIC, &time);
    return time.tv_sec;
}

static void call(const char *path, const char *interface, const char *method, sd_bus_message_handler_t done, const char *types, ...) {
    va_list arguments;
    va_start(arguments, types);
    int error = sd_bus_call_method_asyncv(bus, NULL, BLUEZ, path, interface, method, done, NULL, types, arguments);
    va_end(arguments);
    if (error < 0) fprintf(stderr, "padd: %s: %s\n", method, strerror(-error));
}

static void schedule_sleep(void) {
    time_t current = now(), earliest = 0;
    for (int i = 0; sleep_minutes && i < pad_count; i++) {
        struct bluetooth_device *device = known_address(pads[i].address);
        time_t deadline = pads[i].active + sleep_minutes * SECONDS_PER_MINUTE;
        if (pads[i].bus != BUS_BLUETOOTH) continue;
        if (deadline > current) earliest = earliest && earliest < deadline ? earliest : deadline;
        else if (device) call(device->path, DEVICE_INTERFACE, "Disconnect", NULL, "");
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
    call(device->path, PROPERTIES_INTERFACE, "Set", NULL, "ssv", DEVICE_INTERFACE, "Trusted", "b", 1);
}

static void stop_pairing(void) {
    if (pairing == NOT_PAIRING) return;
    if (pairing == SEARCHING && *adapter) call(adapter, ADAPTER_INTERFACE, "StopDiscovery", NULL, "");
    pairing = NOT_PAIRING;
    *target = *target_name = '\0';
    timerfd_settime(watched[PAIRING].fd, 0, &(struct itimerspec){0}, NULL);
    write_controllers();
}

static int on_paired(sd_bus_message *reply, void *data, sd_bus_error *error) {
    (void)data, (void)error;
    struct bluetooth_device *device = known_address(target);
    if (!device || sd_bus_message_is_method_error(reply, NULL)) {
        notify("Could not pair %s", *target_name ? target_name : device ? device->name : "the controller");
        stop_pairing();
        return 0;
    }
    trust(device);
    call(device->path, DEVICE_INTERFACE, "Connect", NULL, "");
    notify("Paired %s", *target_name ? target_name : device->name);
    stop_pairing();
    return 0;
}

static void consider(struct bluetooth_device *device) {
    if (pairing != SEARCHING || !device || !device->seen || device->paired) return;
    if (*target ? !same(device->address, target) : strcmp(device->icon, GAMEPAD_ICON)) return;
    call(adapter, ADAPTER_INTERFACE, "StopDiscovery", NULL, "");
    pairing = BONDING;
    snprintf(target, sizeof target, "%s", device->address);
    call(device->path, DEVICE_INTERFACE, "Pair", on_paired, "");
}

static void start_pairing(const char *address) {
    if (!*adapter) { notify("No Bluetooth adapter"); return; }
    pairing = SEARCHING;
    snprintf(target, sizeof target, "%s", address);
    for (int i = 0; i < known_count; i++) known[i].seen = 0;
    call(adapter, ADAPTER_INTERFACE, "StartDiscovery", NULL, "");
    timerfd_settime(watched[PAIRING].fd, 0, &(struct itimerspec){.it_value.tv_sec = PAIRING_SECONDS}, NULL);
    write_controllers();
}

static void forget(const char *address) {
    struct bluetooth_device *device = known_address(address);
    if (device && *adapter) call(adapter, ADAPTER_INTERFACE, "RemoveDevice", NULL, "o", device->path);
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

static int adopt(struct pad *controller_pad) {
    struct bluetooth_device *device = known_address(controller_pad->address);
    const char *name = SDL_GameControllerName(controller_pad->controller);
    if (controller_pad->bus != BUS_USB || !*controller_pad->address || !*adapter || (device && device->trusted)) return 0;
    if (offered_index(controller_pad->address) >= 0) {
        snprintf(armed, sizeof armed, "%s", controller_pad->address);
        reannounce(controller_pad);
        notify("Pairing %s", name);
        return 1;
    }
    SDL_GameControllerType type = SDL_GameControllerGetType(controller_pad->controller);
    const char *share = type == SDL_CONTROLLER_TYPE_PS5 ? "Create" : type == SDL_CONTROLLER_TYPE_PS4 ? "Share" : NULL;
    if (!share) return 0;
    start_pairing(controller_pad->address);
    snprintf(target_name, sizeof target_name, "%s", name);
    notify("Unplug %s, then hold %s and PS", name, share);
    return 1;
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
    call(BLUEZ_ROOT, AGENT_MANAGER_INTERFACE, "RegisterAgent", NULL, "os", AGENT_PATH, AGENT_CAPABILITY);
    call(BLUEZ_ROOT, AGENT_MANAGER_INTERFACE, "RequestDefaultAgent", NULL, "o", AGENT_PATH);
    call("/", OBJECT_MANAGER_INTERFACE, "GetManagedObjects", on_objects, "");
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

static int seat_of(const char *word) {
    int seat = atoi(word) - 1;
    return seat >= 0 && seat < SEATS ? seat : -1;
}

static void command(char *line) {
    char first[WORD_MAX + 1], second[WORD_MAX + 1];
    int value;
    int words = sscanf(line, "%" STRINGIFY(WORD_MAX) "s %" STRINGIFY(WORD_MAX) "s %d", first, second, &value);
    int seat = words >= 2 ? seat_of(words == 3 ? first : second) : -1;
    if (words == 1 && !strcmp(first, "pair")) {
        if (pairing == NOT_PAIRING) start_pairing("");
        else stop_pairing();
    } else if (words == 1 && !strcmp(first, "batteries")) {
        int changed = 0;
        for (int i = 0; i < pad_count; i++) changed |= read_battery(&pads[i]);
        if (changed) write_controllers();
    } else if (words == 2 && !strcmp(first, "forget")) {
        forget(second);
    } else if (seat < 0) {
        fprintf(stderr, "bad command: %s", line);
    } else if (words == 3) {
        drive(seat, second, value);
    } else if (!strcmp(first, "plug")) {
        plug(seat);
    } else if (!strcmp(first, "unplug")) {
        unplug(seat);
    } else {
        fprintf(stderr, "bad command: %s", line);
    }
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

static void drain(int fd) {
    char buffer[BUFSIZ];
    while (read(fd, buffer, sizeof buffer) > 0) {}
}

static int expired(int timer) {
    uint64_t expirations;
    return read(timer, &expirations, sizeof expirations) > 0;
}

static void capture(const char *node_name) {
    char path[PATH_MAX];
    struct libevdev *device = NULL;
    snprintf(path, sizeof path, DEVICES "/%s", node_name);
    if (strncmp(node_name, PAD_NODE, strlen(PAD_NODE)) || is_virtual(path)) return;
    int fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) { perror(path); return; }
    if (libevdev_new_from_fd(fd, &device) < 0 ||
        (!libevdev_has_event_code(device, EV_KEY, KEY_A) && !libevdev_has_event_code(device, EV_REL, REL_X))) {
        libevdev_free(device);
        close(fd);
        return;
    }
    struct watch *captured = watch(fd, CAPTURED);
    if (!captured) { libevdev_free(device); close(fd); return; }
    libevdev_grab(device, LIBEVDEV_GRAB);
    captured->device = device;
    notify("Connected %s", libevdev_get_name(device));
    write_controllers();
}

static void pass_through(int index) {
    struct libevdev *device = watches[index].device;
    struct input_event event;
    int status;
    while ((status = libevdev_next_event(device, LIBEVDEV_READ_FLAG_NORMAL, &event)) >= 0) {
        forward(event.type, event.code, event.value);
        if (status == LIBEVDEV_READ_STATUS_SYNC)
            while (libevdev_next_event(device, LIBEVDEV_READ_FLAG_SYNC, &event) == LIBEVDEV_READ_STATUS_SYNC)
                forward(event.type, event.code, event.value);
    }
    if (status == -EAGAIN) return;
    notify("Disconnected %s", libevdev_get_name(device));
    libevdev_free(device);
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

static int scaled(int value, int min) { return (value - min) * AXIS_MAX / (SDL_JOYSTICK_AXIS_MAX - min); }

static void sense_with(struct pad *controller_pad, SDL_bool enabled) {
    SDL_GameControllerSetSensorEnabled(controller_pad->controller, SDL_SENSOR_ACCEL, enabled);
    SDL_GameControllerSetSensorEnabled(controller_pad->controller, SDL_SENSOR_GYRO, enabled);
}

static void touched(struct pad *controller_pad) {
    struct pad *previous = pad_of(motion_source);
    controller_pad->active = now();
    if (previous == controller_pad) return;
    if (previous) sense_with(previous, SDL_FALSE);
    sense_with(controller_pad, SDL_TRUE);
    motion_source = controller_pad->instance;
}

static void attach(int index) {
    const char *path = SDL_JoystickPathForIndex(index);
    if (path && is_virtual(path)) return;
    if (pad_count == PHYSICAL_PADS_MAX) { fprintf(stderr, "padd: too many pads\n"); return; }
    SDL_GameController *game_controller = SDL_GameControllerOpen(index);
    if (!game_controller) { fprintf(stderr, "%s: %s\n", SDL_JoystickNameForIndex(index), SDL_GetError()); return; }
    struct pad *controller_pad = &pads[pad_count++];
    *controller_pad = (struct pad){.controller = game_controller, .instance = SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(game_controller)),
                                   .battery = NO_BATTERY, .level = SDL_JoystickCurrentPowerLevel(SDL_GameControllerGetJoystick(game_controller))};
    identify(controller_pad, path);
    read_battery(controller_pad);
    touched(controller_pad);
    notify("Connected %s", SDL_GameControllerName(game_controller));
    struct watch *physical = watch(open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC), PHYSICAL_PAD);
    if (physical) physical->pad = controller_pad->instance;
    write_controllers();
    schedule_sleep();
}

static void detach(SDL_JoystickID instance) {
    struct pad *controller_pad = pad_of(instance);
    if (!controller_pad) return;
    notify("Disconnected %s", SDL_GameControllerName(controller_pad->controller));
    for (int i = 0; i < watched_count; i++)
        if (watches[i].source == PHYSICAL_PAD && watches[i].pad == instance) { unwatch(i); break; }
    withdraw(controller_pad->address);
    udev_device_unref(controller_pad->hid);
    SDL_GameControllerClose(controller_pad->controller);
    *controller_pad = pads[--pad_count];
    if (motion_source == instance) motion_source = NO_PAD;
    if (motion_source == NO_PAD && pad_count) touched(&pads[0]);
    write_controllers();
}

static int guide_button(struct pad *controller_pad) {
    SDL_GameControllerButtonBind bind = SDL_GameControllerGetBindForButton(controller_pad->controller, SDL_CONTROLLER_BUTTON_GUIDE);
    return bind.bindType == SDL_CONTROLLER_BINDTYPE_BUTTON ? bind.value.button : NO_BUTTON;
}

static void press_guide_of(struct pad *controller_pad, int pressed) {
    int claimed = pressed ? adopt(controller_pad) : controller_pad->claimed_guide;
    controller_pad->claimed_guide = pressed && claimed;
    if (!claimed) drive(0, "guide", pressed);
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
        if (source && event->cbutton.state == SDL_PRESSED) touched(source);
        if (event->cbutton.button != SDL_CONTROLLER_BUTTON_GUIDE) drive(0, SDL_GameControllerGetStringForButton(event->cbutton.button), event->cbutton.state == SDL_PRESSED);
        else if (source && guide_button(source) == NO_BUTTON) press_guide_of(source, event->cbutton.state == SDL_PRESSED);
        break;
    case SDL_CONTROLLERAXISMOTION:
        source = pad_of(event->caxis.which);
        if (source && abs(event->caxis.value) > reach) touched(source);
        drive(0, SDL_GameControllerGetStringForAxis(event->caxis.axis),
              scaled(event->caxis.value, event->caxis.axis >= SDL_CONTROLLER_AXIS_TRIGGERLEFT ? 0 : SDL_JOYSTICK_AXIS_MIN));
        break;
    case SDL_CONTROLLERTOUCHPADDOWN:
    case SDL_CONTROLLERTOUCHPADMOTION:
    case SDL_CONTROLLERTOUCHPADUP:
        if (touchpad[0]) touch(touchpad[0], &event->ctouchpad);
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

int main(void) {
    char settings_directory[PATH_MAX];
    mkfifo(PAD_FIFO, S_IRUSR | S_IWUSR);
    setenv("SDL_JOYSTICK_DISABLE_UDEV", "1", 1);
    SDL_SetHint(SDL_HINT_GAMECONTROLLERCONFIG_FILE, CONTROLLER_MAPPINGS);
    SDL_SetHint(SDL_HINT_NO_SIGNAL_HANDLERS, "1");
    if (SDL_Init(SDL_INIT_GAMECONTROLLER) < 0) { fprintf(stderr, "SDL: %s\n", SDL_GetError()); return 1; }
    reach = SDL_JOYSTICK_AXIS_MAX / atoi(STICK_REACH_DIVISOR);
    udev = udev_new();
    monitor = watch_devices();
    int fixed[FIXED_SOURCES] = {
        [COMMANDS] = open(PAD_FIFO, O_RDWR | O_NONBLOCK | O_CLOEXEC), [HOTPLUG] = monitor ? udev_monitor_get_fd(monitor) : -1,
        [SETTINGS] = inotify_init1(IN_NONBLOCK | IN_CLOEXEC), [PAIRING] = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC),
        [SLEEP] = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC), [BUS] = -1};
    snprintf(settings_directory, sizeof settings_directory, "%.*s", (int)(strrchr(SETTINGS_FILE, '/') - SETTINGS_FILE), SETTINGS_FILE);
    for (enum source source = COMMANDS; source < FIXED_SOURCES; source++) {
        watched[source] = (struct pollfd){.fd = fixed[source], .events = POLLIN};
        watches[source].source = source;
        if (source != BUS && fixed[source] < 0) { perror("padd"); return 1; }
    }
    watched_count = FIXED_SOURCES;
    if (inotify_add_watch(fixed[SETTINGS], settings_directory, IN_MOVED_TO) < 0) { perror(settings_directory); return 1; }
    guide = create_guide();
    keyboard = create_keyboard();
    plug(0);
    read_sleep();
    open_bus();
    capture_devices(udev_enumerate_new(udev));
    for (;;) {
        for (SDL_Event event; SDL_PollEvent(&event);) handle(&event);
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
            case BUS: break;
            case CAPTURED: pass_through(i); break;
            case PHYSICAL_PAD:
                if (watched[i].revents & (POLLERR | POLLHUP)) unwatch(i);
                else drain(watched[i].fd);
                break;
            case VIRTUAL_PAD: read_feedback(i); break;
            }
        }
    }
}
