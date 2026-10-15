#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include "libretro.h"

#define CORE_PATH "LIBRETRO_CORE"
#define CORE(name) ({ static __typeof__(&name) function; if (!function) function = resolve(#name); function; })

static void *core;
static retro_environment_t environment;
static retro_video_refresh_t video_refresh;
static struct retro_game_geometry geometry;

static void fail(const char *what) {
    fprintf(stderr, "frame-size: %s: %s\n", what, dlerror());
    exit(EXIT_FAILURE);
}

__attribute__((constructor)) static void open_core(void) {
    const char *path = getenv(CORE_PATH);
    if (!path) {
        fprintf(stderr, "frame-size: " CORE_PATH " is not set\n");
        exit(EXIT_FAILURE);
    }
    if (!(core = dlopen(path, RTLD_NOW | RTLD_LOCAL))) fail(path);
}

static void *resolve(const char *name) {
    void *function = dlsym(core, name);
    if (!function) fail(name);
    return function;
}

static bool on_environment(unsigned command, void *data) {
    if (!environment(command, data)) return false;
    if (command == RETRO_ENVIRONMENT_SET_GEOMETRY) geometry = *(const struct retro_game_geometry *)data;
    else if (command == RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO) geometry = ((const struct retro_system_av_info *)data)->geometry;
    return true;
}

static void on_video_refresh(const void *data, unsigned width, unsigned height, size_t pitch) {
    if (data && (width != geometry.base_width || height != geometry.base_height)) {
        geometry.base_width = width;
        geometry.base_height = height;
        environment(RETRO_ENVIRONMENT_SET_GEOMETRY, &geometry);
    }
    video_refresh(data, width, height, pitch);
}

RETRO_API void retro_set_environment(retro_environment_t callback) {
    environment = callback;
    CORE(retro_set_environment)(on_environment);
}

RETRO_API void retro_set_video_refresh(retro_video_refresh_t callback) {
    video_refresh = callback;
    CORE(retro_set_video_refresh)(on_video_refresh);
}

RETRO_API void retro_get_system_av_info(struct retro_system_av_info *info) {
    CORE(retro_get_system_av_info)(info);
    geometry = info->geometry;
}

RETRO_API void retro_set_audio_sample(retro_audio_sample_t callback) { CORE(retro_set_audio_sample)(callback); }
RETRO_API void retro_set_audio_sample_batch(retro_audio_sample_batch_t callback) { CORE(retro_set_audio_sample_batch)(callback); }
RETRO_API void retro_set_input_poll(retro_input_poll_t callback) { CORE(retro_set_input_poll)(callback); }
RETRO_API void retro_set_input_state(retro_input_state_t callback) { CORE(retro_set_input_state)(callback); }
RETRO_API void retro_init(void) { CORE(retro_init)(); }
RETRO_API void retro_deinit(void) { CORE(retro_deinit)(); }
RETRO_API unsigned retro_api_version(void) { return CORE(retro_api_version)(); }
RETRO_API void retro_get_system_info(struct retro_system_info *info) { CORE(retro_get_system_info)(info); }
RETRO_API void retro_set_controller_port_device(unsigned port, unsigned device) { CORE(retro_set_controller_port_device)(port, device); }
RETRO_API void retro_reset(void) { CORE(retro_reset)(); }
RETRO_API void retro_run(void) { CORE(retro_run)(); }
RETRO_API size_t retro_serialize_size(void) { return CORE(retro_serialize_size)(); }
RETRO_API bool retro_serialize(void *data, size_t size) { return CORE(retro_serialize)(data, size); }
RETRO_API bool retro_unserialize(const void *data, size_t size) { return CORE(retro_unserialize)(data, size); }
RETRO_API void retro_cheat_reset(void) { CORE(retro_cheat_reset)(); }
RETRO_API void retro_cheat_set(unsigned index, bool enabled, const char *code) { CORE(retro_cheat_set)(index, enabled, code); }
RETRO_API bool retro_load_game(const struct retro_game_info *game) { return CORE(retro_load_game)(game); }
RETRO_API bool retro_load_game_special(unsigned type, const struct retro_game_info *info, size_t count) {
    return CORE(retro_load_game_special)(type, info, count);
}
RETRO_API void retro_unload_game(void) { CORE(retro_unload_game)(); }
RETRO_API unsigned retro_get_region(void) { return CORE(retro_get_region)(); }
RETRO_API void *retro_get_memory_data(unsigned id) { return CORE(retro_get_memory_data)(id); }
RETRO_API size_t retro_get_memory_size(unsigned id) { return CORE(retro_get_memory_size)(id); }
