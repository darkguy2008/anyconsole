#define _GNU_SOURCE
#include <dlfcn.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NAME_LENGTH 15
#define TEXT(value) #value
#define FORMAT(length) "%" TEXT(length) "s %d"

typedef void (*slot_function)(int slot);
typedef void (*control_function)(void);

static int (*real_pthread_cond_wait)(pthread_cond_t *, pthread_mutex_t *);
static _Atomic slot_function pending;
static int pending_slot;
static sem_t posted;

static void save(int slot) {
    ((slot_function)SAVE_SLOT)(slot);
}

static void load(int slot) {
    ((control_function)PAUSE)();
    ((slot_function)LOAD_SLOT)(slot);
    ((control_function)RESUME)();
}

static slot_function function_named(const char *name) {
    if (!strcmp(name, "save")) return save;
    if (!strcmp(name, "load")) return load;
    fprintf(stderr, "unknown redream command %s\n", name);
    return NULL;
}

static void *read_commands(void *path) {
    for (;;) {
        FILE *commands = fopen(path, "r");
        char name[NAME_LENGTH + 1];
        int slot;
        if (!commands) {
            perror(path);
            return NULL;
        }
        while (fscanf(commands, FORMAT(NAME_LENGTH), name, &slot) == 2) {
            slot_function function = function_named(name);
            if (!function) continue;
            pending_slot = slot;
            atomic_store(&pending, function);
            sem_wait(&posted);
        }
        fclose(commands);
    }
}

int pthread_cond_wait(pthread_cond_t *condition, pthread_mutex_t *mutex) {
    slot_function function = condition == *(pthread_cond_t **)FRAME_CONDITION && atomic_load(&pending) ? atomic_exchange(&pending, NULL) : NULL;
    if (!function) return real_pthread_cond_wait(condition, mutex);
    pthread_mutex_unlock(mutex);
    function(pending_slot);
    pthread_mutex_lock(mutex);
    sem_post(&posted);
    return 0;
}

__attribute__((constructor)) static void start(void) {
    pthread_t reader;
    real_pthread_cond_wait = dlsym(RTLD_NEXT, "pthread_cond_wait");
    sem_init(&posted, 0, 0);
    pthread_create(&reader, NULL, read_commands, getenv("REDREAM_COMMANDS"));
}
