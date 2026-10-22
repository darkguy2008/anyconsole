#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <funchook.h>
#include <limits.h>
#include <link.h>
#include <linux/futex.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>

#if !defined(LIVE_SAVE_LIBRARY) || !defined(LOOP_MASK_SITE) || !defined(EXITING_LOOP_MASK) || !defined(LV2_EVENT_QUEUE_SEND) || !defined(CPU_TASK) || !defined(FINALIZE_RUN_REQUEST) || !defined(FAST_CALL) || !defined(GET_SFO_DIR) || \
    !defined(USB_ADD_EVENT) || !defined(GROUP_JOIN) || !defined(SLEEP_UNLOCKED) || !defined(MEMORY_RESET) || !defined(MEMORY_DECOMMIT) || !defined(MEMORY_COMMIT) || !defined(GEM_CONFIG_MUTEX) || !defined(GEM_TRACKER_MUTEX) || !defined(USB_EVENT_MUTEX) || !defined(USB_EVENT_WAITERS) || !defined(USB_EVENTS) || \
    !defined(SYSUTIL_PENDING_CALLBACKS) || !defined(SPU_CACHE_INITIALIZE)
#error "build with -DLIVE_SAVE_LIBRARY=<path of live-save.so> and the flags from app/recipes/rpcs3/addresses"
#endif

#define COMMANDS_VARIABLE "LIVE_SAVE_COMMANDS"
#define COMMAND_FUNCTION "live_save_command"
#define SEND_FUNCTION "live_save_send_event"
#define USB_EVENT_FUNCTION "live_save_usb_add_event"
#define GROUP_JOIN_FUNCTION "live_save_group_join"
#define TASK_ENTERED_FUNCTION "live_save_task_entered"
#define TASK_RETURNED_FUNCTION "live_save_task_returned"
#define SLEPT_FUNCTION "live_save_slept"
#define RUN_FINALIZED_FUNCTION "live_save_run_finalized"
#define CALL_HELD_FUNCTION "live_save_call_held"
#define CALL_RELEASED_FUNCTION "live_save_call_released"
#define SFO_DIR_FUNCTION "live_save_sfo_dir"
#define SPU_CACHE_FUNCTION "live_save_initialize_spu_cache"
#define HUGEPAGE_ADVICE_GRANULE 0x200000

struct live_save_member {
    const char *owner;
    size_t offset;
};

typedef void (*command_function)(const char *command);
typedef void (*method_function)(void *object);
typedef bool (*sleep_function)(void *thread, uint64_t timeout, uint64_t current_time);
typedef void (*fast_call_function)(void *thread, uint32_t address, uint64_t rtoc, bool thread_entry);
typedef void *(*sfo_dir_function)(void *sfo_dir, const void *game_path, const void *title_id);
typedef void (*sfo_dir_fix_function)(void *sfo_dir, const void *game_path);
typedef void (*memory_decommit_function)(void *pointer, size_t size, bool can_be_jit);
typedef void (*memory_commit_function)(void *pointer, size_t size, int protection);
typedef void (*spu_cache_initialize_function)(bool build_existing_cache);
typedef void (*spu_cache_hook_function)(spu_cache_initialize_function initialize, bool build_existing_cache);

static void *send_event;
static void *usb_add_event;
static void *group_join;
void *live_save_group_join_original;
static method_function cpu_task;
static method_function task_entered;
static method_function task_returned;
static sleep_function sleep_unlocked;
static method_function slept;
static method_function finalize_run_request;
static method_function run_finalized;
static fast_call_function fast_call;
static method_function call_held;
static method_function call_released;
static sfo_dir_function get_sfo_dir;
static sfo_dir_fix_function fix_sfo_dir;
static void *memory_reset;
static memory_decommit_function memory_decommit;
static memory_commit_function memory_commit;
static spu_cache_initialize_function spu_cache_initialize;
static spu_cache_hook_function initialize_spu_cache;
static command_function run_command;
static pthread_once_t library_loaded = PTHREAD_ONCE_INIT;
static __thread unsigned call_depth;
unsigned live_save_nested_calls;
int live_save_stopping;
int live_save_draining;
const struct live_save_member live_save_gem_config_mutex = {GEM_CONFIG_MUTEX};
const struct live_save_member live_save_gem_tracker_mutex = {GEM_TRACKER_MUTEX};
const struct live_save_member live_save_usb_event_mutex = {USB_EVENT_MUTEX};
const struct live_save_member live_save_usb_event_waiters = {USB_EVENT_WAITERS};
const struct live_save_member live_save_usb_events = {USB_EVENTS};
const struct live_save_member live_save_sysutil_pending_callbacks = {SYSUTIL_PENDING_CALLBACKS};

static void fail(const char *what) {
    fprintf(stderr, "live-save-loader: %s\n", what);
    exit(EXIT_FAILURE);
}

static int main_program(struct dl_phdr_info *info, size_t size, void *base) {
    (void)size;
    *(uintptr_t *)base = info->dlpi_addr;
    return 1;
}

static void replace_immediate(uintptr_t address, uint32_t expected, uint32_t replacement) {
    uint32_t found;
    memcpy(&found, (void *)address, sizeof found);
    if (found != expected) fail("an immediate to patch does not hold the value this build expects");
    uintptr_t page_size = (uintptr_t)sysconf(_SC_PAGESIZE);
    uintptr_t first_page = address & ~(page_size - 1);
    uintptr_t end = (address + sizeof replacement + page_size - 1) & ~(page_size - 1);
    if (mprotect((void *)first_page, end - first_page, PROT_READ | PROT_WRITE)) fail(strerror(errno));
    memcpy((void *)address, &replacement, sizeof replacement);
    if (mprotect((void *)first_page, end - first_page, PROT_READ | PROT_EXEC)) fail(strerror(errno));
}

__attribute__((naked)) static void route_send_event(void) {
    __asm__("jmp *send_event(%rip)");
}

__attribute__((naked)) static void route_usb_add_event(void) {
    __asm__("jmp *usb_add_event(%rip)");
}

__attribute__((naked)) static void route_group_join(void) {
    __asm__("jmp *group_join(%rip)");
}

static void then(method_function *after, void *object) {
    method_function function = __atomic_load_n(after, __ATOMIC_ACQUIRE);
    if (function) function(object);
}

static void run_cpu_task(void *thread) {
    then(&task_entered, thread);
    cpu_task(thread);
    then(&task_returned, thread);
}

static bool run_sleep_unlocked(void *thread, uint64_t timeout, uint64_t current_time) {
    bool asleep = sleep_unlocked(thread, timeout, current_time);
    then(&slept, thread);
    return asleep;
}

static void enter_nested_call(void *thread) {
    bool held = false;
    for (;;) {
        __atomic_add_fetch(&live_save_nested_calls, 1, __ATOMIC_SEQ_CST);
        if (!__atomic_load_n(&live_save_stopping, __ATOMIC_SEQ_CST)) break;
        __atomic_sub_fetch(&live_save_nested_calls, 1, __ATOMIC_SEQ_CST);
        if (!held) then(&call_held, thread);
        held = true;
        syscall(SYS_futex, &live_save_stopping, FUTEX_WAIT_PRIVATE, 1, NULL, NULL, 0);
    }
    if (held) then(&call_released, thread);
}

static void run_fast_call(void *thread, uint32_t address, uint64_t rtoc, bool thread_entry) {
    if (call_depth++) enter_nested_call(thread);
    fast_call(thread, address, rtoc, thread_entry);
    if (--call_depth && !__atomic_sub_fetch(&live_save_nested_calls, 1, __ATOMIC_SEQ_CST) && __atomic_load_n(&live_save_draining, __ATOMIC_SEQ_CST))
        syscall(SYS_futex, &live_save_nested_calls, FUTEX_WAKE_PRIVATE, INT_MAX, NULL, NULL, 0);
}

static void run_finalize_run_request(void *emulator) {
    finalize_run_request(emulator);
    then(&run_finalized, emulator);
}

static void load(void);

static void *run_get_sfo_dir(void *sfo_dir, const void *game_path, const void *title_id) {
    get_sfo_dir(sfo_dir, game_path, title_id);
    pthread_once(&library_loaded, load);
    fix_sfo_dir(sfo_dir, game_path);
    return sfo_dir;
}

static void run_memory_reset(void *pointer, size_t size, int protection, bool can_be_jit) {
    memory_decommit(pointer, size, can_be_jit);
    memory_commit(pointer, size, protection);
    if (size % HUGEPAGE_ADVICE_GRANULE == 0) madvise(pointer, size, MADV_HUGEPAGE);
}

static void run_spu_cache_initialize(bool build_existing_cache) {
    pthread_once(&library_loaded, load);
    initialize_spu_cache(spu_cache_initialize, build_existing_cache);
}

static void detour(uintptr_t base) {
    funchook_t *hook = funchook_create();
    send_event = (void *)(base + LV2_EVENT_QUEUE_SEND);
    usb_add_event = (void *)(base + USB_ADD_EVENT);
    group_join = (void *)(base + GROUP_JOIN);
    cpu_task = (method_function)(base + CPU_TASK);
    sleep_unlocked = (sleep_function)(base + SLEEP_UNLOCKED);
    finalize_run_request = (method_function)(base + FINALIZE_RUN_REQUEST);
    fast_call = (fast_call_function)(base + FAST_CALL);
    get_sfo_dir = (sfo_dir_function)(base + GET_SFO_DIR);
    memory_reset = (void *)(base + MEMORY_RESET);
    memory_decommit = (memory_decommit_function)(base + MEMORY_DECOMMIT);
    memory_commit = (memory_commit_function)(base + MEMORY_COMMIT);
    spu_cache_initialize = (spu_cache_initialize_function)(base + SPU_CACHE_INITIALIZE);
    if (funchook_prepare(hook, &send_event, route_send_event) || funchook_prepare(hook, &usb_add_event, route_usb_add_event) ||
        funchook_prepare(hook, &group_join, route_group_join) ||
        funchook_prepare(hook, (void **)&cpu_task, run_cpu_task) || funchook_prepare(hook, (void **)&sleep_unlocked, run_sleep_unlocked) ||
        funchook_prepare(hook, (void **)&finalize_run_request, run_finalize_run_request) || funchook_prepare(hook, (void **)&fast_call, run_fast_call) ||
        funchook_prepare(hook, (void **)&get_sfo_dir, run_get_sfo_dir) || funchook_prepare(hook, &memory_reset, run_memory_reset) ||
        funchook_prepare(hook, (void **)&spu_cache_initialize, run_spu_cache_initialize) ||
        funchook_install(hook, 0))
        fail(funchook_error_message(hook));
    live_save_group_join_original = group_join;
}

static void patch_program(void) {
    uintptr_t base = 0;
    dl_iterate_phdr(main_program, &base);
    replace_immediate(base + LOOP_MASK_SITE, EXITING_LOOP_MASK, PARKED_LOOP_MASK);
    detour(base);
}

static void load(void) {
    void *library = dlopen(LIVE_SAVE_LIBRARY, RTLD_NOW | RTLD_GLOBAL);
    if (!library) fail(dlerror());
    command_function run = (command_function)dlsym(library, COMMAND_FUNCTION);
    void *send = dlsym(library, SEND_FUNCTION);
    void *usb_event = dlsym(library, USB_EVENT_FUNCTION);
    void *join = dlsym(library, GROUP_JOIN_FUNCTION);
    method_function entered = (method_function)dlsym(library, TASK_ENTERED_FUNCTION);
    method_function returned = (method_function)dlsym(library, TASK_RETURNED_FUNCTION);
    method_function asleep = (method_function)dlsym(library, SLEPT_FUNCTION);
    method_function finalized = (method_function)dlsym(library, RUN_FINALIZED_FUNCTION);
    method_function held = (method_function)dlsym(library, CALL_HELD_FUNCTION);
    method_function released = (method_function)dlsym(library, CALL_RELEASED_FUNCTION);
    fix_sfo_dir = (sfo_dir_fix_function)dlsym(library, SFO_DIR_FUNCTION);
    initialize_spu_cache = (spu_cache_hook_function)dlsym(library, SPU_CACHE_FUNCTION);
    if (!run || !send || !usb_event || !join || !entered || !returned ||!asleep || !finalized || !held || !released || !fix_sfo_dir || !initialize_spu_cache) fail(dlerror());
    __atomic_store_n(&send_event, send, __ATOMIC_RELEASE);
    __atomic_store_n(&usb_add_event, usb_event, __ATOMIC_RELEASE);
    __atomic_store_n(&group_join, join, __ATOMIC_RELEASE);
    __atomic_store_n(&task_entered, entered, __ATOMIC_RELEASE);
    __atomic_store_n(&task_returned, returned, __ATOMIC_RELEASE);
    __atomic_store_n(&slept, asleep, __ATOMIC_RELEASE);
    __atomic_store_n(&run_finalized, finalized, __ATOMIC_RELEASE);
    __atomic_store_n(&call_held, held, __ATOMIC_RELEASE);
    __atomic_store_n(&call_released, released, __ATOMIC_RELEASE);
    run_command = run;
}

static void *follow(void *path) {
    char *line = NULL;
    size_t size = 0;
    for (;;) {
        FILE *commands = fopen(path, "re");
        if (!commands) fail(strerror(errno));
        while (getline(&line, &size, commands) > 0) {
            line[strcspn(line, "\n")] = '\0';
            pthread_once(&library_loaded, load);
            run_command(line);
        }
        fclose(commands);
    }
    return NULL;
}

__attribute__((constructor)) static void start(void) {
    pthread_t thread;
    char *path = getenv(COMMANDS_VARIABLE);
    if (!path) fail(COMMANDS_VARIABLE " is not set");
    patch_program();
    if (mkfifo(path, S_IRUSR | S_IWUSR) && errno != EEXIST) fail(strerror(errno));
    if (pthread_create(&thread, NULL, follow, path)) fail("cannot start the command thread");
    pthread_detach(thread);
}
