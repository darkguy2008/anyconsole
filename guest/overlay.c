#define _GNU_SOURCE
#include <cairo.h>
#include <dirent.h>
#include <drm_fourcc.h>
#include <errno.h>
#include <fcntl.h>
#include <ftw.h>
#include <gbm.h>
#include <json-c/json.h>
#include <limits.h>
#include <math.h>
#include <pixman.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/inotify.h>
#include <sys/mman.h>
#include <sys/signalfd.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/timerfd.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>
#include <wayland-client.h>
#include <xf86drm.h>
#include "anyconsole.h"
#include "ext-foreign-toplevel-list-v1.h"
#include "ext-image-capture-source-v1.h"
#include "ext-image-copy-capture-v1.h"
#include "linux-dmabuf-v1.h"
#include "security-context-v1.h"
#include "wlr-foreign-toplevel-management-unstable-v1.h"
#include "wlr-layer-shell-unstable-v1.h"

#define PARTIAL ".partial"
#define STATE_PARTIAL STATE_FILE PARTIAL
#define WAYLAND_SOCKET "wayland-0"
#define PIPEWIRE_SOCKET "pipewire-0"
#define SANDBOX_ENGINE "docker"
#define FONT "Inter"
#define ROWS_PER_SCREEN 16
#define FONT_PER_ROW 0.6
#define MARGIN_PER_ROW 0.5
#define GUIDE_WIDTH_DIVISOR 3
#define NOTIFICATION_WIDTH_DIVISOR 3
#define NOTIFICATION_SECONDS 4
#define NOTIFICATIONS_MAX 4
#define REPEAT_DELAY_MS 400
#define REPEAT_INTERVAL_MS 80
#define NS_PER_MS 1000000
#define MS_PER_SECOND 1000
#define NS_PER_SECOND ((double)MS_PER_SECOND * NS_PER_MS)
#define PERCENT 100
#define RGB_MASK 0x00ffffffu
#define VOLUME_STEP "5%"
#define GAMES_MAX 512
#define STORES_MAX 32
#define DEVICES_MAX 32
#define DEPTH_MAX 4
#define ROWS_MAX (GAMES_MAX + 1)
#define TEXT_MAX 256
#define GAME_KEY_MAX (2 * NAME_MAX + 2)
#define WINDOWS_MAX (GAMES_MAX + 1)
#define RESOLUTIONS_MAX 8
#define SLEEP_CHOICES_MAX 8
#define AUTO "Auto"
#define MHZ_PER_HZ 1000
#define REFRESH_PRECISION 100
#define BYTES_PER_PIXEL 4
#define BUFFERS 2
#define REWIND_STATES 20
#define SAVE_SECONDS 5
#define SAVE_CHANGE_PERCENT 1
#define STATES "states"
#define STATE_EXTENSION ".state"
#define SCREEN_EXTENSION ".screen.png"
#define REWIND_TREE_DEPTH 2
#define CAROUSEL_ROWS 4
#define OUTLINE_PER_ROW 0.1
#define SECONDS_PER_MINUTE 60
#define LINUX_DMABUF_VERSION 3
#define BAR_WIDTH_DIVISOR 3
#define BAR_PER_ROW 0.5
#define LAUNCH_STEPS 2
#define NO_DEVICE 0
#define UNAPPLIED (-2)
#define NOTIFY_PREFIX "notify "
#define STRINGIFY_(x) #x
#define STRINGIFY(x) STRINGIFY_(x)
#define PROFILE_NAME "Player %d"
#define EMULATE_FILE "emulate.json"
#define ATTRACT_PROMPT "Press any button"
#define ATTRACT_PROMPT_ROW (ROWS_PER_SCREEN * 3 / 4)
#define ATTRACT_FADE_SECONDS 1.0

enum screen { HOME, STORE, GAME, GUIDE, OUTPUTS, MICROPHONES, CONTROLLERS, CONTROLLER, SETTINGS, WHOIS, LOG_OUT, ATTRACT };
static const struct { const char *name, *title; } screens[] = {
    [HOME] = {"home", "Home"}, [STORE] = {"store", "Store"}, [GAME] = {"game", ""},
    [GUIDE] = {"guide", "Guide"}, [OUTPUTS] = {"outputs", "Output"}, [MICROPHONES] = {"microphones", "Microphone"},
    [CONTROLLERS] = {"controllers", "Controllers"}, [CONTROLLER] = {"controller", ""}, [SETTINGS] = {"settings", "Settings"},
    [WHOIS] = {"whois", ""}, [LOG_OUT] = {"logout", ""}, [ATTRACT] = {"attract", ""},
};
enum action { NOTHING, OPEN_STORE, OPEN_SETTINGS, OPEN_GAME, PLAY, RESUME, CLOSE, UNINSTALL, INSTALL, SHOW_DASHBOARD, VOLUME,
              OPEN_OUTPUTS, OPEN_MICROPHONES, SET_DEVICE, OPEN_CONTROLLERS, PAIR, OPEN_CONTROLLER, FORGET, RELOAD, RESTART, POWER_OFF,
              CHOOSE_DISPLAY, CHOOSE_DISPLAY_MODE, SAVE_DISPLAY_MODE, CHOOSE_RESOLUTION, CHOOSE_SLEEP, REWIND, LOGIN, EMULATE, LOG_OUT_PLAYER, CONFIRM_LOG_OUT, MICROPHONE_VOLUME, SECOND_SCREEN };
enum command { NONE, UP, DOWN, LEFT, RIGHT, CONFIRM, BACK, GUIDE_BUTTON };
static const char *const commands[] = {[NONE] = "other", [UP] = "up", [DOWN] = "down", [LEFT] = "left", [RIGHT] = "right",
                                       [CONFIRM] = "confirm", [BACK] = "back", [GUIDE_BUTTON] = "guide"};
static const struct { const char *name, *label; } mode_labels[] = {
    {"analog", "Analog pad"}, {"digital", "Digital pad"}, {"keyboard", "Keyboard"}, {"mouse", "Mouse"}, {"keyboard-mouse", "Keyboard & mouse"}};
enum surface_mode { HIDDEN, NOTIFICATIONS_ONLY, FULL };
enum task { INSTALLING, UNINSTALLING, LAUNCHING };
static const char *const tasks[] = {[INSTALLING] = "installing", [UNINSTALLING] = "uninstalling", [LAUNCHING] = "launching"};

struct game {
    char slug[NAME_MAX + 1], platform[NAME_MAX + 1], title[TEXT_MAX], job_text[TEXT_MAX], job_line[TEXT_MAX];
    json_object *emulate;
    int installed, commands, context, paused, closing, job_output, percent, states_watch, rewinds, probe_output, owner, second_screen, screen_shown;
    size_t job_length;
    enum task task;
    unsigned stores;
    pid_t pid, job, freezer, probe;
};

struct view { enum screen screen; int game, selected; enum action action; int arg; };
struct row { char label[TEXT_MAX], value[TEXT_MAX]; enum action action; int arg; };
struct device { int id; char name[TEXT_MAX], node[TEXT_MAX]; };
struct member { int device, profile; };
struct notification { char text[TEXT_MAX]; struct timespec expires; };
struct window { struct zwlr_foreign_toplevel_handle_v1 *handle; struct ext_foreign_toplevel_handle_v1 *listed; char app_id[TEXT_MAX]; };
struct buffer { struct wl_buffer *buffer; void *pixels; int busy; };
struct capture {
    struct ext_image_copy_capture_session_v1 *session;
    struct ext_image_copy_capture_frame_v1 *frame;
    struct gbm_bo *bo;
    struct wl_buffer *buffer;
    struct wl_array modifiers;
    int game, width, height, save, current;
};

static struct game games[GAMES_MAX];
static int game_count, active = -1;
static char stores[STORES_MAX][PATH_MAX];
static int store_count;
static pid_t refresher, monitor;
static int refresh_pending, monitor_output = -1;
static json_tokener *monitor_parser;
static struct view dashboard[DEPTH_MAX] = {{.screen = HOME, .game = -1}}, guides[PROFILES + 1][DEPTH_MAX];
static int dashboard_depth = 1, guide_depths[PROFILES + 1], guide_shown, dashboard_open = 1;
static struct row rows[ROWS_MAX];
static struct device outputs[DEVICES_MAX], microphones[DEVICES_MAX];
static char default_output[TEXT_MAX], default_microphone[TEXT_MAX], microphone_pad[TEXT_MAX], chosen_controller[TEXT_MAX], staged_mode[TEXT_MAX];
static int output_count, microphone_count, volume = -1, microphone_volume = -1, microphone_muted, mode_staged;
static int players[PADS], player_count, menu_told = -1, focus = NO_DEVICE, picker = NO_DEVICE, previewed = NO_DEVICE, previewed_seat, repeating_device, guide_profile, applied_game = UNAPPLIED;
static struct member members[INPUT_DEVICES_MAX];
static int member_count;
static struct view whois;
static struct timespec attract_since;
static cairo_surface_t *splash;
static struct notification notifications[NOTIFICATIONS_MAX];
static int notification_count;
static enum command repeating;
static int repeat_timer, notification_timer, save_timer, watcher, log_file, changed = 1;
static json_object *emulate_choices[PROFILES + 1];
static char rewinds[REWIND_STATES][NAME_MAX + 1];
static cairo_surface_t *thumbnails[REWIND_STATES], *pending_screen, *current_screen, *newest_screen;
static char newest_name[NAME_MAX + 1];
static int pending_after;
static struct capture capture;
static int rewind_count, rewind_back;
static char *written_state;
static struct window windows[WINDOWS_MAX];
static struct gbm_device *gbm;
static int window_count, resolutions[RESOLUTIONS_MAX], resolution_count, sleep_choices[SLEEP_CHOICES_MAX], sleep_choice_count;
static json_object *settings, *display_state, *controllers_state, *audio_nodes, *audio_devices;

static struct wl_display *display;
static struct wl_compositor *compositor;
static struct wl_shm *shm;
static struct zwlr_layer_shell_v1 *layer_shell;
static struct wp_security_context_manager_v1 *security;
static struct zwlr_foreign_toplevel_manager_v1 *toplevels;
static struct ext_foreign_toplevel_list_v1 *listing;
static struct ext_foreign_toplevel_image_capture_source_manager_v1 *toplevel_capture;
static struct ext_image_copy_capture_manager_v1 *image_copy;
static struct zwp_linux_dmabuf_v1 *dmabuf;
static struct wl_seat *seat;
static struct wl_surface *surface;
static struct zwlr_layer_surface_v1 *layer;
static enum surface_mode mode;
static struct buffer buffers[BUFFERS];
static int width, height, output_width, output_height, draw_pending;

static pid_t spawn(char *const argv[], int input, int output, int errors) {
    posix_spawn_file_actions_t actions;
    posix_spawnattr_t attributes;
    sigset_t none, piped;
    pid_t pid;
    sigemptyset(&none);
    sigemptyset(&piped);
    sigaddset(&piped, SIGPIPE);
    posix_spawnattr_init(&attributes);
    posix_spawnattr_setflags(&attributes, POSIX_SPAWN_SETSIGMASK | POSIX_SPAWN_SETSIGDEF);
    posix_spawnattr_setsigmask(&attributes, &none);
    posix_spawnattr_setsigdefault(&attributes, &piped);
    posix_spawn_file_actions_init(&actions);
    if (input >= 0) posix_spawn_file_actions_adddup2(&actions, input, STDIN_FILENO);
    else posix_spawn_file_actions_addopen(&actions, STDIN_FILENO, "/dev/null", O_RDONLY, 0);
    posix_spawn_file_actions_adddup2(&actions, output, STDOUT_FILENO);
    posix_spawn_file_actions_adddup2(&actions, errors, STDERR_FILENO);
    int error = posix_spawnp(&pid, argv[0], &actions, &attributes, argv, environ);
    posix_spawn_file_actions_destroy(&actions);
    posix_spawnattr_destroy(&attributes);
    if (error) { fprintf(stderr, "%s: %s\n", argv[0], strerror(error)); return -1; }
    return pid;
}

static void start(char *const argv[]) { spawn(argv, -1, log_file, log_file); }

static void tell_padd(const char *format, ...) {
    char line[PIPE_BUF];
    va_list arguments;
    va_start(arguments, format);
    int length = vsnprintf(line, sizeof line - 1, format, arguments);
    va_end(arguments);
    if (length >= (int)sizeof line - 1) length = sizeof line - 2;
    line[length++] = '\n';
    int fd = open(PAD_FIFO, O_WRONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0 || write(fd, line, length) < 0) perror(PAD_FIFO);
    if (fd >= 0) close(fd);
}

static pid_t spawn_reading(char *const argv[], int *output) {
    int pipe_ends[2];
    *output = -1;
    if (pipe2(pipe_ends, O_CLOEXEC) < 0) { perror(argv[0]); return -1; }
    pid_t pid = spawn(argv, -1, pipe_ends[1], log_file);
    close(pipe_ends[1]);
    if (pid < 0) close(pipe_ends[0]);
    else *output = pipe_ends[0];
    return pid;
}

static void notify(const char *format, ...) {
    if (notification_count == NOTIFICATIONS_MAX)
        memmove(notifications, notifications + 1, sizeof notifications[0] * --notification_count);
    struct notification *notification = &notifications[notification_count++];
    va_list arguments;
    va_start(arguments, format);
    vsnprintf(notification->text, sizeof notification->text, format, arguments);
    va_end(arguments);
    clock_gettime(CLOCK_MONOTONIC, &notification->expires);
    notification->expires.tv_sec += NOTIFICATION_SECONDS;
    struct itimerspec timer = {.it_value = notifications[0].expires};
    timerfd_settime(notification_timer, TFD_TIMER_ABSTIME, &timer, NULL);
    changed = 1;
}

static void expire_notifications(void) {
    struct timespec now;
    int expired = 0;
    clock_gettime(CLOCK_MONOTONIC, &now);
    while (expired < notification_count && notifications[expired].expires.tv_sec <= now.tv_sec) expired++;
    notification_count -= expired;
    memmove(notifications, notifications + expired, sizeof notifications[0] * notification_count);
    struct itimerspec timer = {.it_value = notification_count ? notifications[0].expires : (struct timespec){0}};
    timerfd_settime(notification_timer, TFD_TIMER_ABSTIME, &timer, NULL);
    changed = 1;
}

static const char *key(int index) {
    static char text[GAME_KEY_MAX];
    snprintf(text, sizeof text, "%.*s/%.*s", NAME_MAX, games[index].slug, NAME_MAX, games[index].platform);
    return text;
}

static const char *name(int index) {
    static char text[GAME_KEY_MAX];
    snprintf(text, sizeof text, "%.*s-%.*s", NAME_MAX, games[index].slug, NAME_MAX, games[index].platform);
    return text;
}

static char *rewind_path(char *path, int index, const char *format, ...) {
    va_list arguments;
    int length = snprintf(path, PATH_MAX, REWIND_DIR "/%s", name(index));
    va_start(arguments, format);
    vsnprintf(path + length, PATH_MAX - length, format, arguments);
    va_end(arguments);
    return path;
}

static int remove_entry(const char *path, const struct stat *status, int type, struct FTW *walk) {
    (void)status, (void)type, (void)walk;
    return remove(path);
}

static void remove_rewinds(int index) {
    char path[PATH_MAX];
    nftw(rewind_path(path, index, ""), remove_entry, REWIND_TREE_DEPTH, FTW_DEPTH | FTW_PHYS);
    *newest_name = '\0';
}

static int is_state(const struct dirent *entry) {
    const char *extension = strrchr(entry->d_name, '.');
    return extension && !strncmp(extension, STATE_EXTENSION, strlen(STATE_EXTENSION));
}

static int slot(const char *state) { return atoi(strrchr(state, '.') + strlen(STATE_EXTENSION)); }

static int by_slot(const struct dirent **left, const struct dirent **right) { return slot((*left)->d_name) - slot((*right)->d_name); }

static void remove_state(const char *state) {
    char path[PATH_MAX];
    unlink(rewind_path(path, active, "/" STATES "/%s", state));
    unlink(rewind_path(path, active, "/" STATES "/%s" SCREEN_EXTENSION, state));
}

static void remember_newest(cairo_surface_t *screen) {
    cairo_surface_destroy(newest_screen);
    newest_screen = screen;
    snprintf(newest_name, sizeof newest_name, "%s", rewinds[rewind_count - 1]);
}

static void refresh_rewinds(void) {
    char path[PATH_MAX];
    struct dirent **entries = NULL;
    int count = active < 0 ? 0 : scandir(rewind_path(path, active, "/" STATES), &entries, is_state, by_slot);
    char previous[REWIND_STATES][NAME_MAX + 1];
    cairo_surface_t *cached[REWIND_STATES];
    int previous_count = rewind_count;
    memcpy(previous, rewinds, sizeof rewinds);
    memcpy(cached, thumbnails, sizeof thumbnails);
    rewind_count = 0;
    for (int i = 0; i < count; i++) {
        if (i < count - REWIND_STATES) remove_state(entries[i]->d_name);
        else snprintf(rewinds[rewind_count++], sizeof rewinds[0], "%s", entries[i]->d_name);
        free(entries[i]);
    }
    for (int i = 0; i < rewind_count; i++) {
        thumbnails[i] = NULL;
        for (int j = 0; j < previous_count; j++)
            if (cached[j] && !strcmp(previous[j], rewinds[i])) {
                thumbnails[i] = cached[j];
                cached[j] = NULL;
            }
    }
    for (int j = 0; j < previous_count; j++) cairo_surface_destroy(cached[j]);
    free(entries);
    if (rewind_back > rewind_count) rewind_back = rewind_count;
    if (pending_screen && rewind_count && slot(rewinds[rewind_count - 1]) > pending_after) {
        cairo_surface_write_to_png(pending_screen, rewind_path(path, active, "/" STATES "/%s" SCREEN_EXTENSION, rewinds[rewind_count - 1]));
        remember_newest(pending_screen);
        pending_screen = NULL;
    }
    changed = 1;
}

static cairo_surface_t *state_image(int index, const char *extension) {
    char path[PATH_MAX];
    return cairo_image_surface_create_from_png(rewind_path(path, active, "/" STATES "/%s%s", rewinds[index], extension));
}

static int changed_percent(cairo_surface_t *frame, cairo_surface_t *last) {
    int frame_width = cairo_image_surface_get_width(frame), frame_height = cairo_image_surface_get_height(frame);
    int stride = cairo_image_surface_get_stride(frame), changed_pixels = 0;
    if (cairo_surface_status(last) || cairo_image_surface_get_width(last) != frame_width || cairo_image_surface_get_height(last) != frame_height)
        return PERCENT;
    unsigned char *now = cairo_image_surface_get_data(frame), *before = cairo_image_surface_get_data(last);
    for (int y = 0; y < frame_height; y++)
        for (int x = 0; x < frame_width; x++)
            changed_pixels += (((uint32_t *)(now + y * stride))[x] ^ ((uint32_t *)(before + y * stride))[x]) & RGB_MASK;
    return changed_pixels * PERCENT / (frame_width * frame_height);
}

static const char *rewind_age(void) {
    static char text[TEXT_MAX];
    char path[PATH_MAX];
    struct stat status;
    struct timespec now;
    if (!rewind_back) return "Now";
    clock_gettime(CLOCK_REALTIME, &now);
    long seconds = stat(rewind_path(path, active, "/" STATES "/%s", rewinds[rewind_count - rewind_back]), &status) ? 0 : now.tv_sec - status.st_mtime;
    snprintf(text, sizeof text, "%ld:%02ld ago", seconds / SECONDS_PER_MINUTE, seconds % SECONDS_PER_MINUTE);
    return text;
}

static const char *store_label(int store) { return strrchr(stores[store], '/') + 1; }

static int find_game(const char *slug, const char *platform) {
    for (int i = 0; i < game_count; i++)
        if (!strcmp(games[i].slug, slug) && !strcmp(games[i].platform, platform)) return i;
    if (game_count == GAMES_MAX) return -1;
    struct game *game = &games[game_count];
    snprintf(game->slug, sizeof game->slug, "%s", slug);
    snprintf(game->platform, sizeof game->platform, "%s", platform);
    return game_count++;
}

static void read_title(const char *directory, const char *slug, char *title) {
    char path[PATH_MAX];
    json_object *field;
    snprintf(path, sizeof path, "%s/%s/" MANIFEST, directory, slug);
    json_object *manifest = json_object_from_file(path);
    snprintf(title, TEXT_MAX, "%s", json_object_object_get_ex(manifest, "title", &field) ? json_object_get_string(field) : slug);
    json_object_put(manifest);
}

static void scan(const char *directory, int store) {
    DIR *slugs = opendir(directory);
    for (struct dirent *slug; slugs && (slug = readdir(slugs));) {
        char path[PATH_MAX], title[TEXT_MAX];
        if (slug->d_name[0] == '.' || slug->d_type != DT_DIR) continue;
        snprintf(path, sizeof path, "%s/%s", directory, slug->d_name);
        read_title(directory, slug->d_name, title);
        DIR *platforms = opendir(path);
        for (struct dirent *platform; platforms && (platform = readdir(platforms));) {
            int index = platform->d_name[0] == '.' || platform->d_type != DT_DIR ? -1 : find_game(slug->d_name, platform->d_name);
            if (index < 0) continue;
            snprintf(games[index].title, sizeof games[index].title, "%s", title);
            if (store < 0) games[index].installed = 1;
            else games[index].stores |= 1u << store;
        }
        if (platforms) closedir(platforms);
    }
    if (slugs) closedir(slugs);
}

static void rescan_installed(void) {
    for (int i = 0; i < game_count; i++) games[i].installed = 0;
    scan(GAMES_DIR, -1);
}

static void rescan(void) {
    rescan_installed();
    for (int i = 0; i < game_count; i++) games[i].stores = 0;
    for (int store = 0; store < store_count; store++) {
        char path[PATH_MAX];
        snprintf(path, sizeof path, CATALOG_DIR "/%d", store + 1);
        scan(path, store);
    }
}

static void read_stores(void) {
    FILE *file = fopen(STORES_FILE, "r");
    store_count = 0;
    while (file && store_count < STORES_MAX && fgets(stores[store_count], sizeof stores[0], file)) {
        stores[store_count][strcspn(stores[store_count], "\n")] = '\0';
        if (strchr(stores[store_count], '/')) store_count++;
    }
    if (file) fclose(file);
}

static int store_online(int store) {
    char path[PATH_MAX];
    snprintf(path, sizeof path, CATALOG_DIR "/%d", store + 1);
    return !access(path, F_OK);
}

static void refresh(void) {
    if (refresher > 0) { refresh_pending = 1; return; }
    refresh_pending = 0;
    refresher = spawn((char *[]){"games", "refresh", NULL}, -1, log_file, log_file);
}

static const char *string_at(json_object *object, const char *first, const char *second) {
    json_object *field;
    if (!json_object_object_get_ex(object, first, &field)) return "";
    if (second && !json_object_object_get_ex(field, second, &field)) return "";
    const char *text = json_object_get_string(field);
    return text ? text : "";
}

static int level_in(json_object *props) {
    json_object *volumes;
    if (!json_object_object_get_ex(props, "channelVolumes", &volumes) || !json_object_is_type(volumes, json_type_array) ||
        !json_object_array_length(volumes))
        return -1;
    double sum = 0;
    for (size_t j = 0; j < json_object_array_length(volumes); j++) sum += json_object_get_double(json_object_array_get_idx(volumes, j));
    return cbrt(sum / json_object_array_length(volumes)) * PERCENT + 0.5;
}

static json_object *params_of(json_object *object, const char *name) {
    json_object *info, *params, *list;
    if (!json_object_object_get_ex(object, "info", &info) || !json_object_object_get_ex(info, "params", &params) ||
        !json_object_object_get_ex(params, name, &list) || !json_object_is_type(list, json_type_array))
        return NULL;
    return list;
}

static json_object *volume_props(json_object *node, json_object *props) {
    json_object *device, *routes, *route_props, *list = params_of(node, "Props");
    if (json_object_object_get_ex(audio_devices, string_at(props, "device.id", NULL), &device) && (routes = params_of(device, "Route")))
        for (size_t i = 0; i < json_object_array_length(routes); i++) {
            json_object *route = json_object_array_get_idx(routes, i);
            if (!strcmp(string_at(route, "device", NULL), string_at(props, "card.profile.device", NULL)) &&
                json_object_object_get_ex(route, "props", &route_props) && level_in(route_props) >= 0)
                return route_props;
        }
    for (size_t i = 0; list && i < json_object_array_length(list); i++)
        if (level_in(json_object_array_get_idx(list, i)) >= 0) return json_object_array_get_idx(list, i);
    return NULL;
}

static void read_audio(void) {
    output_count = microphone_count = 0;
    volume = microphone_volume = -1;
    microphone_muted = 0;
    *microphone_pad = 0;
    json_object_object_foreach(audio_nodes, key, node) {
        json_object *info, *props;
        json_object_object_get_ex(node, "info", &info);
        json_object_object_get_ex(info, "props", &props);
        int sink = !strcmp(string_at(props, "media.class", NULL), "Audio/Sink");
        if (sink && !strcmp(string_at(props, "node.name", NULL), default_output)) volume = level_in(volume_props(node, props));
        if (!sink && !strcmp(string_at(props, "node.name", NULL), default_microphone)) {
            json_object *volume_state = volume_props(node, props);
            microphone_volume = level_in(volume_state);
            microphone_muted = !strcmp(string_at(volume_state, "mute", NULL), "true");
            snprintf(microphone_pad, sizeof microphone_pad, "%s", string_at(props, "api.bluez5.address", NULL));
        }
        if ((sink ? output_count : microphone_count) == DEVICES_MAX) continue;
        struct device *device = sink ? &outputs[output_count++] : &microphones[microphone_count++];
        device->id = atoi(key);
        snprintf(device->node, sizeof device->node, "%s", string_at(props, "node.name", NULL));
        snprintf(device->name, sizeof device->name, "%s",
                 *string_at(props, "node.description", NULL) ? string_at(props, "node.description", NULL) : device->node);
    }
    changed = 1;
}

static int track_audio(json_object *object) {
    json_object *field, *metadata, *info, *props;
    char id[TEXT_MAX];
    int relevant = 0;
    snprintf(id, sizeof id, "%d", json_object_object_get_ex(object, "id", &field) ? json_object_get_int(field) : -1);
    const char *type = string_at(object, "type", NULL);
    if (!*type) {
        int known = json_object_object_get_ex(audio_nodes, id, NULL) || json_object_object_get_ex(audio_devices, id, NULL);
        json_object_object_del(audio_nodes, id);
        json_object_object_del(audio_devices, id);
        return known;
    }
    if (!strcmp(type, "PipeWire:Interface:Node") || !strcmp(type, "PipeWire:Interface:Device")) {
        if (!json_object_object_get_ex(object, "info", &info) || !json_object_object_get_ex(info, "props", &props)) return 0;
        const char *class = string_at(props, "media.class", NULL);
        int device = !strcmp(class, "Audio/Device");
        int real_source = !strcmp(class, "Audio/Source") && strcmp(string_at(props, "node.virtual", NULL), "true");
        if (!device && !real_source && strcmp(class, "Audio/Sink")) return 0;
        json_object_object_add(device ? audio_devices : audio_nodes, id, json_object_get(object));
        return 1;
    }
    for (size_t i = 0; json_object_object_get_ex(object, "metadata", &metadata) && i < json_object_array_length(metadata); i++) {
        json_object *entry = json_object_array_get_idx(metadata, i);
        const char *key = string_at(entry, "key", NULL);
        char *chosen = !strcmp(key, "default.audio.sink") ? default_output : !strcmp(key, "default.audio.source") ? default_microphone : NULL;
        if (!chosen) continue;
        snprintf(chosen, TEXT_MAX, "%s", string_at(entry, "value", "name"));
        relevant = 1;
    }
    return relevant;
}

static void start_monitor(void) {
    char socket_path[PATH_MAX];
    snprintf(socket_path, sizeof socket_path, "%s/" PIPEWIRE_SOCKET, getenv("XDG_RUNTIME_DIR"));
    if (monitor > 0 || access(socket_path, F_OK)) return;
    monitor = spawn_reading((char *[]){"pw-dump", "--monitor", "--no-colors", NULL}, &monitor_output);
    json_object_put(audio_nodes);
    json_object_put(audio_devices);
    audio_nodes = json_object_new_object();
    audio_devices = json_object_new_object();
    *default_output = *default_microphone = 0;
    json_tokener_reset(monitor_parser);
}

static void read_monitor(void) {
    char chunk[BUFSIZ];
    int relevant = 0;
    ssize_t count = read(monitor_output, chunk, sizeof chunk);
    for (const char *at = chunk; count > 0;) {
        json_object *changes = json_tokener_parse_ex(monitor_parser, at, count);
        size_t used = json_tokener_get_parse_end(monitor_parser);
        at += used;
        count -= used;
        for (size_t i = 0; json_object_is_type(changes, json_type_array) && i < json_object_array_length(changes); i++)
            relevant |= track_audio(json_object_array_get_idx(changes, i));
        json_object_put(changes);
        if (!changes && json_tokener_get_error(monitor_parser) != json_tokener_continue) json_tokener_reset(monitor_parser);
        if (!changes) break;
    }
    if (relevant) read_audio();
}

static const char *chosen(struct device *list, int count, const char *node) {
    for (int i = 0; i < count; i++)
        if (!strcmp(list[i].node, node)) return list[i].name;
    return "";
}

static json_object *list_at(json_object *object, const char *name) {
    json_object *list;
    return json_object_object_get_ex(object, name, &list) ? list : NULL;
}

static int length_of(json_object *list) { return list ? json_object_array_length(list) : 0; }

static int display_count(void) { return length_of(list_at(display_state, "displays")); }

static const char *display_label(int index) { return string_at(json_object_array_get_idx(list_at(display_state, "displays"), index), "label", NULL); }

static int int_at(json_object *object, const char *name) {
    json_object *field;
    return json_object_object_get_ex(object, name, &field) ? json_object_get_int(field) : 0;
}

static int chosen_resolution(void) { return int_at(settings, "resolution"); }

static const char *display_value(void) {
    static char value[TEXT_MAX];
    const char *pinned = string_at(settings, "display", NULL);
    snprintf(value, sizeof value, AUTO " (%s)", string_at(display_state, "display", NULL));
    return *pinned ? pinned : value;
}

static const char *mode_text(const char *mode) {
    static char text[TEXT_MAX];
    int width, height, refresh;
    if (sscanf(mode, "%dx%d@%d", &width, &height, &refresh) != 3) return mode;
    snprintf(text, sizeof text, "%dx%d %g Hz", width, height, round((double)refresh / MHZ_PER_HZ * REFRESH_PRECISION) / REFRESH_PRECISION);
    return text;
}

static const char *chosen_mode(void) { return mode_staged ? staged_mode : string_at(settings, "mode", NULL); }

static const char *display_mode_value(void) {
    static char value[TEXT_MAX];
    snprintf(value, sizeof value, AUTO " (%s)", mode_text(string_at(display_state, "mode", NULL)));
    return *chosen_mode() ? mode_text(chosen_mode()) : value;
}

static const char *resolution_value(void) {
    static char value[TEXT_MAX];
    if (chosen_resolution()) snprintf(value, sizeof value, "%dp", chosen_resolution());
    else snprintf(value, sizeof value, AUTO " (%s)", string_at(display_state, "canvas", NULL));
    return value;
}

static int cycle(int current, int count, int step) { return (current + step + count + 1) % (count + 1); }

static void cycle_display(int step) {
    int current = 0, count = display_count();
    for (int i = 0; i < count; i++)
        if (!strcmp(display_label(i), string_at(settings, "display", NULL))) current = i + 1;
    int next = cycle(current, count, step);
    start((char *[]){"settings", "display", next ? (char *)display_label(next - 1) : "", NULL});
}

static void cycle_display_mode(int step) {
    json_object *modes = list_at(display_state, "modes");
    int current = 0, count = length_of(modes);
    for (int i = 0; i < count; i++)
        if (!strcmp(json_object_get_string(json_object_array_get_idx(modes, i)), chosen_mode())) current = i + 1;
    int next = cycle(current, count, step);
    snprintf(staged_mode, sizeof staged_mode, "%s", next ? json_object_get_string(json_object_array_get_idx(modes, next - 1)) : "");
    mode_staged = strcmp(staged_mode, string_at(settings, "mode", NULL)) != 0;
}

static void cycle_resolution(int step) {
    char value[TEXT_MAX];
    int current = 0;
    for (int i = 0; i < resolution_count; i++)
        if (resolutions[i] == chosen_resolution()) current = i + 1;
    int next = cycle(current, resolution_count, step);
    snprintf(value, sizeof value, "%d", next ? resolutions[next - 1] : 0);
    start((char *[]){"settings", "resolution", value, NULL});
}

static int chosen_sleep(void) {
    json_object *field;
    return json_object_object_get_ex(settings, "sleep", &field) ? json_object_get_int(field) : sleep_choices[0];
}

static const char *sleep_value(void) {
    static char value[TEXT_MAX];
    if (!chosen_sleep()) return "Never";
    snprintf(value, sizeof value, "%d min", chosen_sleep());
    return value;
}

static void cycle_sleep(int step) {
    char value[TEXT_MAX];
    int current = 0;
    for (int i = 0; i < sleep_choice_count; i++)
        if (sleep_choices[i] == chosen_sleep()) current = i;
    snprintf(value, sizeof value, "%d", sleep_choices[cycle(current, sleep_choice_count - 1, step)]);
    start((char *[]){"settings", "sleep", value, NULL});
}

static void read_settings(void) {
    json_object_put(settings);
    settings = json_object_from_file(SETTINGS_FILE);
    changed = 1;
}

static void read_display(void) {
    char previous[TEXT_MAX];
    snprintf(previous, sizeof previous, "%s", string_at(display_state, "pending", NULL));
    json_object_put(display_state);
    display_state = json_object_from_file(DISPLAY_FILE);
    const char *pending = string_at(display_state, "pending", NULL);
    if (*pending && strcmp(pending, previous)) notify("Close and restart your games to render on %s", pending);
    changed = 1;
}

static json_object *device_of(int id) {
    json_object *list = list_at(controllers_state, "devices");
    for (int i = 0; i < length_of(list); i++)
        if (int_at(json_object_array_get_idx(list, i), "id") == id) return json_object_array_get_idx(list, i);
    return NULL;
}

static void drop_member(int index) {
    if (members[index].device == focus) {
        guide_shown = 0;
        focus = NO_DEVICE;
    }
    members[index] = members[--member_count];
}

static void read_controllers(void) {
    json_object_put(controllers_state);
    controllers_state = json_object_from_file(CONTROLLERS_FILE);
    for (int i = member_count - 1; i >= 0; i--)
        if (!device_of(members[i].device)) drop_member(i);
    if (!device_of(picker)) picker = NO_DEVICE;
    changed = 1;
}

static int controller_count(void) { return length_of(list_at(controllers_state, "controllers")); }

static json_object *controller_at(int index) { return json_object_array_get_idx(list_at(controllers_state, "controllers"), index); }

static int microphone_is_muted(void) {
    for (int i = 0; !microphone_muted && *microphone_pad && i < controller_count(); i++)
        if (!strcasecmp(string_at(controller_at(i), "address", NULL), microphone_pad) && !strcmp(string_at(controller_at(i), "muted", NULL), "true")) return 1;
    return microphone_muted;
}

static const char *controller_name(void) {
    for (int i = 0; i < controller_count(); i++)
        if (!strcasecmp(string_at(controller_at(i), "address", NULL), chosen_controller)) return string_at(controller_at(i), "name", NULL);
    return chosen_controller;
}

static const char *controller_status(json_object *controller) {
    static char text[TEXT_MAX];
    json_object *battery;
    const char *link = string_at(controller, "connection", NULL);
    int length = snprintf(text, sizeof text, "%s", !strcmp(link, "bluetooth") ? "Bluetooth" : !strcmp(link, "wired") ? "Wired" : "Not connected");
    if (json_object_object_get_ex(controller, "battery", &battery) && battery)
        length += snprintf(text + length, sizeof text - length, "  %d%%", json_object_get_int(battery));
    if (int_at(controller, "charging")) snprintf(text + length, sizeof text - length, " charging");
    return text;
}

static const char *profile_name(int profile) {
    static char text[TEXT_MAX];
    snprintf(text, sizeof text, PROFILE_NAME, profile);
    return text;
}

static int seat_of(int profile) {
    for (int i = 0; i < player_count; i++)
        if (players[i] == profile) return i;
    return -1;
}

static int profile_of(int device) {
    for (int i = 0; i < member_count; i++)
        if (members[i].device == device) return members[i].profile;
    return 0;
}

static int attract(void) { return !player_count && picker == NO_DEVICE; }

static const char *kind_of(int device) { return string_at(device_of(device), "kind", NULL); }

static int mode_index(json_object *device, const char *mode) {
    json_object *modes = list_at(device, "modes");
    for (int i = 0; i < length_of(modes); i++)
        if (!strcmp(json_object_get_string(json_object_array_get_idx(modes, i)), mode)) return i;
    return -1;
}

static const char *mode_label(const char *mode) {
    for (unsigned i = 0; i < sizeof mode_labels / sizeof *mode_labels; i++)
        if (!strcmp(mode_labels[i].name, mode)) return mode_labels[i].label;
    return mode;
}

static int by_title(const void *left, const void *right) {
    const struct game *a = &games[*(const int *)left], *b = &games[*(const int *)right];
    int order = strcasecmp(a->title, b->title);
    return order ? order : strcmp(a->platform, b->platform);
}

static int sorted(int *order, int store_view) {
    int count = 0;
    for (int i = 0; i < game_count; i++)
        if (store_view ? games[i].stores != 0 : games[i].installed || games[i].job) order[count++] = i;
    qsort(order, count, sizeof *order, by_title);
    return count;
}

static int add_row(int count, enum action action, int arg, const char *label, const char *format, ...) {
    struct row *row = &rows[count];
    va_list arguments;
    va_start(arguments, format);
    vsnprintf(row->value, sizeof row->value, format, arguments);
    va_end(arguments);
    snprintf(row->label, sizeof row->label, "%s", label);
    row->action = action;
    row->arg = arg;
    return count + 1;
}

static struct window *game_window(int index) {
    for (int i = 0; i < window_count; i++)
        if (!strcmp(windows[i].app_id, key(index))) return &windows[i];
    return NULL;
}

static const char *status(int index) {
    static char text[TEXT_MAX];
    struct game *game = &games[index];
    if (game->job && game->percent < 0) return game->job_text;
    if (game->job) {
        snprintf(text, sizeof text, "%s %d%%", game->job_text, game->percent);
        return text;
    }
    if (game->pid && !game_window(index)) return tasks[LAUNCHING];
    if (game->pid) return game->paused ? "paused" : "running";
    return game->installed ? "installed" : "";
}

static int in_game(void) { return active >= 0 && !dashboard_open; }

static void write_json(const char *path, json_object *object) {
    char partial[PATH_MAX + sizeof PARTIAL];
    snprintf(partial, sizeof partial, "%s" PARTIAL, path);
    if (json_object_to_file_ext(partial, object, JSON_C_TO_STRING_PLAIN) < 0 || rename(partial, path) < 0) perror(path);
}

static const char *emulate_path(int profile) {
    static char path[PATH_MAX];
    snprintf(path, sizeof path, PROFILES_DIR "/%d/" EMULATE_FILE, profile);
    return path;
}

static json_object *emulate_of(int profile) {
    if (!emulate_choices[profile]) emulate_choices[profile] = json_object_from_file(emulate_path(profile));
    if (!emulate_choices[profile]) emulate_choices[profile] = json_object_new_object();
    return emulate_choices[profile];
}

static const char *default_mode(int game, json_object *device) {
    const char *mode = string_at(games[game].emulate, string_at(device, "kind", NULL), NULL);
    return mode_index(device, mode) < 0 ? string_at(device, "native", NULL) : mode;
}

static const char *mode_for(int profile, int game, json_object *device) {
    if (game < 0) return string_at(device, "native", NULL);
    const char *chosen = string_at(emulate_of(profile), key(game), string_at(device, "kind", NULL));
    return mode_index(device, chosen) < 0 ? default_mode(game, device) : chosen;
}

static const char *emulate_value(void) {
    static char value[TEXT_MAX];
    json_object *device = device_of(focus);
    const char *chosen = string_at(emulate_of(profile_of(focus)), key(active), kind_of(focus));
    if (mode_index(device, chosen) >= 0) return mode_label(chosen);
    snprintf(value, sizeof value, "Default (%s)", mode_label(default_mode(active, device)));
    return value;
}

static void choose_mode(int profile, int game, const char *kind, const char *mode) {
    char directory[PATH_MAX];
    json_object *choices = emulate_of(profile), *game_choices;
    if (!json_object_object_get_ex(choices, key(game), &game_choices)) {
        game_choices = json_object_new_object();
        json_object_object_add(choices, key(game), game_choices);
    }
    if (*mode) json_object_object_add(game_choices, kind, json_object_new_string(mode));
    else json_object_object_del(game_choices, kind);
    snprintf(directory, sizeof directory, PROFILES_DIR "/%d", profile);
    mkdir(directory, S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH);
    write_json(emulate_path(profile), choices);
}

static void apply_modes(void) {
    if (active != applied_game) tell_padd("game %s", active >= 0 ? key(active) : NO_GAME);
    applied_game = active;
    for (int i = 0; i < member_count; i++) {
        json_object *device = device_of(members[i].device);
        if (device) tell_padd("mode %d %s", members[i].device, mode_for(members[i].profile, active, device));
    }
}

static void screen_path(int index, char *path) {
    snprintf(path, PATH_MAX, GAMES_DIR "/%s/" SCREEN_FILE, key(index));
}

static void toggle_second_screen(void) {
    char path[PATH_MAX];
    struct game *game = &games[active];
    screen_path(active, path);
    game->screen_shown = !game->screen_shown;
    if (!game->screen_shown) unlink(path);
    else close(open(path, O_WRONLY | O_CREAT | O_CLOEXEC, S_IRUSR | S_IWUSR));
}

static void cycle_mode(int step) {
    json_object *device = device_of(focus), *modes = list_at(device, "modes");
    int current = mode_index(device, string_at(emulate_of(profile_of(focus)), key(active), kind_of(focus))) + 1;
    int next = cycle(current, length_of(modes), step);
    choose_mode(profile_of(focus), active, kind_of(focus), next ? json_object_get_string(json_object_array_get_idx(modes, next - 1)) : "");
    apply_modes();
}

static int owned_games(int profile) {
    int count = 0;
    for (int i = 0; i < game_count; i++) count += games[i].owner == profile && games[i].pid;
    return count;
}

static int rewindable(void) { return in_game() && games[active].rewinds; }

static int build(struct view *view) {
    int count = 0, order[GAMES_MAX];
    char level[TEXT_MAX] = "", microphone_level[TEXT_MAX] = "";
    struct game *game = view->game >= 0 ? &games[view->game] : NULL;
    switch (view->screen) {
    case HOME:
    case STORE:
        for (int i = 0, total = sorted(order, view->screen == STORE); i < total; i++)
            count = add_row(count, OPEN_GAME, order[i], games[order[i]].title, "%s  %s", games[order[i]].platform, status(order[i]));
        if (view->screen == HOME) {
            count = add_row(count, OPEN_STORE, 0, "Store", "");
            count = add_row(count, OPEN_SETTINGS, 0, "Settings", "");
        }
        break;
    case GAME:
        if (game->pid) {
            count = add_row(count, RESUME, 0, "Resume", "");
            count = add_row(count, CLOSE, 0, "Close", "");
        } else if (game->job) {
            count = add_row(count, NOTHING, 0, status(view->game), "");
        } else if (game->installed) {
            count = add_row(count, PLAY, 0, "Play", "");
            count = add_row(count, UNINSTALL, 0, "Uninstall", "");
        } else {
            for (int store = 0; store < store_count; store++)
                if (game->stores & (1u << store)) count = add_row(count, INSTALL, store, "Install from", "%s", store_label(store));
        }
        break;
    case GUIDE:
        if (in_game()) count = add_row(count, SHOW_DASHBOARD, 0, "Dashboard", "");
        if (rewindable()) count = add_row(count, REWIND, 0, "Rewind", "%s", rewind_age());
        if (in_game() && profile_of(focus)) count = add_row(count, EMULATE, 0, "Emulate", "%s", emulate_value());
        if (in_game() && games[active].second_screen)
            count = add_row(count, SECOND_SCREEN, 0, "Second screen", "%s", games[active].screen_shown ? "Inset" : "Off");
        if (volume >= 0) snprintf(level, sizeof level, "%d%%", volume);
        count = add_row(count, VOLUME, 0, "Volume", "%s", level);
        count = add_row(count, OPEN_OUTPUTS, 0, "Output", "%s", chosen(outputs, output_count, default_output));
        count = add_row(count, OPEN_MICROPHONES, 0, "Microphone", "%s", chosen(microphones, microphone_count, default_microphone));
        if (microphone_is_muted()) snprintf(microphone_level, sizeof microphone_level, "Muted");
        else if (microphone_volume >= 0) snprintf(microphone_level, sizeof microphone_level, "%d%%", microphone_volume);
        count = add_row(count, MICROPHONE_VOLUME, 0, "Microphone volume", "%s", microphone_level);
        count = add_row(count, OPEN_CONTROLLERS, 0, "Controllers", "");
        count = add_row(count, RELOAD, 0, "Reload", "");
        count = add_row(count, RESTART, 0, "Restart", "");
        count = add_row(count, POWER_OFF, 0, "Power off", "");
        count = add_row(count, LOG_OUT_PLAYER, 0, "Log out", "");
        break;
    case WHOIS:
        for (int profile = 1; profile <= PROFILES; profile++) {
            if (seat_of(profile) < 0) count = add_row(count, LOGIN, profile, profile_name(profile), "");
            else count = add_row(count, LOGIN, profile, profile_name(profile), "Pad %d", seat_of(profile) + 1);
        }
        break;
    case LOG_OUT:
        count = add_row(count, CONFIRM_LOG_OUT, 0, "Log out", "Closes %d games", owned_games(profile_of(focus)));
        break;
    case ATTRACT:
        break;
    case OUTPUTS:
    case MICROPHONES:
        for (int i = 0, total = view->screen == OUTPUTS ? output_count : microphone_count; i < total; i++) {
            struct device *device = view->screen == OUTPUTS ? &outputs[i] : &microphones[i];
            const char *current = view->screen == OUTPUTS ? default_output : default_microphone;
            count = add_row(count, SET_DEVICE, device->id, device->name, !strcmp(device->node, current) ? "selected" : "");
        }
        break;
    case CONTROLLERS:
        if (int_at(controllers_state, "bluetooth"))
            count = add_row(count, PAIR, 0, "Pair new controller", "%s", int_at(controllers_state, "pairing") ? "Searching" : "");
        for (int i = 0; i < controller_count(); i++)
            count = add_row(count, int_at(controller_at(i), "paired") ? OPEN_CONTROLLER : NOTHING, i, string_at(controller_at(i), "name", NULL),
                            "%s", controller_status(controller_at(i)));
        break;
    case CONTROLLER:
        count = add_row(count, FORGET, 0, "Forget", "");
        break;
    case SETTINGS:
        count = add_row(count, CHOOSE_DISPLAY, 0, "Display", "%s", display_value());
        count = add_row(count, CHOOSE_DISPLAY_MODE, 0, "Display resolution", "%s", display_mode_value());
        if (mode_staged) count = add_row(count, SAVE_DISPLAY_MODE, 0, "Save", "");
        count = add_row(count, CHOOSE_RESOLUTION, 0, "Game resolution", "%s", resolution_value());
        count = add_row(count, CHOOSE_SLEEP, 0, "Controller sleep", "%s", sleep_value());
        break;
    }
    for (int i = 0; i < count; i++)
        if (rows[i].action == view->action && rows[i].arg == view->arg) view->selected = i;
    if (view->selected >= count) view->selected = count ? count - 1 : 0;
    return count;
}

static const char *title(struct view *view) {
    static char text[2 * TEXT_MAX];
    if (view->screen == GAME) return games[view->game].title;
    if (view->screen == CONTROLLER) return controller_name();
    if (view->screen == WHOIS) return player_count ? "Who is using this controller?" : "Who's playing?";
    if (view->screen == LOG_OUT) snprintf(text, sizeof text, "Log out %s", profile_name(profile_of(focus)));
    else if (view->screen == GUIDE && profile_of(focus)) snprintf(text, sizeof text, "%s  \u00b7  Pad %d", profile_name(profile_of(focus)), seat_of(profile_of(focus)) + 1);
    else return screens[view->screen].title;
    return text;
}

static struct view *guide_view(void) { return &guides[guide_profile][guide_depths[guide_profile] - 1]; }

static struct view *top(void) {
    if (picker != NO_DEVICE) return &whois;
    return guide_shown ? guide_view() : &dashboard[dashboard_depth - 1];
}

static int interactive(void) { return dashboard_open || guide_shown || picker != NO_DEVICE; }

static void push(enum screen screen, int game) {
    if (guide_shown) {
        if (guide_depths[guide_profile] < DEPTH_MAX) guides[guide_profile][guide_depths[guide_profile]++] = (struct view){.screen = screen, .game = game};
    } else if (dashboard_depth < DEPTH_MAX) dashboard[dashboard_depth++] = (struct view){.screen = screen, .game = game};
}

static void show_game(int index) {
    if (game_window(index)) zwlr_foreign_toplevel_handle_v1_activate(game_window(index)->handle, seat);
}

static int launching(void) { return active >= 0 && !game_window(active); }

static int listen_socket(int index, char *directory, size_t size) {
    struct sockaddr_un address = {.sun_family = AF_UNIX};
    int closer[2];
    snprintf(directory, size, SOCKETS_DIR "/%s", name(index));
    if (snprintf(address.sun_path, sizeof address.sun_path, "%s/" WAYLAND_SOCKET, directory) >= (int)sizeof address.sun_path) {
        fprintf(stderr, "%s: socket path too long\n", directory);
        return 0;
    }
    mkdir(directory, S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH);
    unlink(address.sun_path);
    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0 || bind(fd, (struct sockaddr *)&address, sizeof address) < 0 || listen(fd, SOMAXCONN) < 0 ||
        pipe2(closer, O_CLOEXEC) < 0) {
        perror(address.sun_path);
        if (fd >= 0) close(fd);
        return 0;
    }
    struct wp_security_context_v1 *context = wp_security_context_manager_v1_create_listener(security, fd, closer[0]);
    wp_security_context_v1_set_sandbox_engine(context, SANDBOX_ENGINE);
    wp_security_context_v1_set_app_id(context, key(index));
    wp_security_context_v1_commit(context);
    wp_security_context_v1_destroy(context);
    close(fd);
    close(closer[0]);
    games[index].context = closer[1];
    return 1;
}

static int game_log(int index) {
    char path[PATH_MAX];
    snprintf(path, sizeof path, LOG_DIR "/%s.log", name(index));
    return open(path, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, S_IRUSR | S_IWUSR);
}

static void send_command(int index, const char *command) {
    if (dprintf(games[index].commands, "%s\n", command) < 0) perror(command);
}

static void resume(int index) {
    if (index != active) {
        cairo_surface_destroy(pending_screen);
        pending_screen = NULL;
    }
    show_game(index);
    active = index;
    dashboard_open = 0;
    guide_shown = 0;
    refresh_rewinds();
    apply_modes();
}

static void ended(int index, int status) {
    struct game *game = &games[index];
    char directory[sizeof SOCKETS_DIR + GAME_KEY_MAX], path[sizeof directory + sizeof WAYLAND_SOCKET];
    close(game->commands);
    close(game->context);
    snprintf(directory, sizeof directory, SOCKETS_DIR "/%s", name(index));
    snprintf(path, sizeof path, "%s/" WAYLAND_SOCKET, directory);
    unlink(path);
    rmdir(directory);
    if (!game->closing && status) notify("%s stopped", game->title);
    game->pid = 0;
    remove_rewinds(index);
    if (active == index) active = -1;
    refresh_rewinds();
    apply_modes();
}

static void prepare_rewinds(int index, char *states) {
    char path[PATH_MAX];
    remove_rewinds(index);
    mkdir(rewind_path(path, index, ""), S_IRWXU);
    mkdir(rewind_path(states, index, "/" STATES), S_IRWXU);
    games[index].states_watch = inotify_add_watch(watcher, states, IN_CLOSE_WRITE);
}

static cairo_surface_t *captured_screen(void) {
    uint32_t stride;
    void *mapping = NULL, *pixels = gbm_bo_map(capture.bo, 0, 0, capture.width, capture.height, GBM_BO_TRANSFER_READ, &stride, &mapping);
    if (!pixels) return NULL;
    cairo_surface_t *screen = cairo_image_surface_create(CAIRO_FORMAT_RGB24, capture.width, capture.height);
    pixman_image_t *shot = pixman_image_create_bits(PIXMAN_x8r8g8b8, capture.width, capture.height, pixels, stride);
    pixman_image_t *target = pixman_image_create_bits(PIXMAN_x8r8g8b8, capture.width, capture.height,
                                                      (uint32_t *)cairo_image_surface_get_data(screen), cairo_image_surface_get_stride(screen));
    pixman_image_composite32(PIXMAN_OP_SRC, shot, NULL, target, 0, 0, 0, 0, 0, 0, capture.width, capture.height);
    pixman_image_unref(shot);
    pixman_image_unref(target);
    gbm_bo_unmap(capture.bo, mapping);
    cairo_surface_mark_dirty(screen);
    return screen;
}

static int changed_since_save(cairo_surface_t *screen) {
    if (!rewind_count) return 1;
    if (strcmp(newest_name, rewinds[rewind_count - 1])) remember_newest(state_image(rewind_count - 1, SCREEN_EXTENSION));
    return changed_percent(screen, newest_screen) >= SAVE_CHANGE_PERCENT;
}

static void end_capture(int captured) {
    cairo_surface_t *screen = captured ? captured_screen() : NULL;
    if (capture.save && capture.game == active && screen && changed_since_save(screen)) {
        cairo_surface_destroy(pending_screen);
        pending_screen = cairo_surface_reference(screen);
        pending_after = rewind_count ? slot(rewinds[rewind_count - 1]) : -1;
        send_command(active, "SAVE_STATE");
    }
    if (capture.current) {
        cairo_surface_destroy(current_screen);
        current_screen = cairo_surface_reference(screen);
        guide_shown = 1;
        changed = 1;
    }
    cairo_surface_destroy(screen);
    if (capture.frame) ext_image_copy_capture_frame_v1_destroy(capture.frame);
    if (capture.buffer) wl_buffer_destroy(capture.buffer);
    if (capture.bo) gbm_bo_destroy(capture.bo);
    ext_image_copy_capture_session_v1_destroy(capture.session);
    wl_array_release(&capture.modifiers);
    capture = (struct capture){0};
}

static struct wl_shm_pool *create_pool(size_t size, void **pixels) {
    int fd = memfd_create("overlay", MFD_CLOEXEC);
    ftruncate(fd, size);
    struct wl_shm_pool *pool = wl_shm_create_pool(shm, fd, size);
    *pixels = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);
    return pool;
}

static void on_frame_transform(void *data, struct ext_image_copy_capture_frame_v1 *frame, uint32_t transform) { (void)data, (void)frame, (void)transform; }

static void on_frame_damage(void *data, struct ext_image_copy_capture_frame_v1 *frame, int32_t x, int32_t y, int32_t width, int32_t height) {
    (void)data, (void)frame, (void)x, (void)y, (void)width, (void)height;
}

static void on_frame_time(void *data, struct ext_image_copy_capture_frame_v1 *frame, uint32_t seconds_high, uint32_t seconds_low, uint32_t nanoseconds) {
    (void)data, (void)frame, (void)seconds_high, (void)seconds_low, (void)nanoseconds;
}

static void on_frame_ready(void *data, struct ext_image_copy_capture_frame_v1 *frame) {
    (void)data, (void)frame;
    end_capture(1);
}

static void on_frame_failed(void *data, struct ext_image_copy_capture_frame_v1 *frame, uint32_t reason) {
    (void)data, (void)frame;
    fprintf(stderr, "overlay: capture failed (%u)\n", reason);
    end_capture(0);
}

static const struct ext_image_copy_capture_frame_v1_listener capture_frame_listener = {
    .transform = on_frame_transform, .damage = on_frame_damage, .presentation_time = on_frame_time, .ready = on_frame_ready, .failed = on_frame_failed};

static void on_capture_size(void *data, struct ext_image_copy_capture_session_v1 *session, uint32_t width, uint32_t height) {
    (void)data, (void)session;
    capture.width = width;
    capture.height = height;
}

static void on_capture_shm_format(void *data, struct ext_image_copy_capture_session_v1 *session, uint32_t format) { (void)data, (void)session, (void)format; }

static void on_capture_device(void *data, struct ext_image_copy_capture_session_v1 *session, struct wl_array *device) {
    (void)data, (void)session;
    drmDevicePtr found;
    if (gbm || drmGetDeviceFromDevId(*(dev_t *)device->data, 0, &found)) return;
    int fd = found->available_nodes & (1 << DRM_NODE_RENDER) ? open(found->nodes[DRM_NODE_RENDER], O_RDWR | O_CLOEXEC) : -1;
    drmFreeDevice(&found);
    if (fd >= 0) gbm = gbm_create_device(fd);
}

static void on_capture_format(void *data, struct ext_image_copy_capture_session_v1 *session, uint32_t format, struct wl_array *modifiers) {
    (void)data, (void)session;
    if (format == DRM_FORMAT_XRGB8888) wl_array_copy(&capture.modifiers, modifiers);
}

static void on_capture_done(void *data, struct ext_image_copy_capture_session_v1 *session) {
    (void)data;
    if (capture.frame) return;
    capture.bo = gbm && capture.modifiers.size ? gbm_bo_create_with_modifiers2(gbm, capture.width, capture.height, DRM_FORMAT_XRGB8888, capture.modifiers.data,
                                                                               capture.modifiers.size / sizeof(uint64_t), GBM_BO_USE_RENDERING)
                                               : NULL;
    if (!capture.bo) {
        fprintf(stderr, "overlay: no capture buffer\n");
        end_capture(0);
        return;
    }
    struct zwp_linux_buffer_params_v1 *params = zwp_linux_dmabuf_v1_create_params(dmabuf);
    int fd = gbm_bo_get_fd(capture.bo);
    uint64_t modifier = gbm_bo_get_modifier(capture.bo);
    zwp_linux_buffer_params_v1_add(params, fd, 0, gbm_bo_get_offset(capture.bo, 0), gbm_bo_get_stride(capture.bo), modifier >> (sizeof(uint32_t) * CHAR_BIT),
                                   (uint32_t)modifier);
    capture.buffer = zwp_linux_buffer_params_v1_create_immed(params, capture.width, capture.height, DRM_FORMAT_XRGB8888, 0);
    zwp_linux_buffer_params_v1_destroy(params);
    close(fd);
    capture.frame = ext_image_copy_capture_session_v1_create_frame(session);
    ext_image_copy_capture_frame_v1_add_listener(capture.frame, &capture_frame_listener, NULL);
    ext_image_copy_capture_frame_v1_attach_buffer(capture.frame, capture.buffer);
    ext_image_copy_capture_frame_v1_damage_buffer(capture.frame, 0, 0, capture.width, capture.height);
    ext_image_copy_capture_frame_v1_capture(capture.frame);
}

static void on_capture_stopped(void *data, struct ext_image_copy_capture_session_v1 *session) {
    (void)data, (void)session;
    end_capture(0);
}

static const struct ext_image_copy_capture_session_v1_listener session_listener = {
    .buffer_size = on_capture_size, .shm_format = on_capture_shm_format, .dmabuf_device = on_capture_device, .dmabuf_format = on_capture_format,
    .done = on_capture_done, .stopped = on_capture_stopped};

static int start_capture(int index) {
    struct ext_foreign_toplevel_handle_v1 *handle = game_window(index) ? game_window(index)->listed : NULL;
    if (!handle) return 0;
    struct ext_image_capture_source_v1 *source = ext_foreign_toplevel_image_capture_source_manager_v1_create_source(toplevel_capture, handle);
    capture.game = index;
    capture.session = ext_image_copy_capture_manager_v1_create_session(image_copy, source, 0);
    ext_image_capture_source_v1_destroy(source);
    ext_image_copy_capture_session_v1_add_listener(capture.session, &session_listener, NULL);
    return 1;
}

static void sample_screen(void) {
    if (!rewindable() || games[active].paused || games[active].closing || capture.session || notification_count) return;
    capture.save = start_capture(active);
}

static void load_rewind(void) {
    char command[TEXT_MAX];
    int chosen = rewind_count - rewind_back;
    if (!rewind_back) return;
    snprintf(command, sizeof command, "LOAD_STATE_SLOT %d", slot(rewinds[chosen]));
    send_command(active, command);
    for (int i = chosen + 1; i < rewind_count; i++) remove_state(rewinds[i]);
    rewind_back = 0;
    resume(active);
}

static void freeze(int index, int pause) {
    struct game *game = &games[index];
    game->paused = pause;
    game->freezer = spawn((char *[]){"games", pause ? "pause" : "resume", game->slug, game->platform, NULL}, -1, log_file, log_file);
    if (game->freezer < 0) game->freezer = 0;
}

static void probe_labels(int index) {
    struct game *game = &games[index];
    game->probe = spawn_reading((char *[]){"games", "labels", game->platform, NULL}, &game->probe_output);
    if (game->probe < 0) game->probe = 0;
}

static void read_probe(int index) {
    char path[PATH_MAX];
    struct game *game = &games[index];
    json_object *labels = json_object_from_fd(game->probe_output), *manifest, *platform = NULL, *overrides = NULL;
    game->rewinds = *string_at(labels, REWIND_LABEL, NULL);
    game->second_screen = *string_at(labels, SCREEN_LABEL, NULL);
    screen_path(index, path);
    game->screen_shown = !access(path, F_OK);
    json_object_put(game->emulate);
    game->emulate = json_tokener_parse(string_at(labels, EMULATE_LABEL, NULL));
    if (!game->emulate) game->emulate = json_object_new_object();
    json_object_put(labels);
    snprintf(path, sizeof path, GAMES_DIR "/%s/" MANIFEST, game->slug);
    manifest = json_object_from_file(path);
    if (json_object_object_get_ex(manifest, game->platform, &platform) && json_object_object_get_ex(platform, "emulate", &overrides)) {
        json_object_object_foreach(overrides, kind, mode) json_object_object_add(game->emulate, kind, json_object_get(mode));
    }
    json_object_put(manifest);
    close(game->probe_output);
    game->probe = 0;
    if (index == active) apply_modes();
    changed = 1;
}

static void finish_close(int index) {
    struct game *game = &games[index];
    if (game->freezer) return;
    if (game->paused) freeze(index, 0);
    if (!game->freezer && kill(game->pid, SIGTERM) < 0) perror("close");
}

static void play(int index) {
    char directory[PATH_MAX], states[PATH_MAX], owner[TEXT_MAX];
    int commands[2];
    struct game *game = &games[index];
    if (!listen_socket(index, directory, sizeof directory)) { notify("Could not start %s", game->title); return; }
    if (pipe2(commands, O_CLOEXEC) < 0) { perror("pipe"); close(game->context); return; }
    int output = game_log(index);
    prepare_rewinds(index, states);
    memset(guide_depths, 0, sizeof guide_depths);
    game->rewinds = 0;
    show_game(index);
    snprintf(owner, sizeof owner, "%d", game->owner);
    game->pid = spawn((char *[]){"games", "run", game->slug, game->platform, directory, states, owner, NULL}, commands[0], output, output);
    close(output);
    close(commands[0]);
    game->commands = commands[1];
    if (game->pid < 0) {
        game->pid = 0;
        ended(index, 1);
        return;
    }
    game->paused = game->closing = 0;
    resume(index);
}

static void close_game(int index) {
    show_game(index);
    games[index].closing = 1;
    finish_close(index);
}

static void start_job(int index, enum task task, char *const argv[]) {
    struct game *game = &games[index];
    game->job = spawn_reading(argv, &game->job_output);
    if (game->job < 0) { game->job = 0; return; }
    game->task = task;
    game->percent = -1;
    game->job_length = 0;
    snprintf(game->job_text, sizeof game->job_text, "%s", tasks[task]);
}

static void launch(int index) {
    start_job(index, LAUNCHING, (char *[]){"games", "prepare", games[index].platform, NULL});
    if (games[index].job) resume(index);
}

static void read_lines(int fd, char *buffer, size_t size, size_t *length, void (*handle_line)(char *, void *), void *context) {
    ssize_t count = read(fd, buffer + *length, size - *length);
    if (count <= 0) return;
    *length += count;
    char *start = buffer;
    for (char *end; (end = memchr(start, '\n', buffer + *length - start)); start = end + 1) {
        *end = '\0';
        handle_line(start, context);
    }
    *length -= start - buffer;
    memmove(buffer, start, *length);
    if (*length == size) *length = 0;
}

static void read_step(char *line, void *context) {
    struct game *game = context;
    game->percent = -1;
    sscanf(line, "%*s %d", &game->percent);
    line[strcspn(line, " ")] = '\0';
    if (strcmp(line, game->job_text) && game->task != LAUNCHING) notify("%s: %s", game->title, line);
    memmove(game->job_text, line, strlen(line) + 1);
    changed = 1;
}

static void read_job(int index) {
    struct game *game = &games[index];
    read_lines(game->job_output, game->job_line, sizeof game->job_line, &game->job_length, read_step, game);
}

static void job_done(int index, int status) {
    struct game *game = &games[index];
    close(game->job_output);
    game->job = 0;
    if (game->task == LAUNCHING) {
        if (status) notify("Could not start %s", game->title);
        else if (seat_of(game->owner) >= 0) play(index);
        return;
    }
    rescan_installed();
    if (game->task == INSTALLING) notify(status ? "Install failed: %s" : "Installed %s", game->title);
    else notify(status ? "Uninstall failed: %s" : "Uninstalled %s", game->title);
}

static void save_session(void) {
    json_object *session = json_object_new_array();
    for (int i = 0; i < player_count; i++) json_object_array_add(session, json_object_new_int(players[i]));
    write_json(SESSION_FILE, session);
    json_object_put(session);
}

static void load_session(void) {
    json_object *session = json_object_from_file(SESSION_FILE);
    for (int i = 0; i < length_of(session) && player_count < PADS; i++) players[player_count++] = json_object_get_int(json_object_array_get_idx(session, i));
    json_object_put(session);
}

static void adopt_members(void) {
    json_object *devices = list_at(controllers_state, "devices");
    member_count = 0;
    for (int i = 0; i < length_of(devices); i++) {
        json_object *device = json_object_array_get_idx(devices, i);
        int pad = int_at(device, "pad");
        if (pad && pad <= player_count) members[member_count++] = (struct member){.device = int_at(device, "id"), .profile = players[pad - 1]};
        else if (pad) tell_padd("assign %d 0", int_at(device, "id"));
    }
}

static void seat_devices(void) {
    save_session();
    tell_padd("pads %d", player_count);
    for (int i = 0; i < member_count; i++) tell_padd("assign %d %d", members[i].device, seat_of(members[i].profile) + 1);
    apply_modes();
    changed = 1;
}

static void open_picker(int device) {
    picker = device;
    whois = (struct view){.screen = WHOIS, .game = -1};
    changed = 1;
}

static int seat_for(int profile) {
    return seat_of(profile) >= 0 ? seat_of(profile) : player_count < PADS ? player_count : -1;
}

static void login(int profile, int device) {
    if (profile < 1 || profile > PROFILES) return;
    if (seat_for(profile) < 0) { notify("Every pad is taken"); return; }
    if (!player_count) {
        dashboard_open = 1;
        dashboard_depth = 1;
        memset(guide_depths, 0, sizeof guide_depths);
    }
    if (seat_of(profile) < 0) players[player_count++] = profile;
    for (int i = member_count - 1; i >= 0; i--)
        if (members[i].device == device) drop_member(i);
    if (device != NO_DEVICE && member_count < INPUT_DEVICES_MAX) members[member_count++] = (struct member){.device = device, .profile = profile};
    if (device == picker) picker = NO_DEVICE;
    seat_devices();
}

static void logout(int profile, int device) {
    int seat = seat_of(profile);
    if (seat < 0) return;
    for (int i = 0; i < game_count; i++)
        if (games[i].owner == profile && games[i].pid) close_game(i);
    for (int i = member_count - 1; i >= 0; i--) {
        if (members[i].profile != profile) continue;
        tell_padd("assign %d 0", members[i].device);
        tell_padd("off %d", members[i].device);
        drop_member(i);
    }
    player_count--;
    memmove(players + seat, players + seat + 1, (player_count - seat) * sizeof *players);
    guide_shown = 0;
    seat_devices();
    if (!player_count && device != NO_DEVICE) open_picker(device);
}

static void act(struct row *row, int device) {
    char id[TEXT_MAX], store[TEXT_MAX];
    struct view *view = top();
    struct game *game = view->game >= 0 ? &games[view->game] : NULL;
    switch (row->action) {
    case NOTHING: case VOLUME: case MICROPHONE_VOLUME: case SECOND_SCREEN: case CHOOSE_DISPLAY: case CHOOSE_DISPLAY_MODE: case CHOOSE_RESOLUTION: case CHOOSE_SLEEP: case EMULATE: break;
    case REWIND: load_rewind(); break;
    case LOGIN: login(row->arg, picker); break;
    case LOG_OUT_PLAYER:
        if (owned_games(profile_of(focus))) push(LOG_OUT, -1);
        else logout(profile_of(focus), focus);
        break;
    case CONFIRM_LOG_OUT: logout(profile_of(focus), focus); break;
    case OPEN_STORE: push(STORE, -1); refresh(); break;
    case OPEN_SETTINGS:
        mode_staged = 0;
        push(SETTINGS, -1);
        break;
    case SAVE_DISPLAY_MODE:
        start((char *[]){"settings", "mode", staged_mode, NULL});
        mode_staged = 0;
        view->action = CHOOSE_DISPLAY_MODE;
        break;
    case OPEN_GAME: push(GAME, row->arg); break;
    case PLAY:
        game->owner = profile_of(device);
        launch(view->game);
        break;
    case RESUME: resume(view->game); break;
    case CLOSE: close_game(view->game); break;
    case UNINSTALL: start_job(view->game, UNINSTALLING, (char *[]){"games", "uninstall", game->slug, game->platform, NULL}); break;
    case INSTALL:
        snprintf(store, sizeof store, "%d", row->arg + 1);
        start_job(view->game, INSTALLING, (char *[]){"games", "install", store, game->slug, game->platform, NULL});
        break;
    case SHOW_DASHBOARD: guide_shown = 0; dashboard_open = 1; dashboard_depth = 1; break;
    case OPEN_OUTPUTS: push(OUTPUTS, -1); break;
    case OPEN_MICROPHONES: push(MICROPHONES, -1); break;
    case SET_DEVICE:
        snprintf(id, sizeof id, "%d", row->arg);
        start((char *[]){"wpctl", "set-default", id, NULL});
        guide_depths[guide_profile]--;
        break;
    case OPEN_CONTROLLERS:
        push(CONTROLLERS, -1);
        tell_padd("batteries");
        break;
    case PAIR: tell_padd("pair"); break;
    case OPEN_CONTROLLER:
        snprintf(chosen_controller, sizeof chosen_controller, "%s", string_at(controller_at(row->arg), "address", NULL));
        push(CONTROLLER, -1);
        break;
    case FORGET:
        tell_padd("forget %s", chosen_controller);
        guide_depths[guide_profile]--;
        break;
    case RELOAD: exit(0);
    case RESTART: start((char *[]){"reboot", NULL}); break;
    case POWER_OFF: start((char *[]){"poweroff", NULL}); break;
    }
}

static void change_volume(char *node, const char *direction) {
    char step[TEXT_MAX];
    snprintf(step, sizeof step, "%s%s", VOLUME_STEP, direction);
    start((char *[]){"wpctl", "set-volume", "-l", VOLUME_LIMIT, node, step, NULL});
}

static void adjust(enum action action, int step) {
    if (action == VOLUME) change_volume("@DEFAULT_AUDIO_SINK@", step < 0 ? "-" : "+");
    if (action == MICROPHONE_VOLUME) change_volume("@DEFAULT_AUDIO_SOURCE@", step < 0 ? "-" : "+");
    if (action == CHOOSE_DISPLAY) cycle_display(step);
    if (action == CHOOSE_DISPLAY_MODE) cycle_display_mode(step);
    if (action == CHOOSE_RESOLUTION) cycle_resolution(step);
    if (action == CHOOSE_SLEEP) cycle_sleep(step);
    if (action == REWIND && rewind_back - step >= 0 && rewind_back - step <= rewind_count) rewind_back -= step;
    if (action == EMULATE) cycle_mode(step);
    if (action == SECOND_SCREEN) toggle_second_screen();
}

static void open_guide(void) {
    rewind_back = 0;
    if (!rewindable() || (!capture.session && !start_capture(active))) {
        guide_shown = 1;
        return;
    }
    capture.current = 1;
}

static void toggle_guide(int device) {
    if (guide_shown && focus == device) {
        guide_shown = 0;
        return;
    }
    guide_profile = profile_of(device);
    if (!guide_depths[guide_profile]) {
        guides[guide_profile][0] = (struct view){.screen = GUIDE, .game = -1, .action = REWIND};
        guide_depths[guide_profile] = 1;
    }
    focus = device;
    if (!guide_shown) open_guide();
}

static void navigate(int device, enum command command) {
    struct view *view = top();
    int count = build(view), selected = view->selected;
    struct row *row = count ? &rows[view->selected] : NULL;
    switch (command) {
    case UP: if (view->selected > 0) view->selected--; break;
    case DOWN: if (view->selected < count - 1) view->selected++; break;
    case LEFT: case RIGHT: if (row) adjust(row->action, command == LEFT ? -1 : 1); break;
    case CONFIRM: if (row) act(row, device); break;
    case BACK: case GUIDE_BUTTON:
        if (picker != NO_DEVICE) picker = NO_DEVICE;
        else if (guide_shown && guide_depths[guide_profile] > 1) guide_depths[guide_profile]--;
        else if (guide_shown) guide_shown = 0;
        else if (dashboard_depth > 1) dashboard_depth--;
        break;
    default: break;
    }
    if (count && (command == UP || command == DOWN)) {
        view->action = rows[view->selected].action;
        view->arg = rows[view->selected].arg;
    }
    if (command != UP && command != DOWN) changed = 1;
    else changed |= view->selected != selected;
}

static void press(int device, enum command command) {
    if (attract()) {
        open_picker(device);
    } else if (picker != NO_DEVICE) {
        if (device == picker) navigate(device, command);
    } else if (!profile_of(device)) {
        if (command == GUIDE_BUTTON || !int_at(device_of(device), "guide")) open_picker(device);
    } else if (command == GUIDE_BUTTON) {
        toggle_guide(device);
        changed = 1;
    } else if (interactive() && (!guide_shown || device == focus)) {
        navigate(device, command);
    }
}

static void hold(int device, enum command command, int pressed) {
    struct itimerspec timer = {0};
    if (pressed) press(device, command);
    if (command < UP || command > RIGHT || (!pressed && (command != repeating || device != repeating_device)) || (pressed && !interactive())) return;
    repeating = pressed ? command : NONE;
    repeating_device = device;
    if (pressed) timer = (struct itimerspec){.it_value = {.tv_nsec = REPEAT_DELAY_MS * NS_PER_MS}, .it_interval = {.tv_nsec = REPEAT_INTERVAL_MS * NS_PER_MS}};
    timerfd_settime(repeat_timer, 0, &timer, NULL);
}

static enum command command_named(const char *name) {
    for (unsigned i = 0; i < sizeof commands / sizeof *commands; i++)
        if (!strcmp(commands[i], name)) return i;
    return NONE;
}

static void hide(void) {
    if (!layer) return;
    zwlr_layer_surface_v1_destroy(layer);
    wl_surface_destroy(surface);
    layer = NULL;
    width = 0;
    mode = HIDDEN;
}

static void on_buffer_release(void *data, struct wl_buffer *wl_buffer) {
    (void)wl_buffer;
    ((struct buffer *)data)->busy = 0;
    changed |= draw_pending;
}

static const struct wl_buffer_listener buffer_listener = {.release = on_buffer_release};

static void free_buffers(void) {
    if (!buffers[0].buffer) return;
    munmap(buffers[0].pixels, (size_t)width * height * BYTES_PER_PIXEL * BUFFERS);
    for (int i = 0; i < BUFFERS; i++) {
        wl_buffer_destroy(buffers[i].buffer);
        buffers[i] = (struct buffer){0};
    }
}

static void allocate_buffers(void) {
    int stride = width * BYTES_PER_PIXEL;
    size_t size = (size_t)stride * height;
    char *pixels;
    struct wl_shm_pool *pool = create_pool(size * BUFFERS, (void **)&pixels);
    for (int i = 0; i < BUFFERS; i++) {
        buffers[i].pixels = pixels + size * i;
        buffers[i].buffer = wl_shm_pool_create_buffer(pool, size * i, width, height, stride, WL_SHM_FORMAT_ARGB8888);
        wl_buffer_add_listener(buffers[i].buffer, &buffer_listener, &buffers[i]);
    }
    wl_shm_pool_destroy(pool);
}

static void on_layer_configure(void *data, struct zwlr_layer_surface_v1 *configured, uint32_t serial, uint32_t configured_width, uint32_t configured_height) {
    (void)data;
    zwlr_layer_surface_v1_ack_configure(configured, serial);
    if ((int)configured_width == width && (int)configured_height == height) return;
    free_buffers();
    width = configured_width;
    height = configured_height;
    if (mode == FULL) {
        output_width = width;
        output_height = height;
    }
    allocate_buffers();
    changed = draw_pending = 1;
}

static void on_layer_closed(void *data, struct zwlr_layer_surface_v1 *closed) {
    (void)data, (void)closed;
    free_buffers();
    hide();
}

static const struct zwlr_layer_surface_v1_listener layer_listener = {.configure = on_layer_configure, .closed = on_layer_closed};

static double row_height(void) { return (double)output_height / ROWS_PER_SCREEN; }

static void show(enum surface_mode wanted) {
    free_buffers();
    hide();
    mode = wanted;
    surface = wl_compositor_create_surface(compositor);
    struct wl_region *nowhere = wl_compositor_create_region(compositor);
    wl_surface_set_input_region(surface, nowhere);
    wl_region_destroy(nowhere);
    layer = zwlr_layer_shell_v1_get_layer_surface(layer_shell, surface, NULL, ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY, "anyconsole");
    if (wanted == FULL) {
        zwlr_layer_surface_v1_set_anchor(layer, ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP | ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM |
                                                    ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT | ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT);
        zwlr_layer_surface_v1_set_exclusive_zone(layer, -1);
    } else {
        zwlr_layer_surface_v1_set_anchor(layer, ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP | ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT);
        zwlr_layer_surface_v1_set_size(layer, output_width / NOTIFICATION_WIDTH_DIVISOR, row_height() * NOTIFICATIONS_MAX);
    }
    zwlr_layer_surface_v1_add_listener(layer, &layer_listener, NULL);
    wl_surface_commit(surface);
}

static double text(cairo_t *cr, double x, double y, const char *string) {
    cairo_text_extents_t extents;
    cairo_font_extents_t font;
    cairo_font_extents(cr, &font);
    cairo_text_extents(cr, string, &extents);
    cairo_move_to(cr, x, y + (row_height() + font.ascent - font.descent) / 2);
    cairo_show_text(cr, string);
    return extents.x_advance;
}

static void value(cairo_t *cr, double from, double to, double y, const char *string) {
    cairo_text_extents_t extents;
    cairo_text_extents(cr, string, &extents);
    cairo_save(cr);
    cairo_rectangle(cr, from, y, to - from, row_height());
    cairo_clip(cr);
    text(cr, extents.x_advance < to - from ? to - extents.x_advance : from, y, string);
    cairo_restore(cr);
}

static void draw_view(cairo_t *cr, struct view *view, int count, double panel_width) {
    double margin = row_height() * MARGIN_PER_ROW;
    int visible = ROWS_PER_SCREEN - 1, first = view->selected >= visible ? view->selected - visible + 1 : 0;
    cairo_set_source_rgb(cr, 0, 0, 0);
    cairo_rectangle(cr, 0, 0, panel_width, height);
    cairo_fill(cr);
    cairo_set_source_rgb(cr, 1, 1, 1);
    text(cr, margin, 0, title(view));
    for (int i = first; i < count && i < first + visible; i++) {
        double y = (i - first + 1) * row_height();
        if (i == view->selected) {
            cairo_rectangle(cr, 0, y, panel_width, row_height());
            cairo_fill(cr);
            cairo_set_source_rgb(cr, 0, 0, 0);
        }
        double label_end = margin + text(cr, margin, y, rows[i].label);
        value(cr, label_end + margin, panel_width - margin, y, rows[i].value);
        cairo_set_source_rgb(cr, 1, 1, 1);
    }
}

static void draw_image(cairo_t *cr, cairo_surface_t *image, double box_width, double box_height) {
    double image_width = cairo_image_surface_get_width(image), image_height = cairo_image_surface_get_height(image);
    if (cairo_surface_status(image)) return;
    double scale = box_width / image_width < box_height / image_height ? box_width / image_width : box_height / image_height;
    cairo_save(cr);
    cairo_translate(cr, floor((box_width - image_width * scale) / 2), floor((box_height - image_height * scale) / 2));
    cairo_scale(cr, scale, scale);
    cairo_set_source_surface(cr, image, 0, 0);
    cairo_paint(cr);
    cairo_restore(cr);
}

static cairo_surface_t *thumbnail(int index, double cell_width, double cell_height) {
    if (thumbnails[index]) return thumbnails[index];
    cairo_surface_t *image = state_image(index, SCREEN_EXTENSION);
    if (cairo_surface_status(image)) {
        cairo_surface_destroy(image);
        return NULL;
    }
    thumbnails[index] = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, cell_width, cell_height);
    cairo_t *cr = cairo_create(thumbnails[index]);
    draw_image(cr, image, cell_width, cell_height);
    cairo_destroy(cr);
    cairo_surface_destroy(image);
    return thumbnails[index];
}

static void draw_preview(cairo_t *cr) {
    cairo_surface_t *image = state_image(rewind_count - rewind_back, SCREEN_EXTENSION);
    cairo_set_source_rgb(cr, 0, 0, 0);
    cairo_paint(cr);
    draw_image(cr, image, width, height);
    cairo_surface_destroy(image);
}

static void draw_carousel(cairo_t *cr) {
    double margin = row_height() * MARGIN_PER_ROW, strip = row_height() * CAROUSEL_ROWS, cell_height = strip - 2 * margin;
    double cell_width = cell_height * width / height, top_edge = height - strip + margin;
    int visible = (width - margin) / (cell_width + margin), shift = rewind_back < visible ? 0 : rewind_back - visible + 1;
    cairo_set_source_rgb(cr, 0, 0, 0);
    cairo_rectangle(cr, 0, height - strip, width, strip);
    cairo_fill(cr);
    for (int back = shift; back <= rewind_count && back < shift + visible; back++) {
        double left_edge = width - (back - shift + 1) * (cell_width + margin);
        cairo_surface_t *cell = back ? thumbnail(rewind_count - back, cell_width, cell_height) : NULL;
        if (cell) {
            cairo_set_source_surface(cr, cell, left_edge, top_edge);
            cairo_paint(cr);
        } else if (!back && current_screen) {
            cairo_save(cr);
            cairo_translate(cr, left_edge, top_edge);
            draw_image(cr, current_screen, cell_width, cell_height);
            cairo_restore(cr);
            cairo_set_source_rgb(cr, 0, 0, 0);
            cairo_rectangle(cr, left_edge, top_edge + cell_height - row_height(), cell_width, row_height());
            cairo_fill(cr);
            cairo_set_source_rgb(cr, 1, 1, 1);
            text(cr, left_edge + margin, top_edge + cell_height - row_height(), "Save");
        }
        if (back != rewind_back) continue;
        cairo_set_source_rgb(cr, 1, 1, 1);
        cairo_set_line_width(cr, row_height() * OUTLINE_PER_ROW);
        cairo_rectangle(cr, left_edge, top_edge, cell_width, cell_height);
        cairo_stroke(cr);
    }
}

static void centered(cairo_t *cr, double y, const char *string) {
    cairo_text_extents_t extents;
    cairo_text_extents(cr, string, &extents);
    text(cr, (width - extents.x_advance) / 2, y, string);
}

static void draw_launch(cairo_t *cr) {
    struct game *game = &games[active];
    double line = row_height() * OUTLINE_PER_ROW, margin = row_height() * MARGIN_PER_ROW;
    double bar_width = (double)width / BAR_WIDTH_DIVISOR, bar_height = row_height() * BAR_PER_ROW;
    double left = (width - bar_width) / 2, top_edge = (height - bar_height) / 2;
    double done = game->job ? fmax(game->percent, 0) / PERCENT : LAUNCH_STEPS - 1;
    cairo_set_source_rgb(cr, 0, 0, 0);
    cairo_paint(cr);
    cairo_set_source_rgb(cr, 1, 1, 1);
    cairo_set_line_width(cr, line);
    cairo_rectangle(cr, left - line, top_edge - line, bar_width + 2 * line, bar_height + 2 * line);
    cairo_stroke(cr);
    cairo_rectangle(cr, left, top_edge, bar_width * done / LAUNCH_STEPS, bar_height);
    cairo_fill(cr);
    centered(cr, top_edge - 2 * line - margin - row_height(), game->title);
    centered(cr, top_edge + bar_height + 2 * line + margin, status(active));
}

static void on_frame(void *data, struct wl_callback *callback, uint32_t time) {
    (void)data, (void)time;
    wl_callback_destroy(callback);
    draw_pending = changed = 1;
}

static const struct wl_callback_listener frame_listener = {.done = on_frame};

static int draw_attract(cairo_t *cr) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    double shown = fmin(1, (now.tv_sec - attract_since.tv_sec + (now.tv_nsec - attract_since.tv_nsec) / NS_PER_SECOND) / ATTRACT_FADE_SECONDS);
    cairo_set_source_rgb(cr, 0, 0, 0);
    cairo_paint(cr);
    draw_image(cr, splash, width, height);
    cairo_set_source_rgba(cr, 1, 1, 1, shown);
    centered(cr, ATTRACT_PROMPT_ROW * row_height(), ATTRACT_PROMPT);
    return shown < 1;
}

static int layer_rows(struct view *view, int top_count, int *intact) {
    if (view == top() && *intact) return top_count;
    *intact = 0;
    return build(view);
}

static void draw(int top_count) {
    struct buffer *buffer = !buffers[0].busy ? &buffers[0] : !buffers[1].busy ? &buffers[1] : NULL;
    if (!buffer) { draw_pending = 1; return; }
    draw_pending = 0;
    cairo_surface_t *canvas = cairo_image_surface_create_for_data(buffer->pixels, CAIRO_FORMAT_ARGB32, width, height, width * BYTES_PER_PIXEL);
    cairo_t *cr = cairo_create(canvas);
    double notification_width = (double)output_width / NOTIFICATION_WIDTH_DIVISOR, margin = row_height() * MARGIN_PER_ROW;
    int animating = 0, intact = 1;
    cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
    cairo_paint(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
    cairo_select_font_face(cr, FONT, CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, row_height() * FONT_PER_ROW);
    if (attract()) {
        animating = draw_attract(cr);
    } else {
        if (launching()) draw_launch(cr);
        if (dashboard_open) draw_view(cr, &dashboard[dashboard_depth - 1], layer_rows(&dashboard[dashboard_depth - 1], top_count, &intact), width);
    }
    if (guide_shown) {
        int guide_count = layer_rows(guide_view(), top_count, &intact);
        int rewinding = guide_count && rows[guide_view()->selected].action == REWIND;
        if (rewinding && rewind_back) draw_preview(cr);
        draw_view(cr, guide_view(), guide_count, (double)width / GUIDE_WIDTH_DIVISOR);
        if (rewinding) draw_carousel(cr);
    }
    if (picker != NO_DEVICE) draw_view(cr, &whois, layer_rows(&whois, top_count, &intact), width);
    for (int i = 0; i < notification_count; i++) {
        cairo_set_source_rgb(cr, 1, 1, 1);
        cairo_rectangle(cr, width - notification_width, i * row_height(), notification_width, row_height());
        cairo_fill(cr);
        cairo_set_source_rgb(cr, 0, 0, 0);
        value(cr, width - notification_width + margin, width - margin, i * row_height(), notifications[i].text);
    }
    cairo_destroy(cr);
    cairo_surface_destroy(canvas);
    buffer->busy = 1;
    if (animating) wl_callback_add_listener(wl_surface_frame(surface), &frame_listener, NULL);
    wl_surface_attach(surface, buffer->buffer, 0, 0);
    wl_surface_damage_buffer(surface, 0, 0, width, height);
    wl_surface_commit(surface);
}

static char *serialize_state(int count) {
    json_object *state = json_object_new_object(), *list = json_object_new_array(), *audio = json_object_new_object();
    json_object *history = json_object_new_object();
    struct view *view = top();
    for (int i = 0; i < count; i++) {
        json_object *row = json_object_new_object();
        json_object_object_add(row, "label", json_object_new_string(rows[i].label));
        json_object_object_add(row, "value", json_object_new_string(rows[i].value));
        json_object_array_add(list, row);
    }
    json_object *session = json_object_new_object(), *entries = json_object_new_array();
    json_object_object_add(state, "screen", json_object_new_string(screens[attract() ? ATTRACT : view->screen].name));
    for (int i = 0; i < player_count; i++) {
        json_object *player = json_object_new_object(), *devices = json_object_new_array();
        for (int j = 0; j < member_count; j++)
            if (members[j].profile == players[i]) json_object_array_add(devices, json_object_new_int(members[j].device));
        json_object_object_add(player, "profile", json_object_new_int(players[i]));
        json_object_object_add(player, "name", json_object_new_string(profile_name(players[i])));
        json_object_object_add(player, "pad", json_object_new_int(i + 1));
        json_object_object_add(player, "devices", devices);
        json_object_array_add(entries, player);
    }
    json_object_object_add(session, "players", entries);
    json_object_object_add(session, "focus", focus == NO_DEVICE ? NULL : json_object_new_int(focus));
    json_object_object_add(session, "picker", picker == NO_DEVICE ? NULL : json_object_new_int(picker));
    json_object_object_add(state, "session", session);
    json_object_object_add(state, "rows", list);
    json_object_object_add(state, "selected", json_object_new_int(view->selected));
    json_object_object_add(state, "dashboard", json_object_new_boolean(dashboard_open));
    json_object_object_add(state, "guide", json_object_new_boolean(guide_shown));
    json_object_object_add(history, "states", json_object_new_int(rewind_count));
    json_object_object_add(history, "back", json_object_new_int(rewind_back));
    json_object_object_add(state, "rewind", history);
    json_object_object_add(state, "active", active < 0 ? NULL : json_object_new_string(key(active)));
    list = json_object_new_array();
    for (int i = 0; i < notification_count; i++) json_object_array_add(list, json_object_new_string(notifications[i].text));
    json_object_object_add(state, "notifications", list);
    list = json_object_new_array();
    for (int i = 0; i < game_count; i++) {
        json_object *game = json_object_new_object(), *offers = json_object_new_array();
        for (int store = 0; store < store_count; store++)
            if (games[i].stores & (1u << store)) json_object_array_add(offers, json_object_new_string(store_label(store)));
        json_object_object_add(game, "key", json_object_new_string(key(i)));
        json_object_object_add(game, "title", json_object_new_string(games[i].title));
        json_object_object_add(game, "status", json_object_new_string(status(i)));
        json_object_object_add(game, "owner", games[i].owner ? json_object_new_int(games[i].owner) : NULL);
        json_object_object_add(game, "stores", offers);
        json_object_array_add(list, game);
    }
    json_object_object_add(state, "games", list);
    list = json_object_new_array();
    for (int store = 0; store < store_count; store++) {
        json_object *entry = json_object_new_object();
        json_object_object_add(entry, "label", json_object_new_string(store_label(store)));
        json_object_object_add(entry, "online", json_object_new_boolean(store_online(store)));
        json_object_array_add(list, entry);
    }
    json_object_object_add(state, "stores", list);
    json_object_object_add(audio, "volume", volume < 0 ? NULL : json_object_new_int(volume));
    json_object_object_add(audio, "output", json_object_new_string(chosen(outputs, output_count, default_output)));
    json_object_object_add(audio, "microphone", json_object_new_string(chosen(microphones, microphone_count, default_microphone)));
    json_object_object_add(audio, "microphone_volume", microphone_volume < 0 ? NULL : json_object_new_int(microphone_volume));
    json_object_object_add(audio, "microphone_muted", json_object_new_boolean(microphone_is_muted()));
    json_object_object_add(state, "audio", audio);
    json_object_object_add(state, "settings", json_object_get(settings));
    json_object_object_add(state, "display", json_object_get(display_state));
    json_object_object_add(state, "controllers", json_object_get(controllers_state));
    char *serialized = strdup(json_object_to_json_string_ext(state, JSON_C_TO_STRING_PLAIN));
    json_object_put(state);
    return serialized;
}

static void write_state(char *serialized) {
    FILE *file = fopen(STATE_PARTIAL, "w");
    if (!file || fputs(serialized, file) < 0 || fclose(file) < 0 || rename(STATE_PARTIAL, STATE_FILE) < 0) perror(STATE_FILE);
    free(written_state);
    written_state = serialized;
}

static void settle(void) {
    if (!changed) return;
    changed = 0;
    if (active >= 0 && !games[active].pid && !games[active].job) active = -1;
    if (active < 0) dashboard_open = 1;
    for (int i = 0; i < game_count; i++) {
        int pause = i != active || interactive();
        if (!games[i].pid || !game_window(i) || games[i].closing || games[i].paused == pause || games[i].freezer) continue;
        freeze(i, pause);
    }
    if (menu_told != (attract() || interactive())) {
        menu_told = attract() || interactive();
        tell_padd("menu %d", menu_told);
        if (!menu_told) hold(repeating_device, repeating, 0);
    }
    if (!attract()) attract_since = (struct timespec){0};
    else if (!attract_since.tv_sec && !attract_since.tv_nsec) clock_gettime(CLOCK_MONOTONIC, &attract_since);
    enum surface_mode wanted = HIDDEN;
    if (attract() || interactive() || launching() || (notification_count && !output_width)) wanted = FULL;
    else if (notification_count) wanted = NOTIFICATIONS_ONLY;
    if (wanted != mode && wanted == HIDDEN) {
        free_buffers();
        hide();
    } else if (wanted != mode) show(wanted);
    int count = build(top());
    int profile = picker != NO_DEVICE && count ? rows[whois.selected].arg : 0;
    int seat = profile ? seat_for(profile) + 1 : 0;
    if (picker != previewed || seat != previewed_seat) {
        if (previewed != NO_DEVICE && previewed != picker) tell_padd("preview %d 0", previewed);
        if (picker != NO_DEVICE) tell_padd("preview %d %d", picker, seat);
        previewed = picker;
        previewed_seat = seat;
    }
    char *serialized = serialize_state(count);
    if (width && (draw_pending || !written_state || strcmp(serialized, written_state))) draw(count);
    if (written_state && !strcmp(serialized, written_state)) free(serialized);
    else write_state(serialized);
}

static void reap(void) {
    int status;
    for (pid_t pid; (pid = waitpid(-1, &status, WNOHANG)) > 0;) {
        int failed = !WIFEXITED(status) || WEXITSTATUS(status);
        if (pid == refresher) {
            refresher = 0;
            rescan();
            changed = 1;
            if (refresh_pending) refresh();
        }
        if (pid == monitor) {
            monitor = 0;
            close(monitor_output);
        }
        for (int i = 0; i < game_count; i++) {
            if (games[i].pid == pid) ended(i, failed);
            if (games[i].job == pid) job_done(i, failed);
            if (games[i].probe == pid) read_probe(i);
            if (games[i].freezer == pid) {
                games[i].freezer = 0;
                if (failed) games[i].paused = !games[i].paused;
                if (games[i].closing && games[i].pid) finish_close(i);
            }
            changed = 1;
        }
    }
}

static int read_game_change(struct inotify_event *event) {
    for (int i = 0; i < game_count; i++) {
        if (event->wd == games[i].states_watch) {
            if (i == active) refresh_rewinds();
            return 1;
        }
    }
    return 0;
}

static void read_changes(int runtime_watch, int display_watch, int controllers_watch) {
    char buffer[BUFSIZ] __attribute__((aligned(__alignof__(struct inotify_event))));
    ssize_t count = read(watcher, buffer, sizeof buffer);
    for (char *at = buffer; count > 0 && at < buffer + count;) {
        struct inotify_event *event = (struct inotify_event *)at;
        at += sizeof *event + event->len;
        if (!event->len || read_game_change(event)) continue;
        if (event->wd == runtime_watch && !strcmp(event->name, PIPEWIRE_SOCKET)) start_monitor();
        else if (event->wd == display_watch && !strcmp(event->name, basename(DISPLAY_FILE))) {
            read_display();
        } else if (event->wd == controllers_watch && !strcmp(event->name, basename(CONTROLLERS_FILE))) {
            read_controllers();
        } else if (event->wd != runtime_watch && !strcmp(event->name, basename(SETTINGS_FILE))) {
            read_settings();
        } else if (event->wd != runtime_watch && !strcmp(event->name, basename(STORES_FILE))) {
            read_stores();
            refresh();
            changed = 1;
        }
    }
}

static void on_toplevel_title(void *data, struct zwlr_foreign_toplevel_handle_v1 *handle, const char *title) { (void)data, (void)handle, (void)title; }

static void on_toplevel_app_id(void *data, struct zwlr_foreign_toplevel_handle_v1 *handle, const char *app_id) {
    (void)data;
    for (int i = 0; i < window_count; i++)
        if (windows[i].handle == handle) snprintf(windows[i].app_id, sizeof windows[i].app_id, "%s", app_id);
    for (int i = 0; i < game_count; i++)
        if (games[i].pid && game_window(i) && game_window(i)->handle == handle) probe_labels(i);
    changed = 1;
}

static void on_toplevel_output(void *data, struct zwlr_foreign_toplevel_handle_v1 *handle, struct wl_output *output) { (void)data, (void)handle, (void)output; }

static void on_toplevel_state(void *data, struct zwlr_foreign_toplevel_handle_v1 *handle, struct wl_array *state) { (void)data, (void)handle, (void)state; }

static void on_toplevel_done(void *data, struct zwlr_foreign_toplevel_handle_v1 *handle) { (void)data, (void)handle; }

static void on_toplevel_closed(void *data, struct zwlr_foreign_toplevel_handle_v1 *handle) {
    (void)data;
    zwlr_foreign_toplevel_handle_v1_destroy(handle);
    for (int i = 0; i < window_count; i++)
        if (windows[i].handle == handle) windows[i--] = windows[--window_count];
}

static const struct zwlr_foreign_toplevel_handle_v1_listener toplevel_listener = {
    .title = on_toplevel_title, .app_id = on_toplevel_app_id, .output_enter = on_toplevel_output, .output_leave = on_toplevel_output,
    .state = on_toplevel_state, .done = on_toplevel_done, .closed = on_toplevel_closed};

static void on_toplevel(void *data, struct zwlr_foreign_toplevel_manager_v1 *manager, struct zwlr_foreign_toplevel_handle_v1 *handle) {
    (void)data, (void)manager;
    if (window_count == WINDOWS_MAX) { zwlr_foreign_toplevel_handle_v1_destroy(handle); return; }
    windows[window_count++] = (struct window){.handle = handle};
    zwlr_foreign_toplevel_handle_v1_add_listener(handle, &toplevel_listener, NULL);
}

static void on_toplevels_finished(void *data, struct zwlr_foreign_toplevel_manager_v1 *manager) { (void)data, (void)manager; }

static const struct zwlr_foreign_toplevel_manager_v1_listener toplevels_listener = {.toplevel = on_toplevel, .finished = on_toplevels_finished};

static void on_listed_closed(void *data, struct ext_foreign_toplevel_handle_v1 *handle) {
    (void)data;
    ext_foreign_toplevel_handle_v1_destroy(handle);
    for (int i = 0; i < window_count; i++)
        if (windows[i].listed == handle) windows[i].listed = NULL;
}

static void on_listed_done(void *data, struct ext_foreign_toplevel_handle_v1 *handle) { (void)data, (void)handle; }

static void on_listed_text(void *data, struct ext_foreign_toplevel_handle_v1 *handle, const char *text) { (void)data, (void)handle, (void)text; }

static void on_listed_app_id(void *data, struct ext_foreign_toplevel_handle_v1 *handle, const char *app_id) {
    (void)data;
    for (int i = 0; i < window_count; i++)
        if (!strcmp(windows[i].app_id, app_id)) windows[i].listed = handle;
}

static const struct ext_foreign_toplevel_handle_v1_listener listed_listener = {
    .closed = on_listed_closed, .done = on_listed_done, .title = on_listed_text, .app_id = on_listed_app_id, .identifier = on_listed_text};

static void on_listed(void *data, struct ext_foreign_toplevel_list_v1 *list, struct ext_foreign_toplevel_handle_v1 *handle) {
    (void)data, (void)list;
    ext_foreign_toplevel_handle_v1_add_listener(handle, &listed_listener, NULL);
}

static void on_listing_finished(void *data, struct ext_foreign_toplevel_list_v1 *list) { (void)data, (void)list; }

static const struct ext_foreign_toplevel_list_v1_listener listing_listener = {.toplevel = on_listed, .finished = on_listing_finished};

static void on_global(void *data, struct wl_registry *registry, uint32_t id, const char *interface, uint32_t version) {
    (void)data, (void)version;
    if (!strcmp(interface, wl_compositor_interface.name))
        compositor = wl_registry_bind(registry, id, &wl_compositor_interface, WL_SURFACE_DAMAGE_BUFFER_SINCE_VERSION);
    else if (!strcmp(interface, wl_shm_interface.name))
        shm = wl_registry_bind(registry, id, &wl_shm_interface, 1);
    else if (!strcmp(interface, zwlr_layer_shell_v1_interface.name))
        layer_shell = wl_registry_bind(registry, id, &zwlr_layer_shell_v1_interface, 1);
    else if (!strcmp(interface, wp_security_context_manager_v1_interface.name))
        security = wl_registry_bind(registry, id, &wp_security_context_manager_v1_interface, 1);
    else if (!strcmp(interface, wl_seat_interface.name))
        seat = wl_registry_bind(registry, id, &wl_seat_interface, 1);
    else if (!strcmp(interface, zwlr_foreign_toplevel_manager_v1_interface.name)) {
        toplevels = wl_registry_bind(registry, id, &zwlr_foreign_toplevel_manager_v1_interface, 1);
        zwlr_foreign_toplevel_manager_v1_add_listener(toplevels, &toplevels_listener, NULL);
    } else if (!strcmp(interface, ext_foreign_toplevel_list_v1_interface.name)) {
        listing = wl_registry_bind(registry, id, &ext_foreign_toplevel_list_v1_interface, 1);
        ext_foreign_toplevel_list_v1_add_listener(listing, &listing_listener, NULL);
    } else if (!strcmp(interface, ext_foreign_toplevel_image_capture_source_manager_v1_interface.name))
        toplevel_capture = wl_registry_bind(registry, id, &ext_foreign_toplevel_image_capture_source_manager_v1_interface, 1);
    else if (!strcmp(interface, ext_image_copy_capture_manager_v1_interface.name))
        image_copy = wl_registry_bind(registry, id, &ext_image_copy_capture_manager_v1_interface, 1);
    else if (!strcmp(interface, zwp_linux_dmabuf_v1_interface.name))
        dmabuf = wl_registry_bind(registry, id, &zwp_linux_dmabuf_v1_interface, LINUX_DMABUF_VERSION);
}

static void on_global_remove(void *data, struct wl_registry *registry, uint32_t id) { (void)data, (void)registry, (void)id; }

static const struct wl_registry_listener registry_listener = {.global = on_global, .global_remove = on_global_remove};

static void directory_of(const char *path, char *directory) { snprintf(directory, PATH_MAX, "%.*s", (int)(strrchr(path, '/') - path), path); }

static void reset_devices(void) {
    read_controllers();
    adopt_members();
    picker = focus = NO_DEVICE;
    guide_shown = 0;
    applied_game = UNAPPLIED;
    menu_told = -1;
    seat_devices();
}

static void know(int device) {
    if (!device_of(device)) read_controllers();
}

static void connected(int device) {
    know(device);
    json_object *entry = device_of(device);
    if (picker == NO_DEVICE && (int_at(entry, "bluetooth") || (player_count && !int_at(entry, "guide")))) open_picker(device);
}

static void handle_message(char *line, void *context) {
    char name[WORD_MAX + 1];
    int first, second = NO_DEVICE, value;
    (void)context;
    if (!strncmp(line, NOTIFY_PREFIX, strlen(NOTIFY_PREFIX))) notify("%s", line + strlen(NOTIFY_PREFIX));
    else if (!strcmp(line, "reset")) reset_devices();
    else if (sscanf(line, "logout %d", &first) == 1) logout(first, NO_DEVICE);
    else if (sscanf(line, "login %d %d", &first, &second) >= 1) login(first, second);
    else if (sscanf(line, "connect %d", &first) == 1) connected(first);
    else if (sscanf(line, "press %d %" STRINGIFY(WORD_MAX) "s %d", &first, name, &value) == 3) {
        know(first);
        hold(first, command_named(name), value);
    }
}

static void read_messages(int fd) {
    static char pending[PIPE_BUF];
    static size_t length;
    read_lines(fd, pending, sizeof pending, &length, handle_message, NULL);
}

static int numbers(const char *list, int *parsed, int max) {
    int count = 0;
    char *end;
    for (const char *at = list; count < max; at = end) {
        parsed[count] = strtol(at, &end, 10);
        if (end == at) break;
        count++;
    }
    return count;
}

int main(void) {
    enum { WAYLAND, CHANGES, SIGNALS, REPEAT, NOTIFICATIONS, MESSAGES, SAVE, MONITOR, FIXED };
    struct pollfd watched[FIXED + GAMES_MAX];
    int jobs[GAMES_MAX];
    char stores_directory[PATH_MAX], display_directory[PATH_MAX], settings_directory[PATH_MAX], controllers_directory[PATH_MAX];
    sigset_t signals;
    signal(SIGPIPE, SIG_IGN);
    sigemptyset(&signals);
    sigaddset(&signals, SIGCHLD);
    sigaddset(&signals, SIGUSR1);
    sigprocmask(SIG_BLOCK, &signals, NULL);
    log_file = open(LOG_DIR "/games.log", O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, S_IRUSR | S_IWUSR);
    display = wl_display_connect(NULL);
    monitor_parser = json_tokener_new();
    if (!display || log_file < 0) { fprintf(stderr, "overlay: no wayland display or log\n"); return 1; }
    wl_registry_add_listener(wl_display_get_registry(display), &registry_listener, NULL);
    wl_display_roundtrip(display);
    if (!compositor || !shm || !layer_shell || !security || !seat || !toplevels || !listing || !toplevel_capture || !image_copy || !dmabuf) { fprintf(stderr, "overlay: compositor lacks a required protocol\n"); return 1; }
    directory_of(STORES_FILE, stores_directory);
    directory_of(DISPLAY_FILE, display_directory);
    directory_of(SETTINGS_FILE, settings_directory);
    directory_of(CONTROLLERS_FILE, controllers_directory);
    resolution_count = numbers(RESOLUTIONS, resolutions, RESOLUTIONS_MAX);
    sleep_choice_count = numbers(SLEEP_MINUTES, sleep_choices, SLEEP_CHOICES_MAX);
    mkfifo(OVERLAY_FIFO, S_IRUSR | S_IWUSR);
    watcher = inotify_init1(IN_CLOEXEC);
    int runtime_watch = inotify_add_watch(watcher, getenv("XDG_RUNTIME_DIR"), IN_CREATE);
    inotify_add_watch(watcher, stores_directory, IN_CLOSE_WRITE | IN_MOVED_TO);
    inotify_add_watch(watcher, settings_directory, IN_MOVED_TO | IN_MASK_ADD);
    int display_watch = inotify_add_watch(watcher, display_directory, IN_MOVED_TO);
    int controllers_watch = inotify_add_watch(watcher, controllers_directory, IN_MOVED_TO);
    repeat_timer = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC);
    notification_timer = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC);
    save_timer = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC);
    timerfd_settime(save_timer, 0, &(struct itimerspec){.it_value = {.tv_sec = SAVE_SECONDS}, .it_interval = {.tv_sec = SAVE_SECONDS}}, NULL);
    watched[WAYLAND] = (struct pollfd){.fd = wl_display_get_fd(display), .events = POLLIN};
    watched[CHANGES] = (struct pollfd){.fd = watcher, .events = POLLIN};
    watched[SIGNALS] = (struct pollfd){.fd = signalfd(-1, &signals, SFD_CLOEXEC), .events = POLLIN};
    watched[REPEAT] = (struct pollfd){.fd = repeat_timer, .events = POLLIN};
    watched[NOTIFICATIONS] = (struct pollfd){.fd = notification_timer, .events = POLLIN};
    watched[MESSAGES] = (struct pollfd){.fd = open(OVERLAY_FIFO, O_RDWR | O_NONBLOCK | O_CLOEXEC), .events = POLLIN};
    watched[SAVE] = (struct pollfd){.fd = save_timer, .events = POLLIN};
    splash = cairo_image_surface_create_from_png(SPLASH_IMAGE);
    read_stores();
    read_settings();
    read_display();
    load_session();
    reset_devices();
    rescan();
    refresh();
    start_monitor();
    settle();
    for (;;) {
        int count = FIXED, job_count = 0;
        watched[MONITOR] = (struct pollfd){.fd = monitor > 0 ? monitor_output : -1, .events = POLLIN};
        for (int i = 0; i < game_count; i++) {
            if (!games[i].job) continue;
            jobs[job_count++] = i;
            watched[count++] = (struct pollfd){.fd = games[i].job_output, .events = POLLIN};
        }
        while (wl_display_prepare_read(display)) wl_display_dispatch_pending(display);
        wl_display_flush(display);
        if (poll(watched, count, -1) < 0) { wl_display_cancel_read(display); continue; }
        if (watched[WAYLAND].revents & POLLIN) wl_display_read_events(display);
        else wl_display_cancel_read(display);
        if (wl_display_dispatch_pending(display) < 0) { fprintf(stderr, "overlay: lost the compositor\n"); return 1; }
        uint64_t expirations;
        struct signalfd_siginfo signal_info;
        if (watched[CHANGES].revents) read_changes(runtime_watch, display_watch, controllers_watch);
        if (watched[SIGNALS].revents && read(watched[SIGNALS].fd, &signal_info, sizeof signal_info) > 0) {
            if (signal_info.ssi_signo == SIGUSR1) refresh();
            else reap();
        }
        if (watched[REPEAT].revents && read(repeat_timer, &expirations, sizeof expirations) > 0) press(repeating_device, repeating);
        if (watched[NOTIFICATIONS].revents && read(notification_timer, &expirations, sizeof expirations) > 0) expire_notifications();
        if (watched[MESSAGES].revents) read_messages(watched[MESSAGES].fd);
        if (watched[SAVE].revents && read(save_timer, &expirations, sizeof expirations) > 0) sample_screen();
        if (watched[MONITOR].revents) read_monitor();
        for (int i = 0; i < job_count; i++)
            if (watched[FIXED + i].revents) read_job(jobs[i]);
        settle();
    }
}
