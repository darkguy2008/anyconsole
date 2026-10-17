#define _GNU_SOURCE
#include <dlfcn.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#define TEMPORARY_EXTENSION ".tmp"
#define WRITE_MODE "wb"

typedef FILE *(*open_function)(const char *path, const char *mode);
typedef int (*close_function)(FILE *file);

static open_function real_fopen, real_fopen64;
static close_function real_fclose;
static char save_suffix[NAME_MAX];
static size_t suffix_length;
static pid_t saver;
static FILE *saving;
static char saving_path[PATH_MAX], saved_path[PATH_MAX];

__attribute__((constructor)) static void resolve(void) {
    real_fopen = (open_function)dlsym(RTLD_NEXT, "fopen");
    real_fopen64 = (open_function)dlsym(RTLD_NEXT, "fopen64");
    real_fclose = (close_function)dlsym(RTLD_NEXT, "fclose");
    suffix_length = snprintf(save_suffix, sizeof save_suffix, "%s" TEMPORARY_EXTENSION, getenv("BACKGROUND_SAVE"));
}

static int is_save(const char *path, const char *mode) {
    if (strcmp(mode, WRITE_MODE)) return 0;
    size_t path_length = strlen(path);
    return path_length > suffix_length && !strcmp(path + path_length - suffix_length, save_suffix);
}

static void fail(const char *what) {
    perror(what);
    unlink(saving_path);
    _exit(EXIT_FAILURE);
}

static void stop_stuck_saver(void) {
    if (saver > 0 && waitpid(saver, NULL, WNOHANG) == 0) {
        fprintf(stderr, "background save %d still running at the next save, killing it\n", saver);
        kill(saver, SIGKILL);
        waitpid(saver, NULL, 0);
    }
    saver = 0;
}

static FILE *open_with(open_function real, const char *path, const char *mode) {
    if (!is_save(path, mode)) return real(path, mode);
    stop_stuck_saver();
    pid_t child = fork();
    if (child < 0) {
        perror("fork");
        return real(path, mode);
    }
    if (child) {
        saver = child;
        return NULL;
    }
    snprintf(saved_path, sizeof saved_path, "%.*s", (int)(strlen(path) - strlen(TEMPORARY_EXTENSION)), path);
    snprintf(saving_path, sizeof saving_path, "%s.%d", path, getpid());
    saving = real(saving_path, mode);
    if (!saving) fail(saving_path);
    return saving;
}

FILE *fopen(const char *path, const char *mode) { return open_with(real_fopen, path, mode); }

FILE *fopen64(const char *path, const char *mode) { return open_with(real_fopen64, path, mode); }

int fclose(FILE *file) {
    if (file != saving) return real_fclose(file);
    int write_failed = ferror(file);
    if (real_fclose(file) || write_failed || rename(saving_path, saved_path) < 0) fail(saving_path);
    _exit(EXIT_SUCCESS);
}
