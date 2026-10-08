#include "activation.h"

#include "transpiler.hpp"
#include "runtime-state.h"
#include "recovery_frame.h"
#include "side_stack.h"
#include "lambda-stack.h"
#include "template_state.h"
#include "gc/gc_heap.h"
#include "../../lib/fiber.h"
#include "../../lib/log.h"
#include "../../lib/memtrack.h"

#include <pthread.h>
#include <string.h>

// Reservations are virtual and committed on touch. RA-O5 tunes the native
// size; the segments only need to match what that stack can nest.
#define ACTIVATION_NATIVE_STACK_BYTES (2u * 1024u * 1024u)
#define ACTIVATION_ROOT_SEGMENT_BYTES (512u * 1024u)
#define ACTIVATION_NUMBER_SEGMENT_BYTES (512u * 1024u)
// Pages every activation touches again near its stack top survive a trim.
#define ACTIVATION_STACK_KEEP_BYTES (16u * 1024u)
#define ACTIVATION_POOL_MAX 32
// Cold-park compaction (§8.1 mitigation 3). Once more than WARM_MAX
// activations are parked on a thread, the oldest is copied out to the heap and
// its pages returned, provided its used stack and segments fit in
// COMPACT_MAX_BYTES; a larger one keeps its pages. A tight await loop never
// exceeds the warm window and pays nothing.
#define ACTIVATION_WARM_MAX 16
#define ACTIVATION_COMPACT_MAX_BYTES (16u * 1024u)
#define ACTIVATION_AMBIENT_EXT_BYTES 128
#define ACTIVATION_AMBIENT_HOOK_MAX 4

// Root-segment slots reserved below the entry's own frames.
enum {
    ACTIVATION_SLOT_ARG = 0,
    ACTIVATION_SLOT_TRANSFER = 1,
    ACTIVATION_SLOT_COUNT = 2,
};

typedef struct ActivationStacks {
    FiberStack native;
    LambdaSideStackRegion root;
    LambdaSideStackRegion number;
    struct ActivationStacks* next_free;
} ActivationStacks;

// Everything that is per-thread *and* LIFO with the native stack (RA5). The
// switch saves the departing stack's copy and installs the arriving one.
typedef struct ActivationAmbient {
    uint64_t* side_root_base;
    uint64_t* side_root_top;
    uint64_t* side_root_commit_limit;
    uint64_t* side_root_limit;
    uint64_t* side_number_base;
    uint64_t* side_number_top;
    uint64_t* side_number_commit_limit;
    uint64_t* side_number_limit;
    uintptr_t stack_limit;
    LambdaStackBounds bounds;
    LambdaRecoveryFrame* recovery_top;
    List* current_vargs;
    const char* current_file;
    LambdaModuleState* active_module_state;
    LambdaModuleState* jit_current_module_state;
    uint32_t execution_depth;
    alignas(16) uint8_t ext[ACTIVATION_AMBIENT_EXT_BYTES];
} ActivationAmbient;

struct Activation {
    ActivationStatus status;
    bool strong;
    bool faulted;
    // native host frames now on this stack (RA10)
    int barrier_depth;
    EvalContext* owner;
    ActivationEntry entry;
    void* user;
    void* sp;
    Activation* resumer;
    ActivationStacks* stacks;
    ActivationAmbient ambient;
    // GC scope depths are relative to the resume chain: an activation's own
    // open defer/no-GC scopes travel with it, layered on its resumer's.
    int gc_entry_defer_depth;
    int gc_own_defer_depth;
#ifndef NDEBUG
    int gc_entry_no_gc_depth;
    int gc_own_no_gc_depth;
#endif
    Item result;
    LambdaFaultRecord fault;
    // Strong activations only: the collector's root list (weak ones are traced
    // by their owners, so visiting them every collection would be wasted).
    Activation* strong_prev;
    Activation* strong_next;
    // Parked and not yet compacted, in park order (oldest first).
    Activation* warm_prev;
    Activation* warm_next;
    bool warm;
    // Too large to compact; never retried while parked.
    bool pinned;
    // Compacted: the used native stack, root and number words live in `image`
    // and the pages behind them are returned. Addresses stay reserved, so a
    // resume copies them back to exactly where interior pointers expect them.
    uint8_t* image;
    size_t image_native_bytes;
    size_t image_root_words;
    size_t image_number_words;
};

typedef struct ActivationThread {
    Activation* current;
    void* base_sp;
    ActivationAmbient base_ambient;
    // set while activation_call_on_base runs work beneath the parked base
    // frames; no activation may switch until it returns (RA6)
    bool on_base;
    Activation* strong;
    Activation* warm_head;
    Activation* warm_tail;
    int warm_count;
} ActivationThread;

static __thread ActivationThread activation_thread;

// Idle stacks belong to no thread, and a runtime may execute on one thread
// and tear down on another, so the pool is process-wide.
static pthread_mutex_t stack_pool_mutex = PTHREAD_MUTEX_INITIALIZER;
static ActivationStacks* stack_pool = NULL;
static int stack_pool_count = 0;

static ActivationAmbientHook ambient_hooks[ACTIVATION_AMBIENT_HOOK_MAX];
static size_t ambient_hook_offsets[ACTIVATION_AMBIENT_HOOK_MAX];
static int ambient_hook_count = 0;
static size_t ambient_hook_bytes = 0;

extern "C" bool activation_register_ambient_hook(const ActivationAmbientHook* hook) {
    if (!hook || !hook->save || !hook->load) return false;
    size_t aligned = (ambient_hook_bytes + 15u) & ~(size_t)15u;
    if (ambient_hook_count >= ACTIVATION_AMBIENT_HOOK_MAX ||
            aligned + hook->size > ACTIVATION_AMBIENT_EXT_BYTES) {
        log_error("activation: ambient hook table full");
        return false;
    }
    ambient_hooks[ambient_hook_count] = *hook;
    ambient_hook_offsets[ambient_hook_count] = aligned;
    ambient_hook_count++;
    ambient_hook_bytes = aligned + hook->size;
    return true;
}

static void ambient_save(ActivationAmbient* ambient) {
    EvalContext* ctx = context;
    if (ctx) {
        ambient->side_root_base = ctx->side_root_base;
        ambient->side_root_top = ctx->side_root_top;
        ambient->side_root_commit_limit = ctx->side_root_commit_limit;
        ambient->side_root_limit = ctx->side_root_limit;
        ambient->side_number_base = ctx->side_number_base;
        ambient->side_number_top = ctx->side_number_top;
        ambient->side_number_commit_limit = ctx->side_number_commit_limit;
        ambient->side_number_limit = ctx->side_number_limit;
        ambient->stack_limit = ctx->stack_limit;
        ambient->current_vargs = ctx->current_vargs;
        ambient->current_file = ctx->current_file;
        ambient->active_module_state = ctx->active_module_state;
        ambient->jit_current_module_state = ctx->jit_current_module_state;
        ambient->execution_depth = ctx->execution_depth;
    }
    ambient->bounds = lambda_stack_bounds_get();
    ambient->recovery_top = lambda_recovery_frame_tls_top;
    for (int i = 0; i < ambient_hook_count; i++) {
        ambient_hooks[i].save(ambient->ext + ambient_hook_offsets[i]);
    }
}

static void ambient_load(const ActivationAmbient* ambient, ActivationStacks* stacks) {
    EvalContext* ctx = context;
    if (ctx) {
        ctx->side_root_base = ambient->side_root_base;
        ctx->side_root_top = ambient->side_root_top;
        ctx->side_root_commit_limit = ambient->side_root_commit_limit;
        ctx->side_root_limit = ambient->side_root_limit;
        ctx->side_number_base = ambient->side_number_base;
        ctx->side_number_top = ambient->side_number_top;
        ctx->side_number_commit_limit = ambient->side_number_commit_limit;
        ctx->side_number_limit = ambient->side_number_limit;
        ctx->stack_limit = ambient->stack_limit;
        ctx->current_vargs = ambient->current_vargs;
        ctx->current_file = ambient->current_file;
        ctx->active_module_state = ambient->active_module_state;
        ctx->jit_current_module_state = ambient->jit_current_module_state;
        ctx->execution_depth = ambient->execution_depth;
    }
    lambda_side_stack_regions_select(stacks ? &stacks->root : NULL,
        stacks ? &stacks->number : NULL);
    lambda_stack_bounds_set(ambient->bounds);
    lambda_recovery_frame_tls_top = ambient->recovery_top;
    for (int i = 0; i < ambient_hook_count; i++) {
        ambient_hooks[i].load(ambient->ext + ambient_hook_offsets[i]);
    }
}

static gc_heap_t* activation_gc(void) {
    return context && context->heap ? context->heap->gc : NULL;
}

// Entering an activation layers its own open GC scopes on the resumer's.
static void gc_depth_enter(Activation* activation) {
    gc_heap_t* gc = activation_gc();
    if (!gc) return;
    activation->gc_entry_defer_depth = gc->defer_collection_depth;
    gc->defer_collection_depth += activation->gc_own_defer_depth;
#ifndef NDEBUG
    activation->gc_entry_no_gc_depth = gc->no_gc_scope_depth;
    gc->no_gc_scope_depth += activation->gc_own_no_gc_depth;
#endif
}

static void gc_depth_leave(Activation* activation) {
    gc_heap_t* gc = activation_gc();
    if (!gc) return;
    activation->gc_own_defer_depth =
        gc->defer_collection_depth - activation->gc_entry_defer_depth;
    gc->defer_collection_depth = activation->gc_entry_defer_depth;
#ifndef NDEBUG
    activation->gc_own_no_gc_depth =
        gc->no_gc_scope_depth - activation->gc_entry_no_gc_depth;
    gc->no_gc_scope_depth = activation->gc_entry_no_gc_depth;
#endif
}

// The segments live inside the stack's mapping and are released with it.
static void stacks_carve_segment(LambdaSideStackRegion* region, uint64_t* base,
                                 size_t bytes) {
    region->base = base;
    region->limit = base + bytes / sizeof(uint64_t);
    region->committed = region->limit;
    region->byte_size = bytes;
}

static void stacks_free(ActivationStacks* stacks) {
    fiber_stack_release(&stacks->native);
    mem_free(stacks);
}

static ActivationStacks* stacks_acquire(void) {
    pthread_mutex_lock(&stack_pool_mutex);
    ActivationStacks* pooled = stack_pool;
    if (pooled) {
        stack_pool = pooled->next_free;
        stack_pool_count--;
    }
    pthread_mutex_unlock(&stack_pool_mutex);
    if (pooled) {
        pooled->next_free = NULL;
        return pooled;
    }
    ActivationStacks* stacks = (ActivationStacks*)mem_calloc(
        1, sizeof(ActivationStacks), MEM_CAT_EVAL);
    if (!stacks) return NULL;
    // One mapping per activation: the stack, then its root and number
    // segments directly above it. One reservation keeps creation to a single
    // map call and keeps the pages an activation touches close together, so
    // neighbouring activations share page-table pages.
    if (!fiber_stack_reserve(&stacks->native, ACTIVATION_NATIVE_STACK_BYTES,
            ACTIVATION_ROOT_SEGMENT_BYTES + ACTIVATION_NUMBER_SEGMENT_BYTES)) {
        log_error("activation: failed to reserve stacks");
        mem_free(stacks);
        return NULL;
    }
    stacks_carve_segment(&stacks->root, (uint64_t*)stacks->native.high,
        ACTIVATION_ROOT_SEGMENT_BYTES);
    stacks_carve_segment(&stacks->number,
        (uint64_t*)(stacks->native.high + ACTIVATION_ROOT_SEGMENT_BYTES),
        ACTIVATION_NUMBER_SEGMENT_BYTES);
    return stacks;
}

static void stacks_release(ActivationStacks* stacks) {
    if (!stacks) return;
    // A pooled stack keeps its mapping but not the pages a deep run touched.
    fiber_stack_trim(&stacks->native, ACTIVATION_STACK_KEEP_BYTES);
    LambdaSideStackRegion* regions[2] = {&stacks->root, &stacks->number};
    for (int i = 0; i < 2; i++) {
        LambdaSideStackRegion* region = regions[i];
        size_t bytes = (size_t)((uint8_t*)region->limit - (uint8_t*)region->base);
        fiber_memory_discard(region->base, bytes);
        fiber_memory_reclaim(region->base, bytes);
    }
    pthread_mutex_lock(&stack_pool_mutex);
    bool pooled = stack_pool_count < ACTIVATION_POOL_MAX;
    if (pooled) {
        stacks->next_free = stack_pool;
        stack_pool = stacks;
        stack_pool_count++;
    }
    pthread_mutex_unlock(&stack_pool_mutex);
    if (!pooled) stacks_free(stacks);
}

extern "C" void activation_release_pool(void) {
    pthread_mutex_lock(&stack_pool_mutex);
    ActivationStacks* stacks = stack_pool;
    stack_pool = NULL;
    stack_pool_count = 0;
    pthread_mutex_unlock(&stack_pool_mutex);
    while (stacks) {
        ActivationStacks* next = stacks->next_free;
        stacks_free(stacks);
        stacks = next;
    }
}

static void strong_link(Activation* activation) {
    if (!activation->strong) return;
    ActivationThread* thread = &activation_thread;
    activation->strong_next = thread->strong;
    if (thread->strong) thread->strong->strong_prev = activation;
    thread->strong = activation;
}

static void strong_unlink(Activation* activation) {
    if (!activation->strong) return;
    ActivationThread* thread = &activation_thread;
    if (activation->strong_prev) activation->strong_prev->strong_next = activation->strong_next;
    else if (thread->strong == activation) thread->strong = activation->strong_next;
    if (activation->strong_next) activation->strong_next->strong_prev = activation->strong_prev;
    activation->strong_prev = activation->strong_next = NULL;
}

static uint64_t* activation_slots(const Activation* activation) {
    return activation->stacks ? activation->stacks->root.base : NULL;
}

static void warm_unlink(Activation* activation) {
    if (!activation->warm) return;
    ActivationThread* thread = &activation_thread;
    if (activation->warm_prev) activation->warm_prev->warm_next = activation->warm_next;
    else thread->warm_head = activation->warm_next;
    if (activation->warm_next) activation->warm_next->warm_prev = activation->warm_prev;
    else thread->warm_tail = activation->warm_prev;
    activation->warm_prev = activation->warm_next = NULL;
    activation->warm = false;
    thread->warm_count--;
}

static void warm_append(Activation* activation) {
    ActivationThread* thread = &activation_thread;
    activation->warm_prev = thread->warm_tail;
    activation->warm_next = NULL;
    if (thread->warm_tail) thread->warm_tail->warm_next = activation;
    else thread->warm_head = activation;
    thread->warm_tail = activation;
    activation->warm = true;
    thread->warm_count++;
}

// The used extent of each region of a parked activation: the native stack
// from its saved stack pointer up, and each segment up to its watermark.
typedef struct ActivationExtent {
    uintptr_t native_low;
    size_t native_bytes;
    size_t root_words;
    size_t number_words;
} ActivationExtent;

static ActivationExtent activation_extent(const Activation* activation) {
    ActivationExtent extent = {};
    ActivationStacks* stacks = activation->stacks;
    extent.native_low = (uintptr_t)activation->sp;
    extent.native_bytes = stacks->native.high - extent.native_low;
    extent.root_words = (size_t)(activation->ambient.side_root_top -
        activation->ambient.side_root_base);
    extent.number_words = (size_t)(activation->ambient.side_number_top -
        activation->ambient.side_number_base);
    return extent;
}

// Page-aligned span covering [low, low + bytes), for discarding the pages a
// region actually touched.
static void discard_span(uintptr_t low, size_t bytes, bool reclaim) {
    if (!bytes) return;
    uintptr_t page = (uintptr_t)fiber_page_size();
    uintptr_t start = low & ~(page - 1);
    uintptr_t end = (low + bytes + page - 1) & ~(page - 1);
    if (reclaim) fiber_memory_reclaim((void*)start, end - start);
    else fiber_memory_discard((void*)start, end - start);
}

static void activation_compact(Activation* activation) {
    ActivationExtent extent = activation_extent(activation);
    size_t root_bytes = extent.root_words * sizeof(uint64_t);
    size_t number_bytes = extent.number_words * sizeof(uint64_t);
    size_t total = extent.native_bytes + root_bytes + number_bytes;
    if (total > ACTIVATION_COMPACT_MAX_BYTES) {
        activation->pinned = true;
        return;
    }
    uint8_t* image = (uint8_t*)mem_alloc(total ? total : 1, MEM_CAT_EVAL);
    if (!image) return;
    ActivationStacks* stacks = activation->stacks;
    memcpy(image, (void*)extent.native_low, extent.native_bytes);
    memcpy(image + extent.native_bytes, stacks->root.base, root_bytes);
    memcpy(image + extent.native_bytes + root_bytes, stacks->number.base, number_bytes);
    log_debug("activation compact: native=%zu root_words=%zu number_words=%zu",
        extent.native_bytes, extent.root_words, extent.number_words);
    activation->image = image;
    activation->image_native_bytes = extent.native_bytes;
    activation->image_root_words = extent.root_words;
    activation->image_number_words = extent.number_words;
    discard_span(extent.native_low, extent.native_bytes, false);
    discard_span((uintptr_t)stacks->root.base, root_bytes, false);
    discard_span((uintptr_t)stacks->number.base, number_bytes, false);
}

static void activation_restore(Activation* activation) {
    if (!activation->image) return;
    ActivationStacks* stacks = activation->stacks;
    uint8_t* image = activation->image;
    size_t root_bytes = activation->image_root_words * sizeof(uint64_t);
    size_t number_bytes = activation->image_number_words * sizeof(uint64_t);
    uintptr_t native_low = stacks->native.high - activation->image_native_bytes;
    discard_span(native_low, activation->image_native_bytes, true);
    discard_span((uintptr_t)stacks->root.base, root_bytes, true);
    discard_span((uintptr_t)stacks->number.base, number_bytes, true);
    memcpy((void*)native_low, image, activation->image_native_bytes);
    memcpy(stacks->root.base, image + activation->image_native_bytes, root_bytes);
    memcpy(stacks->number.base, image + activation->image_native_bytes + root_bytes,
        number_bytes);
    mem_free(image);
    activation->image = NULL;
}

// A freshly parked activation joins the warm window; the oldest one past
// the window is compacted if it is small enough.
static void activation_note_parked(Activation* activation) {
    if (activation->pinned) return;
    warm_append(activation);
    ActivationThread* thread = &activation_thread;
    while (thread->warm_count > ACTIVATION_WARM_MAX) {
        Activation* cold = thread->warm_head;
        warm_unlink(cold);
        activation_compact(cold);
    }
}

// Return to the resumer. The departing activation's state is saved first so a
// later resume reinstalls it exactly; this call returns only on that resume.
static void activation_leave(Activation* activation, ActivationStatus status) {
    ActivationThread* thread = &activation_thread;
    ambient_save(&activation->ambient);
    gc_depth_leave(activation);
    activation->status = status;
    Activation* resumer = activation->resumer;
    activation->resumer = NULL;
    thread->current = resumer;
    ambient_load(resumer ? &resumer->ambient : &thread->base_ambient,
        resumer ? resumer->stacks : NULL);
    fiber_switch(&activation->sp, resumer ? resumer->sp : thread->base_sp);
}

static void activation_thunk(void* arg) {
    Activation* activation = (Activation*)arg;
    uint64_t* slots = activation_slots(activation);
    // Every activation owns an execution boundary at its root: a fault on this
    // stack must land on this stack, never jump to the resumer's (D5.3.6).
    LambdaRecoveryFrame* frame = lambda_recovery_frame_begin_for(
        (Context*)context, LAMBDA_RECOVERY_CAP_EXECUTION_BOUNDARY);
    if (!frame) {
        activation->faulted = true;
        lambda_fault_record_prepare(&activation->fault,
            LAMBDA_FAULT_OUT_OF_MEMORY, ERR_OK);
        slots[ACTIVATION_SLOT_TRANSFER] = err2it(&activation->fault.error).item;
    } else if (LAMBDA_RECOVERY_FRAME_SETJMP(frame)) {
        if (!lambda_recovery_frame_restore_landing(frame)) {
            log_error("activation: landing invariant failed");
            lambda_fault_record_prepare(&frame->fault,
                LAMBDA_FAULT_RUNTIME_BOUNDARY_DEFECT, ERR_OK);
        } else {
            (void)lambda_recovery_frame_fault_item((Context*)context, frame);
        }
        // Keep a copy owned by the activation; the frame is released next.
        activation->fault = frame->fault;
        activation->faulted = true;
        lambda_recovery_frame_end(frame);
        slots[ACTIVATION_SLOT_TRANSFER] = err2it(&activation->fault.error).item;
    } else if (!lambda_recovery_frame_arm(frame)) {
        lambda_recovery_frame_end(frame);
        activation->faulted = true;
        lambda_fault_record_prepare(&activation->fault,
            LAMBDA_FAULT_RUNTIME_BOUNDARY_DEFECT, ERR_OK);
        slots[ACTIVATION_SLOT_TRANSFER] = err2it(&activation->fault.error).item;
    } else {
        // D5.4.1: fresh activation stacks start at depth zero, but their guest
        // frames must block evaluator handoff until they park or return.
        RuntimeExecutionScope execution_scope;
        Item result = activation->entry(activation,
            (Item){.item = slots[ACTIVATION_SLOT_ARG]});
        lambda_recovery_frame_end(frame);
        slots[ACTIVATION_SLOT_TRANSFER] = result.item;
    }
    activation_leave(activation, ACTIVATION_DONE);
    log_error("activation: a finished activation was resumed");
    abort();
}

extern "C" Activation* activation_create(ActivationEntry entry, Item arg,
                                         bool strong, void* user) {
    if (!entry || !context) return NULL;
    Activation* activation = (Activation*)mem_calloc(1, sizeof(Activation), MEM_CAT_EVAL);
    if (!activation) return NULL;
    activation->stacks = stacks_acquire();
    if (!activation->stacks) {
        mem_free(activation);
        return NULL;
    }
    activation->status = ACTIVATION_NEW;
    activation->strong = strong;
    activation->owner = context;
    activation->entry = entry;
    activation->user = user;
    activation->result = ItemNull;
    lambda_fault_record_init(&activation->fault);

    ActivationStacks* stacks = activation->stacks;
    uint64_t* slots = stacks->root.base;
    slots[ACTIVATION_SLOT_ARG] = arg.item;
    slots[ACTIVATION_SLOT_TRANSFER] = ItemNull.item;

    // A fresh stack inherits the creator's execution selectors, then owns
    // them: a later resume from elsewhere reinstalls these, not the resumer's.
    ActivationAmbient* ambient = &activation->ambient;
    ambient_save(ambient);
    ambient->side_root_base = stacks->root.base;
    ambient->side_root_top = stacks->root.base + ACTIVATION_SLOT_COUNT;
    ambient->side_root_commit_limit = stacks->root.committed;
    ambient->side_root_limit = stacks->root.limit;
    ambient->side_number_base = stacks->number.base;
    ambient->side_number_top = stacks->number.base;
    ambient->side_number_commit_limit = stacks->number.committed;
    ambient->side_number_limit = stacks->number.limit;
    ambient->bounds = lambda_stack_bounds_for(stacks->native.low, stacks->native.high);
    ambient->stack_limit = lambda_stack_recoverable_limit_for(ambient->bounds);
    ambient->recovery_top = NULL;
    ambient->current_vargs = NULL;
    ambient->execution_depth = 0;
    for (int i = 0; i < ambient_hook_count; i++) {
        if (ambient_hooks[i].init) {
            ambient_hooks[i].init(ambient->ext + ambient_hook_offsets[i]);
        }
    }
    activation->sp = fiber_stack_prime(&stacks->native, activation_thunk, activation);
    strong_link(activation);
    return activation;
}

extern "C" ActivationStatus activation_resume(Activation* activation, Item input) {
    if (!activation) return ACTIVATION_DONE;
    if (activation->status != ACTIVATION_NEW &&
            activation->status != ACTIVATION_SUSPENDED) {
        return activation->status;
    }
    if (activation->owner != context) {
        log_error("activation: resumed under a different context");
        return activation->status;
    }
    if (activation_thread.on_base) {
        log_error("activation: resume from work running on the base stack");
        return activation->status;
    }
    ActivationThread* thread = &activation_thread;
    warm_unlink(activation);
    activation_restore(activation);
    activation->pinned = false;
    Activation* self = thread->current;
    ambient_save(self ? &self->ambient : &thread->base_ambient);
    activation->resumer = self;
    if (activation->status == ACTIVATION_SUSPENDED) {
        activation_slots(activation)[ACTIVATION_SLOT_TRANSFER] = input.item;
    }
    activation->status = ACTIVATION_RUNNING;
    thread->current = activation;
    gc_depth_enter(activation);
    ambient_load(&activation->ambient, activation->stacks);
    fiber_switch(self ? &self->sp : &thread->base_sp, activation->sp);

    // Back on the resumer: the activation saved itself and reinstalled us. The
    // value it handed over is taken raw; a wide scalar in it still points into
    // the activation's number segment, and each language moves it to an owner
    // of its own (a Lambda task's result slot, the JS driver's frame).
    activation->result.item = activation_slots(activation)[ACTIVATION_SLOT_TRANSFER];
    if (activation->status == ACTIVATION_SUSPENDED) activation_note_parked(activation);
    if (activation->status == ACTIVATION_DONE) {
        strong_unlink(activation);
        stacks_release(activation->stacks);
        activation->stacks = NULL;
    }
    return activation->status;
}

extern "C" Item activation_suspend(Item value) {
    Activation* activation = activation_thread.current;
    if (!activation) {
        log_error("activation: suspend outside an activation");
        return err2it_or_error(err_create_heap(ERR_INVALID_STATE,
            "suspension requires a running activation", NULL));
    }
    if (activation->barrier_depth > 0 || activation_thread.on_base) {
        log_error("activation: suspend beneath a native host frame");
        return err2it_or_error(err_create_heap(ERR_INVALID_STATE,
            "suspension beneath a native host frame", NULL));
    }
    uint64_t* slots = activation_slots(activation);
    slots[ACTIVATION_SLOT_TRANSFER] = value.item;
    activation_leave(activation, ACTIVATION_SUSPENDED);
    // Resumed: the resumer stored its input in the same slot.
    Item input = {.item = slots[ACTIVATION_SLOT_TRANSFER]};
    slots[ACTIVATION_SLOT_TRANSFER] = ItemNull.item;
    return input;
}

typedef struct ActivationBaseCall {
    ActivationCall fn;
    void* arg;
    void* result;
    bool faulted;
} ActivationBaseCall;

// Runs on the base stack. Its own execution boundary keeps a fault there:
// the recovery chain's other frames live on the activation's stack, and no
// jump may cross a stack (D5.3.6v2).
static void* activation_base_call_thunk(void* data) {
    ActivationBaseCall* call = (ActivationBaseCall*)data;
    LambdaRecoveryFrame* frame = lambda_recovery_frame_begin_for(
        (Context*)context, LAMBDA_RECOVERY_CAP_EXECUTION_BOUNDARY);
    if (!frame) {
        call->faulted = true;
    } else if (LAMBDA_RECOVERY_FRAME_SETJMP(frame)) {
        (void)lambda_recovery_frame_restore_landing(frame);
        lambda_recovery_frame_end(frame);
        log_error("activation: fault in work run on the base stack");
        call->faulted = true;
    } else if (!lambda_recovery_frame_arm(frame)) {
        lambda_recovery_frame_end(frame);
        call->faulted = true;
    } else {
        call->result = call->fn(call->arg);
        lambda_recovery_frame_end(frame);
    }
    return NULL;
}

extern "C" void* activation_call_on_base(ActivationCall fn, void* arg, bool* faulted) {
    ActivationThread* thread = &activation_thread;
    if (faulted) *faulted = false;
    if (!fn) return NULL;
    if (!thread->current || thread->on_base || !thread->base_sp) return fn(arg);
    // The base stack is parked in fiber_switch at base_sp; the work runs below
    // it, past any red zone, under the base stack's own bounds and limit.
    EvalContext* ctx = context;
    LambdaStackBounds saved_bounds = lambda_stack_bounds_get();
    uintptr_t saved_limit = ctx ? ctx->stack_limit : 0;
    lambda_stack_bounds_set(thread->base_ambient.bounds);
    if (ctx) ctx->stack_limit = thread->base_ambient.stack_limit;
    thread->on_base = true;
    ActivationBaseCall call = {fn, arg, NULL, false};
    fiber_call_on((void*)((uintptr_t)thread->base_sp - 256),
        activation_base_call_thunk, &call);
    thread->on_base = false;
    lambda_stack_bounds_set(saved_bounds);
    if (ctx) ctx->stack_limit = saved_limit;
    if (faulted) *faulted = call.faulted;
    return call.result;
}

extern "C" void activation_barrier_enter(void) {
    if (activation_thread.current) activation_thread.current->barrier_depth++;
}

extern "C" void activation_barrier_leave(void) {
    Activation* activation = activation_thread.current;
    if (activation && activation->barrier_depth > 0) activation->barrier_depth--;
}

extern "C" Item activation_value(const Activation* activation) {
    return activation ? activation->result : ItemNull;
}

extern "C" const LambdaFaultRecord* activation_fault(const Activation* activation) {
    return activation && activation->faulted ? &activation->fault : NULL;
}

extern "C" ActivationStatus activation_status(const Activation* activation) {
    return activation ? activation->status : ACTIVATION_DONE;
}

extern "C" void* activation_user(const Activation* activation) {
    return activation ? activation->user : NULL;
}

extern "C" Activation* activation_current(void) {
    return activation_thread.current;
}

extern "C" void activation_destroy(Activation* activation) {
    if (!activation) return;
    if (activation->status == ACTIVATION_RUNNING) {
        log_error("activation: destroy of a running activation ignored");
        return;
    }
    warm_unlink(activation);
    if (activation->stacks) {
        // Abandoned while parked or never started: its frames never run again.
        // The recovery chain is read from the stack, so bring a compacted
        // image back first.
        activation_restore(activation);
        lambda_recovery_frame_discard_chain(activation->ambient.recovery_top);
        strong_unlink(activation);
        stacks_release(activation->stacks);
        activation->stacks = NULL;
    }
    mem_free(activation);
}

static void mark_segment(gc_heap_t* gc, uint64_t* base, uint64_t* top) {
    if (!base || !top || top < base) return;
    for (uint64_t* slot = base; slot < top; slot++) gc_mark_item(gc, *slot);
}

static void mark_parked_roots(gc_heap_t* gc, const Activation* activation) {
    if (activation->image) {
        uint64_t* roots = (uint64_t*)(activation->image + activation->image_native_bytes);
        mark_segment(gc, roots, roots + activation->image_root_words);
        return;
    }
    mark_segment(gc, activation->ambient.side_root_base,
        activation->ambient.side_root_top);
}

extern "C" void activation_trace(const Activation* activation, gc_heap_t* gc) {
    if (!activation || !activation->stacks ||
            activation->status == ACTIVATION_RUNNING) return;
    mark_parked_roots(gc, activation);
}

extern "C" void activation_gc_visit_roots(gc_heap_t* gc) {
    ActivationThread* thread = &activation_thread;
    EvalContext* owner = context;
    // The base stack and every activation between it and the running one are
    // suspended mid-call in a resume; their segments are live roots. The
    // running stack's own segment is the Context's live region, scanned by
    // the caller.
    if (thread->current && thread->base_ambient.side_root_base) {
        mark_segment(gc, thread->base_ambient.side_root_base,
            thread->base_ambient.side_root_top);
    }
    for (Activation* resumer = thread->current ? thread->current->resumer : NULL;
            resumer; resumer = resumer->resumer) {
        mark_segment(gc, resumer->ambient.side_root_base,
            resumer->ambient.side_root_top);
    }
    for (Activation* activation = thread->strong; activation;
            activation = activation->strong_next) {
        if (activation->owner == owner && activation->status != ACTIVATION_RUNNING) {
            mark_parked_roots(gc, activation);
        }
    }
    // Template state lives in a native hashmap, outside traced GC objects.
    heap_gc_visit_template_roots(gc);
}
