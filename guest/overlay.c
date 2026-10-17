#define _GNU_SOURCE
#include <cairo.h>
#include <ctype.h>
#include <dirent.h>
#include <drm_fourcc.h>
#include <errno.h>
#include <fcntl.h>
#include <ftw.h>
#include <gbm.h>
#include <glob.h>
#include <json-c/json.h>
#include <libintl.h>
#include <limits.h>
#include <locale.h>
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
#include <time.h>
#include <unistd.h>
#include <wayland-client.h>
#include <xf86drm.h>
#include "anyconsole.h"
#include "ext-foreign-toplevel-list-v1.h"
#include "ext-image-capture-source-v1.h"
#include "ext-image-copy-capture-v1.h"
#include "linux-dmabuf-v1.h"
#include "security-context-v1.h"
#include "skin.h"
#include "ext-background-effect-v1.h"
#include "wlr-foreign-toplevel-management-unstable-v1.h"
#include "wlr-layer-shell-unstable-v1.h"

#define PARTIAL ".partial"
#define STATE_PARTIAL STATE_FILE PARTIAL
#define PIPEWIRE_SOCKET "pipewire-0"
#define SANDBOX_ENGINE "docker"
#define NOTIFICATION_SECONDS 4
#define NOTIFICATIONS_MAX 4
#define REPEAT_DELAY_MS 400
#define REPEAT_INTERVAL_MS 80
#define NS_PER_MS 1000000
#define NS_PER_SECOND ((double)MSEC_PER_SEC * NS_PER_MS)
#define PERCENT 100
#define RGB_MASK 0x00ffffffu
#define VOLUME_STEP "5%"
#define STORES_MAX 32
#define DEVICES_MAX 32
#define DEPTH_MAX 4
#define REQUESTS_MAX 16
#define NO_ROW "no row on any screen matches "
#define REQUEST_MAX PIPE_BUF
#define JOB_FDS 2
#define ROWS_MAX (GAMES_MAX + 1)
#define TEXT_MAX 256
#define GAME_KEY_MAX (2 * NAME_MAX + 2)
#define WINDOWS_MAX (GAMES_MAX + 1)
#define RESOLUTIONS_MAX 8
#define SLEEP_CHOICES_MAX 8
#define AUTO N_("Auto (%s)")
#define BOOST_GOVERNOR "performance"
#define REFRESH_PRECISION 100
#define REWIND_STATES 20
#define SAVE_SLOTS 10
#define UNNUMBERED_SLOT 0
#define KEPT_SESSIONS 4
#define SAVE_SECONDS 5
#define SAVE_CHANGE_PERCENT 1
#define STATES "states"
#define STATE_EXTENSION ".state"
#define SCREEN_EXTENSION ".screen.png"
#define REWIND_TREE_DEPTH 2
#define SECONDS_PER_MINUTE 60
#define LINUX_DMABUF_VERSION 3
#define LAUNCH_STEPS 2
#define NO_DEVICE 0
#define UNAPPLIED (-2)
#define NOTIFY_PREFIX "notify "
#define STRINGIFY_(x) #x
#define STRINGIFY(x) STRINGIFY_(x)
#define EMULATE_FILE "emulate.json"
#define ANALOG_CHOICES_FILE "analog.json"
#define CURRENT_SCREEN "current-screen"
#define RELOAD_SECONDS 0.2
#define SKIN_WATCHES_MAX 256
#define SKIN_TREE_DEPTH 16
#define BLUR_BOXES_MAX 16
#define SKINS_MAX 64
#define DIRECTORY_MODE (S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH)
#define LANGUAGES_MAX 32
#define ZONE_COLUMN 2
#define CATALOG "/LC_MESSAGES/" TEXT_DOMAIN ".mo"
#define _(text) gettext(text)
#define N_(text) text

extern int _nl_msg_cat_cntr;

enum screen { HOME, STORE, GAME, GUIDE, OUTPUTS, MICROPHONES, CONTROLLERS, CONTROLLER, SETTINGS, PLATFORM_RESOLUTIONS, WHOIS, LOG_OUT, ATTRACT, DELETE_DIALOG, OVERWRITE_DIALOG };
enum plane { DASHBOARD_PLANE, GUIDE_PLANE, PICKER_PLANE, DIALOG_PLANE };
static const char *const planes[] = {[DASHBOARD_PLANE] = "dashboard", [GUIDE_PLANE] = "guide", [PICKER_PLANE] = "picker", [DIALOG_PLANE] = "dialog"};
static const struct { const char *name, *title; enum plane plane; } screens[] = {
    [HOME] = {"home", N_("Home")}, [STORE] = {"store", N_("Store")}, [GAME] = {"game", ""},
    [GUIDE] = {"guide", N_("Guide"), GUIDE_PLANE}, [OUTPUTS] = {"outputs", N_("Output"), GUIDE_PLANE}, [MICROPHONES] = {"microphones", N_("Microphone"), GUIDE_PLANE},
    [CONTROLLERS] = {"controllers", N_("Controllers"), GUIDE_PLANE}, [CONTROLLER] = {"controller", "", GUIDE_PLANE}, [SETTINGS] = {"settings", N_("Settings")}, [PLATFORM_RESOLUTIONS] = {"resolutions", N_("Game resolution")},
    [WHOIS] = {"whois", "", PICKER_PLANE}, [LOG_OUT] = {"logout", "", GUIDE_PLANE}, [ATTRACT] = {"attract", ""},
    [DELETE_DIALOG] = {"delete-state", "", DIALOG_PLANE}, [OVERWRITE_DIALOG] = {"overwrite-state", "", DIALOG_PLANE},
};
enum action { NOTHING, OPEN_STORE, OPEN_SETTINGS, OPEN_GAME, PLAY, RESUME, CLOSE, UNINSTALL, INSTALL, SHOW_DASHBOARD, VOLUME,
              OPEN_OUTPUTS, OPEN_MICROPHONES, SET_DEVICE, OPEN_CONTROLLERS, PAIR, OPEN_CONTROLLER, FORGET, RELOAD, RESTART, POWER_OFF,
              CHOOSE_DISPLAY, CHOOSE_DISPLAY_MODE, APPLY_MODE, OPEN_RESOLUTIONS, CHOOSE_RESOLUTION, CHOOSE_SLEEP, REWIND, LOGIN, EMULATE, ANALOG, LOG_OUT_PLAYER, CONFIRM_LOG_OUT, MICROPHONE_VOLUME, SECOND_SCREEN, CHOOSE_SKIN, CLEAR_THUMBNAILS,
              SAVE_STATES, DELETE_STATE, OVERWRITE_STATE, CHOOSE_PICTURE, CHOOSE_LANGUAGE, CHOOSE_GAME_LANGUAGE, CHOOSE_TIME_ZONE, CHOOSE_CITY };
static const char *const actions[] = {
    [NOTHING] = "nothing", [OPEN_STORE] = "open-store", [OPEN_SETTINGS] = "open-settings", [OPEN_GAME] = "open-game", [PLAY] = "play", [RESUME] = "resume",
    [CLOSE] = "close", [UNINSTALL] = "uninstall", [INSTALL] = "install", [SHOW_DASHBOARD] = "dashboard", [VOLUME] = "volume", [OPEN_OUTPUTS] = "open-outputs",
    [OPEN_MICROPHONES] = "open-microphones", [SET_DEVICE] = "set-device", [OPEN_CONTROLLERS] = "open-controllers", [PAIR] = "pair",
    [OPEN_CONTROLLER] = "open-controller", [FORGET] = "forget", [RELOAD] = "reload", [RESTART] = "restart", [POWER_OFF] = "power-off",
    [CHOOSE_DISPLAY] = "display", [CHOOSE_DISPLAY_MODE] = "display-mode", [APPLY_MODE] = "apply-mode", [OPEN_RESOLUTIONS] = "open-resolutions", [CHOOSE_RESOLUTION] = "resolution",
    [CHOOSE_SLEEP] = "sleep", [REWIND] = "rewind", [LOGIN] = "login", [EMULATE] = "emulate", [ANALOG] = "analog", [LOG_OUT_PLAYER] = "log-out",
    [CONFIRM_LOG_OUT] = "confirm-log-out", [MICROPHONE_VOLUME] = "microphone-volume", [SECOND_SCREEN] = "second-screen", [CHOOSE_SKIN] = "skin",
    [CLEAR_THUMBNAILS] = "clear-thumbnails", [SAVE_STATES] = "save-states", [DELETE_STATE] = "delete-state", [OVERWRITE_STATE] = "overwrite-state",
    [CHOOSE_PICTURE] = "picture", [CHOOSE_LANGUAGE] = "language", [CHOOSE_GAME_LANGUAGE] = "game-language",
    [CHOOSE_TIME_ZONE] = "time-zone", [CHOOSE_CITY] = "city"};
static const char *const criteria[] = {"action", "label", "value", "arg"};
enum tab { GAME_TAB, DEVICES_TAB, SYSTEM_TAB };
static const char *const tabs[] = {[GAME_TAB] = N_("Game"), [DEVICES_TAB] = N_("I/O"), [SYSTEM_TAB] = N_("System")};
enum picture { ORIGINAL, RETRO, ENHANCED, PICTURES };
static const struct { const char *name, *label; } pictures[PICTURES] = {[ORIGINAL] = {"original", N_("Original")}, [RETRO] = {"retro", N_("Retro")}, [ENHANCED] = {"enhanced", N_("Enhanced")}};
enum restart { PLAYING, SAVING, CLOSING };
enum command { NONE, UP, DOWN, PAGE_UP, PAGE_DOWN, LEFT, RIGHT, CONFIRM, BACK, GUIDE_BUTTON, X_BUTTON, Y_BUTTON };
static const char *const commands[] = {[NONE] = "other", [UP] = "up", [DOWN] = "down", [PAGE_UP] = "page-up", [PAGE_DOWN] = "page-down",
                                       [LEFT] = "left", [RIGHT] = "right", [CONFIRM] = "confirm", [BACK] = "back", [GUIDE_BUTTON] = "guide",
                                       [X_BUTTON] = "x", [Y_BUTTON] = "y"};
static const struct { const char *name, *label; } mode_labels[] = {
    {"analog", N_("Analog pad")}, {"digital", N_("Digital pad")}, {"keyboard", N_("Keyboard")}, {"mouse", N_("Mouse")}, {"keyboard-mouse", N_("Keyboard & mouse")}};
enum task { INSTALLING, UNINSTALLING, LAUNCHING };
static const char *const tasks[] = {[INSTALLING] = N_("installing"), [UNINSTALLING] = N_("uninstalling"), [LAUNCHING] = N_("launching")};

struct port { int analog, locked, pending, forced; };

struct game {
    char slug[NAME_MAX + 1], platform[NAME_MAX + 1], thumbnail[NAME_MAX + 1], title[TEXT_MAX], job_text[TEXT_MAX], job_line[TEXT_MAX], job_error[TEXT_MAX], error_line[TEXT_MAX];
    json_object *emulate, *analog;
    double thumbnail_aspect;
    time_t thumbnail_time;
    int installed, commands, context, paused, closing, cancelled, job_output, percent, states_watch, rewinds, owner, second_screen, screen_shown, error_output, enhanced, restart_slot, booted, dial_absolute;
    struct port ports[PADS];
    size_t job_length, error_length;
    enum task task;
    enum restart restart;
    unsigned stores;
    pid_t pid, job, freezer;
};

struct view { enum screen screen; int game, selected; enum action action; int arg; enum tab tab; };
struct row { char label[TEXT_MAX], value[TEXT_MAX]; enum action action; int arg; };
struct device { int id; char name[TEXT_MAX], node[TEXT_MAX]; };
struct member { int device, profile; };
struct notification { char text[TEXT_MAX]; struct timespec expires; };
struct window { struct zwlr_foreign_toplevel_handle_v1 *handle; struct ext_foreign_toplevel_handle_v1 *listed; char app_id[TEXT_MAX]; int activated; };
struct request {
    int fd, waiting, game;
    size_t length;
    ssize_t reply_length, sent;
    char *reply, line[REQUEST_MAX], error[sizeof NO_ROW + REQUEST_MAX];
    json_object *notifications, *wanted;
};
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
static pid_t refresher, monitor, thumbnail_fetcher, thumbnail_refresher, thumbnail_clearer, upscalable_probe, image_events;
static int upscalable_output, upscalable_wanted;
static char **wanted_thumbnails;
static int wanted_thumbnail_count, sent_thumbnail_count;
static int refresh_pending, monitor_output = -1, image_events_output = -1;
static json_tokener *monitor_parser;
static struct view dashboard[DEPTH_MAX] = {{.screen = HOME, .game = -1}}, guides[PROFILES + 1][DEPTH_MAX];
static int dashboard_depth = 1, guide_depths[PROFILES + 1], guide_shown, dashboard_open = 1;
static struct row rows[ROWS_MAX];
static struct device outputs[DEVICES_MAX], microphones[DEVICES_MAX];
static char default_output[TEXT_MAX], default_microphone[TEXT_MAX], microphone_pad[TEXT_MAX], chosen_controller[TEXT_MAX], staged_mode[TEXT_MAX];
static int output_count, microphone_count, volume = -1, microphone_volume = -1, microphone_muted, mode_staged;
static int players[PADS], player_count, menu_told = -1, focus = NO_DEVICE, picker = NO_DEVICE, input_device = NO_DEVICE, previewed = NO_DEVICE, previewed_seat, repeating_device, guide_profile, applied_game = UNAPPLIED;
static struct member members[INPUT_DEVICES_MAX];
static int member_count;
static struct view whois;
static struct notification notifications[NOTIFICATIONS_MAX];
static int notification_count;
static enum command repeating;
static int repeat_timer, notification_timer, save_timer, watcher, log_file, changed = 1;
static json_object *emulate_choices[PROFILES + 1], *analog_choices[PROFILES + 1];
static char rewinds[REWIND_STATES][NAME_MAX + 1];
static cairo_surface_t *pending_screen, *newest_screen;
static char newest_name[NAME_MAX + 1];
static int pending_after, save_slot;
static cairo_surface_t *current_screen;
static struct { int game, profile, slot, after; cairo_surface_t *screen; } saving = {.game = -1};
static struct capture capture;
static struct request requests[REQUESTS_MAX];
static int request_count;
static int rewind_count, rewind_back;
static char *written_state, *skin_state;
static struct window windows[WINDOWS_MAX];
static struct gbm_device *gbm;
static int window_count, resolutions[RESOLUTIONS_MAX], resolution_count, sleep_choices[SLEEP_CHOICES_MAX], sleep_choice_count;
static char boot_governor[TEXT_MAX];
static int boosted = -1;
static json_object *settings, *display_state, *controllers_state, *audio_nodes, *audio_devices, *upscalable;

static struct wl_display *display;
static struct wl_compositor *compositor;
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
static struct ext_background_effect_manager_v1 *effects;
static struct ext_background_effect_surface_v1 *effect;
static int width, frame_pending, paint_needed, skin_moving, skin_timer, skin_watches[SKIN_WATCHES_MAX], skin_watch_count;
static struct box blurred[BLUR_BOXES_MAX];
static int blurred_count;
static double reload_due = -1;
static char wanted_skin[NAME_MAX + 1], loaded_skin[NAME_MAX + 1];
static char languages[LANGUAGES_MAX][NAME_MAX + 1], language_names[LANGUAGES_MAX][TEXT_MAX];
static int language_count;
static char **time_zones;
static int time_zone_count, chosen_zone = -1;

static pid_t spawn(char *const argv[], int input, int output, int errors) {
    posix_spawn_file_actions_t actions;
    posix_spawnattr_t attributes;
    sigset_t none, piped;
    pid_t pid;
    sigemptyset(&none);
    sigemptyset(&piped);
    sigaddset(&piped, SIGPIPE);
    posix_spawnattr_init(&attributes);
    posix_spawnattr_setflags(&attributes, POSIX_SPAWN_SETSIGMASK | POSIX_SPAWN_SETSIGDEF | POSIX_SPAWN_SETPGROUP);
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

static pid_t *helpers;
static int helper_count;

static void start(char *const argv[]) {
    pid_t pid = spawn(argv, -1, log_file, log_file);
    if (pid < 0) return;
    helpers = realloc(helpers, (helper_count + 1) * sizeof *helpers);
    helpers[helper_count++] = pid;
}

static int reap_helper(pid_t pid) {
    for (int i = 0; i < helper_count; i++)
        if (helpers[i] == pid) {
            helpers[i] = helpers[--helper_count];
            return 1;
        }
    return 0;
}

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

static pid_t spawn_reading(char *const argv[], int *output, int errors) {
    int pipe_ends[2];
    *output = -1;
    if (pipe2(pipe_ends, O_CLOEXEC) < 0) { perror(argv[0]); return -1; }
    pid_t pid = spawn(argv, -1, pipe_ends[1], errors);
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
    for (int i = 0; i < request_count; i++)
        if (requests[i].waiting) json_object_array_add(requests[i].notifications, json_object_new_string(notification->text));
    struct itimerspec timer = {.it_value = notifications[0].expires};
    timerfd_settime(notification_timer, TFD_TIMER_ABSTIME, &timer, NULL);
    changed = 1;
}

static void fail(int index, const char *format, ...) {
    char text[TEXT_MAX];
    va_list arguments;
    va_start(arguments, format);
    vsnprintf(text, sizeof text, format, arguments);
    va_end(arguments);
    notify("%s", text);
    for (int i = 0; i < request_count; i++)
        if (requests[i].waiting && requests[i].game == index && !*requests[i].error) snprintf(requests[i].error, sizeof requests[i].error, "%s", text);
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

static void remove_tree(const char *path) { nftw(path, remove_entry, REWIND_TREE_DEPTH, FTW_DEPTH | FTW_PHYS); }

static void remove_rewinds(int index) {
    char path[PATH_MAX];
    remove_tree(rewind_path(path, index, ""));
    *newest_name = '\0';
}

static int is_visible(const struct dirent *entry) { return *entry->d_name != '.'; }

static void keep_rewinds(const char *rewind) {
    char path[PATH_MAX], kept[PATH_MAX];
    struct dirent **entries = NULL;
    snprintf(path, sizeof path, REWIND_DIR "/%s", rewind);
    snprintf(kept, sizeof kept, KEPT_REWIND_DIR "/%ld-%s", (long)time(NULL), rewind);
    rename(path, kept);
    int count = scandir(KEPT_REWIND_DIR, &entries, is_visible, alphasort);
    for (int i = 0; i < count; i++) {
        snprintf(path, sizeof path, KEPT_REWIND_DIR "/%s", entries[i]->d_name);
        if (i < count - KEPT_SESSIONS) remove_tree(path);
        free(entries[i]);
    }
    free(entries);
}

static void keep_interrupted_rewinds(void) {
    struct dirent **entries = NULL;
    int count = scandir(REWIND_DIR, &entries, is_visible, alphasort);
    for (int i = 0; i < count; i++) {
        keep_rewinds(entries[i]->d_name);
        free(entries[i]);
    }
    free(entries);
}

static int is_state(const struct dirent *entry) {
    const char *extension = strrchr(entry->d_name, '.');
    return extension && !strncmp(extension, STATE_EXTENSION, strlen(STATE_EXTENSION)) && isdigit((unsigned char)extension[strlen(STATE_EXTENSION)]);
}

static int slot(const char *state) { return atoi(strrchr(state, '.') + strlen(STATE_EXTENSION)); }

static int newest_slot(void) { return rewind_count ? slot(rewinds[rewind_count - 1]) : -1; }

static int by_slot(const struct dirent **left, const struct dirent **right) { return slot((*left)->d_name) - slot((*right)->d_name); }

static void remove_state(const char *state) {
    char path[PATH_MAX];
    unlink(rewind_path(path, active, "/" STATES "/%s", state));
    unlink(rewind_path(path, active, "/" STATES "/%s" SCREEN_EXTENSION, state));
}

static char *slot_directory(char *path, int profile, int slot) {
    snprintf(path, PATH_MAX, PROFILES_DIR "/%d/" STATES "/%s/%d", profile, name(active), slot + 1);
    return path;
}

static int saved_state(int profile, int slot, char *state, time_t *saved) {
    char path[PATH_MAX];
    struct dirent **entries = NULL;
    struct stat status;
    int count = scandir(slot_directory(path, profile, slot), &entries, is_state, alphasort);
    if (count > 0) snprintf(state, NAME_MAX + 1, "%s", entries[0]->d_name);
    for (int i = 0; i < count; i++) free(entries[i]);
    free(entries);
    if (count <= 0) return 0;
    snprintf(path + strlen(path), PATH_MAX - strlen(path), "/%s", state);
    *saved = stat(path, &status) ? 0 : status.st_mtime;
    return 1;
}

static int slot_filled(int profile, int slot) {
    char state[NAME_MAX + 1];
    time_t saved;
    return saved_state(profile, slot, state, &saved);
}

static const char *saved_time(time_t saved) {
    static char text[TEXT_MAX];
    if (!strftime(text, sizeof text, _("%b %d %H:%M"), localtime(&saved))) *text = '\0';
    return text;
}

static const char *save_slot_value(void) {
    static char text[TEXT_MAX];
    char state[NAME_MAX + 1];
    time_t saved;
    int filled = saved_state(guide_profile, save_slot, state, &saved);
    snprintf(text, sizeof text, _("Slot %d  \u00b7  %s"), save_slot + 1, filled ? saved_time(saved) : _("Empty"));
    return text;
}

static void make_directories(char *path) {
    for (char *slash = strchr(path + strlen(PROFILES_DIR), '/'); slash; slash = strchr(slash + 1, '/')) {
        *slash = '\0';
        mkdir(path, DIRECTORY_MODE);
        *slash = '/';
    }
    mkdir(path, DIRECTORY_MODE);
}

static void keep_saved(const char *state) {
    char directory[PATH_MAX], from[PATH_MAX], to[PATH_MAX + NAME_MAX + sizeof SCREEN_EXTENSION];
    remove_tree(slot_directory(directory, saving.profile, saving.slot));
    make_directories(directory);
    snprintf(to, sizeof to, "%s/%s", directory, state);
    if (link(rewind_path(from, active, "/" STATES "/%s", state), to) < 0) perror(to);
    if (saving.screen) {
        cairo_surface_write_to_png(saving.screen, rewind_path(from, active, "/" STATES "/%s" SCREEN_EXTENSION, state));
        snprintf(to, sizeof to, "%s/%s" SCREEN_EXTENSION, directory, state);
        if (link(from, to) < 0) perror(to);
    }
    notify(_("Saved state %d"), saving.slot + 1);
    cairo_surface_destroy(saving.screen);
    saving.screen = NULL;
    saving.game = -1;
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
    rewind_count = 0;
    for (int i = 0; i < count; i++) {
        if (i < count - REWIND_STATES) remove_state(entries[i]->d_name);
        else snprintf(rewinds[rewind_count++], sizeof rewinds[0], "%s", entries[i]->d_name);
        free(entries[i]);
    }
    free(entries);
    if (rewind_back > rewind_count) rewind_back = rewind_count;
    if (pending_screen && rewind_count && slot(rewinds[rewind_count - 1]) > pending_after) {
        cairo_surface_write_to_png(pending_screen, rewind_path(path, active, "/" STATES "/%s" SCREEN_EXTENSION, rewinds[rewind_count - 1]));
        remember_newest(pending_screen);
        pending_screen = NULL;
        paint_needed = 1;
    }
    for (int i = 0; saving.game == active && i < rewind_count; i++)
        if (slot(rewinds[i]) > saving.after) {
            keep_saved(rewinds[i]);
            break;
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
    if (!rewind_back) return _("Now");
    clock_gettime(CLOCK_REALTIME, &now);
    long seconds = stat(rewind_path(path, active, "/" STATES "/%s", rewinds[rewind_count - rewind_back]), &status) ? 0 : now.tv_sec - status.st_mtime;
    snprintf(text, sizeof text, _("%ld:%02ld ago"), seconds / SECONDS_PER_MINUTE, seconds % SECONDS_PER_MINUTE);
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

static const char *string_at(json_object *object, const char *first, const char *second) {
    json_object *field;
    if (!json_object_object_get_ex(object, first, &field)) return "";
    if (second && !json_object_object_get_ex(field, second, &field)) return "";
    const char *text = json_object_get_string(field);
    return text ? text : "";
}

static const char *ui_language(void) {
    const char *chosen = string_at(settings, "language", NULL);
    return *chosen ? chosen : SOURCE_LANGUAGE;
}

static const char *localized_title(json_object *manifest) {
    char tag[NAME_MAX + 1];
    snprintf(tag, sizeof tag, "%s", ui_language());
    for (char *underscore = tag; (underscore = strchr(underscore, '_'));) *underscore = '-';
    const char *title = string_at(manifest, "titles", tag);
    tag[strcspn(tag, "-")] = '\0';
    if (!*title) title = string_at(manifest, "titles", tag);
    return *title ? title : string_at(manifest, "title", NULL);
}

static void scan(const char *directory, int store) {
    DIR *slugs = opendir(directory);
    for (struct dirent *slug; slugs && (slug = readdir(slugs));) {
        char path[PATH_MAX];
        if (slug->d_name[0] == '.' || slug->d_type != DT_DIR) continue;
        snprintf(path, sizeof path, "%s/%s/" MANIFEST, directory, slug->d_name);
        json_object *manifest = json_object_from_file(path);
        const char *title = localized_title(manifest);
        snprintf(path, sizeof path, "%s/%s", directory, slug->d_name);
        DIR *platforms = opendir(path);
        for (struct dirent *platform; platforms && (platform = readdir(platforms));) {
            int index = platform->d_name[0] == '.' || platform->d_type != DT_DIR ? -1 : find_game(slug->d_name, platform->d_name);
            if (index < 0) continue;
            const char *thumbnail = string_at(manifest, platform->d_name, "thumbnail");
            if (strchr(thumbnail, '/')) thumbnail = "";
            if (strcmp(games[index].thumbnail, thumbnail)) games[index].thumbnail_time = 0;
            snprintf(games[index].title, sizeof games[index].title, "%s", *title ? title : slug->d_name);
            snprintf(games[index].thumbnail, sizeof games[index].thumbnail, "%s", thumbnail);
            if (store < 0) games[index].installed = 1;
            else games[index].stores |= 1u << store;
        }
        if (platforms) closedir(platforms);
        json_object_put(manifest);
    }
    if (slugs) closedir(slugs);
}

static void probe_upscalable(void) {
    if (upscalable_probe) { upscalable_wanted = 1; return; }
    upscalable_probe = spawn_reading((char *[]){"games", "upscalable", NULL}, &upscalable_output, log_file);
    if (upscalable_probe < 0) upscalable_probe = 0;
}

static void read_upscalable(void) {
    json_object_put(upscalable);
    upscalable = json_object_from_fd(upscalable_output);
    if (!upscalable) upscalable = json_object_new_object();
    close(upscalable_output);
    upscalable_probe = 0;
    if (upscalable_wanted) {
        upscalable_wanted = 0;
        probe_upscalable();
    }
}

static void start_image_events(void) {
    if (image_events > 0 || access(DOCKER_SOCKET, F_OK)) return;
    image_events = spawn_reading((char *[]){"docker", "events", "--filter", "type=image", NULL}, &image_events_output, log_file);
    if (image_events < 0) image_events = 0;
    probe_upscalable();
}

static void read_image_events(void) {
    char chunk[BUFSIZ];
    if (read(image_events_output, chunk, sizeof chunk) > 0) probe_upscalable();
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

static void forget_thumbnails(void) {
    for (int i = 0; i < wanted_thumbnail_count; i++) free(wanted_thumbnails[i]);
    wanted_thumbnail_count = sent_thumbnail_count = 0;
}

void skin_missing(const char *path) {
    if (strncmp(path, THUMBNAILS_DIR "/", sizeof THUMBNAILS_DIR)) return;
    for (int i = 0; i < wanted_thumbnail_count; i++)
        if (!strcmp(wanted_thumbnails[i], path)) return;
    wanted_thumbnails = realloc(wanted_thumbnails, (wanted_thumbnail_count + 1) * sizeof *wanted_thumbnails);
    wanted_thumbnails[wanted_thumbnail_count++] = strdup(path);
}

static void fetch_thumbnails(void) {
    int count = wanted_thumbnail_count - sent_thumbnail_count;
    if (thumbnail_fetcher > 0 || !count) return;
    char **argv = calloc(count + 3, sizeof *argv);
    argv[0] = "thumbnails";
    argv[1] = "fetch";
    memcpy(argv + 2, wanted_thumbnails + sent_thumbnail_count, count * sizeof *argv);
    thumbnail_fetcher = spawn(argv, -1, log_file, log_file);
    free(argv);
    sent_thumbnail_count = wanted_thumbnail_count;
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
    monitor = spawn_reading((char *[]){"pw-dump", "--monitor", "--no-colors", NULL}, &monitor_output, log_file);
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

static const char *display_value(void) {
    static char value[TEXT_MAX];
    const char *pinned = string_at(settings, "display", NULL);
    snprintf(value, sizeof value, _(AUTO), string_at(display_state, "display", NULL));
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
    snprintf(value, sizeof value, _(AUTO), mode_text(string_at(display_state, "mode", NULL)));
    return *chosen_mode() ? mode_text(chosen_mode()) : value;
}

static int chosen_resolution(const char *platform) { return int_at(list_at(settings, "resolution"), platform); }

static const char *resolution_value(const char *platform) {
    static char value[TEXT_MAX];
    char native[TEXT_MAX];
    json_object *details = NULL;
    json_object_object_get_ex(upscalable, platform, &details);
    snprintf(native, sizeof native, "%dp", int_at(details, "height"));
    int chosen = chosen_resolution(platform);
    if (chosen) snprintf(value, sizeof value, "%dp", chosen);
    else snprintf(value, sizeof value, _(AUTO), native);
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

static void cycle_resolution(int game, int step) {
    char value[TEXT_MAX];
    const char *platform = games[game].platform;
    int current = 0;
    for (int i = 0; i < resolution_count; i++)
        if (resolutions[i] == chosen_resolution(platform)) current = i + 1;
    int next = cycle(current, resolution_count, step);
    snprintf(value, sizeof value, "%s %d", platform, next ? resolutions[next - 1] : 0);
    start((char *[]){"settings", "resolution", value, NULL});
}

static void discard_staged(void) { mode_staged = 0; }

static void apply_staged(void) {
    if (mode_staged) start((char *[]){"settings", "mode", staged_mode, NULL});
    discard_staged();
}

static int chosen_sleep(void) {
    json_object *field;
    return json_object_object_get_ex(settings, "sleep", &field) ? json_object_get_int(field) : sleep_choices[0];
}

static const char *sleep_value(void) {
    static char value[TEXT_MAX];
    if (!chosen_sleep()) return _("Never");
    snprintf(value, sizeof value, _("%d min"), chosen_sleep());
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

static const char *skin_name(void) {
    const char *name = string_at(settings, "skin", NULL);
    return *name ? name : DEFAULT_SKIN;
}

static int by_name(const void *left, const void *right) { return strcmp(left, right); }

static const char *const skin_roots[] = {USER_SKINS_DIR, SKINS_DIR};

static int skin_names(char names[SKINS_MAX][NAME_MAX + 1]) {
    char entry[PATH_MAX];
    int count = 0;
    for (unsigned root = 0; root < sizeof skin_roots / sizeof *skin_roots; root++) {
        struct dirent **folders = NULL;
        int found = scandir(skin_roots[root], &folders, is_visible, NULL);
        for (int i = 0; i < found; i++) {
            int known = 0;
            for (int j = 0; j < count; j++) known |= !strcmp(names[j], folders[i]->d_name);
            snprintf(entry, sizeof entry, "%s/%s/" SKIN_ENTRY, skin_roots[root], folders[i]->d_name);
            if (count < SKINS_MAX && !known && !access(entry, R_OK)) snprintf(names[count++], NAME_MAX + 1, "%s", folders[i]->d_name);
            free(folders[i]);
        }
        free(folders);
    }
    qsort(names, count, sizeof *names, by_name);
    return count;
}

static void cycle_skin(int step) {
    char names[SKINS_MAX][NAME_MAX + 1];
    int count = skin_names(names), current = 0;
    for (int i = 0; i < count; i++)
        if (!strcmp(names[i], skin_name())) current = i;
    if (count) start((char *[]){"settings", "skin", names[(current + step + count) % count], NULL});
}

static int language_index(const char *language) {
    for (int i = 0; i < language_count; i++)
        if (!strcmp(languages[i], language)) return i;
    return 0;
}

static const char *game_language(void) { return string_at(settings, "game-language", NULL); }

static const char *effective_game_language(void) { return *game_language() ? game_language() : ui_language(); }

static const char *game_language_value(void) { return *game_language() ? language_names[language_index(game_language())] : _("Same as UI"); }

static void cycle_language(int step) {
    start((char *[]){"settings", "language", languages[cycle(language_index(ui_language()), language_count - 1, step)], NULL});
}

static void cycle_game_language(int step) {
    int next = cycle(*game_language() ? language_index(game_language()) + 1 : 0, language_count, step);
    start((char *[]){"settings", "game-language", next ? languages[next - 1] : "", NULL});
}

static void use_language(const char *language) {
    setenv("LANGUAGE", language, 1);
    ++_nl_msg_cat_cntr;
}

static int has_catalog(const struct dirent *entry) {
    char path[PATH_MAX];
    snprintf(path, sizeof path, LOCALE_DIR "/%s" CATALOG, entry->d_name);
    return is_visible(entry) && !access(path, R_OK);
}

static void find_languages(void) {
    struct dirent **entries = NULL;
    int count = scandir(LOCALE_DIR, &entries, has_catalog, alphasort);
    if (!setlocale(LC_MESSAGES, MESSAGES_LOCALE)) fprintf(stderr, "overlay: no %s locale, so the UI stays in English\n", MESSAGES_LOCALE);
    bindtextdomain(TEXT_DOMAIN, LOCALE_DIR);
    bind_textdomain_codeset(TEXT_DOMAIN, "UTF-8");
    textdomain(TEXT_DOMAIN);
    snprintf(languages[language_count++], sizeof languages[0], "%s", SOURCE_LANGUAGE);
    for (int i = 0; i < count; i++) {
        if (language_count < LANGUAGES_MAX) snprintf(languages[language_count++], sizeof languages[0], "%s", entries[i]->d_name);
        free(entries[i]);
    }
    free(entries);
    for (int i = 0; i < language_count; i++) {
        use_language(languages[i]);
        snprintf(language_names[i], sizeof language_names[0], "%s", _("English"));
    }
}

static int by_text(const void *left, const void *right) { return strcmp(*(const char *const *)left, *(const char *const *)right); }

static void find_time_zones(void) {
    FILE *table = fopen(ZONE_TABLE, "r");
    char *line = NULL;
    size_t size = 0;
    if (!table) {
        perror(ZONE_TABLE);
        return;
    }
    while (getline(&line, &size, table) >= 0) {
        char *fields = line;
        if (*line == '#') continue;
        for (int column = 0; column < ZONE_COLUMN; column++) strsep(&fields, "\t");
        const char *zone = strsep(&fields, "\t\n");
        if (!zone || !*zone) continue;
        time_zones = realloc(time_zones, (time_zone_count + 1) * sizeof *time_zones);
        time_zones[time_zone_count++] = strdup(zone);
    }
    free(line);
    fclose(table);
    qsort(time_zones, time_zone_count, sizeof *time_zones, by_text);
}

static const char *time_zone(void) { return string_at(settings, "time-zone", NULL); }

static int time_zone_index(void) {
    for (int i = 0; i < time_zone_count; i++)
        if (!strcmp(time_zones[i], time_zone())) return i;
    return -1;
}

static int area_length(const char *zone) { return strcspn(zone, "/"); }

static int same_area(const char *zone, const char *other) { return area_length(zone) == area_length(other) && !strncmp(zone, other, area_length(zone)); }

static int area_start(int zone) { return !zone || !same_area(time_zones[zone - 1], time_zones[zone]); }

static const char *area_value(void) {
    static char value[TEXT_MAX];
    if (chosen_zone < 0) return "UTC";
    snprintf(value, sizeof value, "%.*s", area_length(time_zone()), time_zone());
    return value;
}

static const char *city_value(void) {
    static char value[TEXT_MAX];
    char offset[TEXT_MAX];
    time_t now = time(NULL);
    if (!strftime(offset, sizeof offset, "UTC%z", localtime(&now))) *offset = '\0';
    int length = snprintf(value, sizeof value, "%s", time_zone() + area_length(time_zone()) + 1);
    for (char *space = value; (space = strchr(space, '_'));) *space = ' ';
    snprintf(value + length, sizeof value - length, "  \u00b7  %s", offset);
    return value;
}

static void choose_time_zone(const char *zone) { start((char *[]){"settings", "time-zone", (char *)zone, NULL}); }

static void cycle_time_zone(int step) {
    int areas = 0, current = 0, firsts[time_zone_count + 1];
    for (int i = 0; i < time_zone_count; i++) {
        if (!area_start(i)) continue;
        firsts[++areas] = i;
        if (chosen_zone >= 0 && same_area(time_zones[i], time_zones[chosen_zone])) current = areas;
    }
    int next = cycle(current, areas, step);
    choose_time_zone(next ? time_zones[firsts[next]] : "");
}

static void cycle_city(int step) {
    int first = chosen_zone, count = 0;
    while (!area_start(first)) first--;
    while (first + count < time_zone_count && same_area(time_zones[first], time_zones[first + count])) count++;
    choose_time_zone(time_zones[first + (chosen_zone - first + step + count) % count]);
}

static void use_time_zone(void) {
    if (*time_zone()) setenv("TZ", time_zone(), 1);
    else unsetenv("TZ");
    tzset();
    chosen_zone = time_zone_index();
}

static enum picture chosen_picture(void) {
    const char *chosen = string_at(settings, "picture", NULL);
    for (unsigned picture = 0; picture < PICTURES; picture++)
        if (!strcmp(chosen, pictures[picture].name)) return picture;
    return ORIGINAL;
}

static void cycle_picture(int step) {
    start((char *[]){"settings", "picture", (char *)pictures[(chosen_picture() + step + PICTURES) % PICTURES].name, NULL});
}

static char *skin_folder(const char *name, char folder[PATH_MAX]) {
    char entry[PATH_MAX];
    for (unsigned root = 0; root < sizeof skin_roots / sizeof *skin_roots; root++) {
        snprintf(folder, PATH_MAX, "%s/%.*s", skin_roots[root], NAME_MAX, name);
        snprintf(entry, sizeof entry, "%s/%.*s/" SKIN_ENTRY, skin_roots[root], NAME_MAX, name);
        if (!access(entry, R_OK)) break;
    }
    return folder;
}

static void load_skin(void) {
    char folder[PATH_MAX];
    const char *const choices[] = {skin_name(), DEFAULT_SKIN};
    snprintf(wanted_skin, sizeof wanted_skin, "%s", skin_name());
    scene_crossfade(skin_now());
    *loaded_skin = '\0';
    for (unsigned i = 0; i < sizeof choices / sizeof *choices && !*loaded_skin; i++)
        if (script_load(skin_folder(choices[i], folder))) snprintf(loaded_skin, sizeof loaded_skin, "%s", choices[i]);
    if (skin_state) script_state(skin_state);
    paint_needed = changed = 1;
}

static void read_settings(void) {
    json_object_put(settings);
    settings = json_object_from_file(SETTINGS_FILE);
    if (strcmp(skin_name(), wanted_skin)) load_skin();
    use_language(ui_language());
    use_time_zone();
    changed = 1;
}

static void read_display(void) {
    char previous[TEXT_MAX];
    snprintf(previous, sizeof previous, "%s", string_at(display_state, "pending", NULL));
    json_object_put(display_state);
    display_state = json_object_from_file(DISPLAY_FILE);
    const char *pending = string_at(display_state, "pending", NULL);
    if (*pending && strcmp(pending, previous)) notify(_("Close and restart your games to render on %s"), pending);
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
    int length = snprintf(text, sizeof text, "%s", !strcmp(link, "bluetooth") ? _("Bluetooth") : !strcmp(link, "wired") ? _("Wired") : _("Not connected"));
    if (json_object_object_get_ex(controller, "battery", &battery) && battery)
        length += snprintf(text + length, sizeof text - length, "  %d%%", json_object_get_int(battery));
    if (int_at(controller, "charging")) snprintf(text + length, sizeof text - length, _(" charging"));
    return text;
}

static const char *profile_name(int profile) {
    static char text[TEXT_MAX];
    snprintf(text, sizeof text, _("Player %d"), profile);
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
        if (!strcmp(mode_labels[i].name, mode)) return _(mode_labels[i].label);
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
    if (game->job && game->percent < 0) return _(game->job_text);
    if (game->job) {
        snprintf(text, sizeof text, "%s %d%%", _(game->job_text), game->percent);
        return text;
    }
    if (game->pid && !game_window(index)) return _(tasks[LAUNCHING]);
    if (game->pid) return game->paused ? _("paused") : _("running");
    return game->installed ? _("installed") : "";
}

static int in_game(void) { return active >= 0 && !dashboard_open; }

static enum tab first_tab(void) { return in_game() ? GAME_TAB : DEVICES_TAB; }

static enum tab tab_of(struct view *view) { return view->tab < first_tab() ? first_tab() : view->tab; }

static void write_json(const char *path, json_object *object) {
    char partial[PATH_MAX + sizeof PARTIAL];
    snprintf(partial, sizeof partial, "%s" PARTIAL, path);
    if (json_object_to_file_ext(partial, object, JSON_C_TO_STRING_PLAIN) < 0 || rename(partial, path) < 0) perror(path);
}

static const char *choices_path(int profile, const char *file) {
    static char path[PATH_MAX];
    snprintf(path, sizeof path, PROFILES_DIR "/%d/%s", profile, file);
    return path;
}

static json_object *choices_of(json_object **cache, int profile, const char *file) {
    if (!cache[profile]) cache[profile] = json_object_from_file(choices_path(profile, file));
    if (!cache[profile]) cache[profile] = json_object_new_object();
    return cache[profile];
}

static json_object *emulate_of(int profile) { return choices_of(emulate_choices, profile, EMULATE_FILE); }

static json_object *analog_of(int profile) { return choices_of(analog_choices, profile, ANALOG_CHOICES_FILE); }

static void choose(json_object **cache, int profile, const char *file, const char *outer, const char *inner, const char *value) {
    json_object *choices = choices_of(cache, profile, file), *chosen;
    if (!json_object_object_get_ex(choices, outer, &chosen)) {
        chosen = json_object_new_object();
        json_object_object_add(choices, outer, chosen);
    }
    if (*value) json_object_object_add(chosen, inner, json_object_new_string(value));
    else json_object_object_del(chosen, inner);
    mkdir(choices_path(profile, ""), DIRECTORY_MODE);
    write_json(choices_path(profile, file), choices);
}

static const char *combination(int profile) {
    static char text[INPUT_DEVICES_MAX * TEXT_MAX];
    const char *identities[INPUT_DEVICES_MAX];
    int count = 0, length = 0;
    for (int i = 0; i < member_count; i++)
        if (members[i].profile == profile && device_of(members[i].device)) identities[count++] = string_at(device_of(members[i].device), "identity", NULL);
    qsort(identities, count, sizeof *identities, by_text);
    text[0] = '\0';
    for (int i = 0; i < count; i++) length += snprintf(text + length, sizeof text - length, "%s%s", i ? "+" : "", identities[i]);
    return text;
}

static const char *analog_override(int profile, int game) { return string_at(json_object_object_get(analog_of(profile), games[game].platform), combination(profile), NULL); }

static int analog_auto(int profile, int game) {
    json_object *toggle;
    int seat = seat_of(profile);
    if (json_object_object_get_ex(games[game].analog, "toggle", &toggle) && json_object_object_get_ex(toggle, "button", NULL))
        return seat >= 0 && games[game].ports[seat].analog > 0;
    if (!json_object_get_boolean(json_object_object_get(games[game].analog, "sticks"))) return 0;
    for (int i = 0; i < member_count; i++)
        if (members[i].profile == profile && json_object_get_boolean(json_object_object_get(device_of(members[i].device), "sticks"))) return 1;
    return 0;
}

static int analog_wanted(int profile, int game) {
    const char *chosen = analog_override(profile, game);
    return *chosen ? !strcmp(chosen, "on") : analog_auto(profile, game);
}

static const char *analog_value(void) {
    static char value[TEXT_MAX];
    const char *chosen = analog_override(guide_profile, active);
    if (*chosen) return !strcmp(chosen, "on") ? _("On") : _("Off");
    snprintf(value, sizeof value, _(AUTO), analog_auto(guide_profile, active) ? _("On") : _("Off"));
    return value;
}

static int analog_row(int game) { return games[game].analog && !*string_at(games[game].emulate, "pad", NULL); }

static const char *analog_mode(int profile, int game) {
    return json_object_object_get_ex(games[game].analog, "toggle", NULL) || analog_wanted(profile, game) ? "analog" : "digital";
}

static const char *default_mode(int game, json_object *device) {
    const char *mode = string_at(games[game].emulate, string_at(device, "kind", NULL), NULL);
    return mode_index(device, mode) < 0 ? string_at(device, "native", NULL) : mode;
}

static const char *mode_for(int profile, int game, json_object *device) {
    if (game < 0) return string_at(device, "native", NULL);
    if (analog_row(game) && !strcmp(string_at(device, "kind", NULL), "pad")) return analog_mode(profile, game);
    const char *chosen = string_at(emulate_of(profile), key(game), string_at(device, "kind", NULL));
    return mode_index(device, chosen) < 0 ? default_mode(game, device) : chosen;
}

static const char *emulate_value(void) {
    static char value[TEXT_MAX];
    json_object *device = device_of(focus);
    const char *chosen = string_at(emulate_of(guide_profile), key(active), kind_of(focus));
    if (mode_index(device, chosen) >= 0) return mode_label(chosen);
    snprintf(value, sizeof value, _("Default (%s)"), mode_label(default_mode(active, device)));
    return value;
}

static void send_command(int index, const char *command) {
    if (dprintf(games[index].commands, "%s\n", command) < 0) perror(command);
}

static void set_core_option(int index, const char *option, const char *value) {
    char command[TEXT_MAX];
    snprintf(command, sizeof command, "SET_CORE_OPTION %s %s", option, value);
    send_command(index, command);
}

static void press_analog(int index, int seat, json_object *toggle) {
    json_object *forced;
    if (!json_object_object_get_ex(toggle, "forced", &forced)) {
        tell_padd("tap %d %s", seat + 1, string_at(toggle, "button", NULL));
        return;
    }
    struct port *port = &games[index].ports[seat];
    port->forced = !port->forced;
    set_core_option(index, json_object_get_string(json_object_array_get_idx(forced, seat)), port->forced ? "true" : "false");
}

static void converge_analog(int index) {
    struct game *game = &games[index];
    json_object *toggle;
    if (index < 0 || !game->pid || !json_object_object_get_ex(game->analog, "toggle", &toggle)) return;
    for (int seat = 0; seat < player_count; seat++) {
        struct port *port = &game->ports[seat];
        if (port->analog < 0 || port->analog == analog_wanted(players[seat], index) || port->locked || port->pending) continue;
        port->pending = 1;
        press_analog(index, seat, toggle);
    }
}

static void read_analog(int index) {
    char path[PATH_MAX], line[TEXT_MAX], mode[WORD_MAX + 1], lock[WORD_MAX + 1];
    int port;
    FILE *file = fopen(rewind_path(path, index, "/" STATES "/" ANALOG_FILE), "re");
    if (!file) { perror(path); return; }
    while (fgets(line, sizeof line, file))
        if (sscanf(line, "%d %" STRINGIFY(WORD_MAX) "s %" STRINGIFY(WORD_MAX) "s", &port, mode, lock) == 3 && port >= 1 && port <= PADS) {
            games[index].ports[port - 1] = (struct port){.analog = !strcmp(mode, "on"), .locked = !strcmp(lock, "locked"), .forced = games[index].ports[port - 1].forced};
        }
    fclose(file);
    if (index == active) converge_analog(index);
}

static const char *dial_mode(void) {
    if (active < 0 || !*string_at(games[active].analog, "dial", NULL)) return "off";
    return games[active].dial_absolute ? "absolute" : "relative";
}

static void turn_dial(const char *mode) {
    if (active < 0 || !games[active].pid || !*string_at(games[active].analog, "dial", NULL)) return;
    games[active].dial_absolute = !strcmp(mode, "absolute");
    set_core_option(active, string_at(games[active].analog, "dial", NULL), games[active].dial_absolute ? "enabled" : "disabled");
    tell_padd("dial %s", dial_mode());
}

static void apply_modes(void) {
    if (active != applied_game) tell_padd("game %s", active >= 0 ? key(active) : NO_GAME);
    tell_padd("dial %s", dial_mode());
    applied_game = active;
    for (int i = 0; i < member_count; i++) {
        json_object *device = device_of(members[i].device);
        if (device) tell_padd("mode %d %s", members[i].device, mode_for(members[i].profile, active, device));
    }
    if (active >= 0) converge_analog(active);
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

static void read_controllers(void) {
    int dropped = 0;
    json_object_put(controllers_state);
    controllers_state = json_object_from_file(CONTROLLERS_FILE);
    for (int i = member_count - 1; i >= 0; i--)
        if (!device_of(members[i].device)) {
            drop_member(i);
            dropped = 1;
        }
    if (dropped) apply_modes();
    if (!device_of(picker)) picker = NO_DEVICE;
    changed = 1;
}

static void cycle_mode(int step) {
    json_object *device = device_of(focus), *modes = list_at(device, "modes");
    int current = mode_index(device, string_at(emulate_of(guide_profile), key(active), kind_of(focus))) + 1;
    int next = cycle(current, length_of(modes), step);
    choose(emulate_choices, guide_profile, EMULATE_FILE, key(active), kind_of(focus), next ? json_object_get_string(json_object_array_get_idx(modes, next - 1)) : "");
    apply_modes();
}

static void cycle_analog(int step) {
    static const char *const choices[] = {"", "on", "off"};
    const char *chosen = analog_override(guide_profile, active);
    int current = 0;
    for (unsigned i = 1; i < sizeof choices / sizeof *choices; i++)
        if (!strcmp(chosen, choices[i])) current = i;
    choose(analog_choices, guide_profile, ANALOG_CHOICES_FILE, games[active].platform, combination(guide_profile), choices[cycle(current, sizeof choices / sizeof *choices - 1, step)]);
    apply_modes();
}

static int owned_games(int profile) {
    int count = 0;
    for (int i = 0; i < game_count; i++) count += games[i].owner == profile && games[i].pid;
    return count;
}

static int rewindable(void) { return in_game() && games[active].rewinds; }

static int add_picture_row(int count) { return add_row(count, CHOOSE_PICTURE, 0, _("Display Mode"), "%s", _(pictures[chosen_picture()].label)); }

static int add_display_mode_rows(int count) {
    count = add_row(count, CHOOSE_DISPLAY_MODE, 0, _("Display resolution"), "%s", display_mode_value());
    return mode_staged ? add_row(count, APPLY_MODE, 0, _("Apply"), "") : count;
}

static int upscalable_game(const char *platform) {
    for (int i = 0; i < game_count; i++)
        if (games[i].installed && (!platform || !strcmp(games[i].platform, platform)) && json_object_object_get_ex(upscalable, games[i].platform, NULL)) return i;
    return -1;
}

static int add_platform_resolution_rows(int count) {
    json_object_object_foreach(upscalable, platform, details) {
        int game = upscalable_game(platform);
        if (game >= 0) count = add_row(count, CHOOSE_RESOLUTION, game, string_at(details, "name", NULL), "%s", resolution_value(platform));
    }
    return count;
}

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
            count = add_row(count, OPEN_STORE, 0, _("Store"), "");
            count = add_row(count, OPEN_SETTINGS, 0, _("Settings"), "");
        }
        break;
    case GAME:
        if (game->pid) {
            count = add_row(count, RESUME, 0, _("Resume"), "");
            count = add_row(count, CLOSE, 0, _("Close"), "");
        } else if (game->job) {
            count = add_row(count, NOTHING, 0, status(view->game), "");
        } else if (game->installed) {
            count = add_row(count, PLAY, 0, _("Play"), "");
            count = add_row(count, UNINSTALL, 0, _("Uninstall"), "");
        } else {
            for (int store = 0; store < store_count; store++)
                if (game->stores & (1u << store)) count = add_row(count, INSTALL, store, _("Install from"), "%s", store_label(store));
        }
        break;
    case GUIDE:
        if (tab_of(view) == GAME_TAB) {
            count = add_row(count, SHOW_DASHBOARD, 0, _("Dashboard"), "");
            if (rewindable()) count = add_row(count, REWIND, 0, _("Rewind"), "%s", rewind_age());
            if (rewindable() && guide_profile) count = add_row(count, SAVE_STATES, 0, _("Save States"), "%s", save_slot_value());
            if (guide_profile && analog_row(active)) count = add_row(count, ANALOG, 0, _("Analog"), "%s", analog_value());
            else if (guide_profile && !games[active].analog) count = add_row(count, EMULATE, 0, _("Emulate"), "%s", emulate_value());
            if (games[active].second_screen) count = add_row(count, SECOND_SCREEN, 0, _("Second screen"), "%s", games[active].screen_shown ? _("Inset") : _("Off"));
            count = add_picture_row(count);
            if (json_object_object_get_ex(upscalable, games[active].platform, NULL)) count = add_row(count, CHOOSE_RESOLUTION, active, _("Game resolution"), "%s", resolution_value(games[active].platform));
            break;
        }
        if (tab_of(view) == SYSTEM_TAB) {
            count = add_display_mode_rows(count);
            count = add_row(count, RELOAD, 0, _("Reload"), "");
            count = add_row(count, RESTART, 0, _("Restart"), "");
            count = add_row(count, POWER_OFF, 0, _("Power off"), "");
            count = add_row(count, LOG_OUT_PLAYER, 0, _("Log out"), "");
            break;
        }
        if (volume >= 0) snprintf(level, sizeof level, "%d%%", volume);
        count = add_row(count, VOLUME, 0, _("Volume"), "%s", level);
        count = add_row(count, OPEN_OUTPUTS, 0, _("Output"), "%s", chosen(outputs, output_count, default_output));
        count = add_row(count, OPEN_MICROPHONES, 0, _("Microphone"), "%s", chosen(microphones, microphone_count, default_microphone));
        if (microphone_is_muted()) snprintf(microphone_level, sizeof microphone_level, "%s", _("Muted"));
        else if (microphone_volume >= 0) snprintf(microphone_level, sizeof microphone_level, "%d%%", microphone_volume);
        count = add_row(count, MICROPHONE_VOLUME, 0, _("Microphone volume"), "%s", microphone_level);
        count = add_row(count, OPEN_CONTROLLERS, 0, _("Controllers"), "");
        break;
    case WHOIS:
        for (int profile = 1; profile <= PROFILES; profile++) {
            if (seat_of(profile) < 0) count = add_row(count, LOGIN, profile, profile_name(profile), "");
            else count = add_row(count, LOGIN, profile, profile_name(profile), _("Pad %d"), seat_of(profile) + 1);
        }
        break;
    case LOG_OUT:
        count = add_row(count, CONFIRM_LOG_OUT, 0, _("Log out"), ngettext("Closes %d game", "Closes %d games", owned_games(guide_profile)), owned_games(guide_profile));
        break;
    case ATTRACT:
        break;
    case DELETE_DIALOG:
        if (rewindable()) count = add_row(count, DELETE_STATE, 0, _("Delete"), "");
        break;
    case OVERWRITE_DIALOG:
        if (rewindable()) count = add_row(count, OVERWRITE_STATE, 0, _("Overwrite"), "");
        break;
    case OUTPUTS:
    case MICROPHONES:
        for (int i = 0, total = view->screen == OUTPUTS ? output_count : microphone_count; i < total; i++) {
            struct device *device = view->screen == OUTPUTS ? &outputs[i] : &microphones[i];
            const char *current = view->screen == OUTPUTS ? default_output : default_microphone;
            count = add_row(count, SET_DEVICE, device->id, device->name, !strcmp(device->node, current) ? _("selected") : "");
        }
        break;
    case CONTROLLERS:
        if (int_at(controllers_state, "bluetooth"))
            count = add_row(count, PAIR, 0, _("Pair new controller"), "%s", int_at(controllers_state, "pairing") ? _("Searching") : "");
        for (int i = 0; i < controller_count(); i++)
            count = add_row(count, int_at(controller_at(i), "paired") ? OPEN_CONTROLLER : NOTHING, i, string_at(controller_at(i), "name", NULL),
                            "%s", controller_status(controller_at(i)));
        break;
    case CONTROLLER:
        count = add_row(count, FORGET, 0, _("Forget"), "");
        break;
    case SETTINGS:
        count = add_row(count, CHOOSE_DISPLAY, 0, _("Display"), "%s", display_value());
        count = add_display_mode_rows(count);
        if (upscalable_game(NULL) >= 0) count = add_row(count, OPEN_RESOLUTIONS, 0, _("Game resolution"), "");
        count = add_picture_row(count);
        count = add_row(count, CHOOSE_SLEEP, 0, _("Controller sleep"), "%s", sleep_value());
        count = add_row(count, CHOOSE_SKIN, 0, _("Skin"), "%s", skin_name());
        count = add_row(count, CHOOSE_LANGUAGE, 0, _("Language"), "%s", language_names[language_index(ui_language())]);
        count = add_row(count, CHOOSE_GAME_LANGUAGE, 0, _("Game language"), "%s", game_language_value());
        count = add_row(count, CHOOSE_TIME_ZONE, 0, _("Time zone"), "%s", area_value());
        if (chosen_zone >= 0) count = add_row(count, CHOOSE_CITY, 0, _("City"), "%s", city_value());
        count = add_row(count, CLEAR_THUMBNAILS, 0, _("Clear thumbnails"), "");
        break;
    case PLATFORM_RESOLUTIONS:
        count = add_platform_resolution_rows(count);
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
    if (view->screen == WHOIS) return player_count ? _("Who is using this controller?") : _("Who's playing?");
    if (view->screen == LOG_OUT) snprintf(text, sizeof text, _("Log out %s"), profile_name(guide_profile));
    else if (view->screen == DELETE_DIALOG) snprintf(text, sizeof text, _("Delete save state %d?"), save_slot + 1);
    else if (view->screen == OVERWRITE_DIALOG) snprintf(text, sizeof text, _("Overwrite save state %d?"), save_slot + 1);
    else if (view->screen == GUIDE && guide_profile) snprintf(text, sizeof text, _("%s  \u00b7  Pad %d"), profile_name(guide_profile), seat_of(guide_profile) + 1);
    else return *screens[view->screen].title ? _(screens[view->screen].title) : "";
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

static void select_row(struct view *view, int selected) {
    view->selected = selected;
    view->action = rows[selected].action;
    view->arg = rows[selected].arg;
}

static void show_game(int index) {
    if (game_window(index)) zwlr_foreign_toplevel_handle_v1_activate(game_window(index)->handle, seat);
}

static int launching(void) { return active >= 0 && !game_window(active); }

static int bind_socket(const char *path, int flags) {
    struct sockaddr_un address = {.sun_family = AF_UNIX};
    int fd = -1;
    if (snprintf(address.sun_path, sizeof address.sun_path, "%s", path) >= (int)sizeof address.sun_path) {
        errno = ENAMETOOLONG;
    } else {
        unlink(path);
        fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | flags, 0);
        if (fd >= 0 && (bind(fd, (struct sockaddr *)&address, sizeof address) < 0 || listen(fd, SOMAXCONN) < 0)) {
            close(fd);
            fd = -1;
        }
    }
    if (fd < 0) perror(path);
    return fd;
}

static int listen_socket(int index, char *directory, size_t size) {
    char path[PATH_MAX + sizeof WAYLAND_SOCKET];
    int closer[2];
    snprintf(directory, size, SOCKETS_DIR "/%s", name(index));
    snprintf(path, sizeof path, "%s/" WAYLAND_SOCKET, directory);
    mkdir(directory, DIRECTORY_MODE);
    int fd = bind_socket(path, 0);
    if (fd < 0) return 0;
    if (pipe2(closer, O_CLOEXEC) < 0) {
        perror(path);
        close(fd);
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
    return open(path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, S_IRUSR | S_IWUSR);
}

static void resume(int index) {
    if (index != active) {
        cairo_surface_destroy(pending_screen);
        pending_screen = NULL;
    }
    show_game(index);
    active = index;
    dashboard[0].action = OPEN_GAME;
    dashboard[0].arg = index;
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
    game->pid = 0;
    if (game->restart == CLOSING) return;
    game->restart = PLAYING;
    if (!game->closing && status) {
        fail(index, _("%s stopped"), game->title);
        inotify_rm_watch(watcher, game->states_watch);
        keep_rewinds(name(index));
    } else remove_rewinds(index);
    if (saving.game == index) saving.game = -1;
    if (active == index) active = -1;
    refresh_rewinds();
    apply_modes();
}

static void prepare_rewinds(int index, char *states) {
    char path[PATH_MAX];
    if (games[index].restart == PLAYING) remove_rewinds(index);
    mkdir(rewind_path(path, index, ""), S_IRWXU);
    mkdir(rewind_path(states, index, "/" STATES), S_IRWXU);
    games[index].states_watch = inotify_add_watch(watcher, states, IN_CLOSE_WRITE | IN_MOVED_TO);
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

static int save_state(void) {
    int newest = newest_slot();
    send_command(active, "SAVE_STATE");
    return newest;
}

static void end_capture(int captured) {
    cairo_surface_t *screen = captured ? captured_screen() : NULL;
    if (capture.save && capture.game == active && screen && changed_since_save(screen)) {
        cairo_surface_destroy(pending_screen);
        pending_screen = cairo_surface_reference(screen);
        pending_after = save_state();
    }
    if (capture.current && screen) scene_image(CURRENT_SCREEN, screen);
    if (capture.current) {
        cairo_surface_destroy(current_screen);
        current_screen = cairo_surface_reference(screen);
        guide_shown = 1;
    }
    cairo_surface_destroy(screen);
    if (capture.frame) ext_image_copy_capture_frame_v1_destroy(capture.frame);
    if (capture.buffer) wl_buffer_destroy(capture.buffer);
    if (capture.bo) gbm_bo_destroy(capture.bo);
    ext_image_copy_capture_session_v1_destroy(capture.session);
    wl_array_release(&capture.modifiers);
    capture = (struct capture){0};
    changed = 1;
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
    size_t modifier_count = capture.modifiers.size / sizeof(uint64_t);
    uint64_t *advertised = capture.modifiers.data;
    uint64_t modifier = DRM_FORMAT_MOD_LINEAR;
    capture.bo = gbm && modifier_count ? gbm_bo_create_with_modifiers2(gbm, capture.width, capture.height, DRM_FORMAT_XRGB8888, advertised, modifier_count,
                                                                       GBM_BO_USE_RENDERING)
                                       : NULL;
    if (capture.bo) modifier = gbm_bo_get_modifier(capture.bo);
    else if (gbm && modifier_count == 1 && advertised[0] == DRM_FORMAT_MOD_LINEAR)
        capture.bo = gbm_bo_create(gbm, capture.width, capture.height, DRM_FORMAT_XRGB8888, GBM_BO_USE_RENDERING | GBM_BO_USE_LINEAR);
    if (!capture.bo) {
        fprintf(stderr, "overlay: no capture buffer\n");
        end_capture(0);
        return;
    }
    struct zwp_linux_buffer_params_v1 *params = zwp_linux_dmabuf_v1_create_params(dmabuf);
    int fd = gbm_bo_get_fd(capture.bo);
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

static void load_slot(int index, int number) {
    char command[TEXT_MAX];
    snprintf(command, sizeof command, "LOAD_STATE_SLOT %d", number);
    send_command(index, command);
}

static void load_rewind(void) {
    int chosen = rewind_count - rewind_back;
    if (!rewind_back) return;
    load_slot(active, slot(rewinds[chosen]));
    for (int i = chosen + 1; i < rewind_count; i++) remove_state(rewinds[i]);
    rewind_back = 0;
    resume(active);
}

static void save_to_slot(void) {
    cairo_surface_destroy(saving.screen);
    saving.game = active;
    saving.profile = guide_profile;
    saving.slot = save_slot;
    saving.screen = cairo_surface_reference(current_screen);
    saving.after = save_state();
    resume(active);
}

static void load_saved(void) {
    char state[NAME_MAX + 1], directory[PATH_MAX], from[PATH_MAX + NAME_MAX + 1], to[PATH_MAX];
    time_t saved;
    if (!saved_state(guide_profile, save_slot, state, &saved)) return;
    snprintf(from, sizeof from, "%s/%s", slot_directory(directory, guide_profile, save_slot), state);
    strrchr(state, '.')[strlen(STATE_EXTENSION)] = '\0';
    unlink(rewind_path(to, active, "/" STATES "/%s", state));
    if (link(from, to) < 0) perror(to);
    send_command(active, "LOAD_STATE_SLOT " STRINGIFY(UNNUMBERED_SLOT));
    rewind_back = 0;
    resume(active);
}

static void restart_active(void) {
    if (!rewindable() || games[active].restart || games[active].closing) return;
    games[active].restart = SAVING;
    games[active].restart_slot = save_state();
    resume(active);
}

static int running_games(void) {
    int count = 0;
    for (int i = 0; i < game_count; i++) count += games[i].pid != 0;
    return count;
}

static void rescale_active(void) {
    char states[PATH_MAX];
    start((char *[]){"games", "rescale", games[active].platform, rewind_path(states, active, "/" STATES), NULL});
    restart_active();
}

static void apply_settings(void) {
    const char *platform = active >= 0 && games[active].pid ? games[active].platform : NULL;
    int was_enhanced = chosen_picture() == ENHANCED, was_resolution = platform ? chosen_resolution(platform) : 0;
    char was_language[NAME_MAX + 1], was_game_language[NAME_MAX + 1];
    snprintf(was_language, sizeof was_language, "%s", ui_language());
    snprintf(was_game_language, sizeof was_game_language, "%s", effective_game_language());
    read_settings();
    if ((chosen_picture() == ENHANCED) != was_enhanced && in_game() && games[active].enhanced) restart_active();
    if (platform && chosen_resolution(platform) != was_resolution) rescale_active();
    if (strcmp(ui_language(), was_language)) rescan();
    if (strcmp(effective_game_language(), was_game_language) && running_games())
        notify(_("Restart your games to play them in %s"), language_names[language_index(effective_game_language())]);
}

static void press_save_states(enum command command) {
    int filled = slot_filled(guide_profile, save_slot);
    if (command == X_BUTTON && filled) push(OVERWRITE_DIALOG, -1);
    else if (command == X_BUTTON) save_to_slot();
    else if (command == Y_BUTTON && filled) push(DELETE_DIALOG, -1);
}

static void freeze(int index, int pause) {
    struct game *game = &games[index];
    game->paused = pause;
    game->freezer = spawn((char *[]){"games", pause ? "pause" : "resume", game->slug, game->platform, NULL}, -1, log_file, log_file);
    if (game->freezer < 0) game->freezer = 0;
}

static void read_labels(int index) {
    char path[PATH_MAX];
    struct game *game = &games[index];
    snprintf(path, sizeof path, ENGINE_RUN "/%s" LABELS_EXTENSION, game->platform);
    json_object *labels = json_object_from_file(path), *manifest, *platform = NULL, *overrides = NULL;
    game->rewinds = *string_at(labels, REWIND_LABEL, NULL);
    game->second_screen = *string_at(labels, SCREEN_LABEL, NULL);
    screen_path(index, path);
    game->screen_shown = !access(path, F_OK);
    json_object_put(game->analog);
    game->analog = json_tokener_parse(string_at(labels, ANALOG_LABEL, NULL));
    json_object_put(game->emulate);
    game->emulate = json_object_new_object();
    json_object_put(labels);
    snprintf(path, sizeof path, GAMES_DIR "/%s/" MANIFEST, game->slug);
    manifest = json_object_from_file(path);
    if (json_object_object_get_ex(manifest, game->platform, &platform) && json_object_object_get_ex(platform, "emulate", &overrides)) {
        json_object_object_foreach(overrides, kind, mode) json_object_object_add(game->emulate, kind, json_object_get(mode));
    }
    game->enhanced = json_object_object_get_ex(platform, pictures[ENHANCED].name, NULL);
    json_object_put(manifest);
}

static void finish_close(int index) {
    struct game *game = &games[index];
    if (game->freezer) return;
    if (game->paused) freeze(index, 0);
    if (game->freezer) return;
    if (kill(game->pid, SIGTERM) < 0) perror("close");
    start((char *[]){"games", "close", game->slug, game->platform, NULL});
}

static void play(int index) {
    char directory[PATH_MAX], states[PATH_MAX], owner[TEXT_MAX], resume_slot[TEXT_MAX] = "";
    int commands[2];
    struct game *game = &games[index];
    if (!listen_socket(index, directory, sizeof directory)) { fail(index, _("Could not start %s"), game->title); return; }
    if (pipe2(commands, O_CLOEXEC) < 0) { perror("pipe"); close(game->context); fail(index, _("Could not start %s"), game->title); return; }
    int output = game_log(index);
    prepare_rewinds(index, states);
    if (game->restart == CLOSING) snprintf(resume_slot, sizeof resume_slot, "%d", newest_slot());
    game->restart = PLAYING;
    memset(guide_depths, 0, sizeof guide_depths);
    save_slot = 0;
    game->paused = game->closing = game->booted = game->dial_absolute = 0;
    for (int seat = 0; seat < PADS; seat++) game->ports[seat] = (struct port){.analog = -1, .forced = 1};
    show_game(index);
    snprintf(owner, sizeof owner, "%d", game->owner);
    game->pid = spawn((char *[]){"games", "run", game->slug, game->platform, directory, states, owner, resume_slot, (char *)effective_game_language(), NULL}, commands[0], output, output);
    close(output);
    close(commands[0]);
    game->commands = commands[1];
    if (game->pid < 0) {
        game->pid = 0;
        ended(index, 1);
        return;
    }
    resume(index);
}

static void close_game(int index) {
    show_game(index);
    games[index].closing = 1;
    finish_close(index);
}

static void start_job(int index, enum task task, char *const argv[]) {
    struct game *game = &games[index];
    int errors[2];
    if (pipe2(errors, O_CLOEXEC | O_NONBLOCK) < 0) { perror(argv[0]); fail(index, _("Could not start %s"), game->title); return; }
    game->job = spawn_reading(argv, &game->job_output, errors[1]);
    close(errors[1]);
    if (game->job < 0) { game->job = 0; close(errors[0]); fail(index, _("Could not start %s"), game->title); return; }
    game->error_output = errors[0];
    game->error_length = 0;
    *game->job_error = '\0';
    game->task = task;
    game->percent = -1;
    game->job_length = 0;
    snprintf(game->job_text, sizeof game->job_text, "%s", tasks[task]);
}

static int owner_seated(int index) {
    if (seat_of(games[index].owner) >= 0) return 1;
    fail(index, _("Could not start %s: log in a player first"), games[index].title);
    return 0;
}

static void launch(int index) {
    if (!owner_seated(index)) return;
    start_job(index, LAUNCHING, (char *[]){"games", "prepare", games[index].platform, NULL});
    if (games[index].job) resume(index);
}

static ssize_t read_lines(int fd, char *buffer, size_t size, size_t *length, void (*handle_line)(char *, void *), void *context) {
    ssize_t count = read(fd, buffer + *length, size - *length);
    if (count <= 0) return count;
    *length += count;
    char *start = buffer;
    for (char *end; (end = memchr(start, '\n', buffer + *length - start)); start = end + 1) {
        *end = '\0';
        handle_line(start, context);
    }
    *length -= start - buffer;
    memmove(buffer, start, *length);
    if (*length == size) *length = 0;
    return count;
}

static void read_step(char *line, void *context) {
    struct game *game = context;
    game->percent = -1;
    sscanf(line, "%*s %d", &game->percent);
    line[strcspn(line, " ")] = '\0';
    if (strcmp(line, game->job_text) && game->task != LAUNCHING) notify("%s: %s", game->title, _(line));
    memmove(game->job_text, line, strlen(line) + 1);
    changed = 1;
}

static void read_job(int index) {
    struct game *game = &games[index];
    read_lines(game->job_output, game->job_line, sizeof game->job_line, &game->job_length, read_step, game);
}

static void read_error(char *line, void *context) {
    struct game *game = context;
    dprintf(log_file, "%s\n", line);
    if (*line) snprintf(game->job_error, sizeof game->job_error, "%s", line);
}

static ssize_t read_errors(int index) {
    struct game *game = &games[index];
    return read_lines(game->error_output, game->error_line, sizeof game->error_line, &game->error_length, read_error, game);
}

static void job_done(int index, int status) {
    struct game *game = &games[index];
    char detail[TEXT_MAX + 2] = "";
    while (read_errors(index) > 0);
    close(game->error_output);
    close(game->job_output);
    game->job = 0;
    if (status && *game->job_error) snprintf(detail, sizeof detail, ": %s", game->job_error);
    if (game->task == LAUNCHING) {
        if (game->cancelled) game->cancelled = 0;
        else if (status) fail(index, _("Could not start %s%s"), game->title, detail);
        else if (owner_seated(index)) {
            read_labels(index);
            play(index);
        }
        return;
    }
    rescan_installed();
    if (!status) notify(game->task == INSTALLING ? _("Installed %s") : _("Uninstalled %s"), game->title);
    else fail(index, game->task == INSTALLING ? _("Install failed: %s%s") : _("Uninstall failed: %s%s"), game->title, detail);
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
    picker = input_device = device;
    whois = (struct view){.screen = WHOIS, .game = -1};
    changed = 1;
}

static int seat_for(int profile) {
    return seat_of(profile) >= 0 ? seat_of(profile) : player_count < PADS ? player_count : -1;
}

static void login(int profile, int device) {
    if (profile < 1 || profile > PROFILES) return;
    if (seat_for(profile) < 0) { notify("%s", _("Every pad is taken")); return; }
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

static void logout(int profile) {
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
}

static int inert(enum action action) {
    return action == NOTHING || action == VOLUME || action == MICROPHONE_VOLUME || action == SECOND_SCREEN || action == CHOOSE_DISPLAY ||
           action == CHOOSE_DISPLAY_MODE || action == CHOOSE_RESOLUTION || action == CHOOSE_SLEEP || action == CHOOSE_SKIN || action == EMULATE || action == ANALOG || action == CHOOSE_PICTURE ||
           action == CHOOSE_LANGUAGE || action == CHOOSE_GAME_LANGUAGE || action == CHOOSE_TIME_ZONE || action == CHOOSE_CITY;
}

static void act(struct row *row, int profile) {
    char id[TEXT_MAX], store[TEXT_MAX], directory[PATH_MAX];
    struct view *view = top();
    struct game *game = view->game >= 0 ? &games[view->game] : NULL;
    if (screens[view->screen].plane == DIALOG_PLANE) guide_depths[guide_profile]--;
    switch (row->action) {
    case REWIND: load_rewind(); break;
    case SAVE_STATES: load_saved(); break;
    case DELETE_STATE: remove_tree(slot_directory(directory, guide_profile, save_slot)); break;
    case OVERWRITE_STATE: save_to_slot(); break;
    case CLEAR_THUMBNAILS: thumbnail_clearer = spawn((char *[]){"thumbnails", "clear", NULL}, -1, log_file, log_file); break;
    case LOGIN: login(row->arg, picker); break;
    case LOG_OUT_PLAYER:
        if (owned_games(guide_profile)) push(LOG_OUT, -1);
        else logout(guide_profile);
        break;
    case CONFIRM_LOG_OUT: logout(guide_profile); break;
    case OPEN_STORE: push(STORE, -1); refresh(); break;
    case OPEN_SETTINGS:
        discard_staged();
        push(SETTINGS, -1);
        break;
    case APPLY_MODE:
        apply_staged();
        view->action = CHOOSE_DISPLAY_MODE;
        break;
    case OPEN_RESOLUTIONS: push(PLATFORM_RESOLUTIONS, -1); break;
    case OPEN_GAME: push(GAME, row->arg); break;
    case PLAY:
        game->owner = profile;
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
    default: break;
    }
}

static int press_row(struct row *row, enum command command, int profile) {
    if (command == CONFIRM) act(row, profile);
    else if (row->action == SAVE_STATES) press_save_states(command);
    else return 0;
    return 1;
}

static void change_volume(char *node, const char *direction) {
    char step[TEXT_MAX];
    snprintf(step, sizeof step, "%s%s", VOLUME_STEP, direction);
    start((char *[]){"wpctl", "set-volume", "-l", VOLUME_LIMIT, node, step, NULL});
}

static void adjust(enum action action, int arg, int step) {
    if (action == VOLUME) change_volume("@DEFAULT_AUDIO_SINK@", step < 0 ? "-" : "+");
    if (action == MICROPHONE_VOLUME) change_volume("@DEFAULT_AUDIO_SOURCE@", step < 0 ? "-" : "+");
    if (action == CHOOSE_DISPLAY) cycle_display(step);
    if (action == CHOOSE_DISPLAY_MODE) cycle_display_mode(step);
    if (action == CHOOSE_RESOLUTION) cycle_resolution(arg, step);
    if (action == CHOOSE_SLEEP) cycle_sleep(step);
    if (action == CHOOSE_SKIN) cycle_skin(step);
    if (action == CHOOSE_PICTURE) cycle_picture(step);
    if (action == CHOOSE_LANGUAGE) cycle_language(step);
    if (action == CHOOSE_GAME_LANGUAGE) cycle_game_language(step);
    if (action == CHOOSE_TIME_ZONE) cycle_time_zone(step);
    if (action == CHOOSE_CITY) cycle_city(step);
    if (action == REWIND && rewind_back - step >= 0 && rewind_back - step <= rewind_count) rewind_back -= step;
    if (action == SAVE_STATES && save_slot + step >= 0 && save_slot + step < SAVE_SLOTS) save_slot += step;
    if (action == EMULATE) cycle_mode(step);
    if (action == ANALOG) cycle_analog(step);
    if (action == SECOND_SCREEN) toggle_second_screen();
}

static void open_guide(void) {
    rewind_back = 0;
    discard_staged();
    if (!rewindable() || (!capture.session && !start_capture(active))) {
        guide_shown = 1;
        return;
    }
    capture.current = 1;
}

static void enter_guide(int profile, int device, int reset) {
    guide_profile = profile;
    if (reset || !guide_depths[profile]) {
        guides[profile][0] = (struct view){.screen = GUIDE, .game = -1, .action = REWIND};
        guide_depths[profile] = 1;
    }
    focus = input_device = device;
    if (!guide_shown) open_guide();
}

static void toggle_guide(int device) {
    if (guide_shown && focus == device) {
        guide_shown = 0;
        return;
    }
    enter_guide(profile_of(device), device, 0);
}

static void switch_tab(struct view *view, int step) {
    int next = tab_of(view) + step;
    if (next < (int)first_tab() || next > SYSTEM_TAB) return;
    *view = (struct view){.screen = GUIDE, .game = view->game, .tab = next};
    changed = 1;
}

static void navigate(int device, enum command command) {
    paint_needed = 1;
    input_device = device;
    if (script_command(commands[command]) && command >= UP && command <= RIGHT) {
        changed = 1;
        return;
    }
    struct view *view = top();
    if (view->screen == GUIDE && (command == PAGE_UP || command == PAGE_DOWN)) {
        switch_tab(view, command == PAGE_UP ? -1 : 1);
        return;
    }
    int count = build(view), selected = view->selected;
    struct row *row = count ? &rows[view->selected] : NULL;
    switch (command) {
    case UP: if (view->selected > 0) view->selected--; break;
    case DOWN: if (view->selected < count - 1) view->selected++; break;
    case PAGE_UP: view->selected = 0; break;
    case PAGE_DOWN: if (count) view->selected = count - 1; break;
    case LEFT: case RIGHT: if (row) adjust(row->action, row->arg, command == LEFT ? -1 : 1); break;
    case CONFIRM: case X_BUTTON: case Y_BUTTON: if (row) press_row(row, command, profile_of(device)); break;
    case BACK: case GUIDE_BUTTON:
        if (picker != NO_DEVICE) picker = NO_DEVICE;
        else if (guide_shown && guide_depths[guide_profile] > 1) guide_depths[guide_profile]--;
        else if (guide_shown) guide_shown = 0;
        else if (dashboard_depth > 1) dashboard_depth--;
        break;
    default: break;
    }
    if (count && command >= UP && command <= PAGE_DOWN) select_row(view, view->selected);
    if (command < UP || command > PAGE_DOWN) changed = 1;
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
    } else if (command == BACK && launching() && games[active].job) {
        games[active].cancelled = 1;
        if (kill(-games[active].job, SIGTERM) < 0) perror("cancel launch");
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

double skin_now(void) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return now.tv_sec + now.tv_nsec / NS_PER_SECOND;
}

static int menu_wanted(void) { return attract() || interactive() || launching(); }

static int surface_wanted(void) { return menu_wanted() || notification_count; }

static void hide(void) {
    if (!layer) return;
    scene_detach();
    if (effect) ext_background_effect_surface_v1_destroy(effect);
    zwlr_layer_surface_v1_destroy(layer);
    wl_surface_destroy(surface);
    effect = NULL;
    layer = NULL;
    width = frame_pending = blurred_count = 0;
}

static void on_layer_configure(void *data, struct zwlr_layer_surface_v1 *configured, uint32_t serial, uint32_t configured_width, uint32_t configured_height) {
    (void)data;
    zwlr_layer_surface_v1_ack_configure(configured, serial);
    width = configured_width;
    scene_attach(surface, configured_width, configured_height);
    paint_needed = 1;
}

static void on_layer_closed(void *data, struct zwlr_layer_surface_v1 *closed) {
    (void)data, (void)closed;
    hide();
}

static const struct zwlr_layer_surface_v1_listener layer_listener = {.configure = on_layer_configure, .closed = on_layer_closed};

static void show(void) {
    surface = wl_compositor_create_surface(compositor);
    struct wl_region *nowhere = wl_compositor_create_region(compositor);
    wl_surface_set_input_region(surface, nowhere);
    wl_region_destroy(nowhere);
    layer = zwlr_layer_shell_v1_get_layer_surface(layer_shell, surface, NULL, ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY, "anyconsole");
    zwlr_layer_surface_v1_set_anchor(layer, ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP | ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM |
                                                ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT | ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT);
    zwlr_layer_surface_v1_set_exclusive_zone(layer, -1);
    zwlr_layer_surface_v1_add_listener(layer, &layer_listener, NULL);
    if (effects) effect = ext_background_effect_manager_v1_get_background_effect(effects, surface);
    wl_surface_commit(surface);
}

static void on_frame(void *data, struct wl_callback *callback, uint32_t time) {
    (void)data, (void)time;
    wl_callback_destroy(callback);
    frame_pending = 0;
    paint_needed |= skin_moving;
}

static const struct wl_callback_listener frame_listener = {.done = on_frame};

static void set_blur(void) {
    struct box boxes[BLUR_BOXES_MAX];
    int count = scene_blur(boxes, BLUR_BOXES_MAX);
    if (!effect || (count == blurred_count && !memcmp(boxes, blurred, count * sizeof *boxes))) return;
    struct wl_region *region = wl_compositor_create_region(compositor);
    for (int i = 0; i < count; i++) wl_region_add(region, boxes[i].x, boxes[i].y, boxes[i].width, boxes[i].height);
    ext_background_effect_surface_v1_set_blur_region(effect, region);
    wl_region_destroy(region);
    memcpy(blurred, boxes, count * sizeof *boxes);
    blurred_count = count;
}

static void paint(void) {
    paint_needed = 0;
    int moving = scene_paint(skin_now());
    changed |= moving != skin_moving;
    skin_moving = moving;
    if (!surface_wanted() && !skin_moving) {
        hide();
        return;
    }
    set_blur();
    wl_callback_add_listener(wl_surface_frame(surface), &frame_listener, NULL);
    frame_pending = 1;
    scene_present();
}

void skin_select(int index) {
    struct view *view = top();
    if (index < 0 || index == view->selected || index >= build(view)) return;
    select_row(view, index);
    changed = 1;
}

static int watch_skin_folder(const char *path, const struct stat *status, int type, struct FTW *walk) {
    (void)status, (void)walk;
    if (type != FTW_D || skin_watch_count == SKIN_WATCHES_MAX) return 0;
    int watch = inotify_add_watch(watcher, path, IN_CLOSE_WRITE | IN_MOVED_TO | IN_MOVED_FROM | IN_CREATE | IN_DELETE);
    for (int i = 0; i < skin_watch_count; i++)
        if (skin_watches[i] == watch) return 0;
    skin_watches[skin_watch_count++] = watch;
    return 0;
}

static void watch_skins(void) { nftw(USER_SKINS_DIR, watch_skin_folder, SKIN_TREE_DEPTH, FTW_PHYS); }

static int skin_changed(const struct inotify_event *event) {
    for (int i = 0; i < skin_watch_count; i++)
        if (skin_watches[i] == event->wd) {
            if (event->mask & IN_ISDIR) watch_skins();
            reload_due = skin_now() + RELOAD_SECONDS;
            return 1;
        }
    return 0;
}

static void arm_skin_timer(void) {
    static double armed = -1;
    double due = script_next_timer();
    if (reload_due >= 0 && (due < 0 || reload_due < due)) due = reload_due;
    if (due == armed) return;
    armed = due;
    struct itimerspec timer = {0};
    if (due >= 0) timer.it_value = (struct timespec){(time_t)due, (due - (time_t)due) * NS_PER_SECOND + 1};
    timerfd_settime(skin_timer, TFD_TIMER_ABSTIME, &timer, NULL);
}

static void run_skin_timers(void) {
    double now = skin_now();
    if (reload_due >= 0 && reload_due <= now) {
        reload_due = -1;
        load_skin();
    }
    script_run_timers(now);
    paint_needed = 1;
}

static json_object *row_object(struct row *row) {
    json_object *object = json_object_new_object();
    json_object_object_add(object, "label", json_object_new_string(row->label));
    json_object_object_add(object, "value", json_object_new_string(row->value));
    json_object_object_add(object, "action", json_object_new_string(actions[row->action]));
    json_object_object_add(object, "arg", json_object_new_int(row->arg));
    return object;
}

static json_object *view_object(enum plane plane, struct view *view, int *count) {
    json_object *object = json_object_new_object(), *list = json_object_new_array();
    *count = build(view);
    for (int i = 0; i < *count; i++) json_object_array_add(list, row_object(&rows[i]));
    json_object_object_add(object, "plane", json_object_new_string(planes[plane]));
    json_object_object_add(object, "screen", json_object_new_string(screens[view->screen].name));
    json_object_object_add(object, "title", json_object_new_string(title(view)));
    json_object_object_add(object, "game", view->game >= 0 ? json_object_new_string(key(view->game)) : NULL);
    json_object_object_add(object, "rows", list);
    json_object_object_add(object, "selected", json_object_new_int(view->selected));
    if (view->screen != GUIDE) return object;
    list = json_object_new_array();
    for (enum tab tab = first_tab(); tab <= SYSTEM_TAB; tab++) json_object_array_add(list, json_object_new_string(_(tabs[tab])));
    json_object_object_add(object, "tabs", list);
    json_object_object_add(object, "tab", json_object_new_int(tab_of(view) - first_tab()));
    return object;
}

static json_object *launch_object(void) {
    json_object *object = json_object_new_object();
    int done = games[active].job ? (games[active].percent > 0 ? games[active].percent : 0) : (LAUNCH_STEPS - 1) * PERCENT;
    json_object_object_add(object, "game", json_object_new_string(key(active)));
    json_object_object_add(object, "percent", json_object_new_int(done / LAUNCH_STEPS));
    return object;
}

static json_object *views_object(enum action *choosing) {
    json_object *list = json_object_new_array();
    int count;
    *choosing = NOTHING;
    if (dashboard_open) json_object_array_add(list, view_object(DASHBOARD_PLANE, &dashboard[dashboard_depth - 1], &count));
    if (guide_shown) {
        struct view *top_view = guide_view(), *guide = top_view;
        int dialog = screens[top_view->screen].plane == DIALOG_PLANE;
        if (dialog) guide = &guides[guide_profile][guide_depths[guide_profile] - 2];
        json_object_array_add(list, view_object(GUIDE_PLANE, guide, &count));
        if (guide->selected < count) *choosing = rows[guide->selected].action;
        if (dialog) json_object_array_add(list, view_object(DIALOG_PLANE, top_view, &count));
    }
    if (picker != NO_DEVICE) json_object_array_add(list, view_object(PICKER_PLANE, &whois, &count));
    return list;
}

static json_object *saves_object(int choosing) {
    json_object *saves = json_object_new_object(), *slots = json_object_new_array(), *preview = NULL;
    char state[NAME_MAX + 1], directory[PATH_MAX], path[PATH_MAX + NAME_MAX + sizeof SCREEN_EXTENSION];
    time_t saved;
    for (int i = 0; choosing && i < SAVE_SLOTS; i++) {
        json_object *entry = NULL;
        if (saved_state(guide_profile, i, state, &saved)) {
            entry = json_object_new_object();
            snprintf(path, sizeof path, "%s/%s" SCREEN_EXTENSION, slot_directory(directory, guide_profile, i), state);
            json_object_object_add(entry, "screen", access(path, F_OK) ? NULL : json_object_new_string(path));
            json_object_object_add(entry, "time", json_object_new_string(saved_time(saved)));
            if (i == save_slot) preview = json_object_get(json_object_object_get(entry, "screen"));
        }
        json_object_array_add(slots, entry);
    }
    json_object_object_add(saves, "choosing", json_object_new_boolean(choosing));
    json_object_object_add(saves, "slot", json_object_new_int(save_slot));
    json_object_object_add(saves, "slots", slots);
    json_object_object_add(saves, "preview", preview);
    return saves;
}

static void hint(json_object *hints, enum command command, const char *label) {
    if (label) json_object_object_add(hints, commands[command], json_object_new_string(label));
}

static const char *back_hint(struct view *view) {
    if (picker != NO_DEVICE) return _("Back");
    if (screens[view->screen].plane == DIALOG_PLANE) return _("Cancel");
    if (guide_shown) return guide_depths[guide_profile] > 1 ? _("Back") : _("Close");
    return dashboard_depth > 1 ? _("Back") : NULL;
}

static json_object *hints_object(struct view *view, int count) {
    json_object *hints = json_object_new_object();
    if (launching() && games[active].job) hint(hints, BACK, _("Cancel"));
    if (launching() || attract() || !interactive()) return hints;
    struct row *row = view->selected < count ? &rows[view->selected] : NULL;
    int saves = row && row->action == SAVE_STATES, filled = saves && slot_filled(guide_profile, save_slot);
    const char *confirm = row && !inert(row->action) ? _("OK") : NULL;
    if (row && screens[view->screen].plane == DIALOG_PLANE) confirm = row->label;
    else if (saves || (row && row->action == REWIND)) confirm = (saves ? filled : rewind_back) ? _("Load") : NULL;
    hint(hints, CONFIRM, confirm);
    hint(hints, X_BUTTON, saves ? _("Save") : NULL);
    hint(hints, Y_BUTTON, filled ? _("Delete") : NULL);
    hint(hints, BACK, back_hint(view));
    return hints;
}

static json_object *state_object(int count) {
    json_object *state = json_object_new_object(), *audio = json_object_new_object(), *history = json_object_new_object(), *screens_list = json_object_new_array();
    json_object *list = json_object_new_array();
    struct view *view = top();
    char path[PATH_MAX];
    enum action choosing;
    for (int i = 0; i < count; i++) json_object_array_add(list, row_object(&rows[i]));
    json_object *hints = hints_object(view, count), *views = views_object(&choosing);
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
    json_object_object_add(session, "input", input_device == NO_DEVICE ? NULL : json_object_new_int(input_device));
    json_object_object_add(state, "session", session);
    json_object_object_add(state, "rows", list);
    json_object_object_add(state, "selected", json_object_new_int(view->selected));
    json_object_object_add(state, "dashboard", json_object_new_boolean(dashboard_open));
    json_object_object_add(state, "guide", json_object_new_boolean(guide_shown));
    json_object_object_add(history, "states", json_object_new_int(rewind_count));
    json_object_object_add(history, "back", json_object_new_int(rewind_back));
    for (int back = 1; back <= rewind_count; back++)
        json_object_array_add(screens_list, json_object_new_string(rewind_path(path, active, "/" STATES "/%s" SCREEN_EXTENSION, rewinds[rewind_count - back])));
    json_object_object_add(history, "choosing", json_object_new_boolean(choosing == REWIND));
    json_object_object_add(history, "preview", choosing == REWIND && rewind_back ? json_object_get(json_object_array_get_idx(screens_list, rewind_back - 1)) : NULL);
    json_object_object_add(history, "screens", screens_list);
    json_object_object_add(history, "current", current_screen ? json_object_new_string(CURRENT_SCREEN) : NULL);
    json_object_object_add(state, "views", views);
    json_object_object_add(state, "hints", hints);
    json_object_object_add(state, "launching", launching() ? launch_object() : NULL);
    json_object_object_add(state, "splash", json_object_new_string(SPLASH_IMAGE));
    json_object_object_add(state, "skin", json_object_new_string(loaded_skin));
    json_object_object_add(state, "rewind", history);
    json_object_object_add(state, "saves", saves_object(choosing == SAVE_STATES));
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
        json_object_object_add(game, "platform", json_object_new_string(games[i].platform));
        int named = *games[i].thumbnail &&
                    snprintf(path, sizeof path, THUMBNAILS_DIR "/%s/%s" THUMBNAIL_EXTENSION, games[i].platform, games[i].thumbnail) < (int)sizeof path;
        json_object_object_add(game, "thumbnail", named ? json_object_new_string(path) : NULL);
        struct stat status;
        int cached = named && !stat(path, &status);
        if (cached && status.st_mtime != games[i].thumbnail_time) {
            games[i].thumbnail_time = status.st_mtime;
            games[i].thumbnail_aspect = scene_aspect(path);
        }
        json_object_object_add(game, "thumbnailCached", json_object_new_boolean(cached));
        json_object_object_add(game, "thumbnailAspect", cached && games[i].thumbnail_aspect ? json_object_new_double(games[i].thumbnail_aspect) : NULL);
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
    return state;
}

static void write_state(char *serialized) {
    FILE *file = fopen(STATE_PARTIAL, "w");
    if (!file || fputs(serialized, file) < 0 || fclose(file) < 0 || rename(STATE_PARTIAL, STATE_FILE) < 0) perror(STATE_FILE);
    free(written_state);
    written_state = serialized;
}

static void read_boot_governor(void) {
    FILE *file = fopen(GOVERNOR_FILE, "r");
    if (file && fgets(boot_governor, sizeof boot_governor, file)) boot_governor[strcspn(boot_governor, "\n")] = '\0';
    if (file) fclose(file);
}

static void set_governor(const char *governor) {
    glob_t policies;
    if (glob(CPUFREQ_DIR "/policy*", 0, NULL, &policies)) return;
    for (size_t i = 0; i < policies.gl_pathc; i++) {
        char path[PATH_MAX];
        snprintf(path, sizeof path, "%s/scaling_governor", policies.gl_pathv[i]);
        FILE *file = fopen(path, "w");
        if (!file || fputs(governor, file) < 0 || fclose(file) < 0) perror(path);
    }
    globfree(&policies);
}

static void apply_governor(void) {
    int running = 0;
    for (int i = 0; i < game_count; i++) running |= games[i].pid && !games[i].paused;
    if (running == boosted) return;
    boosted = running;
    set_governor(running ? BOOST_GOVERNOR : boot_governor);
}

static int settled(const struct request *request) {
    if (capture.current || refresher > 0 || helper_count) return 0;
    if (request->game < 0) return 1;
    const struct game *game = &games[request->game];
    return !game->job && !game->freezer && !game->restart && !(game->pid && (!game_window(request->game) || game->closing));
}

static void remove_request(int index) {
    close(requests[index].fd);
    json_object_put(requests[index].notifications);
    json_object_put(requests[index].wanted);
    free(requests[index].reply);
    requests[index] = requests[--request_count];
}

static int flush_reply(struct request *request) {
    while (request->sent < request->reply_length) {
        ssize_t count = write(request->fd, request->reply + request->sent, request->reply_length - request->sent);
        if (count < 0) return errno != EAGAIN && errno != EINTR;
        request->sent += count;
    }
    return 1;
}

static void send_replies(json_object *state) {
    for (int i = request_count - 1; i >= 0; i--) {
        struct request *request = &requests[i];
        if (!request->waiting || request->reply || !(*request->error || settled(request))) continue;
        json_object *reply = json_object_new_object();
        json_object_object_add(reply, "ok", json_object_new_boolean(!*request->error));
        if (*request->error) json_object_object_add(reply, "error", json_object_new_string(request->error));
        json_object_object_add(reply, "notifications", json_object_get(request->notifications));
        json_object_object_add(reply, "state", json_object_get(state));
        request->reply_length = asprintf(&request->reply, "%s\n", json_object_to_json_string_ext(reply, JSON_C_TO_STRING_PLAIN));
        json_object_put(reply);
        if (flush_reply(request)) remove_request(i);
    }
}

static void settle(void) {
    if (!changed) return;
    changed = 0;
    if (active >= 0 && !games[active].pid && !games[active].job) active = -1;
    if (active < 0) dashboard_open = 1;
    if (!guide_shown) {
        cairo_surface_destroy(current_screen);
        current_screen = NULL;
    }
    for (int i = 0; i < game_count; i++) {
        int pause = i != active || interactive();
        if (!games[i].pid || !game_window(i) || games[i].closing || games[i].paused == pause || games[i].freezer) continue;
        freeze(i, pause);
    }
    for (int i = 0; i < game_count; i++) {
        struct window *window = game_window(i);
        if (window && window->activated && (i != active || dashboard_open)) zwlr_foreign_toplevel_handle_v1_set_minimized(window->handle);
    }
    struct window *window = active >= 0 ? game_window(active) : NULL;
    if (window && window->activated && !games[active].booted) {
        games[active].booted = 1;
        tell_padd("boot");
    }
    apply_governor();
    if (menu_told != menu_wanted()) {
        menu_told = menu_wanted();
        tell_padd("menu %d", menu_told);
        if (!menu_told) hold(repeating_device, repeating, 0);
    }
    if (surface_wanted() && !layer) show();
    int count = build(top());
    int profile = picker != NO_DEVICE && count ? rows[whois.selected].arg : 0;
    int seat = profile ? seat_for(profile) + 1 : 0;
    if (picker != previewed || seat != previewed_seat) {
        if (previewed != NO_DEVICE && previewed != picker) tell_padd("preview %d 0", previewed);
        if (picker != NO_DEVICE) tell_padd("preview %d %d", picker, seat);
        previewed = picker;
        previewed_seat = seat;
    }
    json_object *state = state_object(count);
    char *shown_state = strdup(json_object_to_json_string_ext(state, JSON_C_TO_STRING_PLAIN));
    if (!skin_state || strcmp(shown_state, skin_state)) {
        script_state(shown_state);
        paint_needed = 1;
        skin_moving = layer != NULL;
        free(skin_state);
        skin_state = shown_state;
    } else free(shown_state);
    json_object_object_add(state, "moving", json_object_new_boolean(skin_moving));
    char *serialized = strdup(json_object_to_json_string_ext(state, JSON_C_TO_STRING_PLAIN));
    if (written_state && !strcmp(serialized, written_state)) free(serialized);
    else write_state(serialized);
    send_replies(state);
    json_object_put(state);
}

static void reap(void) {
    int status;
    for (pid_t pid; (pid = waitpid(-1, &status, WNOHANG)) > 0;) {
        int failed = !WIFEXITED(status) || WEXITSTATUS(status);
        if (reap_helper(pid)) apply_settings();
        if (pid == refresher) {
            refresher = 0;
            rescan();
            changed = 1;
            thumbnail_refresher = spawn((char *[]){"thumbnails", "refresh", NULL}, -1, log_file, log_file);
            if (refresh_pending) refresh();
        }
        if (pid == thumbnail_fetcher) {
            thumbnail_fetcher = 0;
            paint_needed = changed = 1;
        }
        if (pid == thumbnail_refresher) {
            thumbnail_refresher = 0;
            forget_thumbnails();
            paint_needed = changed = 1;
        }
        if (pid == thumbnail_clearer) {
            thumbnail_clearer = 0;
            notify("%s", _("Thumbnails cleared"));
            forget_thumbnails();
            paint_needed = changed = 1;
        }
        if (pid == monitor) {
            monitor = 0;
            close(monitor_output);
        }
        if (pid == image_events) {
            image_events = 0;
            close(image_events_output);
        }
        if (pid == upscalable_probe) {
            read_upscalable();
            changed = 1;
        }
        for (int i = 0; i < game_count; i++) {
            if (games[i].pid == pid) ended(i, failed);
            if (!games[i].pid && games[i].restart == CLOSING) play(i);
            if (games[i].job == pid) job_done(i, failed);
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
            int analog = !strcmp(event->name, ANALOG_FILE);
            if (analog) read_analog(i);
            if (i != active || analog) return 1;
            refresh_rewinds();
            if (games[i].restart == SAVING && newest_slot() > games[i].restart_slot) {
                games[i].restart = CLOSING;
                close_game(i);
            }
            return 1;
        }
    }
    return 0;
}

static void read_changes(int runtime_watch, int display_watch, int controllers_watch, int docker_watch) {
    char buffer[BUFSIZ] __attribute__((aligned(__alignof__(struct inotify_event))));
    ssize_t count = read(watcher, buffer, sizeof buffer);
    for (char *at = buffer; count > 0 && at < buffer + count;) {
        struct inotify_event *event = (struct inotify_event *)at;
        at += sizeof *event + event->len;
        if (!event->len || read_game_change(event) || skin_changed(event)) continue;
        if (event->wd == runtime_watch && !strcmp(event->name, PIPEWIRE_SOCKET)) start_monitor();
        else if (event->wd == docker_watch && !strcmp(event->name, basename(DOCKER_SOCKET))) start_image_events();
        else if (event->wd == display_watch && !strcmp(event->name, basename(DISPLAY_FILE))) {
            read_display();
        } else if (event->wd == controllers_watch && !strcmp(event->name, basename(CONTROLLERS_FILE))) {
            read_controllers();
        } else if (event->wd != runtime_watch && !strcmp(event->name, basename(SETTINGS_FILE))) {
            apply_settings();
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

static void on_toplevel_state(void *data, struct zwlr_foreign_toplevel_handle_v1 *handle, struct wl_array *states) {
    (void)data;
    uint32_t *state;
    for (int i = 0; i < window_count; i++)
        if (windows[i].handle == handle) {
            windows[i].activated = 0;
            wl_array_for_each(state, states) windows[i].activated |= *state == ZWLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_ACTIVATED;
        }
    changed = 1;
}

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
    else if (!strcmp(interface, ext_background_effect_manager_v1_interface.name))
        effects = wl_registry_bind(registry, id, &ext_background_effect_manager_v1_interface, 1);
    else if (!strcmp(interface, zwp_linux_dmabuf_v1_interface.name))
        dmabuf = wl_registry_bind(registry, id, &zwp_linux_dmabuf_v1_interface, LINUX_DMABUF_VERSION);
}

static void on_global_remove(void *data, struct wl_registry *registry, uint32_t id) { (void)data, (void)registry, (void)id; }

static const struct wl_registry_listener registry_listener = {.global = on_global, .global_remove = on_global_remove};

static void directory_of(const char *path, char *directory) { snprintf(directory, PATH_MAX, "%.*s", (int)(strrchr(path, '/') - path), path); }

static void reset_devices(void) {
    read_controllers();
    adopt_members();
    picker = focus = input_device = NO_DEVICE;
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

static void refuse(struct request *request, const char *format, ...) {
    va_list arguments;
    va_start(arguments, format);
    vsnprintf(request->error, sizeof request->error, format, arguments);
    va_end(arguments);
}

static int matches(json_object *wanted, const struct row *row, int game) {
    char arg[TEXT_MAX];
    snprintf(arg, sizeof arg, "%d", row->arg);
    const char *actual[] = {actions[row->action], row->label, row->value, arg};
    if (game >= 0 && row->action == OPEN_GAME && row->arg != game) return 0;
    for (unsigned i = 0; i < sizeof criteria / sizeof *criteria; i++)
        if (json_object_object_get_ex(wanted, criteria[i], NULL) && strcmp(string_at(wanted, criteria[i], NULL), actual[i])) return 0;
    return 1;
}

static int find_row(json_object *wanted, struct view *view, int game) {
    int count = build(view);
    for (int i = 0; i < count; i++)
        if (matches(wanted, &rows[i], game)) return i;
    return -1;
}

static void open_screen(enum screen screen, int game, enum tab tab, int profile, int device) {
    picker = NO_DEVICE;
    switch (screens[screen].plane) {
    case PICKER_PLANE:
        if (device != NO_DEVICE) open_picker(device);
        break;
    case DASHBOARD_PLANE:
        guide_shown = 0;
        dashboard_open = 1;
        dashboard_depth = 1;
        if (screen != HOME) push(screen, game);
        break;
    case GUIDE_PLANE:
    case DIALOG_PLANE:
        for (int i = 0; device == NO_DEVICE && i < member_count; i++)
            if (members[i].profile == profile) device = members[i].device;
        enter_guide(profile, device, 1);
        if (screen == GUIDE) guides[profile][0].tab = tab;
        else guides[profile][guide_depths[profile]++] = (struct view){.screen = screen, .game = game};
        break;
    }
}

static void run_request(struct request *request, json_object *wanted) {
    int game = -1, given = 0, index;
    enum command command = json_object_object_get_ex(wanted, "command", NULL) ? command_named(string_at(wanted, "command", NULL)) : CONFIRM;
    for (unsigned i = 0; i < sizeof criteria / sizeof *criteria; i++) given += json_object_object_get_ex(wanted, criteria[i], NULL);
    int profile = json_object_object_get_ex(wanted, "profile", NULL) ? int_at(wanted, "profile") : guide_shown ? guide_profile : player_count ? players[0] : 0;
    struct view *view = top(), candidate;
    enum screen found = view->screen;
    if (!given) { refuse(request, "give at least one of action, label, value or arg"); return; }
    if (profile < 0 || profile > PROFILES) { refuse(request, "no such profile %d", profile); return; }
    if (json_object_object_get_ex(wanted, "game", NULL)) {
        for (int i = 0; game < 0 && i < game_count; i++)
            if (!strcmp(key(i), string_at(wanted, "game", NULL))) game = i;
        if (game < 0) { refuse(request, "unknown game %s", string_at(wanted, "game", NULL)); return; }
    }
    index = game >= 0 && view->game >= 0 && view->game != game ? -1 : find_row(wanted, view, game);
    for (enum screen screen = HOME; index < 0 && screen < sizeof screens / sizeof *screens; screen++) {
        if (screen == GAME && game < 0) continue;
        enum tab last = screen == GUIDE ? SYSTEM_TAB : first_tab();
        for (enum tab tab = first_tab(); index < 0 && tab <= last; tab++) {
            candidate = (struct view){.screen = screen, .game = game, .tab = tab};
            index = find_row(wanted, &candidate, game);
            if (index < 0) continue;
            found = screen;
            open_screen(screen, game, tab, profile, int_at(wanted, "device"));
            if (capture.current) {
                request->wanted = json_object_get(wanted);
                return;
            }
        }
    }
    if (index < 0) { refuse(request, NO_ROW "%s", json_object_to_json_string_ext(wanted, JSON_C_TO_STRING_PLAIN)); return; }
    view = top();
    if (view->screen == found) select_row(view, index);
    request->game = view->game;
    if (json_object_object_get_ex(wanted, "step", NULL)) adjust(rows[index].action, rows[index].arg, int_at(wanted, "step"));
    else if (!press_row(&rows[index], command, profile)) refuse(request, "%s takes no %s", actions[rows[index].action], commands[command]);
    if (request->game < 0) request->game = active;
}

static void handle_request(char *line, void *context) {
    struct request *request = context;
    if (request->waiting) return;
    json_object *wanted = json_tokener_parse(line);
    request->waiting = 1;
    request->notifications = json_object_new_array();
    request->game = -1;
    if (json_object_is_type(wanted, json_type_object)) run_request(request, wanted);
    else refuse(request, "request is not a JSON object");
    json_object_put(wanted);
    changed = 1;
}

static void accept_requests(int listener) {
    for (int fd; (fd = accept4(listener, NULL, NULL, SOCK_NONBLOCK | SOCK_CLOEXEC)) >= 0;) {
        if (request_count == REQUESTS_MAX) close(fd);
        else requests[request_count++] = (struct request){.fd = fd, .game = -1};
    }
}

static void notify_from(char *line) {
    char *argument = strstr(line, NOTIFY_SEPARATOR);
    if (argument) *argument = '\0';
    notify(_(line), argument ? argument + strlen(NOTIFY_SEPARATOR) : "");
}

static void handle_message(char *line, void *context) {
    char name[WORD_MAX + 1];
    int first, value;
    (void)context;
    if (!strncmp(line, NOTIFY_PREFIX, strlen(NOTIFY_PREFIX))) notify_from(line + strlen(NOTIFY_PREFIX));
    else if (!strcmp(line, "reset")) reset_devices();
    else if (sscanf(line, "connect %d", &first) == 1) connected(first);
    else if (sscanf(line, "dial %" STRINGIFY(WORD_MAX) "s", name) == 1) turn_dial(name);
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
    enum { WAYLAND, CHANGES, SIGNALS, REPEAT, NOTIFICATIONS, MESSAGES, SAVE, SKIN, DECODED, MONITOR, IMAGE_EVENTS, LISTENER, FIXED };
    struct pollfd watched[FIXED + JOB_FDS * GAMES_MAX + REQUESTS_MAX];
    int jobs[GAMES_MAX], served[REQUESTS_MAX];
    char stores_directory[PATH_MAX], display_directory[PATH_MAX], settings_directory[PATH_MAX], controllers_directory[PATH_MAX], docker_directory[PATH_MAX];
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
    if (!compositor || !layer_shell || !security || !seat || !toplevels || !listing || !toplevel_capture || !image_copy || !dmabuf) { fprintf(stderr, "overlay: compositor lacks a required protocol\n"); return 1; }
    directory_of(STORES_FILE, stores_directory);
    directory_of(DISPLAY_FILE, display_directory);
    directory_of(SETTINGS_FILE, settings_directory);
    directory_of(CONTROLLERS_FILE, controllers_directory);
    directory_of(DOCKER_SOCKET, docker_directory);
    resolution_count = numbers(RESOLUTIONS, resolutions, RESOLUTIONS_MAX);
    upscalable = json_object_new_object();
    sleep_choice_count = numbers(SLEEP_MINUTES, sleep_choices, SLEEP_CHOICES_MAX);
    mkfifo(OVERLAY_FIFO, S_IRUSR | S_IWUSR);
    watcher = inotify_init1(IN_CLOEXEC);
    int runtime_watch = inotify_add_watch(watcher, getenv("XDG_RUNTIME_DIR"), IN_CREATE);
    inotify_add_watch(watcher, stores_directory, IN_CLOSE_WRITE | IN_MOVED_TO);
    inotify_add_watch(watcher, settings_directory, IN_MOVED_TO | IN_MASK_ADD);
    int display_watch = inotify_add_watch(watcher, display_directory, IN_MOVED_TO);
    int controllers_watch = inotify_add_watch(watcher, controllers_directory, IN_MOVED_TO);
    int docker_watch = inotify_add_watch(watcher, docker_directory, IN_CREATE);
    repeat_timer = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC);
    notification_timer = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC);
    save_timer = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC);
    skin_timer = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC);
    mkdir(USER_SKINS_DIR, S_IRWXU);
    watch_skins();
    scene_init(display);
    sound_init();
    timerfd_settime(save_timer, 0, &(struct itimerspec){.it_value = {.tv_sec = SAVE_SECONDS}, .it_interval = {.tv_sec = SAVE_SECONDS}}, NULL);
    watched[WAYLAND] = (struct pollfd){.fd = wl_display_get_fd(display), .events = POLLIN};
    watched[CHANGES] = (struct pollfd){.fd = watcher, .events = POLLIN};
    watched[SIGNALS] = (struct pollfd){.fd = signalfd(-1, &signals, SFD_CLOEXEC), .events = POLLIN};
    watched[REPEAT] = (struct pollfd){.fd = repeat_timer, .events = POLLIN};
    watched[NOTIFICATIONS] = (struct pollfd){.fd = notification_timer, .events = POLLIN};
    watched[MESSAGES] = (struct pollfd){.fd = open(OVERLAY_FIFO, O_RDWR | O_NONBLOCK | O_CLOEXEC), .events = POLLIN};
    watched[SAVE] = (struct pollfd){.fd = save_timer, .events = POLLIN};
    watched[SKIN] = (struct pollfd){.fd = skin_timer, .events = POLLIN};
    watched[DECODED] = (struct pollfd){.fd = scene_start_decoder(), .events = POLLIN};
    watched[LISTENER] = (struct pollfd){.fd = bind_socket(OVERLAY_SOCKET, SOCK_NONBLOCK), .events = POLLIN};
    keep_interrupted_rewinds();
    find_languages();
    find_time_zones();
    read_stores();
    read_settings();
    read_display();
    read_boot_governor();
    load_session();
    reset_devices();
    rescan();
    refresh();
    start_monitor();
    start_image_events();
    settle();
    for (;;) {
        int count = FIXED, job_count = 0, served_count = 0;
        watched[MONITOR] = (struct pollfd){.fd = monitor > 0 ? monitor_output : -1, .events = POLLIN};
        watched[IMAGE_EVENTS] = (struct pollfd){.fd = image_events > 0 ? image_events_output : -1, .events = POLLIN};
        for (int i = 0; i < game_count; i++) {
            if (!games[i].job) continue;
            jobs[job_count++] = i;
            watched[count++] = (struct pollfd){.fd = games[i].job_output, .events = POLLIN};
            watched[count++] = (struct pollfd){.fd = games[i].error_output, .events = POLLIN};
        }
        for (int i = 0; i < request_count; i++) {
            if (requests[i].waiting && !requests[i].reply) continue;
            served[served_count++] = i;
            watched[count++] = (struct pollfd){.fd = requests[i].fd, .events = requests[i].reply ? POLLOUT : POLLIN};
        }
        while (wl_display_prepare_read(display)) wl_display_dispatch_pending(display);
        wl_display_flush(display);
        if (poll(watched, count, -1) < 0) { wl_display_cancel_read(display); continue; }
        if (watched[WAYLAND].revents & POLLIN) wl_display_read_events(display);
        else wl_display_cancel_read(display);
        if (wl_display_dispatch_pending(display) < 0) { fprintf(stderr, "overlay: lost the compositor\n"); return 1; }
        for (int i = 0; i < job_count; i++) {
            if (watched[FIXED + JOB_FDS * i].revents) read_job(jobs[i]);
            if (watched[FIXED + JOB_FDS * i + 1].revents) read_errors(jobs[i]);
        }
        if (watched[MONITOR].revents) read_monitor();
        if (watched[IMAGE_EVENTS].revents) read_image_events();
        uint64_t expirations;
        struct signalfd_siginfo signal_info;
        if (watched[CHANGES].revents) read_changes(runtime_watch, display_watch, controllers_watch, docker_watch);
        if (watched[SIGNALS].revents && read(watched[SIGNALS].fd, &signal_info, sizeof signal_info) > 0) {
            if (signal_info.ssi_signo == SIGUSR1) refresh();
            else reap();
        }
        if (watched[REPEAT].revents && read(repeat_timer, &expirations, sizeof expirations) > 0) press(repeating_device, repeating);
        if (watched[NOTIFICATIONS].revents && read(notification_timer, &expirations, sizeof expirations) > 0) expire_notifications();
        if (watched[MESSAGES].revents) read_messages(watched[MESSAGES].fd);
        if (watched[SAVE].revents && read(save_timer, &expirations, sizeof expirations) > 0) sample_screen();
        if (watched[SKIN].revents && read(skin_timer, &expirations, sizeof expirations) > 0) run_skin_timers();
        if (watched[DECODED].revents && read(watched[DECODED].fd, &expirations, sizeof expirations) > 0) paint_needed = 1;
        if (watched[LISTENER].revents) accept_requests(watched[LISTENER].fd);
        for (int i = served_count - 1; i >= 0; i--) {
            struct request *request = &requests[served[i]];
            if (!watched[FIXED + JOB_FDS * job_count + i].revents) continue;
            if (request->reply ? flush_reply(request) : read_lines(request->fd, request->line, sizeof request->line, &request->length, handle_request, request) <= 0)
                remove_request(served[i]);
        }
        for (int i = 0; !capture.current && i < request_count; i++) {
            json_object *wanted = requests[i].wanted;
            if (!wanted) continue;
            requests[i].wanted = NULL;
            run_request(&requests[i], wanted);
            json_object_put(wanted);
            changed = 1;
        }
        settle();
        if (layer && width && paint_needed && !frame_pending) paint();
        settle();
        fetch_thumbnails();
        arm_skin_timer();
    }
}
