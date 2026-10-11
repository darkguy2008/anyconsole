#define _GNU_SOURCE
#include <dlfcn.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/sendfile.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#define NAME_LENGTH 15
#define PERMISSIONS_LENGTH 4
#define SHARED_FLAG (PERMISSIONS_LENGTH - 1)
#define TEXT(value) #value
#define FORMAT(length) "%" TEXT(length) "s %d"
#define MAPPING_FORMAT(length) "%lx-%lx %" TEXT(length) "s %lx %*s %*s %ms"
#define PARTIAL ".partial"

typedef void (*slot_function)(int slot);
typedef void (*control_function)(void);
typedef void (*path_function)(int game, int slot, char *path, int size);
typedef int (*save_function)(const char *path);

struct snapshot {
    dev_t device;
    ino_t inode;
    int copy;
    struct snapshot *next;
};

static int (*real_pthread_cond_wait)(pthread_cond_t *, pthread_mutex_t *);
static _Atomic slot_function pending;
static int pending_slot;
static sem_t posted;
static pid_t saver;
static char **saves;
static int saves_count;

static void fail(const char *what) {
    perror(what);
    _exit(EXIT_FAILURE);
}

static void send_all(int output, int input, off_t offset, off_t end, const char *path) {
    while (offset < end) {
        if (sendfile(output, input, &offset, end - offset) <= 0) fail(path);
    }
}

static void copy_data(int source, int copy, const char *path) {
    for (off_t data = lseek(source, 0, SEEK_DATA), hole; data >= 0; data = lseek(source, hole, SEEK_DATA)) {
        hole = lseek(source, data, SEEK_HOLE);
        if (lseek(copy, data, SEEK_SET) < 0) fail(path);
        send_all(copy, source, data, hole, path);
    }
}

static int copy_of(const char *path) {
    struct stat status;
    int source = open(path, O_RDONLY | O_CLOEXEC), copy = memfd_create(path, MFD_CLOEXEC);
    if (source < 0 || copy < 0 || fstat(source, &status) < 0 || ftruncate(copy, status.st_size) < 0) fail(path);
    copy_data(source, copy, path);
    close(source);
    return copy;
}

static int snapshot_of(const char *path, struct snapshot **snapshots) {
    struct stat status;
    if (stat(path, &status) < 0 || !S_ISREG(status.st_mode)) return -1;
    for (struct snapshot *known = *snapshots; known; known = known->next) {
        if (known->device == status.st_dev && known->inode == status.st_ino) return known->copy;
    }
    struct snapshot *snapshot = malloc(sizeof *snapshot);
    *snapshot = (struct snapshot){status.st_dev, status.st_ino, copy_of(path), *snapshots};
    *snapshots = snapshot;
    return snapshot->copy;
}

static int protection(const char *permissions) {
    return (permissions[0] == 'r' ? PROT_READ : 0) | (permissions[1] == 'w' ? PROT_WRITE : 0) | (permissions[2] == 'x' ? PROT_EXEC : 0);
}

static void snapshot_shared_files(void) {
    FILE *maps = fopen("/proc/self/maps", "r");
    struct snapshot *snapshots = NULL;
    char *line = NULL, permissions[PERMISSIONS_LENGTH + 1], *path;
    size_t length = 0;
    unsigned long start, end, offset;
    if (!maps) fail("/proc/self/maps");
    while (getline(&line, &length, maps) > 0) {
        if (sscanf(line, MAPPING_FORMAT(PERMISSIONS_LENGTH), &start, &end, permissions, &offset, &path) != 5) continue;
        int copy = permissions[SHARED_FLAG] == 's' ? snapshot_of(path, &snapshots) : -1;
        if (copy >= 0 && mmap((void *)start, end - start, protection(permissions), MAP_SHARED | MAP_FIXED, copy, offset) == MAP_FAILED) fail(path);
        free(path);
    }
    free(line);
    fclose(maps);
}

static void state_path(int slot, char path[PATH_MAX]) { ((path_function)STATE_PATH)(**(int **)CURRENT_GAME, slot, path, PATH_MAX); }

static int save_with_saves(const char *path, const int copies[]) {
    char partial[PATH_MAX + sizeof PARTIAL];
    snprintf(partial, sizeof partial, "%s" PARTIAL, path);
    if (!((save_function)SAVE_STATE)(partial)) return 0;
    int state = open(partial, O_WRONLY | O_CLOEXEC);
    if (state < 0 || lseek(state, 0, SEEK_END) < 0) fail(partial);
    for (int i = 0; i < saves_count; i++) {
        off_t size = lseek(copies[i], 0, SEEK_END);
        send_all(state, copies[i], 0, size, partial);
        if (write(state, &size, sizeof size) != sizeof size) fail(partial);
    }
    close(state);
    return rename(partial, path) == 0;
}

static void restore_saves(const char *path) {
    int state = open(path, O_RDONLY | O_CLOEXEC);
    off_t end = lseek(state, 0, SEEK_END), size;
    if (end < 0) fail(path);
    for (int i = saves_count - 1; i >= 0; i--) {
        end -= sizeof size;
        if (pread(state, &size, sizeof size, end) != sizeof size) fail(path);
        end -= size;
        int save = open(saves[i], O_WRONLY | O_TRUNC | O_CLOEXEC);
        if (save < 0) fail(saves[i]);
        send_all(save, state, end, end + size, saves[i]);
        close(save);
    }
    close(state);
}

static void save(int slot) {
    char path[PATH_MAX];
    int snapshotted[2];
    state_path(slot, path);
    if (pipe2(snapshotted, O_CLOEXEC) < 0) {
        perror("pipe");
        return;
    }
    ((control_function)PAUSE)();
    saver = fork();
    if (!saver) {
        close(snapshotted[0]);
        snapshot_shared_files();
        int copies[saves_count];
        for (int i = 0; i < saves_count; i++) copies[i] = copy_of(saves[i]);
        close(snapshotted[1]);
        _exit(save_with_saves(path, copies) ? EXIT_SUCCESS : EXIT_FAILURE);
    }
    if (saver < 0) perror("fork");
    close(snapshotted[1]);
    if (read(snapshotted[0], &(char){0}, 1) < 0) perror("read");
    close(snapshotted[0]);
    ((control_function)RESUME)();
}

static void load(int slot) {
    char path[PATH_MAX];
    state_path(slot, path);
    ((control_function)PAUSE)();
    restore_saves(path);
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
            if (saver > 0) waitpid(saver, NULL, 0);
            saver = 0;
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
    for (char *save = strtok(strdup(getenv("REDREAM_SAVES")), " "); save; save = strtok(NULL, " ")) {
        saves = realloc(saves, ++saves_count * sizeof *saves);
        saves[saves_count - 1] = save;
    }
    sem_init(&posted, 0, 0);
    pthread_create(&reader, NULL, read_commands, getenv("REDREAM_COMMANDS"));
}
