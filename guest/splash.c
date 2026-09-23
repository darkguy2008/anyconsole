#define _GNU_SOURCE
#include <fcntl.h>
#include <math.h>
#include <png.h>
#include <pixman.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <wayland-client.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include "xdg-shell.h"

#define SPLASH_PATH "/usr/share/anyconsole/splash.png"
#define BYTES_PER_PIXEL 4
#define MAX_DRM_DEVICES 16

static pixman_image_t *splash;

static void load_splash(void) {
	png_image png = {.version = PNG_IMAGE_VERSION};
	if (!png_image_begin_read_from_file(&png, SPLASH_PATH)) {
		fprintf(stderr, "splash: %s: %s\n", SPLASH_PATH, png.message);
		exit(1);
	}
	png.format = PNG_FORMAT_BGRA;
	void *pixels = malloc(PNG_IMAGE_SIZE(png));
	png_image_finish_read(&png, NULL, pixels, 0, NULL);
	splash = pixman_image_create_bits(PIXMAN_x8r8g8b8, png.width, png.height, pixels, PNG_IMAGE_ROW_STRIDE(png));
}

static void paint(void *pixels, int width, int height, int stride) {
	memset(pixels, 0, (size_t)stride * height);
	pixman_image_t *target = pixman_image_create_bits(PIXMAN_x8r8g8b8, width, height, pixels, stride);
	int splash_width = pixman_image_get_width(splash), splash_height = pixman_image_get_height(splash);
	double scale = fmin((double)width / splash_width, (double)height / splash_height);
	int fit_width = splash_width * scale, fit_height = splash_height * scale;
	struct pixman_f_transform inverse;
	pixman_transform_t transform;
	pixman_f_transform_init_scale(&inverse, 1 / scale, 1 / scale);
	pixman_transform_from_pixman_f_transform(&transform, &inverse);
	pixman_image_set_transform(splash, &transform);
	pixman_image_set_filter(splash, PIXMAN_FILTER_GOOD, NULL, 0);
	pixman_image_composite32(PIXMAN_OP_SRC, splash, NULL, target, 0, 0, 0, 0,
	                         (width - fit_width) / 2, (height - fit_height) / 2, fit_width, fit_height);
	pixman_image_unref(target);
}

static void detach(void) {
	if (fork())
		_exit(0);
}

static void show_on_card(const char *path) {
	int fd = open(path, O_RDWR | O_CLOEXEC);
	drmModeRes *resources = drmModeGetResources(fd);
	if (!resources)
		return;
	for (int i = 0; i < resources->count_connectors; i++) {
		drmModeConnector *connector = drmModeGetConnector(fd, resources->connectors[i]);
		if (connector->connection != DRM_MODE_CONNECTED || !connector->count_modes || !connector->count_encoders)
			continue;
		drmModeEncoder *encoder = drmModeGetEncoder(fd, connector->encoder_id ? connector->encoder_id : connector->encoders[0]);
		uint32_t crtc_id = encoder->crtc_id ? encoder->crtc_id : resources->crtcs[__builtin_ctz(encoder->possible_crtcs)];
		drmModeCrtc *crtc = drmModeGetCrtc(fd, crtc_id);
		drmModeModeInfo mode = crtc->mode_valid ? crtc->mode : connector->modes[0];
		struct drm_mode_create_dumb dumb = {.width = mode.hdisplay, .height = mode.vdisplay, .bpp = BYTES_PER_PIXEL * 8};
		struct drm_mode_map_dumb map = {0};
		uint32_t framebuffer;
		if (drmIoctl(fd, DRM_IOCTL_MODE_CREATE_DUMB, &dumb) ||
		    drmModeAddFB(fd, dumb.width, dumb.height, 24, dumb.bpp, dumb.pitch, dumb.handle, &framebuffer))
			return;
		map.handle = dumb.handle;
		drmIoctl(fd, DRM_IOCTL_MODE_MAP_DUMB, &map);
		void *pixels = mmap(NULL, dumb.size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, map.offset);
		paint(pixels, dumb.width, dumb.height, dumb.pitch);
		if (drmModeSetCrtc(fd, crtc_id, framebuffer, 0, 0, &connector->connector_id, 1, &mode))
			return;
		drmDropMaster(fd);
		detach();
		pause();
	}
}

static void show_on_drm(void) {
	drmDevicePtr devices[MAX_DRM_DEVICES];
	int count = drmGetDevices2(0, devices, MAX_DRM_DEVICES);
	for (int i = 0; i < count; i++)
		if (devices[i]->available_nodes & (1 << DRM_NODE_PRIMARY))
			show_on_card(devices[i]->nodes[DRM_NODE_PRIMARY]);
	fprintf(stderr, "splash: no connected display\n");
	exit(1);
}

static struct wl_compositor *compositor;
static struct wl_shm *shm;
static struct xdg_wm_base *wm_base;
static struct wl_surface *surface;
static int output_width, output_height, surface_width, surface_height, painted_width, painted_height, was_activated;

static void on_output_geometry(void *data, struct wl_output *output, int32_t x, int32_t y, int32_t physical_width, int32_t physical_height,
                               int32_t subpixel, const char *make, const char *model, int32_t transform) {
	(void)data, (void)output, (void)x, (void)y, (void)physical_width, (void)physical_height, (void)subpixel, (void)make, (void)model, (void)transform;
}

static void on_output_mode(void *data, struct wl_output *output, uint32_t flags, int32_t width, int32_t height, int32_t refresh) {
	(void)data, (void)output, (void)refresh;
	if (flags & WL_OUTPUT_MODE_CURRENT) {
		output_width = width;
		output_height = height;
	}
}

static const struct wl_output_listener output_listener = {.geometry = on_output_geometry, .mode = on_output_mode};

static void on_global(void *data, struct wl_registry *registry, uint32_t name, const char *interface, uint32_t version) {
	(void)data, (void)version;
	if (!strcmp(interface, wl_compositor_interface.name))
		compositor = wl_registry_bind(registry, name, &wl_compositor_interface, 1);
	else if (!strcmp(interface, wl_shm_interface.name))
		shm = wl_registry_bind(registry, name, &wl_shm_interface, 1);
	else if (!strcmp(interface, xdg_wm_base_interface.name))
		wm_base = wl_registry_bind(registry, name, &xdg_wm_base_interface, 1);
	else if (!strcmp(interface, wl_output_interface.name))
		wl_output_add_listener(wl_registry_bind(registry, name, &wl_output_interface, 1), &output_listener, NULL);
}

static void on_global_remove(void *data, struct wl_registry *registry, uint32_t name) { (void)data, (void)registry, (void)name; }

static const struct wl_registry_listener registry_listener = {.global = on_global, .global_remove = on_global_remove};

static void on_ping(void *data, struct xdg_wm_base *base, uint32_t serial) {
	(void)data;
	xdg_wm_base_pong(base, serial);
}

static const struct xdg_wm_base_listener wm_base_listener = {.ping = on_ping};

static void on_toplevel_configure(void *data, struct xdg_toplevel *toplevel, int32_t width, int32_t height, struct wl_array *states) {
	(void)data, (void)toplevel;
	surface_width = width ? width : output_width;
	surface_height = height ? height : output_height;
	int activated = 0;
	uint32_t *state;
	wl_array_for_each(state, states) activated |= *state == XDG_TOPLEVEL_STATE_ACTIVATED;
	if (was_activated && !activated)
		exit(0);
	was_activated = activated;
}

static void on_toplevel_close(void *data, struct xdg_toplevel *toplevel) {
	(void)data, (void)toplevel;
	exit(0);
}

static const struct xdg_toplevel_listener toplevel_listener = {.configure = on_toplevel_configure, .close = on_toplevel_close};

static void on_frame_done(void *data, struct wl_callback *callback, uint32_t time) {
	(void)data, (void)time;
	wl_callback_destroy(callback);
	detach();
}

static const struct wl_callback_listener frame_listener = {.done = on_frame_done};

static void attach_splash(void) {
	painted_width = surface_width;
	painted_height = surface_height;
	int stride = surface_width * BYTES_PER_PIXEL, size = stride * surface_height;
	int fd = memfd_create("splash", MFD_CLOEXEC);
	ftruncate(fd, size);
	void *pixels = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	paint(pixels, surface_width, surface_height, stride);
	munmap(pixels, size);
	struct wl_shm_pool *pool = wl_shm_create_pool(shm, fd, size);
	struct wl_buffer *buffer = wl_shm_pool_create_buffer(pool, 0, surface_width, surface_height, stride, WL_SHM_FORMAT_XRGB8888);
	wl_shm_pool_destroy(pool);
	close(fd);
	wl_surface_attach(surface, buffer, 0, 0);
	wl_surface_damage(surface, 0, 0, surface_width, surface_height);
}

static void on_surface_configure(void *data, struct xdg_surface *xdg_surface, uint32_t serial) {
	(void)data;
	xdg_surface_ack_configure(xdg_surface, serial);
	if (!painted_width)
		wl_callback_add_listener(wl_surface_frame(surface), &frame_listener, NULL);
	if (surface_width != painted_width || surface_height != painted_height)
		attach_splash();
	wl_surface_commit(surface);
}

static const struct xdg_surface_listener surface_listener = {.configure = on_surface_configure};

static void show_on_wayland(void) {
	struct wl_display *display = wl_display_connect(NULL);
	if (!display) {
		fprintf(stderr, "splash: no wayland display\n");
		exit(1);
	}
	wl_registry_add_listener(wl_display_get_registry(display), &registry_listener, NULL);
	wl_display_roundtrip(display);
	wl_display_roundtrip(display);
	xdg_wm_base_add_listener(wm_base, &wm_base_listener, NULL);
	surface = wl_compositor_create_surface(compositor);
	struct xdg_surface *xdg_surface = xdg_wm_base_get_xdg_surface(wm_base, surface);
	xdg_surface_add_listener(xdg_surface, &surface_listener, NULL);
	struct xdg_toplevel *toplevel = xdg_surface_get_toplevel(xdg_surface);
	xdg_toplevel_add_listener(toplevel, &toplevel_listener, NULL);
	xdg_toplevel_set_fullscreen(toplevel, NULL);
	wl_surface_commit(surface);
	while (wl_display_dispatch(display) != -1)
		;
	exit(0);
}

int main(int argc, char **argv) {
	load_splash();
	if (argc == 2 && !strcmp(argv[1], "drm"))
		show_on_drm();
	if (argc == 2 && !strcmp(argv[1], "wayland"))
		show_on_wayland();
	fprintf(stderr, "usage: splash drm|wayland\n");
	return 1;
}
