#define _GNU_SOURCE
#include <cairo.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <ftw.h>
#include <json-c/json.h>
#include <libevdev/libevdev.h>
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
#include "anyconsole.h"
#include "security-context-v1.h"
#include "wlr-foreign-toplevel-management-unstable-v1.h"
#include "wlr-layer-shell-unstable-v1.h"
#include "wlr-screencopy-unstable-v1.h"

#define INPUT_DIR "/dev/input"
#define STATE_PARTIAL STATE_FILE ".partial"
#define MANIFEST "manifest.json"
#define WAYLAND_SOCKET "wayland-0"
#define PIPEWIRE_SOCKET "pipewire-0"
#define SANDBOX_ENGINE "docker"
#define FONT "Inter"
#define ROWS_PER_SCREEN 16
#define FONT_PER_ROW 0.6
#define MARGIN_PER_ROW 0.5
#define GUIDE_WIDTH_DIVISOR 3
#define NOTIFICATION_WIDTH_DIVISOR 3
#define STICK_REACH_DIVISOR 4
#define NOTIFICATION_SECONDS 4
#define NOTIFICATIONS_MAX 4
#define REPEAT_DELAY_MS 400
#define REPEAT_INTERVAL_MS 80
#define NS_PER_MS 1000000
#define PERCENT 100
#define VOLUME_STEP "5%"
#define GAMES_MAX 512
#define STORES_MAX 32
#define INPUTS_MAX 32
#define DEVICES_MAX 32
#define AUDIO_OBJECTS_MAX 256
#define DEPTH_MAX 4
#define ROWS_MAX (GAMES_MAX + 1)
#define TEXT_MAX 256
#define GAME_KEY_MAX (2 * NAME_MAX + 2)
#define WINDOWS_MAX (GAMES_MAX + 1)
#define RESOLUTIONS_MAX 8
#define AUTO "Auto"
#define BYTES_PER_PIXEL 4
#define BUFFERS 2
#define REWIND_STATES 20
#define SAVE_SECONDS 5
#define SAVE_CHANGE_PERCENT 1
#define STATES "states"
#define FRAMES "frames"
#define STATE_EXTENSION ".state"
#define THUMBNAIL_EXTENSION ".png"
#define SCREEN_EXTENSION ".screen.png"
#define REWIND_TREE_DEPTH 2
#define CAROUSEL_ROWS 4
#define OUTLINE_PER_ROW 0.1
#define SECONDS_PER_MINUTE 60

enum screen { HOME, STORE, GAME, GUIDE, OUTPUTS, INPUTS, SETTINGS };
static const struct { const char *name, *title; } screens[] = {
    [HOME] = {"home", "Home"}, [STORE] = {"store", "Store"}, [GAME] = {"game", ""},
    [GUIDE] = {"guide", "Guide"}, [OUTPUTS] = {"outputs", "Output"}, [INPUTS] = {"inputs", "Input"},
    [SETTINGS] = {"settings", "Settings"},
};
enum action { NOTHING, OPEN_STORE, OPEN_SETTINGS, OPEN_GAME, PLAY, RESUME, CLOSE, UNINSTALL, INSTALL, SHOW_DASHBOARD, VOLUME,
              OPEN_OUTPUTS, OPEN_INPUTS, SET_DEVICE, RESTART, POWER_OFF, CHOOSE_DISPLAY, CHOOSE_RESOLUTION, REWIND };
enum command { NONE, UP, DOWN, LEFT, RIGHT, CONFIRM, BACK, GUIDE_BUTTON };
enum surface_mode { HIDDEN, NOTIFICATIONS_ONLY, FULL };

struct game {
    char slug[NAME_MAX + 1], platform[NAME_MAX + 1], title[TEXT_MAX], job_text[TEXT_MAX];
    int installed, commands, context, paused, closing, job_output, installing, states_watch, frames_watch;
    unsigned stores;
    pid_t pid, job;
};

struct view { enum screen screen; int game, selected; enum action action; int arg; };
struct row { char label[TEXT_MAX], value[TEXT_MAX]; enum action action; int arg; };
struct device { int id; char name[TEXT_MAX], node[TEXT_MAX]; };
struct input { struct libevdev *device; int guide, x, y, center, reach; };
struct notification { char text[TEXT_MAX]; struct timespec expires; };
struct window { struct zwlr_foreign_toplevel_handle_v1 *handle; char app_id[TEXT_MAX]; };
struct buffer { struct wl_buffer *buffer; void *pixels; int busy; };
struct capture { struct zwlr_screencopy_frame_v1 *frame; struct wl_buffer *buffer; unsigned char *pixels; pixman_format_code_t format; int game, width, height, stride, save, current; };

static struct game games[GAMES_MAX];
static int game_count, active = -1;
static char stores[STORES_MAX][PATH_MAX];
static int store_count;
static pid_t refresher, monitor;
static int refresh_pending, monitor_output = -1;
static json_tokener *monitor_parser;
static int audio_objects[AUDIO_OBJECTS_MAX], audio_object_count;
static struct view dashboard[DEPTH_MAX] = {{.screen = HOME, .game = -1}}, guide[DEPTH_MAX] = {{.screen = GUIDE, .game = -1}};
static int dashboard_depth = 1, guide_depth = 1, guide_shown, dashboard_open = 1;
static struct row rows[ROWS_MAX];
static struct device outputs[DEVICES_MAX], inputs[DEVICES_MAX];
static char default_output[TEXT_MAX], default_input[TEXT_MAX];
static int output_count, input_count, volume = -1;
static struct input devices[INPUTS_MAX];
static int device_count;
static char connected[INPUTS_MAX][NAME_MAX + 1];
static int connected_count;
static struct notification notifications[NOTIFICATIONS_MAX];
static int notification_count;
static enum command repeating;
static int repeat_timer, notification_timer, save_timer, watcher, log_file, changed = 1;
static char rewinds[REWIND_STATES][NAME_MAX + 1];
static cairo_surface_t *thumbnails[REWIND_STATES], *pending_screen, *current_screen;
static int pending_after;
static struct capture capture;
static int rewind_count, rewind_back;
static char *written_state;
static struct window windows[WINDOWS_MAX];
static int window_count, resolutions[RESOLUTIONS_MAX], resolution_count;
static json_object *settings, *display_state;

static struct wl_display *display;
static struct wl_compositor *compositor;
static struct wl_shm *shm;
static struct zwlr_layer_shell_v1 *layer_shell;
static struct wp_security_context_manager_v1 *security;
static struct zwlr_foreign_toplevel_manager_v1 *toplevels;
static struct zwlr_screencopy_manager_v1 *screencopy;
static struct wl_output *display_output;
static uint32_t display_output_id;
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

static json_object *read_json(char *const argv[]) {
    int output;
    pid_t pid = spawn_reading(argv, &output);
    if (pid < 0) return NULL;
    json_object *parsed = json_object_from_fd(output);
    close(output);
    waitpid(pid, NULL, 0);
    return parsed;
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
    snprintf(text, sizeof text, "%s/%s", games[index].slug, games[index].platform);
    return text;
}

static const char *name(int index) {
    static char text[GAME_KEY_MAX];
    snprintf(text, sizeof text, "%s-%s", games[index].slug, games[index].platform);
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
    unlink(rewind_path(path, active, "/" STATES "/%s" THUMBNAIL_EXTENSION, state));
    unlink(rewind_path(path, active, "/" STATES "/%s" SCREEN_EXTENSION, state));
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
        cairo_surface_destroy(pending_screen);
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
            changed_pixels += ((uint32_t *)(now + y * stride))[x] != ((uint32_t *)(before + y * stride))[x];
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

static void read_audio(void) {
    json_object *objects = read_json((char *[]){"pw-dump", NULL}), *field;
    int level_output;
    pid_t level_reader = spawn_reading((char *[]){"wpctl", "get-volume", "@DEFAULT_AUDIO_SINK@", NULL}, &level_output);
    FILE *level_text = level_reader > 0 ? fdopen(level_output, "r") : NULL;
    double level;
    volume = level_text && fscanf(level_text, "Volume: %lf", &level) == 1 ? level * PERCENT + 0.5 : -1;
    if (level_text) fclose(level_text);
    if (level_reader > 0) waitpid(level_reader, NULL, 0);
    output_count = input_count = 0;
    for (size_t i = 0; json_object_is_type(objects, json_type_array) && i < json_object_array_length(objects); i++) {
        json_object *object = json_object_array_get_idx(objects, i), *metadata, *info;
        for (size_t j = 0; json_object_object_get_ex(object, "metadata", &metadata) && j < json_object_array_length(metadata); j++) {
            json_object *entry = json_object_array_get_idx(metadata, j);
            if (!strcmp(string_at(entry, "key", NULL), "default.audio.sink"))
                snprintf(default_output, sizeof default_output, "%s", string_at(entry, "value", "name"));
            if (!strcmp(string_at(entry, "key", NULL), "default.audio.source"))
                snprintf(default_input, sizeof default_input, "%s", string_at(entry, "value", "name"));
        }
        if (!json_object_object_get_ex(object, "info", &info) || !json_object_object_get_ex(info, "props", &info)) continue;
        int sink = !strcmp(string_at(info, "media.class", NULL), "Audio/Sink");
        if (!sink && strcmp(string_at(info, "media.class", NULL), "Audio/Source")) continue;
        if ((sink ? output_count : input_count) == DEVICES_MAX) continue;
        struct device *device = sink ? &outputs[output_count++] : &inputs[input_count++];
        json_object_object_get_ex(object, "id", &field);
        device->id = json_object_get_int(field);
        snprintf(device->node, sizeof device->node, "%s", string_at(info, "node.name", NULL));
        snprintf(device->name, sizeof device->name, "%s",
                 *string_at(info, "node.description", NULL) ? string_at(info, "node.description", NULL) : device->node);
    }
    json_object_put(objects);
    changed = 1;
}

static int is_audio_object(json_object *object) {
    json_object *field;
    int id = json_object_object_get_ex(object, "id", &field) ? json_object_get_int(field) : -1;
    const char *type = string_at(object, "type", NULL);
    for (int i = 0; i < audio_object_count; i++) {
        if (audio_objects[i] != id) continue;
        if (!*type) audio_objects[i] = audio_objects[--audio_object_count];
        return 1;
    }
    if (strcmp(type, "PipeWire:Interface:Node") && strcmp(type, "PipeWire:Interface:Device") && strcmp(type, "PipeWire:Interface:Metadata"))
        return 0;
    if (audio_object_count < AUDIO_OBJECTS_MAX) audio_objects[audio_object_count++] = id;
    return 1;
}

static void start_monitor(void) {
    char socket_path[PATH_MAX];
    snprintf(socket_path, sizeof socket_path, "%s/" PIPEWIRE_SOCKET, getenv("XDG_RUNTIME_DIR"));
    if (monitor > 0 || access(socket_path, F_OK)) return;
    monitor = spawn_reading((char *[]){"pw-dump", "--monitor", "--no-colors", NULL}, &monitor_output);
    audio_object_count = 0;
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
            relevant |= is_audio_object(json_object_array_get_idx(changes, i));
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

static json_object *displays(void) {
    json_object *list;
    return json_object_object_get_ex(display_state, "displays", &list) ? list : NULL;
}

static int display_count(void) {
    json_object *list = displays();
    return list ? json_object_array_length(list) : 0;
}

static const char *display_label(int index) { return string_at(json_object_array_get_idx(displays(), index), "label", NULL); }

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

static void cycle_resolution(int step) {
    char value[TEXT_MAX];
    int current = 0;
    for (int i = 0; i < resolution_count; i++)
        if (resolutions[i] == chosen_resolution()) current = i + 1;
    int next = cycle(current, resolution_count, step);
    snprintf(value, sizeof value, "%d", next ? resolutions[next - 1] : 0);
    start((char *[]){"settings", "resolution", value, NULL});
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

static int by_title(const void *left, const void *right) {
    const struct game *a = &games[*(const int *)left], *b = &games[*(const int *)right];
    int order = strcasecmp(a->title, b->title);
    return order ? order : strcmp(a->platform, b->platform);
}

static int sorted(int *order, int store_view) {
    int count = 0;
    for (int i = 0; i < game_count; i++)
        if (store_view ? games[i].stores != 0 : games[i].installed) order[count++] = i;
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

static const char *status(struct game *game) {
    if (game->job) return game->job_text;
    if (game->pid) return game->paused ? "paused" : "running";
    return game->installed ? "installed" : "";
}

static int rewindable(void) { return active >= 0 && !dashboard_open; }

static int build(struct view *view) {
    int count = 0, order[GAMES_MAX];
    char level[TEXT_MAX] = "";
    struct game *game = view->game >= 0 ? &games[view->game] : NULL;
    switch (view->screen) {
    case HOME:
    case STORE:
        for (int i = 0, total = sorted(order, view->screen == STORE); i < total; i++)
            count = add_row(count, OPEN_GAME, order[i], games[order[i]].title, "%s  %s", games[order[i]].platform, status(&games[order[i]]));
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
            count = add_row(count, NOTHING, 0, game->job_text, "");
        } else if (game->installed) {
            count = add_row(count, PLAY, 0, "Play", "");
            count = add_row(count, UNINSTALL, 0, "Uninstall", "");
        } else {
            for (int store = 0; store < store_count; store++)
                if (game->stores & (1u << store)) count = add_row(count, INSTALL, store, "Install from", "%s", store_label(store));
        }
        break;
    case GUIDE:
        if (rewindable()) {
            count = add_row(count, SHOW_DASHBOARD, 0, "Dashboard", "");
            count = add_row(count, REWIND, 0, "Rewind", "%s", rewind_age());
        }
        if (volume >= 0) snprintf(level, sizeof level, "%d%%", volume);
        count = add_row(count, VOLUME, 0, "Volume", "%s", level);
        count = add_row(count, OPEN_OUTPUTS, 0, "Output", "%s", chosen(outputs, output_count, default_output));
        count = add_row(count, OPEN_INPUTS, 0, "Input", "%s", chosen(inputs, input_count, default_input));
        count = add_row(count, RESTART, 0, "Restart", "");
        count = add_row(count, POWER_OFF, 0, "Power off", "");
        break;
    case OUTPUTS:
    case INPUTS:
        for (int i = 0, total = view->screen == OUTPUTS ? output_count : input_count; i < total; i++) {
            struct device *device = view->screen == OUTPUTS ? &outputs[i] : &inputs[i];
            const char *current = view->screen == OUTPUTS ? default_output : default_input;
            count = add_row(count, SET_DEVICE, device->id, device->name, !strcmp(device->node, current) ? "selected" : "");
        }
        break;
    case SETTINGS:
        count = add_row(count, CHOOSE_DISPLAY, 0, "Display", "%s", display_value());
        count = add_row(count, CHOOSE_RESOLUTION, 0, "Resolution", "%s", resolution_value());
        break;
    }
    for (int i = 0; i < count; i++)
        if (rows[i].action == view->action && rows[i].arg == view->arg) view->selected = i;
    if (view->selected >= count) view->selected = count ? count - 1 : 0;
    return count;
}

static const char *title(struct view *view) { return view->screen == GAME ? games[view->game].title : screens[view->screen].title; }

static struct view *top(void) { return guide_shown ? &guide[guide_depth - 1] : &dashboard[dashboard_depth - 1]; }

static int interactive(void) { return dashboard_open || guide_shown; }

static void push(enum screen screen, int game) {
    if (guide_shown) {
        if (guide_depth < DEPTH_MAX) guide[guide_depth++] = (struct view){.screen = screen, .game = game};
    } else if (dashboard_depth < DEPTH_MAX) dashboard[dashboard_depth++] = (struct view){.screen = screen, .game = game};
}

static struct window *game_window(int index) {
    for (int i = 0; i < window_count; i++)
        if (!strcmp(windows[i].app_id, key(index))) return &windows[i];
    return NULL;
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
}

static void prepare_rewinds(int index, char *states, char *frames) {
    char path[PATH_MAX];
    remove_rewinds(index);
    mkdir(rewind_path(path, index, ""), S_IRWXU);
    mkdir(rewind_path(states, index, "/" STATES), S_IRWXU);
    mkdir(rewind_path(frames, index, "/" FRAMES), S_IRWXU);
    games[index].states_watch = inotify_add_watch(watcher, states, IN_CLOSE_WRITE);
    games[index].frames_watch = inotify_add_watch(watcher, frames, IN_CLOSE_WRITE);
}

static void request_frame(void) {
    if (active >= 0 && !games[active].paused && !games[active].closing) send_command(active, "SCREENSHOT");
}

static cairo_surface_t *captured_screen(void) {
    cairo_surface_t *screen = cairo_image_surface_create(CAIRO_FORMAT_RGB24, capture.width, capture.height);
    pixman_image_t *shot = pixman_image_create_bits(capture.format, capture.width, capture.height, (uint32_t *)capture.pixels, capture.stride);
    pixman_image_t *target = pixman_image_create_bits(PIXMAN_x8r8g8b8, capture.width, capture.height,
                                                      (uint32_t *)cairo_image_surface_get_data(screen), cairo_image_surface_get_stride(screen));
    pixman_image_composite32(PIXMAN_OP_SRC, shot, NULL, target, 0, 0, 0, 0, 0, 0, capture.width, capture.height);
    pixman_image_unref(shot);
    pixman_image_unref(target);
    cairo_surface_mark_dirty(screen);
    return screen;
}

static void end_capture(int captured) {
    cairo_surface_t *screen = captured ? captured_screen() : NULL;
    if (capture.save && capture.game == active) {
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
    zwlr_screencopy_frame_v1_destroy(capture.frame);
    if (capture.buffer) {
        wl_buffer_destroy(capture.buffer);
        munmap(capture.pixels, (size_t)capture.stride * capture.height);
    }
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

static void on_capture_buffer(void *data, struct zwlr_screencopy_frame_v1 *frame, uint32_t format, uint32_t buffer_width, uint32_t buffer_height, uint32_t stride) {
    (void)data;
    size_t size = (size_t)stride * buffer_height;
    if (format == WL_SHM_FORMAT_XRGB8888 || format == WL_SHM_FORMAT_ARGB8888) capture.format = PIXMAN_x8r8g8b8;
    else if (format == WL_SHM_FORMAT_XBGR8888 || format == WL_SHM_FORMAT_ABGR8888) capture.format = PIXMAN_x8b8g8r8;
    else { end_capture(0); return; }
    struct wl_shm_pool *pool = create_pool(size, (void **)&capture.pixels);
    capture.buffer = wl_shm_pool_create_buffer(pool, 0, buffer_width, buffer_height, stride, format);
    capture.width = buffer_width;
    capture.height = buffer_height;
    capture.stride = stride;
    wl_shm_pool_destroy(pool);
    zwlr_screencopy_frame_v1_copy(frame, capture.buffer);
}

static void on_capture_flags(void *data, struct zwlr_screencopy_frame_v1 *frame, uint32_t flags) { (void)data, (void)frame, (void)flags; }

static void on_capture_ready(void *data, struct zwlr_screencopy_frame_v1 *frame, uint32_t seconds_high, uint32_t seconds_low, uint32_t nanoseconds) {
    (void)data, (void)frame, (void)seconds_high, (void)seconds_low, (void)nanoseconds;
    end_capture(1);
}

static void on_capture_failed(void *data, struct zwlr_screencopy_frame_v1 *frame) {
    (void)data, (void)frame;
    end_capture(0);
}

static const struct zwlr_screencopy_frame_v1_listener capture_listener = {
    .buffer = on_capture_buffer, .flags = on_capture_flags, .ready = on_capture_ready, .failed = on_capture_failed};

static void start_capture(int index) {
    json_object *box = NULL;
    json_object_object_get_ex(display_state, "area_box", &box);
    capture.game = index;
    capture.frame = zwlr_screencopy_manager_v1_capture_output_region(screencopy, 0, display_output, int_at(box, "x"), int_at(box, "y"),
                                                                    int_at(box, "width"), int_at(box, "height"));
    zwlr_screencopy_frame_v1_add_listener(capture.frame, &capture_listener, NULL);
}

static void check_frame(int index, const char *file) {
    char path[PATH_MAX];
    if (index != active || capture.frame || notification_count) return;
    cairo_surface_t *frame = cairo_image_surface_create_from_png(rewind_path(path, index, "/" FRAMES "/%s", file));
    cairo_surface_t *last = rewind_count ? state_image(rewind_count - 1, THUMBNAIL_EXTENSION) : NULL;
    if (!cairo_surface_status(frame) && (!last || changed_percent(frame, last) >= SAVE_CHANGE_PERCENT)) {
        capture.save = display_output != NULL;
        if (capture.save) start_capture(index);
        else send_command(index, "SAVE_STATE");
    }
    cairo_surface_destroy(frame);
    cairo_surface_destroy(last);
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

static void play(int index) {
    char directory[PATH_MAX], states[PATH_MAX], frames[PATH_MAX];
    int commands[2];
    struct game *game = &games[index];
    if (!listen_socket(index, directory, sizeof directory)) { notify("Could not start %s", game->title); return; }
    if (pipe2(commands, O_CLOEXEC) < 0) { perror("pipe"); close(game->context); return; }
    int output = game_log(index);
    prepare_rewinds(index, states, frames);
    guide[0] = (struct view){.screen = GUIDE, .game = -1, .action = REWIND};
    guide_depth = 1;
    show_game(index);
    game->pid = spawn((char *[]){"games", "run", game->slug, game->platform, directory, states, frames, NULL}, commands[0], output, output);
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
    struct game *game = &games[index];
    show_game(index);
    if (game->paused) send_command(index, "PAUSE_TOGGLE");
    if (kill(game->pid, SIGTERM) < 0) perror("close");
    game->paused = 0;
    game->closing = 1;
}

static void start_job(int index, int installing, char *const argv[]) {
    struct game *game = &games[index];
    game->job = spawn_reading(argv, &game->job_output);
    if (game->job < 0) { game->job = 0; return; }
    game->installing = installing;
    snprintf(game->job_text, sizeof game->job_text, installing ? "installing" : "uninstalling");
}

static void read_job(int index) {
    struct game *game = &games[index];
    char chunk[TEXT_MAX];
    ssize_t count = read(game->job_output, chunk, sizeof chunk - 1);
    if (count <= 0) return;
    chunk[count] = '\0';
    for (char *line = strtok(chunk, "\n"); line; line = strtok(NULL, "\n")) {
        snprintf(game->job_text, sizeof game->job_text, "%s", line);
        notify("%s: %s", game->title, line);
    }
}

static void job_done(int index, int status) {
    struct game *game = &games[index];
    close(game->job_output);
    game->job = 0;
    rescan_installed();
    if (game->installing) notify(status ? "Install failed: %s" : "Installed %s", game->title);
    else notify(status ? "Uninstall failed: %s" : "Uninstalled %s", game->title);
}

static void act(struct row *row) {
    char id[TEXT_MAX], store[TEXT_MAX];
    struct view *view = top();
    struct game *game = view->game >= 0 ? &games[view->game] : NULL;
    switch (row->action) {
    case NOTHING: case VOLUME: case CHOOSE_DISPLAY: case CHOOSE_RESOLUTION: break;
    case REWIND: load_rewind(); break;
    case OPEN_STORE: push(STORE, -1); refresh(); break;
    case OPEN_SETTINGS: push(SETTINGS, -1); break;
    case OPEN_GAME: push(GAME, row->arg); break;
    case PLAY: play(view->game); break;
    case RESUME: resume(view->game); break;
    case CLOSE: close_game(view->game); break;
    case UNINSTALL: start_job(view->game, 0, (char *[]){"games", "uninstall", game->slug, game->platform, NULL}); break;
    case INSTALL:
        snprintf(store, sizeof store, "%d", row->arg + 1);
        start_job(view->game, 1, (char *[]){"games", "install", store, game->slug, game->platform, NULL});
        break;
    case SHOW_DASHBOARD: guide_shown = 0; dashboard_open = 1; dashboard_depth = 1; break;
    case OPEN_OUTPUTS: push(OUTPUTS, -1); break;
    case OPEN_INPUTS: push(INPUTS, -1); break;
    case SET_DEVICE:
        snprintf(id, sizeof id, "%d", row->arg);
        start((char *[]){"wpctl", "set-default", id, NULL});
        guide_depth--;
        break;
    case RESTART: start((char *[]){"reboot", NULL}); break;
    case POWER_OFF: start((char *[]){"poweroff", NULL}); break;
    }
}

static void change_volume(const char *direction) {
    char step[TEXT_MAX];
    snprintf(step, sizeof step, "%s%s", VOLUME_STEP, direction);
    start((char *[]){"wpctl", "set-volume", "-l", VOLUME_LIMIT, "@DEFAULT_AUDIO_SINK@", step, NULL});
}

static void adjust(enum action action, int step) {
    if (action == VOLUME) change_volume(step < 0 ? "-" : "+");
    if (action == CHOOSE_DISPLAY) cycle_display(step);
    if (action == CHOOSE_RESOLUTION) cycle_resolution(step);
    if (action == REWIND && rewind_back - step >= 0 && rewind_back - step <= rewind_count) rewind_back -= step;
}

static void open_guide(void) {
    rewind_back = 0;
    if (!rewindable() || !display_output) {
        guide_shown = 1;
        return;
    }
    capture.current = 1;
    if (!capture.frame) start_capture(active);
}

static void press(enum command command) {
    if (command == GUIDE_BUTTON) {
        if (guide_shown) guide_shown = 0;
        else open_guide();
        changed = 1;
        return;
    }
    if (!interactive()) return;
    struct view *view = top();
    int count = build(view), selected = view->selected;
    struct row *row = count ? &rows[view->selected] : NULL;
    switch (command) {
    case UP: if (view->selected > 0) view->selected--; break;
    case DOWN: if (view->selected < count - 1) view->selected++; break;
    case LEFT: case RIGHT: if (row) adjust(row->action, command == LEFT ? -1 : 1); break;
    case CONFIRM: if (row) act(row); break;
    case BACK:
        if (guide_shown && guide_depth > 1) guide_depth--;
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

static void hold(enum command command, int pressed) {
    struct itimerspec timer = {0};
    if (pressed) press(command);
    if (command < UP || command > RIGHT || (!pressed && command != repeating) || (pressed && !interactive())) return;
    repeating = pressed ? command : NONE;
    if (pressed) timer = (struct itimerspec){.it_value = {.tv_nsec = REPEAT_DELAY_MS * NS_PER_MS}, .it_interval = {.tv_nsec = REPEAT_INTERVAL_MS * NS_PER_MS}};
    timerfd_settime(repeat_timer, 0, &timer, NULL);
}

static enum command key_command(unsigned code) {
    switch (code) {
    case KEY_UP: case BTN_DPAD_UP: return UP;
    case KEY_DOWN: case BTN_DPAD_DOWN: return DOWN;
    case KEY_LEFT: case BTN_DPAD_LEFT: return LEFT;
    case KEY_RIGHT: case BTN_DPAD_RIGHT: return RIGHT;
    case KEY_ENTER: case KEY_KPENTER: case KEY_X: case BTN_SOUTH: case BTN_START: return CONFIRM;
    case KEY_ESC: case KEY_BACKSPACE: case KEY_Z: case BTN_EAST: return BACK;
    case BTN_MODE: return GUIDE_BUTTON;
    default: return NONE;
    }
}

static void steer(int *state, int direction, enum command negative, enum command positive) {
    if (direction == *state) return;
    if (*state) hold(*state < 0 ? negative : positive, 0);
    *state = direction;
    if (direction) hold(direction < 0 ? negative : positive, 1);
}

static int stick(struct input *input, int value) {
    return value < input->center - input->reach ? -1 : value > input->center + input->reach ? 1 : 0;
}

static void open_input(const char *node) {
    char path[PATH_MAX];
    struct libevdev *device;
    snprintf(path, sizeof path, INPUT_DIR "/%s", node);
    if (strncmp(node, "event", strlen("event")) || device_count == INPUTS_MAX) return;
    int fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0 || libevdev_new_from_fd(fd, &device) < 0) {
        perror(path);
        if (fd >= 0) close(fd);
        return;
    }
    int minimum = libevdev_get_abs_minimum(device, ABS_X), maximum = libevdev_get_abs_maximum(device, ABS_X);
    devices[device_count++] = (struct input){.device = device, .guide = !strcmp(libevdev_get_name(device), GUIDE_DEVICE),
                                             .center = (minimum + maximum) / 2, .reach = (maximum - minimum) / STICK_REACH_DIVISOR};
}

static void handle_event(struct input *input, struct input_event *event) {
    if (event->type == EV_KEY && event->value != 2 && key_command(event->code) != NONE) hold(key_command(event->code), event->value);
    if (event->type != EV_ABS) return;
    if (event->code == ABS_HAT0X) steer(&input->x, event->value, LEFT, RIGHT);
    if (event->code == ABS_HAT0Y) steer(&input->y, event->value, UP, DOWN);
    if (event->code == ABS_X && input->reach) steer(&input->x, stick(input, event->value), LEFT, RIGHT);
    if (event->code == ABS_Y && input->reach) steer(&input->y, stick(input, event->value), UP, DOWN);
}

static void read_input(int index, int discard) {
    struct input *input = &devices[index];
    struct input_event event;
    int status;
    while ((status = libevdev_next_event(input->device, LIBEVDEV_READ_FLAG_NORMAL, &event)) >= 0) {
        if (status == LIBEVDEV_READ_STATUS_SYNC)
            while (libevdev_next_event(input->device, LIBEVDEV_READ_FLAG_SYNC, &event) == LIBEVDEV_READ_STATUS_SYNC) continue;
        else if (!discard) handle_event(input, &event);
    }
    if (discard) input->x = input->y = 0;
    if (status == -EAGAIN) return;
    close(libevdev_get_fd(input->device));
    libevdev_free(input->device);
    *input = devices[--device_count];
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

static void draw(int top_count) {
    struct buffer *buffer = !buffers[0].busy ? &buffers[0] : !buffers[1].busy ? &buffers[1] : NULL;
    if (!buffer) { draw_pending = 1; return; }
    draw_pending = 0;
    cairo_surface_t *canvas = cairo_image_surface_create_for_data(buffer->pixels, CAIRO_FORMAT_ARGB32, width, height, width * BYTES_PER_PIXEL);
    cairo_t *cr = cairo_create(canvas);
    double notification_width = (double)output_width / NOTIFICATION_WIDTH_DIVISOR, margin = row_height() * MARGIN_PER_ROW;
    cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
    cairo_paint(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
    cairo_select_font_face(cr, FONT, CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, row_height() * FONT_PER_ROW);
    if (launching()) {
        cairo_set_source_rgb(cr, 0, 0, 0);
        cairo_paint(cr);
    }
    if (dashboard_open) draw_view(cr, &dashboard[dashboard_depth - 1], guide_shown ? build(&dashboard[dashboard_depth - 1]) : top_count, width);
    if (guide_shown) {
        int guide_count = dashboard_open ? build(&guide[guide_depth - 1]) : top_count;
        if (guide_count && rows[guide[guide_depth - 1].selected].action == REWIND && rewind_back) draw_preview(cr);
        draw_view(cr, &guide[guide_depth - 1], guide_count, (double)width / GUIDE_WIDTH_DIVISOR);
        if (guide_depth == 1 && rewindable()) draw_carousel(cr);
    }
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
    wl_surface_attach(surface, buffer->buffer, 0, 0);
    wl_surface_damage_buffer(surface, 0, 0, width, height);
    wl_surface_commit(surface);
}

static json_object *string_array(char list[][NAME_MAX + 1], int count) {
    json_object *array = json_object_new_array();
    for (int i = 0; i < count; i++) json_object_array_add(array, json_object_new_string(strchr(list[i], ' ') + 1));
    return array;
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
    json_object_object_add(state, "screen", json_object_new_string(screens[view->screen].name));
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
        json_object_object_add(game, "status", json_object_new_string(status(&games[i])));
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
    json_object_object_add(audio, "input", json_object_new_string(chosen(inputs, input_count, default_input)));
    json_object_object_add(state, "audio", audio);
    json_object_object_add(state, "inputs", string_array(connected, connected_count));
    json_object_object_add(state, "settings", json_object_get(settings));
    json_object_object_add(state, "display", json_object_get(display_state));
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
    if (active < 0) dashboard_open = 1;
    for (int i = 0; i < game_count; i++) {
        int pause = i != active || interactive();
        if (!games[i].pid || games[i].closing || games[i].paused == pause) continue;
        send_command(i, "PAUSE_TOGGLE");
        games[i].paused = pause;
    }
    enum surface_mode wanted = HIDDEN;
    if (interactive() || launching() || (notification_count && !output_width)) wanted = FULL;
    else if (notification_count) wanted = NOTIFICATIONS_ONLY;
    if (wanted != mode && wanted == HIDDEN) {
        free_buffers();
        hide();
    } else if (wanted != mode) show(wanted);
    int count = build(top());
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
            changed = 1;
        }
    }
}

static void track_input(const char *entry, int present) {
    for (int i = 0; i < connected_count; i++) {
        if (strcmp(connected[i], entry)) continue;
        if (!present) memcpy(connected[i], connected[--connected_count], sizeof connected[0]);
        return;
    }
    if (present && connected_count < INPUTS_MAX) snprintf(connected[connected_count++], sizeof connected[0], "%s", entry);
}

static int read_game_change(struct inotify_event *event) {
    for (int i = 0; i < game_count; i++) {
        if (event->wd == games[i].frames_watch) {
            check_frame(i, event->name);
            return 1;
        }
        if (event->wd == games[i].states_watch) {
            if (i == active) refresh_rewinds();
            return 1;
        }
    }
    return 0;
}

static void read_changes(int input_watch, int inputs_watch, int runtime_watch, int display_watch) {
    char buffer[BUFSIZ] __attribute__((aligned(__alignof__(struct inotify_event))));
    ssize_t count = read(watcher, buffer, sizeof buffer);
    for (char *at = buffer; count > 0 && at < buffer + count;) {
        struct inotify_event *event = (struct inotify_event *)at;
        at += sizeof *event + event->len;
        if (!event->len || read_game_change(event)) continue;
        if (event->wd == input_watch) open_input(event->name);
        else if (event->wd == runtime_watch && !strcmp(event->name, PIPEWIRE_SOCKET)) start_monitor();
        else if (event->wd == inputs_watch && strchr(event->name, ' ')) {
            track_input(event->name, event->mask & IN_CREATE);
            notify("%s %s", event->mask & IN_CREATE ? "Connected" : "Disconnected", strchr(event->name, ' ') + 1);
        } else if (event->wd == display_watch && !strcmp(event->name, basename(DISPLAY_FILE))) {
            read_display();
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
    } else if (!strcmp(interface, zwlr_screencopy_manager_v1_interface.name))
        screencopy = wl_registry_bind(registry, id, &zwlr_screencopy_manager_v1_interface, 1);
    else if (!strcmp(interface, wl_output_interface.name)) {
        display_output = wl_registry_bind(registry, id, &wl_output_interface, 1);
        display_output_id = id;
    }
}

static void on_global_remove(void *data, struct wl_registry *registry, uint32_t id) {
    (void)data, (void)registry;
    if (id != display_output_id || !display_output) return;
    wl_output_destroy(display_output);
    display_output = NULL;
}

static const struct wl_registry_listener registry_listener = {.global = on_global, .global_remove = on_global_remove};

static void directory_of(const char *path, char *directory) { snprintf(directory, PATH_MAX, "%.*s", (int)(strrchr(path, '/') - path), path); }

static void scan_directory(const char *path, void (*found)(const char *)) {
    DIR *directory = opendir(path);
    for (struct dirent *entry; directory && (entry = readdir(directory));) found(entry->d_name);
    if (directory) closedir(directory);
}

static void found_input(const char *entry) {
    if (strchr(entry, ' ')) track_input(entry, 1);
}

int main(void) {
    enum { WAYLAND, CHANGES, CHILDREN, REPEAT, NOTIFICATIONS, SAVE, MONITOR, FIXED };
    struct pollfd watched[FIXED + INPUTS_MAX + GAMES_MAX];
    int jobs[GAMES_MAX], polled[INPUTS_MAX];
    char stores_directory[PATH_MAX], display_directory[PATH_MAX], settings_directory[PATH_MAX], *end;
    sigset_t children;
    signal(SIGPIPE, SIG_IGN);
    sigemptyset(&children);
    sigaddset(&children, SIGCHLD);
    sigprocmask(SIG_BLOCK, &children, NULL);
    log_file = open(LOG_DIR "/games.log", O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, S_IRUSR | S_IWUSR);
    display = wl_display_connect(NULL);
    monitor_parser = json_tokener_new();
    if (!display || log_file < 0) { fprintf(stderr, "overlay: no wayland display or log\n"); return 1; }
    wl_registry_add_listener(wl_display_get_registry(display), &registry_listener, NULL);
    wl_display_roundtrip(display);
    if (!compositor || !shm || !layer_shell || !security || !seat || !toplevels || !screencopy) { fprintf(stderr, "overlay: compositor lacks a required protocol\n"); return 1; }
    directory_of(STORES_FILE, stores_directory);
    directory_of(DISPLAY_FILE, display_directory);
    directory_of(SETTINGS_FILE, settings_directory);
    for (const char *at = RESOLUTIONS; resolution_count < RESOLUTIONS_MAX && (resolutions[resolution_count] = strtol(at, &end, 10)) > 0; at = end)
        resolution_count++;
    watcher = inotify_init1(IN_CLOEXEC);
    int input_watch = inotify_add_watch(watcher, INPUT_DIR, IN_CREATE);
    int inputs_watch = inotify_add_watch(watcher, INPUTS_DIR, IN_CREATE | IN_DELETE);
    int runtime_watch = inotify_add_watch(watcher, getenv("XDG_RUNTIME_DIR"), IN_CREATE);
    inotify_add_watch(watcher, stores_directory, IN_CLOSE_WRITE | IN_MOVED_TO);
    inotify_add_watch(watcher, settings_directory, IN_MOVED_TO | IN_MASK_ADD);
    int display_watch = inotify_add_watch(watcher, display_directory, IN_MOVED_TO);
    repeat_timer = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC);
    notification_timer = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC);
    save_timer = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC);
    timerfd_settime(save_timer, 0, &(struct itimerspec){.it_value = {.tv_sec = SAVE_SECONDS}, .it_interval = {.tv_sec = SAVE_SECONDS}}, NULL);
    watched[WAYLAND] = (struct pollfd){.fd = wl_display_get_fd(display), .events = POLLIN};
    watched[CHANGES] = (struct pollfd){.fd = watcher, .events = POLLIN};
    watched[CHILDREN] = (struct pollfd){.fd = signalfd(-1, &children, SFD_CLOEXEC), .events = POLLIN};
    watched[REPEAT] = (struct pollfd){.fd = repeat_timer, .events = POLLIN};
    watched[NOTIFICATIONS] = (struct pollfd){.fd = notification_timer, .events = POLLIN};
    watched[SAVE] = (struct pollfd){.fd = save_timer, .events = POLLIN};
    scan_directory(INPUT_DIR, open_input);
    scan_directory(INPUTS_DIR, found_input);
    read_stores();
    read_settings();
    read_display();
    rescan();
    refresh();
    start_monitor();
    settle();
    for (;;) {
        int count = FIXED, device_polls = 0, job_count = 0, was_interactive = interactive();
        watched[MONITOR] = (struct pollfd){.fd = monitor > 0 ? monitor_output : -1, .events = POLLIN};
        for (int i = 0; i < device_count; i++) {
            if (!devices[i].guide && !was_interactive) continue;
            polled[device_polls++] = i;
            watched[count++] = (struct pollfd){.fd = libevdev_get_fd(devices[i].device), .events = POLLIN};
        }
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
        if (watched[CHANGES].revents) read_changes(input_watch, inputs_watch, runtime_watch, display_watch);
        if (watched[CHILDREN].revents && read(watched[CHILDREN].fd, &signal_info, sizeof signal_info) > 0) reap();
        if (watched[REPEAT].revents && read(repeat_timer, &expirations, sizeof expirations) > 0) press(repeating);
        if (watched[NOTIFICATIONS].revents && read(notification_timer, &expirations, sizeof expirations) > 0) expire_notifications();
        if (watched[SAVE].revents && read(save_timer, &expirations, sizeof expirations) > 0) request_frame();
        if (watched[MONITOR].revents) read_monitor();
        for (int i = device_polls - 1; i >= 0; i--)
            if (watched[FIXED + i].revents) read_input(polled[i], 0);
        for (int i = 0; i < job_count; i++)
            if (watched[FIXED + device_polls + i].revents) read_job(jobs[i]);
        if (!was_interactive && interactive())
            for (int i = device_count - 1; i >= 0; i--)
                if (!devices[i].guide) read_input(i, 1);
        settle();
    }
}
