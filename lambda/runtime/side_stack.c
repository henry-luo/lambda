#include "side_stack.h"

#include "../lambda.h"
#include "../../lib/log.h"

#if defined(_WIN32)
#include <windows.h>
#else
#include <sys/mman.h>
#include <unistd.h>
#if !defined(MAP_ANON) && defined(MAP_ANONYMOUS)
#define MAP_ANON MAP_ANONYMOUS
#endif
#endif

#if defined(_MSC_VER)
#define SIDE_STACK_TLS __declspec(thread)
#else
#define SIDE_STACK_TLS __thread
#endif

typedef struct SideStackRegion {
    uint64_t* base;
    uint64_t* committed;
    uint64_t* limit;
    size_t byte_size;
} SideStackRegion;

static SIDE_STACK_TLS SideStackRegion root_region = {0};
static SIDE_STACK_TLS SideStackRegion number_region = {0};

static bool side_stack_region_reserve(SideStackRegion* region, size_t byte_size) {
    if (region->base) return true;
#if defined(_WIN32)
    void* memory = VirtualAlloc(NULL, byte_size, MEM_RESERVE, PAGE_NOACCESS);
#else
    void* memory = mmap(NULL, byte_size, PROT_READ | PROT_WRITE,
                        MAP_PRIVATE | MAP_ANON, -1, 0);
    if (memory == MAP_FAILED) memory = NULL;
#endif
    if (!memory) return false;
    region->base = (uint64_t*)memory;
    region->limit = region->base + byte_size / sizeof(uint64_t);
#if defined(_WIN32)
    region->committed = region->base;
#else
    // mmap reserves address space with demand-paged read/write access.
    region->committed = region->limit;
#endif
    region->byte_size = byte_size;
    return true;
}

static bool side_stack_region_ensure(SideStackRegion* region, uint64_t* end) {
    if (!region || !region->base || !end || end > region->limit) return false;
    if (end <= region->committed) return true;
#if defined(_WIN32)
    SYSTEM_INFO info;
    GetSystemInfo(&info);
    size_t page_size = info.dwPageSize ? (size_t)info.dwPageSize : 4096u;
    uintptr_t target = ((uintptr_t)end + page_size - 1u) & ~(page_size - 1u);
    if (target > (uintptr_t)region->limit) target = (uintptr_t)region->limit;
    size_t bytes = target - (uintptr_t)region->committed;
    // Windows reservations are inaccessible until the advancing watermark
    // commits their pages; committing the full budget would consume commit
    // charge even when a thread only touches a few slots.
    void* committed = VirtualAlloc(region->committed, bytes, MEM_COMMIT,
                                   PAGE_READWRITE);
    if (!committed) return false;
    region->committed = (uint64_t*)target;
    return true;
#else
    return true;
#endif
}

bool lambda_side_stack_bind(Context* runtime_context) {
    if (!runtime_context) return false;
    if (!side_stack_region_reserve(&root_region,
                                   LAMBDA_SIDE_ROOT_RESERVE_BYTES) ||
        !side_stack_region_reserve(&number_region,
                                   LAMBDA_SIDE_NUMBER_RESERVE_BYTES)) {
        log_error("side-stack reserve: unable to reserve root/number regions");
        return false;
    }
    bool root_bound = runtime_context->side_root_base == root_region.base &&
        runtime_context->side_root_top >= root_region.base &&
        runtime_context->side_root_top <= root_region.limit;
    bool number_bound = runtime_context->side_number_base == number_region.base &&
        runtime_context->side_number_top >= number_region.base &&
        runtime_context->side_number_top <= number_region.limit;
    runtime_context->side_root_base = root_region.base;
    if (!root_bound) runtime_context->side_root_top = root_region.base;
    runtime_context->side_root_commit_limit = root_region.committed;
    runtime_context->side_root_limit = root_region.limit;
    runtime_context->side_number_base = number_region.base;
    if (!number_bound) runtime_context->side_number_top = number_region.base;
    runtime_context->side_number_commit_limit = number_region.committed;
    runtime_context->side_number_limit = number_region.limit;
    return true;
}

bool lambda_side_stack_ensure(Context* runtime_context, size_t root_slots,
                              size_t number_slots) {
    if (!runtime_context) return false;
    if (!runtime_context->side_root_base && !lambda_side_stack_bind(runtime_context)) {
        return false;
    }
    if (root_slots > (size_t)(runtime_context->side_root_limit -
                              runtime_context->side_root_top) ||
        number_slots > (size_t)(runtime_context->side_number_limit -
                                runtime_context->side_number_top)) {
        return false;
    }
    uint64_t* root_end = runtime_context->side_root_top + root_slots;
    uint64_t* number_end = runtime_context->side_number_top + number_slots;
    if (!side_stack_region_ensure(&root_region, root_end) ||
        !side_stack_region_ensure(&number_region, number_end)) {
        return false;
    }
    runtime_context->side_root_commit_limit = root_region.committed;
    runtime_context->side_number_commit_limit = number_region.committed;
    return true;
}

void lambda_side_stack_reset(Context* runtime_context) {
    if (!runtime_context) return;
    runtime_context->side_root_top = runtime_context->side_root_base;
    runtime_context->side_number_top = runtime_context->side_number_base;
}

LambdaSideStackSnapshot lambda_side_stack_snapshot(Context* runtime_context) {
    LambdaSideStackSnapshot snapshot = {0};
    if (!runtime_context) return snapshot;
    snapshot.root_top = runtime_context->side_root_top;
    snapshot.number_top = runtime_context->side_number_top;
    return snapshot;
}

void lambda_side_stack_restore(Context* runtime_context,
                               LambdaSideStackSnapshot snapshot) {
    if (!runtime_context) return;
    if (runtime_context->side_root_base && runtime_context->side_root_limit &&
        snapshot.root_top >= runtime_context->side_root_base &&
        snapshot.root_top <= runtime_context->side_root_limit) {
        runtime_context->side_root_top = snapshot.root_top;
    }
    if (runtime_context->side_number_base && runtime_context->side_number_limit &&
        snapshot.number_top >= runtime_context->side_number_base &&
        snapshot.number_top <= runtime_context->side_number_limit) {
        runtime_context->side_number_top = snapshot.number_top;
    }
}

LambdaRecoveryCheckpoint lambda_recovery_checkpoint_capture(Context* runtime_context) {
    LambdaRecoveryCheckpoint checkpoint = {0};
    checkpoint.context = runtime_context;
    checkpoint.side_stack = lambda_side_stack_snapshot(runtime_context);
    checkpoint.active = runtime_context != NULL;
    return checkpoint;
}

void lambda_recovery_checkpoint_restore(LambdaRecoveryCheckpoint* checkpoint) {
    if (!checkpoint || !checkpoint->active) return;
    // A non-local jump can skip any number of nested generated/native frames;
    // restore both allocation regions before the landing path may allocate.
    lambda_side_stack_restore(checkpoint->context, checkpoint->side_stack);
    checkpoint->active = false;
}

void lambda_recovery_checkpoint_disarm(LambdaRecoveryCheckpoint* checkpoint) {
    if (checkpoint) checkpoint->active = false;
}

uint64_t* lambda_side_number_alloc(Context* runtime_context) {
    if (!runtime_context || !lambda_side_stack_ensure(runtime_context, 0, 1)) {
        return NULL;
    }
    return runtime_context->side_number_top++;
}

static void side_stack_region_decommit(SideStackRegion* region, uint64_t* top) {
    if (!region || !top || !region->limit || top >= region->limit) return;
#if defined(_WIN32)
    SYSTEM_INFO info;
    GetSystemInfo(&info);
    size_t page_size = info.dwPageSize ? (size_t)info.dwPageSize : 4096u;
    uintptr_t page_mask = (uintptr_t)page_size - 1u;
    uintptr_t start = ((uintptr_t)top + page_mask) & ~page_mask;
    uintptr_t end = (uintptr_t)region->committed & ~page_mask;
    if (start < end && VirtualFree((void*)start, end - start, MEM_DECOMMIT)) {
        region->committed = (uint64_t*)start;
    }
#else
    long page_size = sysconf(_SC_PAGESIZE);
    if (page_size <= 0) return;
    uintptr_t page_mask = (uintptr_t)page_size - 1u;
    uintptr_t start = ((uintptr_t)top + page_mask) & ~page_mask;
    uintptr_t end = (uintptr_t)region->limit & ~page_mask;
    if (start < end) madvise((void*)start, end - start, MADV_DONTNEED);
#endif
}

void lambda_side_stack_decommit_unused(Context* runtime_context) {
    if (!runtime_context) return;
    side_stack_region_decommit(&root_region, runtime_context->side_root_top);
    side_stack_region_decommit(&number_region, runtime_context->side_number_top);
    runtime_context->side_root_commit_limit = root_region.committed;
    runtime_context->side_number_commit_limit = number_region.committed;
}

bool lambda_root_frame_begin(Context* runtime_context, LambdaRootFrame* frame,
                             size_t slot_count) {
    if (!frame) return false;
    frame->context = runtime_context;
    frame->watermark = NULL;
    frame->slots = NULL;
    frame->slot_count = 0;
    frame->next_slot = 0;
    frame->active = false;
    if (!runtime_context ||
            !lambda_side_stack_ensure(runtime_context, slot_count, 0)) {
        return false;
    }

    frame->watermark = runtime_context->side_root_top;
    frame->slots = frame->watermark;
    frame->slot_count = slot_count;
    // Zero before publishing the new watermark so a forced collection can
    // never interpret stale words from a previous activation as live roots.
    for (size_t i = 0; i < slot_count; i++) frame->slots[i] = 0;
    runtime_context->side_root_top += slot_count;
    frame->active = true;
    return true;
}

uint64_t* lambda_root_frame_slot(LambdaRootFrame* frame, size_t index) {
    if (!frame || !frame->active || index >= frame->slot_count) return NULL;
    return &frame->slots[index];
}

uint64_t* lambda_root_frame_take_slot(LambdaRootFrame* frame) {
    if (!frame || frame->next_slot >= frame->slot_count) return NULL;
    return lambda_root_frame_slot(frame, frame->next_slot++);
}

void lambda_root_frame_end(LambdaRootFrame* frame) {
    if (!frame || !frame->active) return;
    // Restore only a properly nested frame. A mismatched watermark means a
    // generated/native child frame escaped its activation and must not be
    // hidden by silently rewinding through it.
    uint64_t* expected_top = frame->slots + frame->slot_count;
    if (frame->context && frame->context->side_root_top == expected_top) {
        frame->context->side_root_top = frame->watermark;
    } else {
        log_error("native-root-frame: non-LIFO frame restoration");
    }
    frame->active = false;
}
