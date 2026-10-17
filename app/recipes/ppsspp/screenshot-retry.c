// screenshot-retry.c
//
// PPSSPP v1.20.4 destroys the savestate screenshot callback while it is still running.
// A state saved before the game has a display framebuffer fails to take its screenshot,
// the failure path calls the callback in place, and the callback queues a retry that
// wipes the very std::function it is executing in. The next copy reads the freed closure
// and the process aborts with std::length_error. PPSSPP_BUG.md has the full story.
//
// Upstream's fix is to move the callback out of g_pendingScreenshot before calling it.
// This .so does exactly that, in FailPendingScreenshot. The engine dispatches the pending
// screenshot from four failure paths; each ends by loading the callback's std::function
// into %rdi and calling through its _M_invoke slot. At load time the hook rewrites those
// (lea, call) pairs to call FailPendingScreenshot instead, so the closure is taken out of
// the global before it can be freed underneath itself.
//
// PPSSPPSDL is stripped and links libstdc++ statically, so there are no symbols to hook.
// app/recipes/ppsspp/addresses finds the callback and the four sites in the binary at
// build time and passes them in as -D flags. Nothing here is hardcoded.

#define _GNU_SOURCE
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>

#if !defined(SCREENSHOT_CALLBACK) || !defined(SCREENSHOT_SITES)
#error "build with the flags from app/recipes/ppsspp/addresses"
#endif

// A libstdc++ std::function, as far as this needs to know: 16 bytes of in-place closure
// storage, then the manager that destroys it and the invoke that runs it.
struct callback {
    uint8_t closure[16];
    void *manager;
    void *invoke;
};

// g_pendingScreenshot.callback and the four lea/call pairs that dispatch it, both found
// in the binary by app/recipes/ppsspp/addresses. The sites come sorted ascending, so the
// first and last bound the pages the constructor has to make writable.
static struct callback *const pending = (struct callback *)(uintptr_t)SCREENSHOT_CALLBACK;
static const uint64_t sites[] = { SCREENSHOT_SITES };

#define SITE_COUNT (sizeof sites / sizeof sites[0])
#define SITE_SIZE 13
#define PAGE_SIZE 0x1000

// The upstream fix: take the callback out of the global, clear the slot, then call the
// local copy. Because the slot is empty, a re-entrant ScheduleScreenshot's
// `g_pendingScreenshot = {}` does not free the closure being called, and the retry the
// callback queues survives.
static void FailPendingScreenshot(void) {
    struct callback callback = *pending;
    memset(pending, 0, sizeof *pending);
    if (!callback.manager) return;
    int result = 0;  // ScreenshotResult::ScreenshotNotPossible
    ((void (*)(void *, void *))callback.invoke)(&callback, &result);
    // _M_manager with operation 3 (destroy_functor) releases the closure.
    ((void (*)(void *, const void *, int))callback.manager)(&callback, &callback, 3);
}

// Each site is `lea rdi, [rip + SCREENSHOT_CALLBACK]; call [rip + ...]`, 13 bytes
// together. Turn it into `movabs $FailPendingScreenshot, %rax; call %rax; nop`, the same
// size. The absolute destination is written here at load time because the hook is loaded
// wherever ASLR puts it, out of reach of the original rip-relative call.
static void write_jump(uint8_t *site) {
    uint64_t destination = (uint64_t)(uintptr_t)FailPendingScreenshot;
    site[0] = 0x48;  // movabs $destination, %rax
    site[1] = 0xb8;
    memcpy(site + 2, &destination, sizeof destination);
    site[10] = 0xff;  // call %rax
    site[11] = 0xd0;
    site[12] = 0x90;  // nop, to keep the region 13 bytes
}

__attribute__((constructor)) static void patch_callback_sites(void) {
    uint64_t start = sites[0] & ~(uint64_t)(PAGE_SIZE - 1);
    uint64_t end = (sites[SITE_COUNT - 1] + SITE_SIZE + PAGE_SIZE - 1) & ~(uint64_t)(PAGE_SIZE - 1);
    // Make the pages holding the sites writable but not executable while we edit them,
    // then put them back to executable. The constructor runs before any other thread, so
    // nothing executes these pages in between.
    if (mprotect((void *)start, end - start, PROT_READ | PROT_WRITE)) {
        fprintf(stderr, "[screenshot-retry] mprotect failed: %s\n", strerror(errno));
        return;
    }
    for (size_t i = 0; i < SITE_COUNT; i++) write_jump((uint8_t *)(uintptr_t)sites[i]);
    mprotect((void *)start, end - start, PROT_READ | PROT_EXEC);
    fprintf(stderr, "[screenshot-retry] patched %zu/%zu sites -> FailPendingScreenshot %p\n",
            SITE_COUNT, SITE_COUNT, (void *)FailPendingScreenshot);
}
