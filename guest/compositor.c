#define _GNU_SOURCE
#include <EGL/egl.h>
#include <GLES3/gl32.h>
#include <GLES2/gl2ext.h>
#include <drm_fourcc.h>
#include <json-c/json.h>
#include <limits.h>
#include <math.h>
#include <signal.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/inotify.h>
#include <sys/sysmacros.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <vulkan/vulkan.h>
#include <wayland-server-core.h>
#include <wlr/backend.h>
#include <wlr/backend/drm.h>
#include <wlr/backend/libinput.h>
#include <wlr/backend/multi.h>
#include <wlr/backend/session.h>
#include <wlr/render/allocator.h>
#include <wlr/render/egl.h>
#include <wlr/render/gles2.h>
#include <wlr/render/swapchain.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/interfaces/wlr_ext_image_capture_source_v1.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_ext_foreign_toplevel_list_v1.h>
#include <wlr/types/wlr_ext_image_copy_capture_v1.h>
#include <wlr/types/wlr_foreign_toplevel_management_v1.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_layer_shell_v1.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_pointer.h>
#include <wlr/types/wlr_pointer_constraints_v1.h>
#include <wlr/types/wlr_screencopy_v1.h>
#include <wlr/types/wlr_relative_pointer_v1.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_security_context_v1.h>
#include <wlr/types/wlr_viewporter.h>
#include <wlr/types/wlr_xdg_output_v1.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/log.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <xkbcommon/xkbcommon.h>
#include "anyconsole.h"
#include "ext-background-effect-v1-protocol.h"
#include "ext-image-capture-source-v1-protocol.h"
#include "shader.h"

#define ASPECT_WIDTH 16
#define ASPECT_HEIGHT 9
#define MIN_INTEGER_SCALE 2
#define ASPECT_FIELD ", Aspect: "
#define GPUS_MAX 8
#define TEXT_MAX 256
#define NO_VULKAN -1
#define UNKNOWN "Unknown"
#define DISPLAY_PARTIAL DISPLAY_FILE ".partial"
#define SEAT_NAME "seat0"
#define COMPOSITOR_VERSION 6
#define XDG_SHELL_VERSION 6
#define LAYER_SHELL_VERSION 4
#define TOPLEVEL_LIST_VERSION 1
#define IMAGE_COPY_CAPTURE_VERSION 1
#define USEC_PER_MSEC 1000
#define CORNERS 4
#define BACKGROUND_EFFECT_VERSION 1
#define GAME_REFRESH_MHZ (60 * MHZ_PER_HZ)
#define RETRO_PASSES SHADERS_DIR "/passes"
#define RETRO_PASS SHADERS_DIR "/%d.%s"
#define RETRO_PICTURE "retro"
#define RETRO_POSITION 0
#define RETRO_COORDINATE 1
#define RETRO_BLOCKS 2
#define RETRO_ORIGINAL_UNIT 1

enum filter { NEAREST, LINEAR, PIXEL };
static const char *filters[] = {[NEAREST] = "nearest", [LINEAR] = "linear", [PIXEL] = "pixel"};
static const VkPhysicalDeviceType preference[] = {VK_PHYSICAL_DEVICE_TYPE_OTHER, VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU,
                                                  VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU, VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU};

static const char vertex_source[] =
    "attribute vec2 position;\n"
    "attribute vec2 coordinate;\n"
    "varying vec2 uv;\n"
    "void main() {\n"
    "    uv = coordinate;\n"
    "    gl_Position = vec4(position, 0.0, 1.0);\n"
    "}\n";
static const char *const sampler_prefixes[] = {
    "#define SAMPLER sampler2D\n",
    "#extension GL_OES_EGL_image_external : require\n#define SAMPLER samplerExternalOES\n",
};
static const char pixel_source[] =
    "precision highp float;\n"
    "uniform SAMPLER source;\n"
    "uniform vec2 size;\n"
    "uniform vec2 extent;\n"
    "varying vec2 uv;\n"
    "const float half_pi = 1.5707963267948966;\n"
    "const float max_extent = 0.25;\n"
    "void main() {\n"
    "    vec2 pixel = uv * size - 0.5;\n"
    "    vec2 base = floor(pixel);\n"
    "    vec2 phase = pixel - base;\n"
    "    vec2 shift = 0.5 + 0.5 * sin(half_pi * clamp((phase - 0.5) / min(extent, vec2(max_extent)), -1.0, 1.0));\n"
    "    gl_FragColor = texture2D(source, (base + 0.5 + shift) / size);\n"
    "}\n";

struct gpu {
    struct wlr_device *device;
    struct wlr_backend *backend;
    int rank, has_display, renders;
    char name[TEXT_MAX];
};

struct display {
    struct wlr_output *output;
    struct gpu *gpu;
    unsigned connected;
    struct wl_list link;
    struct wl_listener frame, needs_frame, destroy;
};

struct blur {
    struct wlr_surface_synced synced;
    pixman_region32_t pending, current;
    struct wl_resource *resource;
    struct wl_listener surface_destroy;
    struct wl_list link;
};
struct window {
    struct wlr_xdg_toplevel *toplevel;
    struct wlr_foreign_toplevel_handle_v1 *handle;
    struct wlr_ext_foreign_toplevel_handle_v1 *listed;
    struct wlr_ext_image_capture_source_v1 source;
    char app_id[TEXT_MAX];
    int sandboxed, display_width;
    long log_read;
    float aspect;
    struct wl_list link;
    struct wl_listener commit, map, unmap, destroy, activate, request_fullscreen;
};

struct keyboard { struct wlr_keyboard *keyboard; struct wl_listener key, modifiers, destroy; };
struct pointer { struct wl_listener motion, button, axis, frame, destroy; };
struct program { GLuint id; GLint position, coordinate, size, extent; };
enum scale_type { SOURCE_SCALE, VIEWPORT_SCALE, ABSOLUTE_SCALE };
struct retro_target { GLuint texture, framebuffer; int width, height; GLint filter; };
struct retro_pass {
    GLuint program;
    GLint output_size[RETRO_BLOCKS], source_size[RETRO_BLOCKS], original_size[RETRO_BLOCKS], frame_count[RETRO_BLOCKS];
    enum scale_type type_x, type_y;
    float scale_x, scale_y;
    int floating;
    unsigned frame_count_mod;
    struct retro_target target;
};

static struct wl_display *display;
static struct wl_event_source *flip_retry;
static struct wlr_session *session;
static struct wlr_backend *backend;
static struct wlr_renderer *renderer;
static struct wlr_allocator *allocator;
static struct wlr_seat *seat;
static struct wlr_layer_shell_v1 *layer_shell;
static struct wlr_security_context_manager_v1 *security;
static struct wlr_foreign_toplevel_manager_v1 *foreign;
static struct wlr_screencopy_manager_v1 *screencopy;
static struct wlr_ext_foreign_toplevel_list_v1 *listing;
static struct wlr_ext_image_copy_capture_manager_v1 *image_copy;
static struct wl_global *toplevel_capture;
static struct wlr_output_layout *layout;
static struct wlr_relative_pointer_manager_v1 *relative;
static struct xkb_keymap *keymap;
static struct gpu gpus[GPUS_MAX], *render_gpu;
static int gpu_count;
static struct wl_list displays, windows, blurs;
static struct display *active, *pending;
static struct window *shown;
static struct wlr_layer_surface_v1 *overlay;
static struct wl_listener overlay_commit, overlay_destroy;
static struct wlr_swapchain *canvas_chain;
static struct program programs[sizeof sampler_prefixes / sizeof *sampler_prefixes];
static const char *const scale_types[] = {[SOURCE_SCALE] = "source", [VIEWPORT_SCALE] = "viewport", [ABSOLUTE_SCALE] = "absolute"};
static const char *const retro_blocks[RETRO_BLOCKS] = {"global", "registers"};
static struct retro_pass *retro_passes;
static struct retro_target retro_original;
static int retro_pass_count;
static struct wlr_swapchain *retro_chain;
static GLuint retro_frames;
static int retro;
static char pinned[TEXT_MAX], chosen_mode[TEXT_MAX], *written_state;
static struct wlr_output_mode *applied_mode;
static int shown_refresh, watcher, manifest_watch = -1;
static int resolution, canvas_width, canvas_height, started, stopping, dirty;
static unsigned connections;
static double pointer_x, pointer_y;
static pid_t session_leader;

static void listen(struct wl_listener *listener, struct wl_signal *signal, wl_notify_func_t notify) {
    listener->notify = notify;
    wl_signal_add(signal, listener);
}

static int has_extension(VkPhysicalDevice device, const char *name) {
    uint32_t count = 0;
    vkEnumerateDeviceExtensionProperties(device, NULL, &count, NULL);
    VkExtensionProperties *extensions = calloc(count, sizeof *extensions);
    vkEnumerateDeviceExtensionProperties(device, NULL, &count, extensions);
    int found = 0;
    for (uint32_t i = 0; i < count && !found; i++) found = !strcmp(extensions[i].extensionName, name);
    free(extensions);
    return found;
}

static int rank_of(VkPhysicalDeviceType type) {
    for (int i = 0; i < (int)(sizeof preference / sizeof *preference); i++)
        if (preference[i] == type) return i;
    return NO_VULKAN;
}

static void rank_gpus(void) {
    VkApplicationInfo application = {.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO, .apiVersion = VK_API_VERSION_1_1};
    VkInstanceCreateInfo info = {.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, .pApplicationInfo = &application};
    VkPhysicalDevice devices[GPUS_MAX];
    uint32_t count = GPUS_MAX;
    VkInstance instance;
    for (int i = 0; i < gpu_count; i++) {
        drmVersionPtr version = drmGetVersion(gpus[i].device->fd);
        gpus[i].rank = NO_VULKAN;
        snprintf(gpus[i].name, sizeof gpus[i].name, "%s", version ? version->name : UNKNOWN);
        drmFreeVersion(version);
    }
    if (vkCreateInstance(&info, NULL, &instance) != VK_SUCCESS) return;
    if (vkEnumeratePhysicalDevices(instance, &count, devices) < VK_SUCCESS) count = 0;
    for (uint32_t i = 0; i < count; i++) {
        VkPhysicalDeviceDrmPropertiesEXT drm = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRM_PROPERTIES_EXT};
        VkPhysicalDeviceProperties2 properties = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, .pNext = &drm};
        if (!has_extension(devices[i], VK_EXT_PHYSICAL_DEVICE_DRM_EXTENSION_NAME)) continue;
        vkGetPhysicalDeviceProperties2(devices[i], &properties);
        for (int j = 0; j < gpu_count; j++) {
            if (!drm.hasPrimary || gpus[j].device->dev != makedev(drm.primaryMajor, drm.primaryMinor)) continue;
            gpus[j].rank = rank_of(properties.properties.deviceType);
            snprintf(gpus[j].name, sizeof gpus[j].name, "%s", properties.properties.deviceName);
        }
    }
    vkDestroyInstance(instance, NULL);
}

static int connected(struct gpu *gpu) {
    drmModeRes *resources = drmModeGetResources(gpu->device->fd);
    int found = 0;
    for (int i = 0; resources && !found && i < resources->count_connectors; i++) {
        drmModeConnector *connector = drmModeGetConnector(gpu->device->fd, resources->connectors[i]);
        found = connector && connector->connection == DRM_MODE_CONNECTED;
        drmModeFreeConnector(connector);
    }
    drmModeFreeResources(resources);
    return found;
}

static int has_render_node(struct gpu *gpu) {
    char *render_node = drmGetRenderDeviceNameFromFd(gpu->device->fd);
    int found = render_node != NULL;
    free(render_node);
    return found;
}

static dev_t handed_over(void) {
    FILE *file = fopen(RENDER_GPU_FILE, "r");
    unsigned major_number, minor_number;
    dev_t device = file && fscanf(file, "%u:%u", &major_number, &minor_number) == 2 ? makedev(major_number, minor_number) : 0;
    if (file) fclose(file);
    return device;
}

static int startup_order(struct gpu *a, struct gpu *b, dev_t handed) {
    int a_handed = a->device->dev == handed, b_handed = b->device->dev == handed;
    if (a->renders != b->renders) return a->renders - b->renders;
    if (a->has_display != b->has_display) return a->has_display - b->has_display;
    if (a_handed != b_handed) return a_handed - b_handed;
    return a->rank - b->rank;
}

static int start_gpu(struct gpu *gpu) {
    gpu->backend = wlr_drm_backend_create(session, gpu->device, gpu == render_gpu ? NULL : render_gpu->backend);
    return gpu->backend && wlr_multi_backend_add(backend, gpu->backend);
}

static int start_gpus(void) {
    struct wlr_device *devices[GPUS_MAX];
    ssize_t count = wlr_session_find_gpus(session, GPUS_MAX, devices);
    for (gpu_count = 0; gpu_count < count; gpu_count++) {
        gpus[gpu_count].device = devices[gpu_count];
        gpus[gpu_count].has_display = connected(&gpus[gpu_count]);
        gpus[gpu_count].renders = has_render_node(&gpus[gpu_count]);
    }
    if (!gpu_count) return 0;
    rank_gpus();
    dev_t handed = handed_over();
    render_gpu = &gpus[0];
    for (int i = 1; i < gpu_count; i++)
        if (startup_order(&gpus[i], render_gpu, handed) > 0) render_gpu = &gpus[i];
    if (!start_gpu(render_gpu)) return 0;
    for (int i = 0; i < gpu_count; i++)
        if (&gpus[i] != render_gpu) start_gpu(&gpus[i]);
    return 1;
}

static void name_mode(struct wlr_output_mode *mode, char name[TEXT_MAX]) { snprintf(name, TEXT_MAX, "%dx%d@%d", mode->width, mode->height, mode->refresh); }

static const char *label(struct wlr_output *output) { return output->description ? output->description : output->name; }

static int is_pinned(struct display *entry) { return *pinned && !strcmp(label(entry->output), pinned); }

static int better(struct display *a, struct display *b) {
    if (!b) return 1;
    if (is_pinned(a) != is_pinned(b)) return is_pinned(a);
    if (a->gpu->rank != b->gpu->rank) return a->gpu->rank > b->gpu->rank;
    if (a->connected != b->connected) return a->connected > b->connected;
    return a->gpu == render_gpu && b->gpu != render_gpu;
}

static int games_running(void) {
    struct window *window;
    wl_list_for_each(window, &windows, link) if (window->sandboxed) return 1;
    return 0;
}

static struct wlr_box centered(struct wlr_box outer, int width, int height) {
    return (struct wlr_box){outer.x + (outer.width - width) / 2, outer.y + (outer.height - height) / 2, width, height};
}

static struct wlr_box fit(struct wlr_box outer, int aspect_width, int aspect_height) {
    int width = outer.width, height = outer.height;
    if (width * aspect_height > height * aspect_width) width = height * aspect_width / aspect_height;
    else height = width * aspect_height / aspect_width;
    return centered(outer, width, height);
}

static struct wlr_box place(struct wlr_box outer, int width, int height) {
    int across = outer.width / width, down = outer.height / height, factor = across < down ? across : down;
    if (factor < MIN_INTEGER_SCALE) return fit(outer, width, height);
    return centered(outer, width * factor, height * factor);
}

static void size_window(struct window *window) {
    int fullscreen = window->toplevel->requested.fullscreen;
    wlr_xdg_toplevel_set_fullscreen(window->toplevel, fullscreen);
    wlr_xdg_toplevel_set_size(window->toplevel, fullscreen ? canvas_width : 0, fullscreen ? canvas_height : 0);
}

static struct wlr_box area(void) { return fit((struct wlr_box){0, 0, active->output->width, active->output->height}, ASPECT_WIDTH, ASPECT_HEIGHT); }

static enum filter choose_filter(struct wlr_box target, int width, int height) {
    if (!width || (target.width % width == 0 && target.height % height == 0)) return NEAREST;
    if (target.width < width || !wlr_renderer_is_gles2(renderer)) return LINEAR;
    return PIXEL;
}

static struct wlr_box overlay_box(void) {
    uint32_t anchor = overlay->current.anchor, horizontal = anchor & (ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT | ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT),
             vertical = anchor & (ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP | ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM);
    int width = overlay->current.actual_width, height = overlay->current.actual_height;
    int x = horizontal == ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT ? 0 : horizontal == ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT ? canvas_width - width : (canvas_width - width) / 2;
    int y = vertical == ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP ? 0 : vertical == ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM ? canvas_height - height : (canvas_height - height) / 2;
    return (struct wlr_box){x, y, width, height};
}

static void configure_overlay(void) {
    if (!overlay || !overlay->initialized) return;
    wlr_layer_surface_v1_configure(overlay, overlay->current.desired_width ? overlay->current.desired_width : (uint32_t)canvas_width,
                                   overlay->current.desired_height ? overlay->current.desired_height : (uint32_t)canvas_height);
}

static void redraw(void) {
    dirty = 1;
    if (active) wlr_output_schedule_frame(active->output);
}

static json_object *box_json(struct wlr_box box) {
    json_object *placement = json_object_new_object();
    json_object_object_add(placement, "x", json_object_new_int(box.x));
    json_object_object_add(placement, "y", json_object_new_int(box.y));
    json_object_object_add(placement, "width", json_object_new_int(box.width));
    json_object_object_add(placement, "height", json_object_new_int(box.height));
    return placement;
}

static void write_state(void) {
    json_object *state = json_object_new_object(), *list = json_object_new_array();
    char canvas[TEXT_MAX] = "", frame[TEXT_MAX] = "";
    struct display *entry;
    wl_list_for_each(entry, &displays, link) {
        json_object *item = json_object_new_object();
        json_object_object_add(item, "label", json_object_new_string(label(entry->output)));
        json_object_object_add(item, "gpu", json_object_new_string(entry->gpu->name));
        json_object_array_add(list, item);
    }
    if (shown) snprintf(frame, sizeof frame, "%ux%u", shown->source.width, shown->source.height);
    if (active) {
        struct wlr_box box = area();
        snprintf(canvas, sizeof canvas, "%dx%d", canvas_width, canvas_height);
        json_object_object_add(state, "filter", json_object_new_string(filters[choose_filter(box, canvas_width, canvas_height)]));
        json_object_object_add(state, "area_box", box_json(box));
        if (shown && shown->source.width && shown->source.height)
            json_object_object_add(state, "picture_box", box_json(place(box, shown->display_width, shown->source.height)));
        json_object *modes = json_object_new_array();
        struct wlr_output_mode *mode;
        char name[TEXT_MAX];
        wl_list_for_each(mode, &active->output->modes, link) {
            name_mode(mode, name);
            int known = 0;
            for (size_t i = 0; i < json_object_array_length(modes); i++)
                known |= !strcmp(json_object_get_string(json_object_array_get_idx(modes, i)), name);
            if (!known) json_object_array_add(modes, json_object_new_string(name));
        }
        json_object_object_add(state, "modes", modes);
        name_mode(active->output->current_mode, name);
        json_object_object_add(state, "mode", json_object_new_string(name));
    }
    json_object_object_add(state, "displays", list);
    json_object_object_add(state, "display", active ? json_object_new_string(label(active->output)) : NULL);
    json_object_object_add(state, "gpu", json_object_new_string(render_gpu->name));
    json_object_object_add(state, "vulkan", json_object_new_boolean(render_gpu->rank != NO_VULKAN));
    json_object_object_add(state, "canvas", json_object_new_string(canvas));
    json_object_object_add(state, "pending", pending ? json_object_new_string(label(pending->output)) : NULL);
    json_object_object_add(state, "shown", shown ? json_object_new_string(shown->app_id) : NULL);
    json_object_object_add(state, "frame", json_object_new_string(frame));
    char *serialized = strdup(json_object_to_json_string_ext(state, JSON_C_TO_STRING_PLAIN));
    json_object_put(state);
    if (written_state && !strcmp(serialized, written_state)) { free(serialized); return; }
    FILE *file = fopen(DISPLAY_PARTIAL, "w");
    if (!file || fputs(serialized, file) < 0 || fclose(file) < 0 || rename(DISPLAY_PARTIAL, DISPLAY_FILE) < 0) perror(DISPLAY_FILE);
    free(written_state);
    written_state = serialized;
}

static void update_canvas(void) {
    int width = canvas_width, height = canvas_height;
    struct window *window;
    if (resolution > 0) {
        width = resolution * ASPECT_WIDTH / ASPECT_HEIGHT;
        height = resolution;
    } else if (active) {
        struct wlr_box box = area();
        width = box.width;
        height = box.height;
    }
    if (width == canvas_width && height == canvas_height) return;
    canvas_width = width;
    canvas_height = height;
    wl_list_for_each(window, &windows, link)
        if (window->toplevel->base->initialized) size_window(window);
    configure_overlay();
    redraw();
}

static struct wlr_output_mode *display_mode(struct wlr_output *output) {
    struct wlr_output_mode *mode, *base = wlr_output_preferred_mode(output), *best;
    int refresh = GAME_REFRESH_MHZ;
    char name[TEXT_MAX];
    wl_list_for_each(mode, &output->modes, link) {
        name_mode(mode, name);
        if (!strcmp(name, chosen_mode)) base = mode, refresh = mode->refresh;
    }
    if (shown_refresh) refresh = shown_refresh;
    best = base;
    wl_list_for_each(mode, &output->modes, link)
        if (mode->width == base->width && mode->height == base->height && abs(mode->refresh - refresh) < abs(best->refresh - refresh))
            best = mode;
    return best;
}

static void activate(struct display *wanted) {
    struct display *entry;
    wl_list_for_each(entry, &displays, link) {
        struct wlr_output_state state;
        struct wlr_output_mode *mode = entry == wanted ? display_mode(entry->output) : NULL;
        wlr_output_state_init(&state);
        wlr_output_state_set_enabled(&state, entry == wanted);
        if (mode) {
            applied_mode = mode;
            wlr_output_state_set_mode(&state, mode);
            if (!wlr_output_test_state(entry->output, &state)) wlr_output_state_set_mode(&state, wlr_output_preferred_mode(entry->output));
        }
        if (!wlr_output_commit_state(entry->output, &state)) fprintf(stderr, "compositor: could not configure %s\n", entry->output->name);
        wlr_output_state_finish(&state);
        if (entry == wanted) wlr_output_layout_add_auto(layout, entry->output);
        else {
            wlr_output_layout_remove(layout, entry->output);
            wlr_output_destroy_global(entry->output);
        }
    }
    active = wanted;
    redraw();
}

static void hand_over(struct gpu *gpu) {
    FILE *file = fopen(RENDER_GPU_FILE, "w");
    if (!file || fprintf(file, "%u:%u\n", major(gpu->device->dev), minor(gpu->device->dev)) < 0 || fclose(file) < 0) perror(RENDER_GPU_FILE);
    stopping = 1;
    wl_display_terminate(display);
}

static void choose_display(void) {
    struct display *best = NULL, *entry;
    if (stopping) return;
    wl_list_for_each(entry, &displays, link) if (better(entry, best)) best = entry;
    pending = best && best->gpu != render_gpu && best->gpu->renders ? best : NULL;
    if (pending && !games_running()) {
        hand_over(pending->gpu);
        return;
    }
    struct display *wanted = pending && active ? active : best;
    if (wanted != active || (wanted && display_mode(wanted->output) != applied_mode)) activate(wanted);
    update_canvas();
    write_state();
}

static int overlay_visible(void) { return overlay && overlay->surface->mapped; }

static int scan_out(struct wlr_output_state *state) {
    struct wlr_surface *surface = shown ? shown->toplevel->base->surface : NULL;
    if (!surface || !surface->buffer || overlay_visible() || retro) return 0;
    struct wlr_buffer *buffer = &surface->buffer->base;
    struct wlr_box picture = place(area(), shown->display_width, buffer->height);
    if (picture.width != buffer->width || picture.height != buffer->height) return 0;
    wlr_output_state_set_buffer(state, buffer);
    state->buffer_dst_box = picture;
    if (wlr_output_test_state(active->output, state)) return 1;
    wlr_output_state_finish(state);
    wlr_output_state_init(state);
    return 0;
}

static struct wlr_box scaled(struct wlr_box inner, struct wlr_box box) {
    return (struct wlr_box){box.x + inner.x * box.width / canvas_width, box.y + inner.y * box.height / canvas_height,
                            inner.width * box.width / canvas_width, inner.height * box.height / canvas_height};
}

static void draw_scene(struct wlr_render_pass *pass, struct wlr_box target, struct wlr_box box, struct wlr_texture *game, int game_width, struct wlr_texture *interface,
                       enum wlr_scale_filter_mode mode) {
    struct wlr_box picture = game ? place(box, game_width, game->height) : (struct wlr_box){0};
    pixman_region32_t bars;
    pixman_region32_init_rect(&bars, picture.x, picture.y, picture.width, picture.height);
    pixman_region32_inverse(&bars, &bars, &(pixman_box32_t){target.x, target.y, target.x + target.width, target.y + target.height});
    wlr_render_pass_add_rect(pass, &(struct wlr_render_rect_options){.box = target, .color = {0, 0, 0, 1}, .clip = &bars});
    pixman_region32_fini(&bars);
    if (game) wlr_render_pass_add_texture(pass, &(struct wlr_render_texture_options){.texture = game, .dst_box = picture, .filter_mode = mode});
    if (interface)
        wlr_render_pass_add_texture(pass, &(struct wlr_render_texture_options){.texture = interface, .dst_box = scaled(overlay_box(), box), .filter_mode = mode});
}

static void init_region(void *state) { pixman_region32_init(state); }

static void finish_region(void *state) { pixman_region32_fini(state); }

static void move_region(void *destination, void *source) { pixman_region32_copy(destination, source); }

static const struct wlr_surface_synced_impl blur_state = {
    .state_size = sizeof(pixman_region32_t), .init_state = init_region, .finish_state = finish_region, .move_state = move_region};

static void forget_blur(struct blur *blur) {
    wl_resource_set_user_data(blur->resource, NULL);
    wl_list_remove(&blur->surface_destroy.link);
    wl_list_remove(&blur->link);
    wlr_surface_synced_finish(&blur->synced);
    pixman_region32_fini(&blur->pending);
    pixman_region32_fini(&blur->current);
    free(blur);
}

static void on_blurred_surface_destroy(struct wl_listener *listener, void *data) {
    (void)data;
    struct blur *blur = wl_container_of(listener, blur, surface_destroy);
    forget_blur(blur);
}

static void destroy_resource(struct wl_client *client, struct wl_resource *resource) {
    (void)client;
    wl_resource_destroy(resource);
}

static void set_blur_region(struct wl_client *client, struct wl_resource *resource, struct wl_resource *region) {
    (void)client;
    struct blur *blur = wl_resource_get_user_data(resource);
    if (!blur) {
        wl_resource_post_error(resource, EXT_BACKGROUND_EFFECT_SURFACE_V1_ERROR_SURFACE_DESTROYED, "the surface was destroyed");
        return;
    }
    if (region) pixman_region32_copy(&blur->pending, wlr_region_from_resource(region));
    else pixman_region32_clear(&blur->pending);
}

static const struct ext_background_effect_surface_v1_interface blur_interface = {.destroy = destroy_resource, .set_blur_region = set_blur_region};

static void on_blur_resource_destroy(struct wl_resource *resource) {
    struct blur *blur = wl_resource_get_user_data(resource);
    if (blur) forget_blur(blur);
}

static void get_background_effect(struct wl_client *client, struct wl_resource *manager, uint32_t id, struct wl_resource *surface_resource) {
    struct wlr_surface *surface = wlr_surface_from_resource(surface_resource);
    struct blur *blur;
    wl_list_for_each(blur, &blurs, link) {
        if (blur->synced.surface != surface) continue;
        wl_resource_post_error(manager, EXT_BACKGROUND_EFFECT_MANAGER_V1_ERROR_BACKGROUND_EFFECT_EXISTS, "the surface already has a background effect");
        return;
    }
    blur = calloc(1, sizeof *blur);
    blur->resource = wl_resource_create(client, &ext_background_effect_surface_v1_interface, wl_resource_get_version(manager), id);
    wl_resource_set_implementation(blur->resource, &blur_interface, blur, on_blur_resource_destroy);
    wlr_surface_synced_init(&blur->synced, surface, &blur_state, &blur->pending, &blur->current);
    listen(&blur->surface_destroy, &surface->events.destroy, on_blurred_surface_destroy);
    wl_list_insert(&blurs, &blur->link);
}

static const struct ext_background_effect_manager_v1_interface effects_interface = {.destroy = destroy_resource, .get_background_effect = get_background_effect};

static void bind_effects(struct wl_client *client, void *data, uint32_t version, uint32_t id) {
    (void)data;
    struct wl_resource *resource = wl_resource_create(client, &ext_background_effect_manager_v1_interface, version, id);
    wl_resource_set_implementation(resource, &effects_interface, NULL, NULL);
    ext_background_effect_manager_v1_send_capabilities(resource, EXT_BACKGROUND_EFFECT_MANAGER_V1_CAPABILITY_BLUR);
}

static const pixman_region32_t *overlay_blur(void) {
    struct blur *blur;
    if (!overlay_visible()) return NULL;
    wl_list_for_each(blur, &blurs, link) {
        if (blur->synced.surface == overlay->surface && pixman_region32_not_empty(&blur->current)) return &blur->current;
    }
    return NULL;
}

static struct wlr_buffer *acquire(struct wlr_swapchain **chain, int width, int height) {
    if (!*chain || (*chain)->width != width || (*chain)->height != height) {
        wlr_swapchain_destroy(*chain);
        *chain = wlr_swapchain_create(allocator, width, height, &active->output->swapchain->format);
    }
    return *chain ? wlr_swapchain_acquire(*chain) : NULL;
}

static struct wlr_texture *scaled_copy(struct wlr_swapchain **chain, struct wlr_texture *source, int width, int height, struct wlr_buffer **held) {
    *held = acquire(chain, width, height);
    if (!*held) return NULL;
    struct wlr_render_pass *pass = wlr_renderer_begin_buffer_pass(renderer, *held, NULL);
    wlr_render_pass_add_texture(pass, &(struct wlr_render_texture_options){
                                          .texture = source, .dst_box = {0, 0, width, height}, .filter_mode = WLR_SCALE_FILTER_BILINEAR, .blend_mode = WLR_RENDER_BLEND_MODE_NONE});
    wlr_render_pass_submit(pass);
    return wlr_texture_from_buffer(renderer, *held);
}

static struct wlr_texture *blurred(struct wlr_texture *scene, struct wlr_buffer *held[2 * BLUR_LEVELS]) {
    static struct wlr_swapchain *chains[2 * BLUR_LEVELS];
    struct wlr_texture *step = scene, *steps[2 * BLUR_LEVELS] = {0};
    for (int i = 0; i < 2 * BLUR_LEVELS - 1 && step; i++) {
        int level = i < BLUR_LEVELS ? i + 1 : 2 * BLUR_LEVELS - 1 - i;
        step = steps[i] = scaled_copy(&chains[i], step, canvas_width >> level, canvas_height >> level, &held[i]);
    }
    for (int i = 0; i < 2 * BLUR_LEVELS - 2; i++)
        if (steps[i]) wlr_texture_destroy(steps[i]);
    return step;
}

static struct wlr_texture *compose(struct wlr_texture *game, int game_width, struct wlr_buffer **composed) {
    if (!overlay_visible()) return game;
    *composed = acquire(&canvas_chain, canvas_width, canvas_height);
    if (!*composed) return game;
    struct wlr_render_pass *pass = wlr_renderer_begin_buffer_pass(renderer, *composed, NULL);
    struct wlr_box canvas = {0, 0, canvas_width, canvas_height}, box = overlay_box();
    struct wlr_texture *interface = wlr_surface_get_texture(overlay->surface);
    const pixman_region32_t *blur = overlay_blur();
    draw_scene(pass, canvas, canvas, game, game_width, blur ? NULL : interface, WLR_SCALE_FILTER_BILINEAR);
    wlr_render_pass_submit(pass);
    if (!blur) return wlr_texture_from_buffer(renderer, *composed);
    struct wlr_buffer *held[2 * BLUR_LEVELS] = {0};
    struct wlr_texture *scene = wlr_texture_from_buffer(renderer, *composed), *soft = blurred(scene, held);
    pixman_region32_t clip;
    pixman_region32_init(&clip);
    pixman_region32_copy(&clip, blur);
    pixman_region32_translate(&clip, box.x, box.y);
    pass = wlr_renderer_begin_buffer_pass(renderer, *composed, NULL);
    if (soft) wlr_render_pass_add_texture(pass, &(struct wlr_render_texture_options){.texture = soft, .dst_box = canvas, .clip = &clip, .filter_mode = WLR_SCALE_FILTER_BILINEAR});
    wlr_render_pass_add_texture(pass, &(struct wlr_render_texture_options){.texture = interface, .dst_box = box, .filter_mode = WLR_SCALE_FILTER_BILINEAR});
    wlr_render_pass_submit(pass);
    pixman_region32_fini(&clip);
    if (soft) wlr_texture_destroy(soft);
    wlr_texture_destroy(scene);
    for (int i = 0; i < 2 * BLUR_LEVELS; i++)
        if (held[i]) wlr_buffer_unlock(held[i]);
    return wlr_texture_from_buffer(renderer, *composed);
}

static struct wlr_gles2_texture_attribs current_attributes(struct wlr_texture *texture) {
    struct wlr_gles2_texture_attribs attributes;
    struct wlr_egl *egl = wlr_gles2_renderer_get_egl(renderer);
    wlr_gles2_texture_get_attribs(texture, &attributes);
    eglMakeCurrent(wlr_egl_get_display(egl), EGL_NO_SURFACE, EGL_NO_SURFACE, wlr_egl_get_context(egl));
    return attributes;
}

static void draw_quad(GLint position, GLint coordinate, const GLfloat *corners) {
    static const GLfloat coordinates[] = {0, 0, 1, 0, 0, 1, 1, 1};
    glVertexAttribPointer(position, 2, GL_FLOAT, GL_FALSE, 0, corners);
    glVertexAttribPointer(coordinate, 2, GL_FLOAT, GL_FALSE, 0, coordinates);
    glEnableVertexAttribArray(position);
    glEnableVertexAttribArray(coordinate);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, CORNERS);
    glDisableVertexAttribArray(position);
    glDisableVertexAttribArray(coordinate);
}

static struct program *pixel_program(GLenum target) {
    struct program *program = &programs[target == GL_TEXTURE_EXTERNAL_OES];
    if (program->id) return program;
    program->id = link_shaders(vertex_source, (const char *[]){sampler_prefixes[target == GL_TEXTURE_EXTERNAL_OES], pixel_source}, 2);
    program->position = glGetAttribLocation(program->id, "position");
    program->coordinate = glGetAttribLocation(program->id, "coordinate");
    program->size = glGetUniformLocation(program->id, "size");
    program->extent = glGetUniformLocation(program->id, "extent");
    return program;
}

static void blit(struct wlr_gles2_texture_attribs attributes, GLint filter, int width, int height, GLfloat extent_x, GLfloat extent_y, const GLfloat *corners) {
    struct program *program = pixel_program(attributes.target);
    glUseProgram(program->id);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(attributes.target, attributes.tex);
    glTexParameteri(attributes.target, GL_TEXTURE_MIN_FILTER, filter);
    glTexParameteri(attributes.target, GL_TEXTURE_MAG_FILTER, filter);
    glUniform2f(program->size, width, height);
    glUniform2f(program->extent, extent_x, extent_y);
    draw_quad(program->position, program->coordinate, corners);
    glBindTexture(attributes.target, 0);
}

static int draw_pixel(struct wlr_output_state *state, struct wlr_box box, struct wlr_texture *game, int game_width) {
    struct wlr_output *output = active->output;
    if (!wlr_output_configure_primary_swapchain(output, state, &output->swapchain)) return 0;
    struct wlr_buffer *buffer = wlr_swapchain_acquire(output->swapchain), *composed = NULL;
    if (!buffer) return 0;
    struct wlr_texture *texture = compose(game, game_width, &composed);
    if (!texture) {
        wlr_buffer_unlock(buffer);
        return 0;
    }
    GLuint framebuffer = wlr_gles2_renderer_get_buffer_fbo(renderer, buffer);
    struct wlr_box picture = composed ? box : place(box, game_width, texture->height);
    GLfloat left = 2.0f * picture.x / output->width - 1, right = 2.0f * (picture.x + picture.width) / output->width - 1;
    GLfloat top = 2.0f * picture.y / output->height - 1, bottom = 2.0f * (picture.y + picture.height) / output->height - 1;
    const GLfloat corners[] = {left, top, right, top, left, bottom, right, bottom};
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    glViewport(0, 0, output->width, output->height);
    glDisable(GL_BLEND);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    blit(current_attributes(texture), GL_LINEAR, texture->width, texture->height, (GLfloat)texture->width / picture.width,
        (GLfloat)texture->height / picture.height, corners);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glFlush();
    wlr_output_state_set_buffer(state, buffer);
    wlr_buffer_unlock(buffer);
    if (composed) {
        wlr_texture_destroy(texture);
        wlr_buffer_unlock(composed);
    }
    return 1;
}

static char *read_text(const char *path) {
    FILE *file = fopen(path, "r");
    char *text = NULL;
    size_t length = 0;
    getdelim(&text, &length, '\0', file);
    fclose(file);
    return text;
}

static void retro_locations(GLuint program, const char *name, GLint *locations) {
    char uniform[TEXT_MAX];
    for (int block = 0; block < RETRO_BLOCKS; block++) {
        snprintf(uniform, sizeof uniform, "%s.%s", retro_blocks[block], name);
        locations[block] = glGetUniformLocation(program, uniform);
    }
}

static void retro_size(const GLint *locations, int width, int height) {
    for (int block = 0; block < RETRO_BLOCKS; block++) glUniform4f(locations[block], width, height, 1.0f / width, 1.0f / height);
}

static enum scale_type parse_scale_type(char *name) {
    enum scale_type type = SOURCE_SCALE;
    for (size_t i = 0; i < sizeof scale_types / sizeof *scale_types; i++)
        if (!strcmp(name, scale_types[i])) type = i;
    free(name);
    return type;
}

static GLuint retro_program(int index) {
    static const GLfloat identity[] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    char path[PATH_MAX], *name;
    GLint locations[RETRO_BLOCKS];
    float value;
    snprintf(path, sizeof path, RETRO_PASS, index, "vert");
    char *vertex = read_text(path);
    snprintf(path, sizeof path, RETRO_PASS, index, "frag");
    char *fragment = read_text(path);
    GLuint program = link_shaders(vertex, (const char *[]){fragment}, 1);
    free(vertex);
    free(fragment);
    glUseProgram(program);
    glUniform1i(glGetUniformLocation(program, "Source"), 0);
    glUniform1i(glGetUniformLocation(program, "Original"), RETRO_ORIGINAL_UNIT);
    retro_locations(program, "MVP", locations);
    for (int block = 0; block < RETRO_BLOCKS; block++) glUniformMatrix4fv(locations[block], 1, GL_FALSE, identity);
    snprintf(path, sizeof path, RETRO_PASS, index, "parameters");
    FILE *parameters = fopen(path, "r");
    while (fscanf(parameters, "%ms %f", &name, &value) == 2) {
        retro_locations(program, name, locations);
        for (int block = 0; block < RETRO_BLOCKS; block++) glUniform1f(locations[block], value);
        free(name);
    }
    fclose(parameters);
    return program;
}

static void load_retro(void) {
    char *type_x, *type_y;
    int linear;
    struct retro_pass pass = {0};
    FILE *passes = fopen(RETRO_PASSES, "r");
    while (fscanf(passes, "%ms %f %ms %f %d %d %u", &type_x, &pass.scale_x, &type_y, &pass.scale_y, &linear, &pass.floating, &pass.frame_count_mod) == 7) {
        (retro_pass_count ? &retro_passes[retro_pass_count - 1].target : &retro_original)->filter = linear ? GL_LINEAR : GL_NEAREST;
        pass.type_x = parse_scale_type(type_x);
        pass.type_y = parse_scale_type(type_y);
        pass.program = retro_program(retro_pass_count);
        retro_locations(pass.program, "OutputSize", pass.output_size);
        retro_locations(pass.program, "SourceSize", pass.source_size);
        retro_locations(pass.program, "OriginalSize", pass.original_size);
        retro_locations(pass.program, "FrameCount", pass.frame_count);
        retro_passes = realloc(retro_passes, (retro_pass_count + 1) * sizeof *retro_passes);
        retro_passes[retro_pass_count++] = pass;
    }
    fclose(passes);
}

static void bind_target(struct retro_target *target, int width, int height, int floating) {
    if (target->width == width && target->height == height) {
        glBindFramebuffer(GL_FRAMEBUFFER, target->framebuffer);
        return;
    }
    if (!target->texture) {
        glGenTextures(1, &target->texture);
        glGenFramebuffers(1, &target->framebuffer);
    }
    target->width = width;
    target->height = height;
    attach_texture(target->texture, target->framebuffer, width, height, floating ? GL_RGBA16F : GL_RGBA8, floating ? GL_HALF_FLOAT : GL_UNSIGNED_BYTE,
        target->filter, GL_CLAMP_TO_BORDER);
}

static int pass_size(enum scale_type type, float scale, int source, int viewport) {
    switch (type) {
    case VIEWPORT_SCALE: return viewport * scale;
    case ABSOLUTE_SCALE: return scale;
    default: return source * scale;
    }
}

static struct wlr_texture *retro_picture(struct wlr_texture *game, struct wlr_buffer **held) {
    static const GLfloat corners[] = {-1, -1, 1, -1, -1, 1, 1, 1};
    struct wlr_box screen = area(), picture = place(screen, shown->display_width, game->height);
    struct wlr_gles2_texture_attribs attributes = current_attributes(game);
    if (!retro_passes) load_retro();
    glDisable(GL_BLEND);
    bind_target(&retro_original, game->width, game->height, 0);
    glViewport(0, 0, game->width, game->height);
    blit(attributes, GL_NEAREST, game->width, game->height, 1, 1, corners);
    glActiveTexture(GL_TEXTURE0 + RETRO_ORIGINAL_UNIT);
    glBindTexture(GL_TEXTURE_2D, retro_original.texture);
    glActiveTexture(GL_TEXTURE0);
    struct retro_target *source = &retro_original;
    for (int i = 0; i < retro_pass_count; i++) {
        struct retro_pass *pass = &retro_passes[i];
        int width = pass_size(pass->type_x, pass->scale_x, source->width, picture.width), height = pass_size(pass->type_y, pass->scale_y, source->height, picture.height);
        GLuint frame = pass->frame_count_mod ? retro_frames % pass->frame_count_mod : retro_frames;
        int x = 0, y = 0;
        if (i < retro_pass_count - 1) bind_target(&pass->target, width, height, pass->floating);
        else {
            *held = acquire(&retro_chain, screen.width, screen.height);
            if (!*held) break;
            glBindFramebuffer(GL_FRAMEBUFFER, wlr_gles2_renderer_get_buffer_fbo(renderer, *held));
            glClearColor(0, 0, 0, 1);
            glClear(GL_COLOR_BUFFER_BIT);
            x = picture.x - screen.x;
            y = picture.y - screen.y;
        }
        glViewport(x, y, width, height);
        glUseProgram(pass->program);
        glBindTexture(GL_TEXTURE_2D, source->texture);
        for (int block = 0; block < RETRO_BLOCKS; block++) glUniform1ui(pass->frame_count[block], frame);
        retro_size(pass->output_size, width, height);
        retro_size(pass->source_size, source->width, source->height);
        retro_size(pass->original_size, game->width, game->height);
        draw_quad(RETRO_POSITION, RETRO_COORDINATE, corners);
        source = &pass->target;
    }
    retro_frames++;
    glActiveTexture(GL_TEXTURE0 + RETRO_ORIGINAL_UNIT);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glFlush();
    return *held ? wlr_texture_from_buffer(renderer, *held) : game;
}

static void paint(struct wlr_output_state *state) {
    struct wlr_output *output = active->output;
    struct wlr_box box = area();
    struct wlr_texture *surface = shown ? wlr_surface_get_texture(shown->toplevel->base->surface) : NULL, *game = surface;
    struct wlr_texture *interface = overlay_visible() ? wlr_surface_get_texture(overlay->surface) : NULL;
    struct wlr_buffer *held = NULL;
    if (retro && surface) game = retro_picture(surface, &held);
    int width = held ? (int)game->width : shown ? shown->display_width : 0;
    enum filter filter = game && !interface ? choose_filter(place(box, width, game->height), game->width, game->height)
                                            : choose_filter(box, canvas_width, canvas_height);
    if (!((filter == PIXEL || overlay_blur()) && (game || interface) && draw_pixel(state, box, game, width))) {
        enum wlr_scale_filter_mode mode = filter == NEAREST ? WLR_SCALE_FILTER_NEAREST : WLR_SCALE_FILTER_BILINEAR;
        struct wlr_render_pass *pass = wlr_output_begin_render_pass(output, state, NULL);
        draw_scene(pass, (struct wlr_box){0, 0, output->width, output->height}, box, game, width, interface, mode);
        wlr_render_pass_submit(pass);
    }
    if (!held) return;
    wlr_texture_destroy(game);
    wlr_buffer_unlock(held);
}

static void on_frame(struct wl_listener *listener, void *data) {
    (void)data;
    struct display *entry = wl_container_of(listener, entry, frame);
    if (entry != active || (!dirty && !entry->output->needs_frame)) return;
    struct wlr_output_state state;
    dirty = 0;
    wlr_output_state_init(&state);
    if (!scan_out(&state)) paint(&state);
    // i915 can reject a flip with EBUSY right after the previous flip's event (drm/i915 kernel issue 13410);
    // retrying at the next refresh only works around it until that kernel race is fixed.
    if (!wlr_output_commit_state(entry->output, &state)) wl_event_source_timer_update(flip_retry, MSEC_PER_SEC * MHZ_PER_HZ / entry->output->refresh);
    wlr_output_state_finish(&state);
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    if (shown) wlr_surface_send_frame_done(shown->toplevel->base->surface, &now);
    if (overlay) wlr_surface_send_frame_done(overlay->surface, &now);
}

static int on_flip_retry(void *data) {
    (void)data;
    redraw();
    return 0;
}

static void on_needs_frame(struct wl_listener *listener, void *data) {
    (void)data;
    struct display *entry = wl_container_of(listener, entry, needs_frame);
    wlr_output_schedule_frame(entry->output);
}

static void on_display_destroy(struct wl_listener *listener, void *data) {
    (void)data;
    struct display *entry = wl_container_of(listener, entry, destroy);
    wl_list_remove(&entry->frame.link);
    wl_list_remove(&entry->needs_frame.link);
    wl_list_remove(&entry->destroy.link);
    wl_list_remove(&entry->link);
    if (active == entry) active = NULL;
    if (pending == entry) pending = NULL;
    free(entry);
    choose_display();
}

static void on_new_output(struct wl_listener *listener, void *data) {
    (void)listener;
    struct wlr_output *output = data;
    struct display *entry = calloc(1, sizeof *entry);
    wlr_output_init_render(output, allocator, renderer);
    entry->output = output;
    entry->connected = started ? ++connections : 0;
    for (int i = 0; i < gpu_count; i++)
        if (gpus[i].backend == output->backend) entry->gpu = &gpus[i];
    listen(&entry->frame, &output->events.frame, on_frame);
    listen(&entry->needs_frame, &output->events.needs_frame, on_needs_frame);
    listen(&entry->destroy, &output->events.destroy, on_display_destroy);
    wl_list_insert(&displays, &entry->link);
    if (started) choose_display();
}

static void read_manifest(struct window *window) {
    const char *platform = window ? strchr(window->app_id, '/') : NULL;
    char directory[PATH_MAX], path[sizeof directory + sizeof "/" MANIFEST];
    json_object *manifest, *settings, *refresh;
    if (manifest_watch >= 0) inotify_rm_watch(watcher, manifest_watch);
    manifest_watch = -1;
    shown_refresh = 0;
    if (!platform) return;
    snprintf(directory, sizeof directory, GAMES_DIR "/%.*s", (int)(platform - window->app_id), window->app_id);
    snprintf(path, sizeof path, "%s/" MANIFEST, directory);
    manifest_watch = inotify_add_watch(watcher, directory, IN_MOVED_TO | IN_CLOSE_WRITE);
    manifest = json_object_from_file(path);
    if (json_object_object_get_ex(manifest, platform + 1, &settings) && json_object_object_get_ex(settings, "refresh", &refresh))
        shown_refresh = (int)lround(json_object_get_double(refresh) * MHZ_PER_HZ);
    json_object_put(manifest);
}

static void show(struct window *window) {
    if (shown) {
        wlr_xdg_toplevel_set_activated(shown->toplevel, false);
        wlr_foreign_toplevel_handle_v1_set_activated(shown->handle, false);
    }
    shown = window;
    if (window) {
        wlr_xdg_toplevel_set_activated(window->toplevel, true);
        struct wlr_surface *surface = window->toplevel->base->surface;
        struct wlr_keyboard *keyboard = wlr_seat_get_keyboard(seat);
        wlr_foreign_toplevel_handle_v1_set_activated(window->handle, true);
        if (keyboard) wlr_seat_keyboard_notify_enter(seat, surface, keyboard->keycodes, keyboard->num_keycodes, &keyboard->modifiers);
        else wlr_seat_keyboard_notify_enter(seat, surface, NULL, 0, NULL);
        wlr_seat_pointer_notify_enter(seat, surface, pointer_x, pointer_y);
    } else {
        wlr_seat_keyboard_notify_clear_focus(seat);
        wlr_seat_pointer_notify_clear_focus(seat);
    }
    read_manifest(window);
    choose_display();
    redraw();
}

static void on_window_activate(struct wl_listener *listener, void *data) {
    (void)data;
    struct window *window = wl_container_of(listener, window, activate);
    show(window);
}

static void schedule_game_frame(struct wlr_ext_image_capture_source_v1 *source) {
    pixman_region32_t damage;
    pixman_region32_init_rect(&damage, 0, 0, source->width, source->height);
    wl_signal_emit_mutable(&source->events.frame, &(struct wlr_ext_image_capture_source_v1_frame_event){.damage = &damage});
    pixman_region32_fini(&damage);
}

static void copy_game_frame(struct wlr_ext_image_capture_source_v1 *source, struct wlr_ext_image_copy_capture_frame_v1 *frame,
                            struct wlr_ext_image_capture_source_v1_frame_event *event) {
    (void)event;
    struct window *window = wl_container_of(source, window, source);
    struct wlr_texture *texture = wlr_surface_get_texture(window->toplevel->base->surface);
    if (!texture || texture->width != (uint32_t)frame->buffer->width || texture->height != (uint32_t)frame->buffer->height) {
        wlr_ext_image_copy_capture_frame_v1_fail(frame, EXT_IMAGE_COPY_CAPTURE_FRAME_V1_FAILURE_REASON_BUFFER_CONSTRAINTS);
        return;
    }
    struct wlr_render_pass *pass = wlr_renderer_begin_buffer_pass(renderer, frame->buffer, NULL);
    if (!pass) {
        wlr_ext_image_copy_capture_frame_v1_fail(frame, EXT_IMAGE_COPY_CAPTURE_FRAME_V1_FAILURE_REASON_UNKNOWN);
        return;
    }
    wlr_render_pass_add_texture(pass, &(struct wlr_render_texture_options){.texture = texture, .blend_mode = WLR_RENDER_BLEND_MODE_NONE});
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    if (wlr_render_pass_submit(pass)) wlr_ext_image_copy_capture_frame_v1_ready(frame, WL_OUTPUT_TRANSFORM_NORMAL, &now);
    else wlr_ext_image_copy_capture_frame_v1_fail(frame, EXT_IMAGE_COPY_CAPTURE_FRAME_V1_FAILURE_REASON_UNKNOWN);
}

static const struct wlr_ext_image_capture_source_v1_interface game_source = {.schedule_frame = schedule_game_frame, .copy_frame = copy_game_frame};

static int display_width(struct window *window) {
    const char *platform = strchr(window->app_id, '/');
    char path[PATH_MAX], *line = NULL, *field;
    size_t size = 0;
    float logged;
    if (!platform) return window->source.width;
    snprintf(path, sizeof path, LOG_DIR "/%.*s-%s.log", (int)(platform - window->app_id), window->app_id, platform + 1);
    FILE *log = fopen(path, "r");
    if (log && !fseek(log, window->log_read, SEEK_SET)) {
        while (getline(&line, &size, log) > 0)
            if ((field = strstr(line, ASPECT_FIELD)) && sscanf(field, ASPECT_FIELD "%f", &logged) == 1) window->aspect = logged;
        window->log_read = ftell(log);
    }
    free(line);
    if (log) fclose(log);
    return window->aspect > 0 ? (int)lroundf(window->source.height * window->aspect) : (int)window->source.width;
}

static void on_window_commit(struct wl_listener *listener, void *data) {
    (void)data;
    struct window *window = wl_container_of(listener, window, commit);
    if (window->toplevel->base->initial_commit) size_window(window);
    struct wlr_surface *surface = window->toplevel->base->surface;
    if (surface->buffer && ((uint32_t)surface->buffer->base.width != window->source.width || (uint32_t)surface->buffer->base.height != window->source.height)) {
        window->source.width = surface->buffer->base.width;
        window->source.height = surface->buffer->base.height;
        window->display_width = display_width(window);
        wl_signal_emit_mutable(&window->source.events.constraints_update, NULL);
        write_state();
    }
    if (window == shown) redraw();
}

static void on_window_request_fullscreen(struct wl_listener *listener, void *data) {
    (void)data;
    struct window *window = wl_container_of(listener, window, request_fullscreen);
    if (window->toplevel->base->initialized) size_window(window);
}

static void on_window_map(struct wl_listener *listener, void *data) {
    (void)data;
    struct window *window = wl_container_of(listener, window, map);
    window->handle = wlr_foreign_toplevel_handle_v1_create(foreign);
    wlr_foreign_toplevel_handle_v1_set_app_id(window->handle, window->app_id);
    listen(&window->activate, &window->handle->events.request_activate, on_window_activate);
    window->listed = wlr_ext_foreign_toplevel_handle_v1_create(listing, &(struct wlr_ext_foreign_toplevel_handle_v1_state){.app_id = window->app_id});
    window->listed->data = window;
    show(window);
}

static void on_window_unmap(struct wl_listener *listener, void *data) {
    (void)data;
    struct window *window = wl_container_of(listener, window, unmap);
    if (window == shown) show(NULL);
    wl_list_remove(&window->activate.link);
    wlr_foreign_toplevel_handle_v1_destroy(window->handle);
    wlr_ext_foreign_toplevel_handle_v1_destroy(window->listed);
}

static void on_window_destroy(struct wl_listener *listener, void *data) {
    (void)data;
    struct window *window = wl_container_of(listener, window, destroy);
    wl_list_remove(&window->commit.link);
    wl_list_remove(&window->map.link);
    wl_list_remove(&window->unmap.link);
    wl_list_remove(&window->destroy.link);
    wl_list_remove(&window->request_fullscreen.link);
    wl_list_remove(&window->link);
    wlr_ext_image_capture_source_v1_finish(&window->source);
    free(window);
    choose_display();
}

static void on_new_toplevel(struct wl_listener *listener, void *data) {
    (void)listener;
    struct wlr_xdg_toplevel *toplevel = data;
    struct window *window = calloc(1, sizeof *window);
    const struct wlr_security_context_v1_state *context =
        wlr_security_context_manager_v1_lookup_client(security, wl_resource_get_client(toplevel->resource));
    window->toplevel = toplevel;
    window->sandboxed = context != NULL;
    wlr_ext_image_capture_source_v1_init(&window->source, &game_source);
    window->source.dmabuf_device = render_gpu->device->dev;
    wlr_drm_format_set_add(&window->source.dmabuf_formats, DRM_FORMAT_XRGB8888, DRM_FORMAT_MOD_LINEAR);
    snprintf(window->app_id, sizeof window->app_id, "%s", context && context->app_id ? context->app_id : "");
    listen(&window->commit, &toplevel->base->surface->events.commit, on_window_commit);
    listen(&window->map, &toplevel->base->surface->events.map, on_window_map);
    listen(&window->unmap, &toplevel->base->surface->events.unmap, on_window_unmap);
    listen(&window->destroy, &toplevel->events.destroy, on_window_destroy);
    listen(&window->request_fullscreen, &toplevel->events.request_fullscreen, on_window_request_fullscreen);
    wl_list_insert(&windows, &window->link);
}

static void on_overlay_commit(struct wl_listener *listener, void *data) {
    (void)listener, (void)data;
    if (overlay->initial_commit) configure_overlay();
    redraw();
}

static void on_overlay_destroy(struct wl_listener *listener, void *data) {
    (void)listener, (void)data;
    wl_list_remove(&overlay_commit.link);
    wl_list_remove(&overlay_destroy.link);
    overlay = NULL;
    redraw();
}

static void on_new_layer_surface(struct wl_listener *listener, void *data) {
    (void)listener;
    struct wlr_layer_surface_v1 *layer = data;
    if (overlay) {
        wlr_layer_surface_v1_destroy(layer);
        return;
    }
    overlay = layer;
    listen(&overlay_commit, &layer->surface->events.commit, on_overlay_commit);
    listen(&overlay_destroy, &layer->events.destroy, on_overlay_destroy);
}

static void on_key(struct wl_listener *listener, void *data) {
    struct keyboard *entry = wl_container_of(listener, entry, key);
    struct wlr_keyboard_key_event *event = data;
    wlr_seat_set_keyboard(seat, entry->keyboard);
    wlr_seat_keyboard_notify_key(seat, event->time_msec, event->keycode, event->state);
}

static void on_modifiers(struct wl_listener *listener, void *data) {
    (void)data;
    struct keyboard *entry = wl_container_of(listener, entry, modifiers);
    wlr_seat_set_keyboard(seat, entry->keyboard);
    wlr_seat_keyboard_notify_modifiers(seat, &entry->keyboard->modifiers);
}

static void on_keyboard_destroy(struct wl_listener *listener, void *data) {
    (void)data;
    struct keyboard *entry = wl_container_of(listener, entry, destroy);
    wl_list_remove(&entry->key.link);
    wl_list_remove(&entry->modifiers.link);
    wl_list_remove(&entry->destroy.link);
    free(entry);
}

static void on_motion(struct wl_listener *listener, void *data) {
    (void)listener;
    struct wlr_pointer_motion_event *event = data;
    pointer_x = fmin(fmax(pointer_x + event->delta_x, 0), canvas_width);
    pointer_y = fmin(fmax(pointer_y + event->delta_y, 0), canvas_height);
    wlr_relative_pointer_manager_v1_send_relative_motion(relative, seat, (uint64_t)event->time_msec * USEC_PER_MSEC, event->delta_x,
                                                         event->delta_y, event->unaccel_dx, event->unaccel_dy);
    wlr_seat_pointer_notify_motion(seat, event->time_msec, pointer_x, pointer_y);
}

static void on_button(struct wl_listener *listener, void *data) {
    (void)listener;
    struct wlr_pointer_button_event *event = data;
    wlr_seat_pointer_notify_button(seat, event->time_msec, event->button, event->state);
}

static void on_axis(struct wl_listener *listener, void *data) {
    (void)listener;
    struct wlr_pointer_axis_event *event = data;
    wlr_seat_pointer_notify_axis(seat, event->time_msec, event->orientation, event->delta, event->delta_discrete, event->source,
                                 event->relative_direction);
}

static void on_pointer_frame(struct wl_listener *listener, void *data) {
    (void)listener, (void)data;
    wlr_seat_pointer_notify_frame(seat);
}

static void on_pointer_destroy(struct wl_listener *listener, void *data) {
    (void)data;
    struct pointer *entry = wl_container_of(listener, entry, destroy);
    wl_list_remove(&entry->motion.link);
    wl_list_remove(&entry->button.link);
    wl_list_remove(&entry->axis.link);
    wl_list_remove(&entry->frame.link);
    wl_list_remove(&entry->destroy.link);
    free(entry);
}

static void on_new_input(struct wl_listener *listener, void *data) {
    (void)listener;
    struct wlr_input_device *device = data;
    if (device->type == WLR_INPUT_DEVICE_KEYBOARD) {
        struct keyboard *entry = calloc(1, sizeof *entry);
        entry->keyboard = wlr_keyboard_from_input_device(device);
        wlr_keyboard_set_keymap(entry->keyboard, keymap);
        listen(&entry->key, &entry->keyboard->events.key, on_key);
        listen(&entry->modifiers, &entry->keyboard->events.modifiers, on_modifiers);
        listen(&entry->destroy, &device->events.destroy, on_keyboard_destroy);
        wlr_seat_set_keyboard(seat, entry->keyboard);
    } else if (device->type == WLR_INPUT_DEVICE_POINTER) {
        struct pointer *entry = calloc(1, sizeof *entry);
        struct wlr_pointer *pointer = wlr_pointer_from_input_device(device);
        listen(&entry->motion, &pointer->events.motion, on_motion);
        listen(&entry->button, &pointer->events.button, on_button);
        listen(&entry->axis, &pointer->events.axis, on_axis);
        listen(&entry->frame, &pointer->events.frame, on_pointer_frame);
        listen(&entry->destroy, &device->events.destroy, on_pointer_destroy);
    }
}

static void on_new_constraint(struct wl_listener *listener, void *data) {
    (void)listener;
    struct wlr_pointer_constraint_v1 *constraint = data;
    if (shown && constraint->surface == shown->toplevel->base->surface) wlr_pointer_constraint_v1_send_activated(constraint);
}

static void create_toplevel_source(struct wl_client *client, struct wl_resource *manager, uint32_t id, struct wl_resource *toplevel) {
    (void)manager;
    struct wlr_ext_foreign_toplevel_handle_v1 *handle = wlr_ext_foreign_toplevel_handle_v1_from_resource(toplevel);
    wlr_ext_image_capture_source_v1_create_resource(handle ? &((struct window *)handle->data)->source : NULL, client, id);
}

static const struct ext_foreign_toplevel_image_capture_source_manager_v1_interface toplevel_capture_requests = {
    .create_source = create_toplevel_source, .destroy = destroy_resource};

static void bind_toplevel_capture(struct wl_client *client, void *data, uint32_t version, uint32_t id) {
    (void)data;
    struct wl_resource *resource = wl_resource_create(client, &ext_foreign_toplevel_image_capture_source_manager_v1_interface, version, id);
    if (!resource) {
        wl_client_post_no_memory(client);
        return;
    }
    wl_resource_set_implementation(resource, &toplevel_capture_requests, NULL, NULL);
}

static bool visible_to(const struct wl_client *client, const struct wl_global *global, void *data) {
    (void)data;
    return !wlr_security_context_manager_v1_lookup_client(security, client) ||
           (global != layer_shell->global && global != security->global && global != foreign->global && global != screencopy->global &&
            global != listing->global && global != image_copy->global && global != toplevel_capture);
}

static void load_settings(void) {
    json_object *settings = json_object_from_file(SETTINGS_FILE), *field;
    snprintf(pinned, sizeof pinned, "%s", json_object_object_get_ex(settings, "display", &field) ? json_object_get_string(field) : "");
    resolution = json_object_object_get_ex(settings, "resolution", &field) ? json_object_get_int(field) : 0;
    snprintf(chosen_mode, sizeof chosen_mode, "%s", json_object_object_get_ex(settings, "mode", &field) ? json_object_get_string(field) : "");
    retro = json_object_object_get_ex(settings, "picture", &field) && !strcmp(json_object_get_string(field), RETRO_PICTURE);
    json_object_put(settings);
    redraw();
}

static int on_watched_change(int fd, uint32_t mask, void *data) {
    (void)mask, (void)data;
    char buffer[BUFSIZ] __attribute__((aligned(__alignof__(struct inotify_event))));
    ssize_t count = read(fd, buffer, sizeof buffer);
    for (char *at = buffer; count > 0 && at < buffer + count;) {
        struct inotify_event *event = (struct inotify_event *)at;
        at += sizeof *event + event->len;
        if (!event->len) continue;
        if (event->wd == manifest_watch && !strcmp(event->name, MANIFEST)) read_manifest(shown);
        else if (!strcmp(event->name, basename(SETTINGS_FILE))) load_settings();
        else continue;
        choose_display();
    }
    return 0;
}

static int on_child(int signal_number, void *data) {
    (void)signal_number, (void)data;
    for (pid_t pid; (pid = waitpid(-1, NULL, WNOHANG)) > 0;)
        if (pid == session_leader) wl_display_terminate(display);
    return 0;
}

static pid_t spawn_session(char *argv[]) {
    posix_spawnattr_t attributes;
    sigset_t none;
    pid_t pid;
    sigemptyset(&none);
    posix_spawnattr_init(&attributes);
    posix_spawnattr_setflags(&attributes, POSIX_SPAWN_SETSIGMASK | POSIX_SPAWN_SETPGROUP);
    posix_spawnattr_setsigmask(&attributes, &none);
    int error = posix_spawnp(&pid, argv[0], NULL, &attributes, argv, environ);
    posix_spawnattr_destroy(&attributes);
    if (error) { fprintf(stderr, "%s: %s\n", argv[0], strerror(error)); return -1; }
    return pid;
}

int main(int argc, char *argv[]) {
    static struct wl_listener new_output, new_input, new_toplevel, new_layer_surface, new_constraint;
    char settings_directory[PATH_MAX];
    if (argc < 2) { fprintf(stderr, "usage: compositor <session command>...\n"); return 1; }
    wlr_log_init(WLR_INFO, NULL);
    wl_list_init(&displays);
    wl_list_init(&windows);
    wl_list_init(&blurs);
    display = wl_display_create();
    struct wl_event_loop *loop = wl_display_get_event_loop(display);
    session = wlr_session_create(loop);
    backend = wlr_multi_backend_create(loop);
    if (!session || !start_gpus()) { fprintf(stderr, "compositor: no usable GPU\n"); return 1; }
    wlr_multi_backend_add(backend, wlr_libinput_backend_create(session));
    renderer = wlr_renderer_autocreate(backend);
    allocator = renderer ? wlr_allocator_autocreate(backend, renderer) : NULL;
    if (!allocator) { fprintf(stderr, "compositor: no renderer\n"); return 1; }
    wlr_renderer_init_wl_display(renderer, display);
    wlr_compositor_create(display, COMPOSITOR_VERSION, renderer);
    struct wlr_xdg_shell *xdg_shell = wlr_xdg_shell_create(display, XDG_SHELL_VERSION);
    layer_shell = wlr_layer_shell_v1_create(display, LAYER_SHELL_VERSION);
    security = wlr_security_context_manager_v1_create(display);
    foreign = wlr_foreign_toplevel_manager_v1_create(display);
    screencopy = wlr_screencopy_manager_v1_create(display);
    listing = wlr_ext_foreign_toplevel_list_v1_create(display, TOPLEVEL_LIST_VERSION);
    image_copy = wlr_ext_image_copy_capture_manager_v1_create(display, IMAGE_COPY_CAPTURE_VERSION);
    toplevel_capture = wl_global_create(display, &ext_foreign_toplevel_image_capture_source_manager_v1_interface, ext_foreign_toplevel_image_capture_source_manager_v1_interface.version, NULL,
                                        bind_toplevel_capture);
    wl_global_create(display, &ext_background_effect_manager_v1_interface, BACKGROUND_EFFECT_VERSION, NULL, bind_effects);
    layout = wlr_output_layout_create(display);
    wlr_xdg_output_manager_v1_create(display, layout);
    wlr_viewporter_create(display);
    relative = wlr_relative_pointer_manager_v1_create(display);
    struct wlr_pointer_constraints_v1 *constraints = wlr_pointer_constraints_v1_create(display);
    seat = wlr_seat_create(display, SEAT_NAME);
    wlr_seat_set_capabilities(seat, WL_SEAT_CAPABILITY_KEYBOARD | WL_SEAT_CAPABILITY_POINTER);
    struct xkb_context *context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    keymap = xkb_keymap_new_from_names(context, NULL, XKB_KEYMAP_COMPILE_NO_FLAGS);
    wl_display_set_global_filter(display, visible_to, NULL);
    listen(&new_output, &backend->events.new_output, on_new_output);
    listen(&new_input, &backend->events.new_input, on_new_input);
    listen(&new_toplevel, &xdg_shell->events.new_toplevel, on_new_toplevel);
    listen(&new_layer_surface, &layer_shell->events.new_surface, on_new_layer_surface);
    listen(&new_constraint, &constraints->events.new_constraint, on_new_constraint);
    const char *socket = wl_display_add_socket_auto(display);
    if (!socket) { fprintf(stderr, "compositor: no wayland socket\n"); return 1; }
    setenv("WAYLAND_DISPLAY", socket, 1);
    snprintf(settings_directory, sizeof settings_directory, "%.*s", (int)(strrchr(SETTINGS_FILE, '/') - SETTINGS_FILE), SETTINGS_FILE);
    watcher = inotify_init1(IN_CLOEXEC | IN_NONBLOCK);
    inotify_add_watch(watcher, settings_directory, IN_MOVED_TO);
    wl_event_loop_add_fd(loop, watcher, WL_EVENT_READABLE, on_watched_change, NULL);
    wl_event_loop_add_signal(loop, SIGCHLD, on_child, NULL);
    flip_retry = wl_event_loop_add_timer(loop, on_flip_retry, NULL);
    load_settings();
    if (!wlr_backend_start(backend)) { fprintf(stderr, "compositor: could not start the backend\n"); return 1; }
    started = 1;
    choose_display();
    if (stopping) return 0;
    session_leader = spawn_session(argv + 1);
    if (session_leader < 0) return 1;
    wl_display_run(display);
    kill(-session_leader, SIGTERM);
    return 0;
}
