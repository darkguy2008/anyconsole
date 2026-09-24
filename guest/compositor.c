#define _GNU_SOURCE
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
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
#include <wlr/types/wlr_compositor.h>
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
#include <wlr/types/wlr_xdg_output_v1.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/log.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <xkbcommon/xkbcommon.h>
#include "anyconsole.h"

#define ASPECT_WIDTH 16
#define ASPECT_HEIGHT 9
#define GPUS_MAX 8
#define TEXT_MAX 256
#define NO_VULKAN -1
#define UNKNOWN "Unknown"
#define DISPLAY_PARTIAL DISPLAY_FILE ".partial"
#define SEAT_NAME "seat0"
#define COMPOSITOR_VERSION 6
#define XDG_SHELL_VERSION 6
#define LAYER_SHELL_VERSION 4
#define USEC_PER_MSEC 1000
#define CORNERS 4

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
    int rank, has_display;
    char name[TEXT_MAX];
};

struct display {
    struct wlr_output *output;
    struct gpu *gpu;
    unsigned connected;
    struct wl_list link;
    struct wl_listener frame, needs_frame, destroy;
};

struct window {
    struct wlr_xdg_toplevel *toplevel;
    struct wlr_foreign_toplevel_handle_v1 *handle;
    char app_id[TEXT_MAX];
    int sandboxed, frame_width, frame_height;
    struct wl_list link;
    struct wl_listener commit, map, unmap, destroy, activate, request_fullscreen;
};

struct keyboard { struct wlr_keyboard *keyboard; struct wl_listener key, modifiers, destroy; };
struct pointer { struct wl_listener motion, button, axis, frame, destroy; };
struct program { GLuint id; GLint position, coordinate, size, extent; };

static struct wl_display *display;
static struct wlr_session *session;
static struct wlr_backend *backend;
static struct wlr_renderer *renderer;
static struct wlr_allocator *allocator;
static struct wlr_seat *seat;
static struct wlr_layer_shell_v1 *layer_shell;
static struct wlr_security_context_manager_v1 *security;
static struct wlr_foreign_toplevel_manager_v1 *foreign;
static struct wlr_screencopy_manager_v1 *screencopy;
static struct wlr_output_layout *layout;
static struct wlr_relative_pointer_manager_v1 *relative;
static struct xkb_keymap *keymap;
static struct gpu gpus[GPUS_MAX], *render_gpu;
static int gpu_count;
static struct wl_list displays, windows;
static struct display *active, *pending;
static struct window *shown;
static struct wlr_layer_surface_v1 *overlay;
static struct wl_listener overlay_commit, overlay_destroy;
static struct wlr_swapchain *canvas_chain;
static struct program programs[sizeof sampler_prefixes / sizeof *sampler_prefixes];
static char pinned[TEXT_MAX], *written_state;
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

static dev_t handed_over(void) {
    FILE *file = fopen(RENDER_GPU_FILE, "r");
    unsigned major_number, minor_number;
    dev_t device = file && fscanf(file, "%u:%u", &major_number, &minor_number) == 2 ? makedev(major_number, minor_number) : 0;
    if (file) fclose(file);
    return device;
}

static int startup_order(struct gpu *a, struct gpu *b, dev_t handed) {
    int a_handed = a->device->dev == handed, b_handed = b->device->dev == handed;
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

static struct wlr_box area(void) {
    int width = active->output->width, height = active->output->height;
    if (width * ASPECT_HEIGHT > height * ASPECT_WIDTH) width = height * ASPECT_WIDTH / ASPECT_HEIGHT;
    else height = width * ASPECT_HEIGHT / ASPECT_WIDTH;
    return (struct wlr_box){(active->output->width - width) / 2, (active->output->height - height) / 2, width, height};
}

static enum filter choose_filter(struct wlr_box box) {
    if (!canvas_width || (box.width % canvas_width == 0 && box.height % canvas_height == 0)) return NEAREST;
    if (box.width < canvas_width || !wlr_renderer_is_gles2(renderer)) return LINEAR;
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

static void write_state(void) {
    json_object *state = json_object_new_object(), *list = json_object_new_array();
    char canvas[TEXT_MAX] = "", box_size[TEXT_MAX] = "", frame[TEXT_MAX] = "";
    struct display *entry;
    wl_list_for_each(entry, &displays, link) {
        json_object *item = json_object_new_object();
        json_object_object_add(item, "label", json_object_new_string(label(entry->output)));
        json_object_object_add(item, "gpu", json_object_new_string(entry->gpu->name));
        json_object_array_add(list, item);
    }
    if (shown) snprintf(frame, sizeof frame, "%dx%d", shown->frame_width, shown->frame_height);
    if (active) {
        struct wlr_box box = area();
        snprintf(canvas, sizeof canvas, "%dx%d", canvas_width, canvas_height);
        snprintf(box_size, sizeof box_size, "%dx%d", box.width, box.height);
        json_object_object_add(state, "filter", json_object_new_string(filters[choose_filter(box)]));
        json_object *placement = json_object_new_object();
        json_object_object_add(placement, "x", json_object_new_int(box.x));
        json_object_object_add(placement, "y", json_object_new_int(box.y));
        json_object_object_add(placement, "width", json_object_new_int(box.width));
        json_object_object_add(placement, "height", json_object_new_int(box.height));
        json_object_object_add(state, "area_box", placement);
    }
    json_object_object_add(state, "displays", list);
    json_object_object_add(state, "display", active ? json_object_new_string(label(active->output)) : NULL);
    json_object_object_add(state, "gpu", json_object_new_string(render_gpu->name));
    json_object_object_add(state, "vulkan", json_object_new_boolean(render_gpu->rank != NO_VULKAN));
    json_object_object_add(state, "canvas", json_object_new_string(canvas));
    json_object_object_add(state, "area", json_object_new_string(box_size));
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
        if (window->toplevel->base->initialized) wlr_xdg_toplevel_set_size(window->toplevel, canvas_width, canvas_height);
    configure_overlay();
    redraw();
}

static void activate(struct display *wanted) {
    struct display *entry;
    wl_list_for_each(entry, &displays, link) {
        struct wlr_output_state state;
        struct wlr_output_mode *mode = wlr_output_preferred_mode(entry->output);
        wlr_output_state_init(&state);
        wlr_output_state_set_enabled(&state, entry == wanted);
        if (entry == wanted && mode) wlr_output_state_set_mode(&state, mode);
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
    pending = best && best->gpu != render_gpu ? best : NULL;
    if (pending && !games_running()) {
        hand_over(pending->gpu);
        return;
    }
    struct display *wanted = pending && active ? active : best;
    if (wanted != active) activate(wanted);
    update_canvas();
    write_state();
}

static int overlay_visible(void) { return overlay && overlay->surface->mapped; }

static int scan_out(struct wlr_output_state *state) {
    struct wlr_surface *surface = shown ? shown->toplevel->base->surface : NULL;
    struct wlr_box box = area();
    if (!surface || !surface->buffer || overlay_visible() || box.width != active->output->width || box.height != active->output->height ||
        surface->buffer->base.width != box.width || surface->buffer->base.height != box.height)
        return 0;
    wlr_output_state_set_buffer(state, &surface->buffer->base);
    if (wlr_output_test_state(active->output, state)) return 1;
    wlr_output_state_finish(state);
    wlr_output_state_init(state);
    return 0;
}

static struct wlr_texture *compose(struct wlr_buffer **composed) {
    struct wlr_texture *game = shown ? wlr_surface_get_texture(shown->toplevel->base->surface) : NULL;
    if (!overlay_visible() && (!game || (game->width == (uint32_t)canvas_width && game->height == (uint32_t)canvas_height))) return game;
    if (!canvas_chain || canvas_chain->width != canvas_width || canvas_chain->height != canvas_height) {
        wlr_swapchain_destroy(canvas_chain);
        canvas_chain = wlr_swapchain_create(allocator, canvas_width, canvas_height, &active->output->swapchain->format);
    }
    *composed = canvas_chain ? wlr_swapchain_acquire(canvas_chain, NULL) : NULL;
    if (!*composed) return game;
    struct wlr_render_pass *pass = wlr_renderer_begin_buffer_pass(renderer, *composed, NULL);
    if (!game) wlr_render_pass_add_rect(pass, &(struct wlr_render_rect_options){.box = {0, 0, canvas_width, canvas_height}, .color = {0, 0, 0, 1}});
    else wlr_render_pass_add_texture(pass, &(struct wlr_render_texture_options){.texture = game, .dst_box = {0, 0, canvas_width, canvas_height}});
    struct wlr_texture *interface = overlay_visible() ? wlr_surface_get_texture(overlay->surface) : NULL;
    if (interface) wlr_render_pass_add_texture(pass, &(struct wlr_render_texture_options){.texture = interface, .dst_box = overlay_box()});
    wlr_render_pass_submit(pass);
    return wlr_texture_from_buffer(renderer, *composed);
}

static GLuint compile(GLenum type, const char *prefix, const char *source) {
    GLuint shader = glCreateShader(type);
    const char *sources[] = {prefix, source};
    GLint compiled;
    glShaderSource(shader, 2, sources, NULL);
    glCompileShader(shader);
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (!compiled) {
        char log[TEXT_MAX];
        glGetShaderInfoLog(shader, sizeof log, NULL, log);
        fprintf(stderr, "compositor: shader: %s\n", log);
    }
    return shader;
}

static struct program *pixel_program(GLenum target) {
    struct program *program = &programs[target == GL_TEXTURE_EXTERNAL_OES];
    if (program->id) return program;
    program->id = glCreateProgram();
    glAttachShader(program->id, compile(GL_VERTEX_SHADER, "", vertex_source));
    glAttachShader(program->id, compile(GL_FRAGMENT_SHADER, sampler_prefixes[target == GL_TEXTURE_EXTERNAL_OES], pixel_source));
    glLinkProgram(program->id);
    program->position = glGetAttribLocation(program->id, "position");
    program->coordinate = glGetAttribLocation(program->id, "coordinate");
    program->size = glGetUniformLocation(program->id, "size");
    program->extent = glGetUniformLocation(program->id, "extent");
    return program;
}

static int draw_pixel(struct wlr_output_state *state, struct wlr_box box) {
    struct wlr_output *output = active->output;
    if (!wlr_output_configure_primary_swapchain(output, state, &output->swapchain)) return 0;
    struct wlr_buffer *buffer = wlr_swapchain_acquire(output->swapchain, NULL), *composed = NULL;
    if (!buffer) return 0;
    struct wlr_texture *texture = compose(&composed);
    if (!texture) {
        wlr_buffer_unlock(buffer);
        return 0;
    }
    GLuint framebuffer = wlr_gles2_renderer_get_buffer_fbo(renderer, buffer);
    struct wlr_gles2_texture_attribs attributes;
    struct wlr_egl *egl = wlr_gles2_renderer_get_egl(renderer);
    wlr_gles2_texture_get_attribs(texture, &attributes);
    eglMakeCurrent(wlr_egl_get_display(egl), EGL_NO_SURFACE, EGL_NO_SURFACE, wlr_egl_get_context(egl));
    struct program *program = pixel_program(attributes.target);
    GLfloat left = 2.0f * box.x / output->width - 1, right = 2.0f * (box.x + box.width) / output->width - 1;
    GLfloat top = 2.0f * box.y / output->height - 1, bottom = 2.0f * (box.y + box.height) / output->height - 1;
    const GLfloat corners[] = {left, top, right, top, left, bottom, right, bottom}, coordinates[] = {0, 0, 1, 0, 0, 1, 1, 1};
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    glViewport(0, 0, output->width, output->height);
    glDisable(GL_BLEND);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    glUseProgram(program->id);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(attributes.target, attributes.tex);
    glTexParameteri(attributes.target, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(attributes.target, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glUniform2f(program->size, texture->width, texture->height);
    glUniform2f(program->extent, (GLfloat)texture->width / box.width, (GLfloat)texture->height / box.height);
    glVertexAttribPointer(program->position, 2, GL_FLOAT, GL_FALSE, 0, corners);
    glVertexAttribPointer(program->coordinate, 2, GL_FLOAT, GL_FALSE, 0, coordinates);
    glEnableVertexAttribArray(program->position);
    glEnableVertexAttribArray(program->coordinate);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, CORNERS);
    glDisableVertexAttribArray(program->position);
    glDisableVertexAttribArray(program->coordinate);
    glBindTexture(attributes.target, 0);
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

static struct wlr_box scaled(struct wlr_box inner, struct wlr_box box) {
    return (struct wlr_box){box.x + inner.x * box.width / canvas_width, box.y + inner.y * box.height / canvas_height,
                            inner.width * box.width / canvas_width, inner.height * box.height / canvas_height};
}

static void paint(struct wlr_output_state *state) {
    struct wlr_output *output = active->output;
    struct wlr_box box = area();
    enum filter filter = choose_filter(box);
    struct wlr_texture *game = shown ? wlr_surface_get_texture(shown->toplevel->base->surface) : NULL;
    struct wlr_texture *interface = overlay_visible() ? wlr_surface_get_texture(overlay->surface) : NULL;
    if (filter == PIXEL && (game || interface) && draw_pixel(state, box)) return;
    enum wlr_scale_filter_mode mode = filter == NEAREST ? WLR_SCALE_FILTER_NEAREST : WLR_SCALE_FILTER_BILINEAR;
    struct wlr_render_pass *pass = wlr_output_begin_render_pass(output, state, NULL, NULL);
    if (!game || box.width != output->width || box.height != output->height)
        wlr_render_pass_add_rect(pass, &(struct wlr_render_rect_options){.box = {0, 0, output->width, output->height}, .color = {0, 0, 0, 1}});
    if (game) wlr_render_pass_add_texture(pass, &(struct wlr_render_texture_options){.texture = game, .dst_box = box, .filter_mode = mode});
    if (interface)
        wlr_render_pass_add_texture(pass, &(struct wlr_render_texture_options){.texture = interface, .dst_box = scaled(overlay_box(), box), .filter_mode = mode});
    wlr_render_pass_submit(pass);
}

static void on_frame(struct wl_listener *listener, void *data) {
    (void)data;
    struct display *entry = wl_container_of(listener, entry, frame);
    if (entry != active || (!dirty && !entry->output->needs_frame)) return;
    struct wlr_output_state state;
    dirty = 0;
    wlr_output_state_init(&state);
    if (!scan_out(&state)) paint(&state);
    if (!wlr_output_commit_state(entry->output, &state)) fprintf(stderr, "compositor: could not present on %s\n", entry->output->name);
    wlr_output_state_finish(&state);
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    if (shown) wlr_surface_send_frame_done(shown->toplevel->base->surface, &now);
    if (overlay) wlr_surface_send_frame_done(overlay->surface, &now);
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
    redraw();
    write_state();
}

static void on_window_activate(struct wl_listener *listener, void *data) {
    (void)data;
    struct window *window = wl_container_of(listener, window, activate);
    show(window);
}

static void on_window_commit(struct wl_listener *listener, void *data) {
    (void)data;
    struct window *window = wl_container_of(listener, window, commit);
    if (window->toplevel->base->initial_commit) {
        wlr_xdg_toplevel_set_fullscreen(window->toplevel, true);
        wlr_xdg_toplevel_set_size(window->toplevel, canvas_width, canvas_height);
    }
    struct wlr_surface *surface = window->toplevel->base->surface;
    if (surface->buffer && (surface->buffer->base.width != window->frame_width || surface->buffer->base.height != window->frame_height)) {
        window->frame_width = surface->buffer->base.width;
        window->frame_height = surface->buffer->base.height;
        write_state();
    }
    if (window == shown) redraw();
}

static void on_window_request_fullscreen(struct wl_listener *listener, void *data) {
    (void)data;
    struct window *window = wl_container_of(listener, window, request_fullscreen);
    if (window->toplevel->base->initialized) wlr_xdg_surface_schedule_configure(window->toplevel->base);
}

static void on_window_map(struct wl_listener *listener, void *data) {
    (void)data;
    struct window *window = wl_container_of(listener, window, map);
    window->handle = wlr_foreign_toplevel_handle_v1_create(foreign);
    wlr_foreign_toplevel_handle_v1_set_app_id(window->handle, window->app_id);
    listen(&window->activate, &window->handle->events.request_activate, on_window_activate);
    show(window);
}

static void on_window_unmap(struct wl_listener *listener, void *data) {
    (void)data;
    struct window *window = wl_container_of(listener, window, unmap);
    if (window == shown) show(NULL);
    wl_list_remove(&window->activate.link);
    wlr_foreign_toplevel_handle_v1_destroy(window->handle);
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

static bool visible_to(const struct wl_client *client, const struct wl_global *global, void *data) {
    (void)data;
    return !wlr_security_context_manager_v1_lookup_client(security, client) ||
           (global != layer_shell->global && global != security->global && global != foreign->global && global != screencopy->global);
}

static void load_settings(void) {
    json_object *settings = json_object_from_file(SETTINGS_FILE), *field;
    snprintf(pinned, sizeof pinned, "%s", json_object_object_get_ex(settings, "display", &field) ? json_object_get_string(field) : "");
    resolution = json_object_object_get_ex(settings, "resolution", &field) ? json_object_get_int(field) : 0;
    json_object_put(settings);
}

static int on_settings_change(int fd, uint32_t mask, void *data) {
    (void)mask, (void)data;
    char buffer[BUFSIZ] __attribute__((aligned(__alignof__(struct inotify_event))));
    ssize_t count = read(fd, buffer, sizeof buffer);
    for (char *at = buffer; count > 0 && at < buffer + count;) {
        struct inotify_event *event = (struct inotify_event *)at;
        at += sizeof *event + event->len;
        if (event->len && !strcmp(event->name, basename(SETTINGS_FILE))) {
            load_settings();
            choose_display();
        }
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
    layout = wlr_output_layout_create(display);
    wlr_xdg_output_manager_v1_create(display, layout);
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
    int watcher = inotify_init1(IN_CLOEXEC | IN_NONBLOCK);
    inotify_add_watch(watcher, settings_directory, IN_MOVED_TO);
    wl_event_loop_add_fd(loop, watcher, WL_EVENT_READABLE, on_settings_change, NULL);
    wl_event_loop_add_signal(loop, SIGCHLD, on_child, NULL);
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
