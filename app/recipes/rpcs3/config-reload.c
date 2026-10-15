#define _GNU_SOURCE
#include <limits.h>
#include <link.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/inotify.h>
#include <unistd.h>

#define CONFIG_PATH "RPCS3_CONFIG"

struct string_view {
    size_t length;
    const char *data;
};

typedef bool (*from_string_function)(void *node, struct string_view text, bool dynamic);

static uintptr_t base;
static const char *path, *name;
static int watcher;

static void fail(const char *what) {
    perror(what);
    exit(EXIT_FAILURE);
}

static int main_program(struct dl_phdr_info *info, size_t size, void *data) {
    (void)size, (void)data;
    base = info->dlpi_addr;
    return 1;
}

static void apply(void) {
    void **config = (void **)(base + G_CFG);
    char *text = NULL;
    size_t size = 0;
    if (!*config) return;
    FILE *file = fopen(path, "re");
    if (!file) fail(path);
    ssize_t length = getdelim(&text, &size, '\0', file);
    fclose(file);
    if (length < 0) fail(path);
    bool applied = ((from_string_function)(base + FROM_STRING))(config, (struct string_view){(size_t)length, text}, true);
    fprintf(stderr, "config-reload: %s %s\n", applied ? "applied" : "could not apply", path);
    free(text);
}

static void *follow(void *unused) {
    (void)unused;
    char buffer[BUFSIZ] __attribute__((aligned(__alignof__(struct inotify_event))));
    for (ssize_t count; (count = read(watcher, buffer, sizeof buffer)) > 0;) {
        for (char *at = buffer; at < buffer + count;) {
            struct inotify_event *event = (struct inotify_event *)at;
            at += sizeof *event + event->len;
            if (event->len && !strcmp(event->name, name)) apply();
        }
    }
    fail("config-reload: inotify");
    return NULL;
}

__attribute__((constructor)) static void start(void) {
    pthread_t thread;
    if (!(path = getenv(CONFIG_PATH))) {
        fprintf(stderr, "config-reload: " CONFIG_PATH " is not set\n");
        exit(EXIT_FAILURE);
    }
    name = basename(path);
    char directory[PATH_MAX];
    snprintf(directory, sizeof directory, "%.*s", (int)(name - path), path);
    watcher = inotify_init1(IN_CLOEXEC);
    if (watcher < 0 || inotify_add_watch(watcher, directory, IN_MOVED_TO) < 0) fail(path);
    dl_iterate_phdr(main_program, NULL);
    if (pthread_create(&thread, NULL, follow, NULL)) fail("config-reload: thread");
    pthread_detach(thread);
}
