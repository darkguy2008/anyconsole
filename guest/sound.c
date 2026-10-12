#define _GNU_SOURCE
#include <limits.h>
#include <opusfile.h>
#include <pipewire/pipewire.h>
#include <spa/param/audio/format-utils.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "skin.h"

#define RATE 48000
#define STEREO 2
#define SOUNDS_MAX 64
#define VOICES_MAX 8
#define DECODE_FRAMES 4096
#define PARAMS_BUFFER 1024

struct sound { char path[PATH_MAX]; float *samples; long frames; };
struct voice { struct sound *sound; long at; };

static struct sound sounds[SOUNDS_MAX];
static int sound_count;
static struct voice voices[VOICES_MAX];
static struct pw_thread_loop *loop;
static struct pw_stream *stream;

static void on_process(void *data) {
    (void)data;
    struct pw_buffer *buffer = pw_stream_dequeue_buffer(stream);
    if (!buffer) return;
    struct spa_data *target = &buffer->buffer->datas[0];
    int playing = 0;
    float *out = target->data;
    uint32_t frames = target->maxsize / (sizeof *out * STEREO);
    if (buffer->requested && buffer->requested < frames) frames = buffer->requested;
    memset(out, 0, (size_t)frames * STEREO * sizeof *out);
    for (int i = 0; i < VOICES_MAX; i++) {
        struct voice *voice = &voices[i];
        for (uint32_t frame = 0; voice->sound && frame < frames && voice->at < voice->sound->frames; frame++, voice->at++)
            for (int channel = 0; channel < STEREO; channel++) out[frame * STEREO + channel] += voice->sound->samples[voice->at * STEREO + channel];
        if (voice->sound && voice->at >= voice->sound->frames) voice->sound = NULL;
        playing |= voice->sound != NULL;
    }
    target->chunk->offset = 0;
    target->chunk->stride = sizeof *out * STEREO;
    target->chunk->size = frames * STEREO * sizeof *out;
    pw_stream_queue_buffer(stream, buffer);
    if (!playing) pw_stream_flush(stream, true);
}

static void on_drained(void *data) {
    (void)data;
    pw_stream_set_active(stream, false);
}

static const struct pw_stream_events stream_events = {PW_VERSION_STREAM_EVENTS, .process = on_process, .drained = on_drained};

void sound_init(void) {
    uint8_t storage[PARAMS_BUFFER];
    struct spa_pod_builder builder = SPA_POD_BUILDER_INIT(storage, sizeof storage);
    pw_init(NULL, NULL);
    loop = pw_thread_loop_new("sound", NULL);
    stream = pw_stream_new_simple(pw_thread_loop_get_loop(loop), "anyconsole interface",
                                  pw_properties_new(PW_KEY_MEDIA_TYPE, "Audio", PW_KEY_MEDIA_CATEGORY, "Playback", PW_KEY_MEDIA_ROLE, "Notification", NULL),
                                  &stream_events, NULL);
    const struct spa_pod *params[] = {
        spa_format_audio_raw_build(&builder, SPA_PARAM_EnumFormat, &SPA_AUDIO_INFO_RAW_INIT(.format = SPA_AUDIO_FORMAT_F32, .channels = STEREO, .rate = RATE))};
    pw_stream_connect(stream, PW_DIRECTION_OUTPUT, PW_ID_ANY, PW_STREAM_FLAG_AUTOCONNECT | PW_STREAM_FLAG_MAP_BUFFERS | PW_STREAM_FLAG_RT_PROCESS | PW_STREAM_FLAG_INACTIVE, params, 1);
    pw_thread_loop_start(loop);
}

static struct sound *decode(const char *path) {
    for (int i = 0; i < sound_count; i++)
        if (!strcmp(sounds[i].path, path)) return &sounds[i];
    if (sound_count == SOUNDS_MAX) return NULL;
    int error;
    OggOpusFile *file = op_open_file(path, &error);
    if (!file) {
        fprintf(stderr, "sound: cannot open %s (%d)\n", path, error);
        return NULL;
    }
    struct sound *sound = &sounds[sound_count++];
    snprintf(sound->path, sizeof sound->path, "%s", path);
    float chunk[DECODE_FRAMES * STEREO];
    for (int read; (read = op_read_float_stereo(file, chunk, DECODE_FRAMES * STEREO)) > 0; sound->frames += read) {
        sound->samples = realloc(sound->samples, (sound->frames + read) * STEREO * sizeof *sound->samples);
        memcpy(sound->samples + sound->frames * STEREO, chunk, (size_t)read * STEREO * sizeof *chunk);
    }
    op_free(file);
    return sound;
}

void sound_play(const char *path) {
    struct sound *sound = decode(path);
    if (!sound || !loop) return;
    pw_thread_loop_lock(loop);
    struct voice *oldest = &voices[0];
    for (int i = 0; i < VOICES_MAX && oldest->sound; i++)
        if (!voices[i].sound || voices[i].at > oldest->at) oldest = &voices[i];
    *oldest = (struct voice){sound, 0};
    pw_stream_set_active(stream, true);
    pw_thread_loop_unlock(loop);
}
