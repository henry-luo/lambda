#include "concurrency.h"

#include "lambda.hpp"
#include "lambda-data.hpp"
#include "lambda-error.h"
#include "transpiler.hpp"
#include "runtime/gc/gc_heap.h"
#include "../lib/log.h"
#include "../lib/memtrack.h"
#include "../lib/strbuf.h"
#include "../lib/uv_loop.h"

#include <assert.h>
#include <fcntl.h>
#include <limits.h>
#include <string.h>
#include <uv.h>

extern __thread EvalContext* context;
extern "C" Item lambda_concurrency_fn_call_into(Function* fn, List* args,
    uint64_t* result_home);
extern "C" Function* lambda_concurrency_to_closure(fn_ptr ptr, int arity, void* env);
extern "C" void heap_register_gc_root(uint64_t* slot);
extern "C" void heap_unregister_gc_root(uint64_t* slot);
extern "C" void heap_register_gc_root_range(uint64_t* base, int count);
extern "C" void heap_unregister_gc_root_range(uint64_t* base);
extern "C" StrBuf* lambda_get_local_path_from_item(Item item);

typedef enum LambdaParkKind {
    LAMBDA_PARK_NONE = 0,
    LAMBDA_PARK_RECEIVE,
    LAMBDA_PARK_WAIT,
    LAMBDA_PARK_SELECT,
    LAMBDA_PARK_SLEEP,
    LAMBDA_PARK_FILE_READ,
} LambdaParkKind;

typedef struct LambdaMailbox {
    Item* items;
    int capacity;
    int head;
    int count;
} LambdaMailbox;

typedef struct LambdaWaitGroup LambdaWaitGroup;

typedef struct LambdaWaitLink {
    LambdaTask* target;
    LambdaWaitGroup* group;
    struct LambdaWaitLink* next_target;
    struct LambdaWaitLink* next_group;
} LambdaWaitLink;

typedef struct LambdaTaskTimer {
    uv_timer_t timer;
    LambdaTask* task;
    bool timeout_error;
} LambdaTaskTimer;

typedef struct LambdaFileRead {
    uv_fs_t request;
    LambdaTask* task;
    uv_file file;
    char* path;
    char* bytes;
    size_t capacity;
    ssize_t length;
    int error;
    bool done;
} LambdaFileRead;

typedef struct LambdaTaskObserver {
    LambdaTaskCompletionFn callback;
    LambdaTaskObserverDestroyFn destroy_data;
    void* data;
    struct LambdaTaskObserver* next;
} LambdaTaskObserver;

struct LambdaAsyncFrame {
    LambdaTask* task;
    int state;
    Item* slots;
    uint8_t* slot_is_item;
    int slot_count;
    int slot_capacity;
    LambdaAsyncFrame* next;
    LambdaTaskScope* scope_base;
};

typedef struct LambdaTaskLaunchFrame {
    Item* roots;
    int root_count;
    List args;
} LambdaTaskLaunchFrame;

struct LambdaTaskScope {
    LambdaTask* owner;
    LambdaTask* children;
    LambdaTaskScope* parent;
    bool cancelling;
    bool masked_cleanup;
};

struct LambdaWaitGroup {
    LambdaTask* waiter;
    LambdaWaitLink* links;
    bool select_result;
    bool settled;
};

struct LambdaTask {
    LambdaScheduler* scheduler;
    uint64_t id;
    LambdaTaskState state;
    LambdaParkKind park_kind;
    LambdaTaskResumeFn resume;
    LambdaTaskFrameDestroyFn destroy_frame;
    void* frame;
    Item* frame_roots;
    int frame_root_count;
    Item handle;
    Item result;
    uint64_t result_scalar;
    Item resume_value;
    uint64_t resume_value_scalar;
    bool has_resume_value;
    bool cancel_requested;
    bool cleanup_masked;
    bool started;
    bool queued;
    uint64_t last_send_sequence;
    uint64_t completion_sequence;
    LambdaMailbox mailbox;
    LambdaWaitLink* waiters;
    LambdaWaitGroup* wait_group;
    LambdaTaskTimer* timer;
    LambdaFileRead* file_read;
    LambdaAsyncFrame* async_frames;
    LambdaAsyncFrame* async_cursor;
    LambdaTaskScope* scope_top;
    LambdaTaskScope* owner_scope;
    LambdaTask* next_scope_child;
    LambdaTaskObserver* observers;
    LambdaTask* next_all;
    LambdaTask* next_run;
};

struct LambdaScheduler {
    LambdaTask* all_tasks;
    LambdaTask* run_head;
    LambdaTask* run_tail;
    LambdaTask* current;
    uint64_t next_task_id;
    uint64_t event_sequence;
    int mailbox_capacity;
    int live_count;
    bool draining;
    uv_async_t wake;
    bool wake_initialized;
    bool wake_refed;
    bool wake_closed;
};

static char task_handle_brand;
static LambdaScheduler* attached_scheduler;
static LambdaPromiseIsFn promise_is;
static LambdaPromiseWaitFn promise_wait;
static LambdaHandleToPromiseFn handle_to_promise;
static bool scheduler_has_heap(void);
static LambdaTask* lambda_current_task(void);

extern "C" void lambda_concurrency_set_promise_bridge(
    LambdaPromiseIsFn is_promise, LambdaPromiseWaitFn wait_promise,
    LambdaHandleToPromiseFn to_promise) {
    promise_is = is_promise;
    promise_wait = wait_promise;
    handle_to_promise = to_promise;
}

static Item task_handle_get(void* data, Item key) {
    (void)data;
    (void)key;
    return ItemNull;
}

static void task_handle_set(void* data, Item key, Item value) {
    (void)data;
    (void)key;
    (void)value;
    log_error("concurrency handle: task handles are immutable");
}

static int64_t task_handle_count(void* data) {
    (void)data;
    return 0;
}

static SymbolKeyList* task_handle_keys(void* data) {
    (void)data;
    return NULL;
}

static Item task_handle_at(void* data, int64_t index) {
    (void)data;
    (void)index;
    return ItemNull;
}

static void task_handle_destroy(void* data) {
    (void)data;
}

static void async_frame_reset_from(LambdaAsyncFrame* frame) {
    while (frame) {
        frame->state = 0;
        for (int i = 0; i < frame->slot_count; i++) {
            owned_item_slot_store(frame->slots, frame->slot_capacity, i, ItemNull);
            if (frame->slot_is_item) frame->slot_is_item[i] = 1;
        }
        frame = frame->next;
    }
}

static void async_frames_destroy(LambdaTask* task) {
    LambdaAsyncFrame* frame = task ? task->async_frames : NULL;
    while (frame) {
        LambdaAsyncFrame* next = frame->next;
        if (frame->slots && frame->slot_capacity > 0 && scheduler_has_heap()) {
            heap_unregister_gc_root_range((uint64_t*)frame->slots);
        }
        mem_free(frame->slots);
        mem_free(frame->slot_is_item);
        mem_free(frame);
        frame = next;
    }
    if (task) {
        task->async_frames = NULL;
        task->async_cursor = NULL;
    }
}

static void task_scopes_destroy(LambdaTask* task) {
    LambdaTaskScope* scope = task ? task->scope_top : NULL;
    while (scope) {
        LambdaTaskScope* parent = scope->parent;
        mem_free(scope);
        scope = parent;
    }
    if (task) task->scope_top = NULL;
}

static VMapVtable task_handle_vtable = {
    task_handle_get,
    task_handle_set,
    task_handle_count,
    task_handle_keys,
    task_handle_at,
    task_handle_at,
    task_handle_destroy,
};

static bool scheduler_has_heap(void) {
    return context && context->heap && context->heap->gc;
}

static Item task_error(LambdaErrorCode code, const char* message) {
    LambdaError* error = err_create_heap(code, message, NULL);
    return error ? err2it(error) : ItemError;
}

static bool task_read_milliseconds(Item item, int64_t* out) {
    TypeId type_id = get_type_id(item);
    // Inline int56 values store payload bits in the Item itself; only INT64
    // carries a pointer that is valid for get_int64().
    if (type_id == LMD_TYPE_INT) {
        *out = item.get_int56();
        return true;
    }
    if (type_id == LMD_TYPE_INT64) {
        *out = item.get_int64();
        return true;
    }
    return false;
}

static void scheduler_enqueue(LambdaTask* task) {
    if (!task || task->state == LAMBDA_TASK_DONE || task->queued) return;
    task->state = LAMBDA_TASK_RUNNABLE;
    task->queued = true;
    task->next_run = NULL;
    if (task->scheduler->run_tail) task->scheduler->run_tail->next_run = task;
    else task->scheduler->run_head = task;
    task->scheduler->run_tail = task;
    if (task->scheduler->wake_initialized) {
        if (!task->scheduler->wake_refed) {
            uv_ref((uv_handle_t*)&task->scheduler->wake);
            task->scheduler->wake_refed = true;
        }
        uv_async_send(&task->scheduler->wake);
    }
}

static void scheduler_release_wake_if_idle(LambdaScheduler* scheduler) {
    if (scheduler && scheduler->wake_initialized && scheduler->wake_refed &&
            !scheduler->run_head) {
        uv_unref((uv_handle_t*)&scheduler->wake);
        scheduler->wake_refed = false;
    }
}

static LambdaTask* scheduler_dequeue(LambdaScheduler* scheduler) {
    LambdaTask* task = scheduler ? scheduler->run_head : NULL;
    if (!task) return NULL;
    scheduler->run_head = task->next_run;
    if (!scheduler->run_head) scheduler->run_tail = NULL;
    task->next_run = NULL;
    task->queued = false;
    return task;
}

static void wait_group_unlink(LambdaWaitGroup* group) {
    if (!group) return;
    for (LambdaWaitLink* link = group->links; link; link = link->next_group) {
        LambdaWaitLink** slot = &link->target->waiters;
        while (*slot && *slot != link) slot = &(*slot)->next_target;
        if (*slot == link) *slot = link->next_target;
    }
}

static void wait_group_free(LambdaWaitGroup* group) {
    if (!group) return;
    LambdaWaitLink* link = group->links;
    while (link) {
        LambdaWaitLink* next = link->next_group;
        mem_free(link);
        link = next;
    }
    mem_free(group);
}

static void task_timer_close(LambdaTask* task) {
    if (!task || !task->timer) return;
    LambdaTaskTimer* timer = task->timer;
    task->timer = NULL;
    timer->task = NULL;
    uv_timer_stop(&timer->timer);
    if (!uv_is_closing((uv_handle_t*)&timer->timer)) {
        uv_close((uv_handle_t*)&timer->timer, [](uv_handle_t* handle) {
            mem_free((LambdaTaskTimer*)handle->data);
        });
    }
}

static void task_resume_with(LambdaTask* task, Item value) {
    if (!task || task->state == LAMBDA_TASK_DONE) return;
    if (task->wait_group) {
        LambdaWaitGroup* group = task->wait_group;
        task->wait_group = NULL;
        wait_group_unlink(group);
        wait_group_free(group);
    }
    task_timer_close(task);
    owned_item_slot_store(&task->resume_value, 1, 0, value);
    task->has_resume_value = true;
    task->park_kind = LAMBDA_PARK_NONE;
    scheduler_enqueue(task);
}

static void file_read_release(LambdaFileRead* read) {
    if (!read) return;
    mem_free(read->bytes);
    mem_free(read->path);
    mem_free(read);
}

static void file_read_mark_done(LambdaFileRead* read, int error) {
    if (!read || read->done) return;
    read->error = error;
    read->done = true;
    if (read->task && read->task->state != LAMBDA_TASK_DONE) {
        lambda_task_resume(read->task);
    }
}

static void file_read_close(LambdaFileRead* read);

static void file_read_read_cb(uv_fs_t* request) {
    LambdaFileRead* read = request ? (LambdaFileRead*)request->data : NULL;
    if (!read) return;
    ssize_t result = request->result;
    uv_fs_req_cleanup(request);
    if (result < 0) {
        read->error = (int)result;
    } else {
        read->length = result;
        read->bytes[result] = '\0';
    }
    file_read_close(read);
}

static void file_read_close_cb(uv_fs_t* request) {
    LambdaFileRead* read = request ? (LambdaFileRead*)request->data : NULL;
    if (!read) return;
    int close_error = request->result < 0 ? (int)request->result : 0;
    uv_fs_req_cleanup(request);
    read->file = -1;
    file_read_mark_done(read, read->error ? read->error : close_error);
}

static void file_read_close(LambdaFileRead* read) {
    if (!read) return;
    if (read->file < 0) {
        file_read_mark_done(read, read->error);
        return;
    }
    read->request.data = read;
    int status = uv_fs_close(lambda_uv_loop(), &read->request,
        read->file, file_read_close_cb);
    if (status < 0) {
        uv_fs_req_cleanup(&read->request);
        read->file = -1;
        file_read_mark_done(read, read->error ? read->error : status);
    }
}

static void file_read_stat_cb(uv_fs_t* request) {
    LambdaFileRead* read = request ? (LambdaFileRead*)request->data : NULL;
    if (!read) return;
    int64_t file_size = request->result < 0 ? -1 : request->statbuf.st_size;
    int stat_error = request->result < 0 ? (int)request->result : 0;
    uv_fs_req_cleanup(request);
    if (stat_error || file_size < 0 || (uint64_t)file_size >= (uint64_t)UINT_MAX) {
        read->error = stat_error ? stat_error : UV_EFBIG;
        file_read_close(read);
        return;
    }
    read->capacity = (size_t)file_size + 1;
    read->bytes = (char*)mem_alloc(read->capacity, MEM_CAT_EVAL);
    if (!read->bytes) {
        read->error = UV_ENOMEM;
        file_read_close(read);
        return;
    }
    if (file_size == 0) {
        read->bytes[0] = '\0';
        read->length = 0;
        file_read_close(read);
        return;
    }
    uv_buf_t buffer = uv_buf_init(read->bytes, (unsigned int)file_size);
    read->request.data = read;
    int status = uv_fs_read(lambda_uv_loop(), &read->request, read->file,
        &buffer, 1, 0, file_read_read_cb);
    if (status < 0) {
        uv_fs_req_cleanup(&read->request);
        read->error = status;
        file_read_close(read);
    }
}

static void file_read_open_cb(uv_fs_t* request) {
    LambdaFileRead* read = request ? (LambdaFileRead*)request->data : NULL;
    if (!read) return;
    int open_result = (int)request->result;
    uv_fs_req_cleanup(request);
    if (open_result < 0) {
        file_read_mark_done(read, open_result);
        return;
    }
    read->file = open_result;
    read->request.data = read;
    int status = uv_fs_fstat(lambda_uv_loop(), &read->request,
        read->file, file_read_stat_cb);
    if (status < 0) {
        uv_fs_req_cleanup(&read->request);
        read->error = status;
        file_read_close(read);
    }
}

static void wake_waiters(LambdaTask* target) {
    while (target->waiters) {
        LambdaWaitGroup* group = target->waiters->group;
        if (group && !group->settled) {
            group->settled = true;
            Item value = group->select_result ? target->handle : target->result;
            task_resume_with(group->waiter, value);
        } else {
            // wait-group teardown removes every target link, so never retain a link
            // across a resume that can free the group containing it.
            LambdaWaitLink* stale = target->waiters;
            target->waiters = stale->next_target;
            mem_free(stale);
        }
    }
}

static bool task_timer_start(LambdaTask* task, uint64_t timeout_ms, bool timeout_error) {
    if (!task || timeout_ms == 0) return false;
    if (!lambda_uv_loop() && lambda_uv_init() != 0) return false;
    LambdaTaskTimer* timer = (LambdaTaskTimer*)mem_calloc(
        1, sizeof(LambdaTaskTimer), MEM_CAT_EVAL);
    if (!timer) return false;
    timer->task = task;
    timer->timeout_error = timeout_error;
    if (uv_timer_init(lambda_uv_loop(), &timer->timer) != 0) {
        mem_free(timer);
        return false;
    }
    // libuv initializes the handle storage, so attach native ownership afterward.
    timer->timer.data = timer;
    task->timer = timer;
    uv_timer_start(&timer->timer, [](uv_timer_t* handle) {
        LambdaTaskTimer* timer = (LambdaTaskTimer*)handle->data;
        LambdaTask* task = timer ? timer->task : NULL;
        if (!task || task->state != LAMBDA_TASK_PARKED) return;
        Item value = timer->timeout_error
            ? task_error(ERR_TIMEOUT, "task wait timed out") : ItemNull;
        task_resume_with(task, value);
    }, timeout_ms, 0);
    return true;
}

static bool task_wait_targets(LambdaTask* waiter, LambdaTask** targets, int count,
                              bool select_result, uint64_t timeout_ms) {
    if (!waiter || !targets || count <= 0) return false;
    LambdaWaitGroup* group = (LambdaWaitGroup*)mem_calloc(
        1, sizeof(LambdaWaitGroup), MEM_CAT_EVAL);
    if (!group) return false;
    group->waiter = waiter;
    group->select_result = select_result;
    for (int i = 0; i < count; i++) {
        LambdaWaitLink* link = (LambdaWaitLink*)mem_calloc(
            1, sizeof(LambdaWaitLink), MEM_CAT_EVAL);
        if (!link) {
            wait_group_unlink(group);
            wait_group_free(group);
            return false;
        }
        link->target = targets[i];
        link->group = group;
        link->next_group = group->links;
        group->links = link;
        link->next_target = targets[i]->waiters;
        targets[i]->waiters = link;
    }
    waiter->wait_group = group;
    lambda_task_park(waiter);
    if (timeout_ms > 0 && !task_timer_start(waiter, timeout_ms, true)) {
        waiter->wait_group = NULL;
        wait_group_unlink(group);
        wait_group_free(group);
        // The timer failure is returned synchronously from the current poll;
        // requeueing that same task would execute it twice.
        waiter->state = LAMBDA_TASK_RUNNABLE;
        waiter->park_kind = LAMBDA_PARK_NONE;
        return false;
    }
    return true;
}

static void scheduler_uv_drain(void) {
    if (attached_scheduler) lambda_scheduler_run_ready(attached_scheduler);
}

extern "C" LambdaScheduler* lambda_scheduler_create(int mailbox_capacity) {
    // The scheduler owns pure-Lambda loop readiness from its first task. If
    // initialization waits for a child timer, an initial drain can observe a
    // null loop and stop before that child ever gets its first poll.
    if (!lambda_uv_loop() && lambda_uv_init() != 0) {
        log_error("concurrency scheduler: failed to initialize uv loop");
        return NULL;
    }
    LambdaScheduler* scheduler = (LambdaScheduler*)mem_calloc(
        1, sizeof(LambdaScheduler), MEM_CAT_EVAL);
    if (!scheduler) return NULL;
    scheduler->mailbox_capacity = mailbox_capacity > 0
        ? mailbox_capacity : LAMBDA_MAILBOX_DEFAULT_CAPACITY;
    scheduler->next_task_id = 1;
    if (uv_async_init(lambda_uv_loop(), &scheduler->wake, [](uv_async_t* handle) {
            LambdaScheduler* ready = handle ? (LambdaScheduler*)handle->data : NULL;
            if (ready) lambda_scheduler_run_ready(ready);
        }) != 0) {
        mem_free(scheduler);
        log_error("concurrency scheduler: failed to initialize wake handle");
        return NULL;
    }
    scheduler->wake.data = scheduler;
    scheduler->wake_initialized = true;
    uv_unref((uv_handle_t*)&scheduler->wake);
    attached_scheduler = scheduler;
    lambda_uv_set_task_drain(scheduler_uv_drain);
    log_debug("concurrency scheduler: created mailbox_capacity=%d",
        scheduler->mailbox_capacity);
    return scheduler;
}

extern "C" void lambda_scheduler_destroy(LambdaScheduler* scheduler) {
    if (!scheduler) return;
    if (attached_scheduler == scheduler) {
        attached_scheduler = NULL;
        lambda_uv_set_task_drain(NULL);
    }
    // Unlink wait groups while every target record is still alive. Freeing a
    // target first would leave another task's wait-group link pointing at it.
    for (LambdaTask* task = scheduler->all_tasks; task; task = task->next_all) {
        if (task->wait_group) {
            wait_group_unlink(task->wait_group);
            wait_group_free(task->wait_group);
            task->wait_group = NULL;
        }
        task_timer_close(task);
        if (task->file_read && !task->file_read->done) {
            uv_cancel((uv_req_t*)&task->file_read->request);
        }
    }
    // File-system requests retain their task pointer through the libuv
    // callback. Finish cancellation before releasing task records.
    bool pending_file_read = true;
    while (pending_file_read) {
        pending_file_read = false;
        for (LambdaTask* pending = scheduler->all_tasks; pending; pending = pending->next_all) {
            if (pending->file_read && !pending->file_read->done) {
                pending_file_read = true;
                break;
            }
        }
        if (pending_file_read) uv_run(lambda_uv_loop(), UV_RUN_ONCE);
    }
    LambdaTask* task = scheduler->all_tasks;
    while (task) {
        LambdaTask* next = task->next_all;
        if (task->frame_roots && task->frame_root_count > 0 && scheduler_has_heap()) {
            heap_unregister_gc_root_range((uint64_t*)task->frame_roots);
        }
        if (scheduler_has_heap()) {
            heap_unregister_gc_root(&task->handle.item);
            heap_unregister_gc_root(&task->result.item);
            heap_unregister_gc_root(&task->resume_value.item);
            heap_unregister_gc_root_range((uint64_t*)task->mailbox.items);
        }
        async_frames_destroy(task);
        task_scopes_destroy(task);
        LambdaTaskObserver* observer = task->observers;
        while (observer) {
            LambdaTaskObserver* next_observer = observer->next;
            if (observer->destroy_data) observer->destroy_data(observer->data);
            mem_free(observer);
            observer = next_observer;
        }
        if (task->destroy_frame && task->frame) task->destroy_frame(task->frame);
        file_read_release(task->file_read);
        mem_free(task->mailbox.items);
        mem_free(task);
        task = next;
    }
    if (scheduler->wake_initialized &&
            !uv_is_closing((uv_handle_t*)&scheduler->wake)) {
        uv_close((uv_handle_t*)&scheduler->wake, [](uv_handle_t* handle) {
            LambdaScheduler* owner = handle ? (LambdaScheduler*)handle->data : NULL;
            if (owner) owner->wake_closed = true;
        });
        while (!scheduler->wake_closed) uv_run(lambda_uv_loop(), UV_RUN_NOWAIT);
    }
    mem_free(scheduler);
    log_debug("concurrency scheduler: destroyed");
}

extern "C" LambdaTask* lambda_task_create(LambdaScheduler* scheduler,
    LambdaTaskResumeFn resume, void* frame, LambdaTaskFrameDestroyFn destroy_frame) {
    if (!scheduler || !scheduler_has_heap()) return NULL;
    LambdaTask* task = (LambdaTask*)mem_calloc(1, sizeof(LambdaTask), MEM_CAT_EVAL);
    if (!task) return NULL;
    task->mailbox.items = (Item*)mem_calloc((size_t)scheduler->mailbox_capacity * 2,
        sizeof(Item), MEM_CAT_EVAL);
    if (!task->mailbox.items) {
        mem_free(task);
        return NULL;
    }
    VMap* handle = (VMap*)heap_calloc(sizeof(VMap), LMD_TYPE_VMAP);
    if (!handle) {
        mem_free(task->mailbox.items);
        mem_free(task);
        return NULL;
    }
    handle->type_id = LMD_TYPE_VMAP;
    handle->vtable = &task_handle_vtable;
    handle->host_type = &task_handle_brand;
    handle->host_data = task;

    task->scheduler = scheduler;
    task->id = scheduler->next_task_id++;
    task->state = LAMBDA_TASK_RUNNABLE;
    task->resume = resume;
    task->frame = frame;
    task->destroy_frame = destroy_frame;
    task->handle = (Item){.vmap = handle};
    task->result = ItemNull;
    task->resume_value = ItemNull;
    task->mailbox.capacity = scheduler->mailbox_capacity;
    task->next_all = scheduler->all_tasks;
    scheduler->all_tasks = task;
    scheduler->live_count++;

    heap_register_gc_root(&task->handle.item);
    heap_register_gc_root(&task->result.item);
    heap_register_gc_root(&task->resume_value.item);
    heap_register_gc_root_range((uint64_t*)task->mailbox.items, task->mailbox.capacity);
    scheduler_enqueue(task);
    log_debug("concurrency task: created id=%llu", (unsigned long long)task->id);
    return task;
}

extern "C" void lambda_task_set_frame_roots(LambdaTask* task, Item* roots, int count) {
    if (!task || !scheduler_has_heap()) return;
    if (task->frame_roots && task->frame_root_count > 0) {
        heap_unregister_gc_root_range((uint64_t*)task->frame_roots);
    }
    task->frame_roots = roots;
    task->frame_root_count = count > 0 ? count : 0;
    if (task->frame_roots && task->frame_root_count > 0) {
        heap_register_gc_root_range((uint64_t*)task->frame_roots, task->frame_root_count);
    }
}

extern "C" int lambda_scheduler_run_one(LambdaScheduler* scheduler) {
    LambdaTask* task = scheduler_dequeue(scheduler);
    if (!task) return 0;
    scheduler->current = task;
    task->async_cursor = task->async_frames;
    if (task->cancel_requested && !task->cleanup_masked && !task->started) {
        // A task cancelled before its first poll has no lexical scopes yet.
        // Once started, cancellation must re-enter its state machine so `^`
        // propagation executes structured cancel-then-join cleanup.
        lambda_task_complete(task, task_error(ERR_CANCELLED, "task cancelled"));
    } else if (!task->resume) {
        lambda_task_complete(task, ItemNull);
    } else {
        task->started = true;
        Item result = ItemNull;
        LambdaTaskPoll poll = task->resume(task, task->frame, &result);
        if (task->state != LAMBDA_TASK_DONE) {
            if (poll == LAMBDA_TASK_POLL_DONE) lambda_task_complete(task, result);
            else if (poll == LAMBDA_TASK_POLL_READY) scheduler_enqueue(task);
            else task->state = LAMBDA_TASK_PARKED;
        }
    }
    scheduler->current = NULL;
    return 1;
}

extern "C" int lambda_scheduler_run_ready(LambdaScheduler* scheduler) {
    if (!scheduler || scheduler->draining) return 0;
    scheduler->draining = true;
    int ran = 0;
    LambdaTask* boundary = scheduler->run_tail;
    while (scheduler->run_head) {
        LambdaTask* task = scheduler->run_head;
        ran += lambda_scheduler_run_one(scheduler);
        if (task == boundary) break;
    }
    scheduler->draining = false;
    scheduler_release_wake_if_idle(scheduler);
    return ran;
}

typedef struct LambdaDrainWatchdogState {
    bool fired;
    bool grace_turn;
} LambdaDrainWatchdogState;

static void scheduler_drain_watchdog_cb(uv_timer_t* timer) {
    LambdaDrainWatchdogState* state = timer
        ? (LambdaDrainWatchdogState*)timer->data : NULL;
    if (!state) return;
    if (!state->grace_turn) {
        // After process descheduling, an earlier awaited timer and this
        // watchdog can be overdue in the same timer phase. Give the loop one
        // full checkpoint turn so that Promise can resume its Lambda task.
        state->grace_turn = true;
        uv_timer_start(timer, scheduler_drain_watchdog_cb, 0, 0);
        return;
    }
    state->fired = true;
}

extern "C" int lambda_scheduler_drain(LambdaScheduler* scheduler) {
    if (!scheduler) return 0;
    uv_loop_t* loop = lambda_uv_loop();
    LambdaDrainWatchdogState watchdog_state = {false, false};
    uv_timer_t watchdog;
    bool watchdog_initialized = false;
    bool watchdog_started = false;
    int ran = 0;
    while (scheduler->live_count > 0 && !watchdog_state.fired) {
        int step = lambda_scheduler_run_ready(scheduler);
        ran += step;
        if (scheduler->live_count == 0) break;
        // run_ready intentionally stops at its initial FIFO boundary. A task
        // may enqueue a child behind that boundary; poll only after the next
        // macrotask batch has had a chance to start its I/O/timer wait.
        if (scheduler->run_head) continue;
        if (!loop) break;
        if (!watchdog_initialized && uv_timer_init(loop, &watchdog) == 0) {
            watchdog.data = &watchdog_state;
            watchdog_initialized = true;
        }
        if (watchdog_initialized && (!watchdog_started || step > 0)) {
            // The watchdog measures an idle wait, not task execution. Lazy JS
            // compilation can run inside a task poll and exceed wall time under
            // parallel load before its awaited timer has even been armed.
            watchdog_state.fired = false;
            watchdog_state.grace_turn = false;
            if (uv_timer_start(&watchdog,
                    scheduler_drain_watchdog_cb, 5000, 0) == 0) {
                watchdog_started = true;
            }
        }
        uv_run(loop, UV_RUN_ONCE);
        if (step == 0 && !uv_loop_alive(loop) && !scheduler->run_head) break;
    }
    if (watchdog_initialized) {
        if (watchdog_started) uv_timer_stop(&watchdog);
        if (!uv_is_closing((uv_handle_t*)&watchdog)) {
            uv_close((uv_handle_t*)&watchdog, NULL);
        }
        uv_run(loop, UV_RUN_NOWAIT);
    }
    if (scheduler->live_count > 0) {
        log_error("concurrency scheduler: drain stopped with %d live task(s)%s",
            scheduler->live_count, watchdog_state.fired ? " after watchdog timeout" : " without runnable work");
        return -1;
    }
    return ran;
}

extern "C" int lambda_scheduler_live_count(const LambdaScheduler* scheduler) {
    return scheduler ? scheduler->live_count : 0;
}

extern "C" LambdaTask* lambda_scheduler_current(LambdaScheduler* scheduler) {
    return scheduler ? scheduler->current : NULL;
}

extern "C" Item lambda_task_handle(LambdaTask* task) {
    return task ? task->handle : ItemNull;
}

extern "C" bool lambda_task_handle_is(Item item) {
    return get_type_id(item) == LMD_TYPE_VMAP && item.vmap &&
        item.vmap->host_type == &task_handle_brand && item.vmap->host_data;
}

extern "C" LambdaTask* lambda_task_from_handle(Item item) {
    return lambda_task_handle_is(item) ? (LambdaTask*)item.vmap->host_data : NULL;
}

extern "C" LambdaTaskState lambda_task_state(const LambdaTask* task) {
    return task ? task->state : LAMBDA_TASK_DONE;
}

extern "C" Item lambda_task_result(const LambdaTask* task) {
    return task ? task->result : ItemNull;
}

extern "C" bool lambda_task_cancel_requested(const LambdaTask* task) {
    return task && task->cancel_requested;
}

extern "C" void lambda_task_set_cleanup_masked(LambdaTask* task, bool masked) {
    if (!task) return;
    bool was_masked = task->cleanup_masked;
    task->cleanup_masked = masked;
    if (was_masked && !masked && task->cancel_requested &&
            task->state == LAMBDA_TASK_PARKED) {
        // Cancellation deferred by cleanup masking must become observable at
        // the first park point after cleanup finishes.
        task_resume_with(task, task_error(ERR_CANCELLED, "task cancelled"));
    }
}

extern "C" LambdaSendStatus lambda_task_send(
    LambdaTask* sender, LambdaTask* target, Item message) {
    if (!target || !target->scheduler) return LAMBDA_SEND_INVALID_HANDLE;
    if (target->state == LAMBDA_TASK_DONE) return LAMBDA_SEND_CLOSED;
    LambdaMailbox* mailbox = &target->mailbox;
    if (mailbox->count >= mailbox->capacity) return LAMBDA_SEND_FULL;
    int tail = (mailbox->head + mailbox->count) % mailbox->capacity;
    // Mailboxes persist across sender activations; the receiving task must not
    // borrow a boxed numeric payload from the sender's number extent.
    owned_item_slot_store(mailbox->items, mailbox->capacity, tail, message);
    mailbox->count++;
    LambdaScheduler* scheduler = target->scheduler;
    uint64_t sequence = ++scheduler->event_sequence;
    if (sender) sender->last_send_sequence = sequence;
    if (target->state == LAMBDA_TASK_PARKED && target->park_kind == LAMBDA_PARK_RECEIVE) {
        Item received = ItemNull;
        lambda_task_mailbox_receive(target, &received);
        task_resume_with(target, received);
    }
    return LAMBDA_SEND_OK;
}

extern "C" bool lambda_task_mailbox_receive(LambdaTask* task, Item* out) {
    if (!task || task->mailbox.count <= 0) return false;
    LambdaMailbox* mailbox = &task->mailbox;
    if (out) *out = mailbox->items[mailbox->head];
    mailbox->items[mailbox->head] = ItemNull;
    mailbox->head = (mailbox->head + 1) % mailbox->capacity;
    mailbox->count--;
    return true;
}

extern "C" int lambda_task_mailbox_count(const LambdaTask* task) {
    return task ? task->mailbox.count : 0;
}

extern "C" int lambda_task_mailbox_capacity(const LambdaTask* task) {
    return task ? task->mailbox.capacity : 0;
}

extern "C" uint64_t lambda_task_last_send_sequence(const LambdaTask* task) {
    return task ? task->last_send_sequence : 0;
}

extern "C" uint64_t lambda_task_completion_sequence(const LambdaTask* task) {
    return task ? task->completion_sequence : 0;
}

extern "C" void lambda_task_park(LambdaTask* task) {
    if (!task || task->state == LAMBDA_TASK_DONE) return;
    task->state = LAMBDA_TASK_PARKED;
}

extern "C" void lambda_task_resume(LambdaTask* task) {
    if (!task || task->state == LAMBDA_TASK_DONE) return;
    task_timer_close(task);
    task->park_kind = LAMBDA_PARK_NONE;
    scheduler_enqueue(task);
}

extern "C" void lambda_task_resume_external(LambdaTask* task, Item result) {
    task_resume_with(task, result);
}

extern "C" void lambda_task_complete(LambdaTask* task, Item result) {
    if (!task || task->state == LAMBDA_TASK_DONE) return;
    task_timer_close(task);
    // Completion is published after the task frame can unwind, so retain any
    // wide scalar in the task-owned companion word before waking observers.
    owned_item_slot_store(&task->result, 1, 0, result);
    task->completion_sequence = ++task->scheduler->event_sequence;
    // K20e is a sequencing invariant: completion is published only after the
    // sender's final successful enqueue has acquired an earlier event number.
    assert(task->completion_sequence > task->last_send_sequence);
    task->state = LAMBDA_TASK_DONE;
    task->park_kind = LAMBDA_PARK_NONE;
    task->scheduler->live_count--;
    wake_waiters(task);
    LambdaTaskObserver* observer = task->observers;
    task->observers = NULL;
    while (observer) {
        LambdaTaskObserver* next = observer->next;
        if (observer->callback) observer->callback(task, task->result, observer->data);
        if (observer->destroy_data) observer->destroy_data(observer->data);
        mem_free(observer);
        observer = next;
    }
    log_debug("concurrency task: completed id=%llu sequence=%llu",
        (unsigned long long)task->id,
        (unsigned long long)task->completion_sequence);
}

extern "C" bool lambda_task_on_complete(LambdaTask* task,
    LambdaTaskCompletionFn callback, void* data,
    LambdaTaskObserverDestroyFn destroy_data) {
    if (!task || !callback) return false;
    if (task->state == LAMBDA_TASK_DONE) {
        callback(task, task->result, data);
        if (destroy_data) destroy_data(data);
        return true;
    }
    LambdaTaskObserver* observer = (LambdaTaskObserver*)mem_calloc(
        1, sizeof(LambdaTaskObserver), MEM_CAT_EVAL);
    if (!observer) return false;
    observer->callback = callback;
    observer->destroy_data = destroy_data;
    observer->data = data;
    observer->next = task->observers;
    task->observers = observer;
    return true;
}

extern "C" bool lambda_task_cancel(LambdaTask* task) {
    if (!task || task->state == LAMBDA_TASK_DONE) return true;
    task->cancel_requested = true;
    if (task->state == LAMBDA_TASK_PARKED && !task->cleanup_masked) {
        task_resume_with(task, task_error(ERR_CANCELLED, "task cancelled"));
    }
    return true;
}

extern "C" bool lambda_task_take_resume_value(LambdaTask* task, Item* out) {
    if (!task || !task->has_resume_value) return false;
    if (out) *out = task->resume_value;
    task->resume_value = ItemNull;
    task->has_resume_value = false;
    return true;
}

static bool async_frame_reserve(LambdaAsyncFrame* frame, int slot_capacity) {
    if (!frame || slot_capacity <= frame->slot_capacity) return frame != NULL;
    Item* resized = (Item*)mem_calloc((size_t)slot_capacity * 2,
        sizeof(Item), MEM_CAT_EVAL);
    uint8_t* resized_kinds = (uint8_t*)mem_calloc((size_t)slot_capacity,
        sizeof(uint8_t), MEM_CAT_EVAL);
    if (!resized || !resized_kinds) {
        mem_free(resized);
        mem_free(resized_kinds);
        return false;
    }
    for (int i = 0; i < frame->slot_count; i++) {
        if (frame->slot_is_item && frame->slot_is_item[i]) {
            owned_item_slot_store(resized, slot_capacity, i, frame->slots[i]);
            resized_kinds[i] = 1;
        } else {
            // Raw MIR spills occupy the unscanned tail. Preserve them without
            // interpreting arbitrary bits as Items during frame growth.
            resized[i] = ItemNull;
            resized[slot_capacity + i].item =
                frame->slots[frame->slot_capacity + i].item;
        }
    }
    if (frame->slots && scheduler_has_heap()) {
        heap_unregister_gc_root_range((uint64_t*)frame->slots);
    }
    mem_free(frame->slots);
    mem_free(frame->slot_is_item);
    frame->slots = resized;
    frame->slot_is_item = resized_kinds;
    frame->slot_capacity = slot_capacity;
    if (scheduler_has_heap()) {
        heap_register_gc_root_range((uint64_t*)frame->slots, frame->slot_capacity);
    }
    return true;
}

extern "C" LambdaAsyncFrame* lambda_async_frame_enter_current(int slot_capacity) {
    LambdaTask* task = context && context->scheduler
        ? lambda_scheduler_current(context->scheduler) : NULL;
    if (!task) return NULL;
    LambdaAsyncFrame* frame = task->async_cursor;
    if (!frame) {
        frame = (LambdaAsyncFrame*)mem_calloc(1, sizeof(LambdaAsyncFrame), MEM_CAT_EVAL);
        if (!frame) return NULL;
        frame->task = task;
        frame->scope_base = task->scope_top;
        if (!task->async_frames) {
            task->async_frames = frame;
        } else {
            LambdaAsyncFrame* tail = task->async_frames;
            while (tail->next) tail = tail->next;
            tail->next = frame;
        }
    }
    if (slot_capacity > 0 && !async_frame_reserve(frame, slot_capacity)) return NULL;
    task->async_cursor = frame->next;
    return frame;
}

extern "C" int lambda_async_frame_state(LambdaAsyncFrame* frame) {
    return frame ? frame->state : 0;
}

extern "C" void lambda_async_frame_set_state(LambdaAsyncFrame* frame, int state) {
    if (frame) frame->state = state;
}

extern "C" Item lambda_async_frame_get(LambdaAsyncFrame* frame, int slot) {
    if (!frame || slot < 0 || slot >= frame->slot_count) return ItemNull;
    return owned_item_slot_read(frame->slots, frame->slot_capacity, slot, false);
}

extern "C" void lambda_async_frame_set(LambdaAsyncFrame* frame, int slot, Item value) {
    if (!frame || slot < 0) return;
    if (slot >= frame->slot_capacity) {
        int next_capacity = frame->slot_capacity > 0 ? frame->slot_capacity : 8;
        while (next_capacity <= slot) next_capacity *= 2;
        // This fallback covers hand-written host use; generated functions reserve
        // their exact compile-time slot count in the prologue.
        if (!async_frame_reserve(frame, next_capacity)) return;
    }
    owned_item_slot_store(frame->slots, frame->slot_capacity, slot, value);
    frame->slot_is_item[slot] = 1;
    if (slot >= frame->slot_count) frame->slot_count = slot + 1;
}

extern "C" uint64_t lambda_async_frame_get_raw(LambdaAsyncFrame* frame, int slot) {
    if (!frame || slot < 0 || slot >= frame->slot_count) return 0;
    return frame->slots[frame->slot_capacity + slot].item;
}

extern "C" void lambda_async_frame_set_raw(LambdaAsyncFrame* frame, int slot,
                                             uint64_t value) {
    if (!frame || slot < 0) return;
    if (slot >= frame->slot_capacity) {
        int next_capacity = frame->slot_capacity > 0 ? frame->slot_capacity : 8;
        while (next_capacity <= slot) next_capacity *= 2;
        if (!async_frame_reserve(frame, next_capacity)) return;
    }
    // MIR I64 temporaries are arbitrary bit patterns. Keep them in the raw
    // tail so the precise root range never interprets payload bits as Items.
    frame->slots[slot] = ItemNull;
    frame->slots[frame->slot_capacity + slot].item = value;
    frame->slot_is_item[slot] = 0;
    if (slot >= frame->slot_count) frame->slot_count = slot + 1;
}

static bool async_word_is_number_home(uint64_t value) {
    if (!context || !context->side_number_base || !context->side_number_top ||
            (value & ITEM_DBL_MASK)) return false;
    uint8_t tag = (uint8_t)(value >> 56);
    uintptr_t payload = value & ~ITEM_HIGH_BYTE_MASK;
    if (tag == LMD_TYPE_FLOAT || tag == LMD_TYPE_FLOAT64) {
        if (payload <= 1) return false;
    } else if (tag != LMD_TYPE_INT64 && tag != LMD_TYPE_UINT64) {
        return false;
    }
    uintptr_t base = (uintptr_t)context->side_number_base;
    uintptr_t top = (uintptr_t)context->side_number_top;
    return payload >= base && payload < top &&
        (payload - base) % sizeof(uint64_t) == 0;
}

static void* async_word_gc_pointer(uint64_t value) {
    if (!value || (value & ITEM_DBL_MASK)) return NULL;
    uint8_t tag = (uint8_t)(value >> 56);
    if (tag == 0) return (void*)(uintptr_t)value;
    if ((tag >= LMD_TYPE_INT64 && tag <= LMD_TYPE_BINARY) ||
            tag == LMD_TYPE_ERROR) {
        return (void*)(uintptr_t)(value & ~ITEM_HIGH_BYTE_MASK);
    }
    return NULL;
}

extern "C" void lambda_async_frame_set_word(LambdaAsyncFrame* frame, int slot,
                                                uint64_t value) {
    // Generated prologues reserve the complete spill layout before any value is
    // saved. Keeping this setter allocation-free lets managed Item words remain
    // borrowed safely across the store and preserves its audited NO_GC contract.
    if (!frame || slot < 0 || slot >= frame->slot_capacity) return;
    void* gc_ptr = async_word_gc_pointer(value);
    bool managed_item = gc_ptr && context && context->heap && context->heap->gc &&
        gc_is_managed(context->heap->gc, gc_ptr);
    if (async_word_is_number_home(value) || managed_item) {
        // Suspension ends the native number/root frame. Rehome scalar Items and
        // retain managed Items; arbitrary words stay outside the scanned half.
        owned_item_slot_store(frame->slots, frame->slot_capacity, slot,
            (Item){.item = value});
        frame->slot_is_item[slot] = 1;
    } else {
        frame->slots[slot] = ItemNull;
        frame->slots[frame->slot_capacity + slot].item = value;
        frame->slot_is_item[slot] = 0;
    }
    if (slot >= frame->slot_count) frame->slot_count = slot + 1;
}

extern "C" uint64_t lambda_async_frame_get_word(LambdaAsyncFrame* frame, int slot) {
    if (!frame || slot < 0 || slot >= frame->slot_count) return 0;
    return frame->slot_is_item && frame->slot_is_item[slot]
        ? frame->slots[slot].item
        : frame->slots[frame->slot_capacity + slot].item;
}

extern "C" void lambda_async_frame_complete(LambdaAsyncFrame* frame) {
    if (!frame) return;
    // A completed call invalidates its own saved state and every deeper call
    // frame, so a later call at the same depth cannot resume stale code.
    async_frame_reset_from(frame);
}

extern "C" LambdaTaskScope* lambda_async_frame_scope_base(LambdaAsyncFrame* frame) {
    return frame ? frame->scope_base : NULL;
}

extern "C" int lambda_task_has_current(void) {
    return context && context->scheduler &&
        lambda_scheduler_current(context->scheduler) ? 1 : 0;
}

static LambdaTaskPoll task_launch_resume(LambdaTask* task, void* data, Item* out) {
    LambdaTaskLaunchFrame* frame = (LambdaTaskLaunchFrame*)data;
    Function* function = frame && frame->root_count > 0 &&
        get_type_id(frame->roots[0]) == LMD_TYPE_FUNC
        ? frame->roots[0].function : NULL;
    // A task publishes its result after this callback returns. Its companion
    // word is therefore the stable home for a MIR public wrapper's wide scalar.
    Item result = lambda_concurrency_fn_call_into(function,
        frame ? &frame->args : NULL, task ? &task->result_scalar : NULL);
    if (result.item == ITEM_TASK_SUSPENDED) return LAMBDA_TASK_POLL_PARKED;
    if (out) *out = result;
    return LAMBDA_TASK_POLL_DONE;
}

static void task_launch_destroy(void* data) {
    LambdaTaskLaunchFrame* frame = (LambdaTaskLaunchFrame*)data;
    if (!frame) return;
    mem_free(frame->roots);
    mem_free(frame);
}

extern "C" Item lambda_task_start_function(Item function, List* args) {
    return lambda_task_start_function_scoped(function, args, false);
}

extern "C" Item lambda_task_start_function_scoped(Item function, List* args, bool escapes) {
    if (!context || !context->scheduler || get_type_id(function) != LMD_TYPE_FUNC) {
        return task_error(ERR_INVALID_OPERATION, "start requires a procedure value and scheduler");
    }
    LambdaTaskLaunchFrame* frame = (LambdaTaskLaunchFrame*)mem_calloc(
        1, sizeof(LambdaTaskLaunchFrame), MEM_CAT_EVAL);
    if (!frame) return task_error(ERR_OUT_OF_MEMORY, "task launch allocation failed");
    int arg_count = args ? (int)args->length : 0;
    frame->root_count = arg_count + 1;
    frame->roots = (Item*)mem_calloc(
        (size_t)frame->root_count, sizeof(Item), MEM_CAT_EVAL);
    if (!frame->roots) {
        mem_free(frame);
        return task_error(ERR_OUT_OF_MEMORY, "task launch roots allocation failed");
    }
    frame->roots[0] = function;
    for (int i = 0; i < arg_count; i++) frame->roots[i + 1] = args->items[i];
    // Task creation allocates the GC-managed handle. Register the launch values
    // before that allocation so precise-only GC cannot reclaim the function or
    // arguments in the gap before the new task assumes ownership of this range.
    if (scheduler_has_heap()) {
        heap_register_gc_root_range((uint64_t*)frame->roots, frame->root_count);
    }
    frame->args.type_id = LMD_TYPE_ARRAY;
    frame->args.items = frame->roots + 1;
    frame->args.length = arg_count;
    frame->args.capacity = arg_count;
    LambdaTask* task = lambda_task_create(context->scheduler, task_launch_resume,
        frame, task_launch_destroy);
    if (!task) {
        if (scheduler_has_heap()) {
            heap_unregister_gc_root_range((uint64_t*)frame->roots);
        }
        task_launch_destroy(frame);
        return task_error(ERR_OUT_OF_MEMORY, "task creation failed");
    }
    lambda_task_set_frame_roots(task, frame->roots, frame->root_count);
    LambdaTask* parent = lambda_current_task();
    if (!escapes && parent && parent->scope_top) {
        task->owner_scope = parent->scope_top;
        task->next_scope_child = parent->scope_top->children;
        parent->scope_top->children = task;
    }
    return lambda_task_handle(task);
}

extern "C" Item lambda_task_run_root_raw(
    void* function_ptr, void* env, int env_count, List* args) {
    if (!function_ptr || !context || !context->scheduler) {
        return task_error(ERR_INVALID_STATE, "task root requires a scheduler");
    }
    int arity = args ? (int)args->length : 0;
    Function* function = lambda_concurrency_to_closure((fn_ptr)function_ptr, arity, env);
    if (!function) return task_error(ERR_OUT_OF_MEMORY, "task root function allocation failed");
    function->closure_field_count = env_count > 0 ? (uint16_t)env_count : 0;
    Item handle = lambda_task_start_function((Item){.function = function}, args);
    LambdaTask* task = lambda_task_from_handle(handle);
    if (!task) return handle;
    if (lambda_scheduler_drain(context->scheduler) < 0) {
        return task_error(ERR_INVALID_STATE, "task root drain did not complete");
    }
    return lambda_task_result(task);
}

static LambdaTask* lambda_current_task(void) {
    return context && context->scheduler
        ? lambda_scheduler_current(context->scheduler) : NULL;
}

extern "C" LambdaTaskScope* lambda_task_scope_enter(void) {
    LambdaTask* task = lambda_current_task();
    if (!task) return NULL;
    LambdaTaskScope* scope = (LambdaTaskScope*)mem_calloc(
        1, sizeof(LambdaTaskScope), MEM_CAT_EVAL);
    if (!scope) return NULL;
    scope->owner = task;
    scope->parent = task->scope_top;
    task->scope_top = scope;
    log_debug("concurrency scope: enter task=%llu", (unsigned long long)task->id);
    return scope;
}

extern "C" LambdaTaskScope* lambda_task_scope_current(void) {
    LambdaTask* task = lambda_current_task();
    return task ? task->scope_top : NULL;
}

extern "C" Item lambda_task_scope_leave(LambdaTaskScope* scope, bool error_exit) {
    LambdaTask* task = lambda_current_task();
    if (!task || !scope || scope->owner != task || task->scope_top != scope) {
        return task_error(ERR_INVALID_STATE, "invalid task scope exit");
    }

    Item resumed = ItemNull;
    (void)lambda_task_take_resume_value(task, &resumed);
    if (error_exit && !scope->cancelling) {
        scope->cancelling = true;
        scope->masked_cleanup = true;
        lambda_task_set_cleanup_masked(task, true);
        for (LambdaTask* child = scope->children; child; child = child->next_scope_child) {
            lambda_task_cancel(child);
        }
    }

    for (LambdaTask* child = scope->children; child; child = child->next_scope_child) {
        if (child->state == LAMBDA_TASK_DONE) continue;
        LambdaTask* targets[1] = {child};
        task->park_kind = LAMBDA_PARK_WAIT;
        if (!task_wait_targets(task, targets, 1, false, 0)) {
            return task_error(ERR_INVALID_STATE, "failed to join scoped task");
        }
        log_debug("concurrency scope: park owner=%llu child=%llu",
            (unsigned long long)task->id, (unsigned long long)child->id);
        return (Item){.item = ITEM_TASK_SUSPENDED};
    }

    task->scope_top = scope->parent;
    if (scope->masked_cleanup) lambda_task_set_cleanup_masked(task, false);
    log_debug("concurrency scope: leave task=%llu", (unsigned long long)task->id);
    mem_free(scope);
    return ItemNull;
}

extern "C" Item lambda_task_scope_unwind(LambdaTaskScope* base, bool error_exit) {
    LambdaTask* task = lambda_current_task();
    if (!task) return task_error(ERR_INVALID_STATE, "task-scope unwind outside task");
    while (task->scope_top != base) {
        if (!task->scope_top) {
            return task_error(ERR_INVALID_STATE, "task-scope unwind crossed function boundary");
        }
        Item result = lambda_task_scope_leave(task->scope_top, error_exit);
        if (result.item == ITEM_TASK_SUSPENDED || get_type_id(result) == LMD_TYPE_ERROR) {
            return result;
        }
    }
    return ItemNull;
}

extern "C" RetItem pn_send(Item handle, Item message) {
    LambdaTask* target = lambda_task_from_handle(handle);
    LambdaTask* sender = lambda_current_task();
    LambdaSendStatus status = lambda_task_send(sender, target, message);
    if (status == LAMBDA_SEND_OK) return ri_ok(ItemNull);
    if (status == LAMBDA_SEND_FULL) {
        LambdaError* error = err_create_heap(ERR_MAILBOX_FULL, "task mailbox full", NULL);
        return ri_err(error);
    }
    LambdaError* error = err_create_heap(ERR_INVALID_OPERATION,
        status == LAMBDA_SEND_CLOSED ? "cannot send to completed task" : "invalid task handle", NULL);
    return ri_err(error);
}

extern "C" RetItem pn_receive(void) {
    LambdaTask* task = lambda_current_task();
    if (!task) return ri_err(err_create_heap(ERR_INVALID_STATE,
        "receive requires a running task", NULL));
    Item resumed = ItemNull;
    if (lambda_task_take_resume_value(task, &resumed)) return item_to_ri(resumed);
    if (task->cancel_requested && !task->cleanup_masked) {
        return ri_err(err_create_heap(ERR_CANCELLED, "task cancelled", NULL));
    }
    Item message = ItemNull;
    if (lambda_task_mailbox_receive(task, &message)) return ri_ok(message);
    task->park_kind = LAMBDA_PARK_RECEIVE;
    lambda_task_park(task);
    return ri_ok((Item){.item = ITEM_TASK_SUSPENDED});
}

static RetItem wait_handle(Item handle, uint64_t timeout_ms) {
    LambdaTask* waiter = lambda_current_task();
    LambdaTask* target = lambda_task_from_handle(handle);
    if (!waiter || !target || waiter->scheduler != target->scheduler) {
        return ri_err(err_create_heap(ERR_INVALID_OPERATION, "invalid task handle", NULL));
    }
    Item resumed = ItemNull;
    if (lambda_task_take_resume_value(waiter, &resumed)) return item_to_ri(resumed);
    if (waiter->cancel_requested && !waiter->cleanup_masked) {
        return ri_err(err_create_heap(ERR_CANCELLED, "task cancelled", NULL));
    }
    if (target->state == LAMBDA_TASK_DONE) return item_to_ri(target->result);
    LambdaTask* targets[1] = {target};
    waiter->park_kind = LAMBDA_PARK_WAIT;
    if (!task_wait_targets(waiter, targets, 1, false, timeout_ms)) {
        return ri_err(err_create_heap(ERR_INVALID_STATE, "failed to wait for task", NULL));
    }
    return ri_ok((Item){.item = ITEM_TASK_SUSPENDED});
}

extern "C" RetItem pn_wait1(Item handle) {
    if (promise_is && promise_wait && promise_is(handle)) {
        return item_to_ri(promise_wait(handle, lambda_current_task()));
    }
    return wait_handle(handle, 0);
}

extern "C" RetItem pn_wait2(Item handle, Item timeout_ms) {
    if (promise_is && promise_is(handle)) {
        return ri_err(err_create_heap(ERR_INVALID_OPERATION,
            "wait timeout is not supported for JavaScript Promises", NULL));
    }
    int64_t value = -1;
    task_read_milliseconds(timeout_ms, &value);
    if (value < 0) return ri_err(err_create_heap(ERR_INVALID_OPERATION,
        "wait timeout must be a non-negative integer", NULL));
    return wait_handle(handle, (uint64_t)value);
}

extern "C" RetItem pn_select(Item handles, Item timeout_ms) {
    LambdaTask* waiter = lambda_current_task();
    if (!waiter || get_type_id(handles) != LMD_TYPE_ARRAY || !handles.array) {
        return ri_err(err_create_heap(ERR_INVALID_OPERATION,
            "select requires an array of task handles", NULL));
    }
    Item resumed = ItemNull;
    if (lambda_task_take_resume_value(waiter, &resumed)) return item_to_ri(resumed);
    int count = (int)handles.array->length;
    if (count <= 0) return ri_err(err_create_heap(ERR_INVALID_OPERATION,
        "select requires at least one task handle", NULL));
    LambdaTask** targets = (LambdaTask**)mem_calloc((size_t)count,
        sizeof(LambdaTask*), MEM_CAT_EVAL);
    if (!targets) return ri_err(err_create_heap(ERR_OUT_OF_MEMORY, "select allocation failed", NULL));
    for (int i = 0; i < count; i++) {
        targets[i] = lambda_task_from_handle(handles.array->items[i]);
        if (!targets[i] || targets[i]->scheduler != waiter->scheduler) {
            mem_free(targets);
            return ri_err(err_create_heap(ERR_INVALID_OPERATION, "invalid task handle", NULL));
        }
        if (targets[i]->state == LAMBDA_TASK_DONE) {
            Item result = targets[i]->handle;
            mem_free(targets);
            return ri_ok(result);
        }
    }
    int64_t timeout = 0;
    task_read_milliseconds(timeout_ms, &timeout);
    waiter->park_kind = LAMBDA_PARK_SELECT;
    bool parked = task_wait_targets(waiter, targets, count, true,
        timeout > 0 ? (uint64_t)timeout : 0);
    mem_free(targets);
    return parked ? ri_ok((Item){.item = ITEM_TASK_SUSPENDED})
                  : ri_err(err_create_heap(ERR_INVALID_STATE, "failed to select tasks", NULL));
}

extern "C" RetItem pn_sleep(Item duration_ms) {
    LambdaTask* task = lambda_current_task();
    int64_t value = -1;
    task_read_milliseconds(duration_ms, &value);
    if (!task || value < 0) return ri_err(err_create_heap(ERR_INVALID_OPERATION,
        "sleep requires a running task and non-negative duration", NULL));
    Item resumed = ItemNull;
    if (lambda_task_take_resume_value(task, &resumed)) return item_to_ri(resumed);
    if (task->cancel_requested && !task->cleanup_masked) {
        return ri_err(err_create_heap(ERR_CANCELLED, "task cancelled", NULL));
    }
    if (value == 0) return ri_ok(ItemNull);
    task->park_kind = LAMBDA_PARK_SLEEP;
    lambda_task_park(task);
    if (!task_timer_start(task, (uint64_t)value, false)) {
        // Timer creation failed during this poll, so restore the current task
        // without enqueueing a duplicate runnable entry.
        task->state = LAMBDA_TASK_RUNNABLE;
        task->park_kind = LAMBDA_PARK_NONE;
        return ri_err(err_create_heap(ERR_INVALID_STATE, "failed to start sleep timer", NULL));
    }
    return ri_ok((Item){.item = ITEM_TASK_SUSPENDED});
}

extern "C" RetItem pn_io_read(Item target) {
    LambdaTask* task = lambda_current_task();
    if (!task) return ri_err(err_create_heap(ERR_INVALID_STATE,
        "io.read requires a running task", NULL));

    LambdaFileRead* read = task->file_read;
    if (read) {
        Item resumed = ItemNull;
        bool has_resume = lambda_task_take_resume_value(task, &resumed);
        if (read->done) {
            task->file_read = NULL;
            int error = read->error;
            ssize_t length = read->length;
            char* bytes = read->bytes;
            read->bytes = NULL;
            file_read_release(read);
            if (task->cancel_requested && !task->cleanup_masked) {
                mem_free(bytes);
                return ri_err(err_create_heap(ERR_CANCELLED, "task cancelled", NULL));
            }
            if (error) {
                mem_free(bytes);
                LambdaErrorCode code = error == UV_ENOENT
                    ? ERR_FILE_NOT_FOUND
                    : (error == UV_EACCES ? ERR_FILE_ACCESS_DENIED : ERR_FILE_READ_ERROR);
                return ri_err(err_create_heap(code, uv_strerror(error), NULL));
            }
            String* string = heap_strcpy(bytes ? bytes : "", length > 0 ? length : 0);
            mem_free(bytes);
            return string ? ri_ok((Item){.item = s2it(string)})
                          : ri_err(err_create_heap(ERR_OUT_OF_MEMORY,
                                "io.read result allocation failed", NULL));
        }
        if (has_resume && task->cancel_requested && !task->cleanup_masked) {
            // The generic cancel wake races with the in-flight fs request.
            // Keep the task parked until libuv releases the request storage.
            uv_cancel((uv_req_t*)&read->request);
        }
        task->park_kind = LAMBDA_PARK_FILE_READ;
        lambda_task_park(task);
        return ri_ok((Item){.item = ITEM_TASK_SUSPENDED});
    }

    if (task->cancel_requested && !task->cleanup_masked) {
        return ri_err(err_create_heap(ERR_CANCELLED, "task cancelled", NULL));
    }
    StrBuf* path = lambda_get_local_path_from_item(target);
    if (!path) return ri_err(err_create_heap(ERR_INVALID_OPERATION,
        "io.read requires a local file target", NULL));
    read = (LambdaFileRead*)mem_calloc(1, sizeof(LambdaFileRead), MEM_CAT_EVAL);
    if (!read) {
        strbuf_free(path);
        return ri_err(err_create_heap(ERR_OUT_OF_MEMORY,
            "io.read request allocation failed", NULL));
    }
    read->task = task;
    read->file = -1;
    read->path = mem_strdup(path->str, MEM_CAT_EVAL);
    strbuf_free(path);
    if (!read->path) {
        file_read_release(read);
        return ri_err(err_create_heap(ERR_OUT_OF_MEMORY,
            "io.read path allocation failed", NULL));
    }
    read->request.data = read;
    int status = uv_fs_open(lambda_uv_loop(), &read->request, read->path,
        O_RDONLY, 0, file_read_open_cb);
    if (status < 0) {
        uv_fs_req_cleanup(&read->request);
        file_read_release(read);
        return ri_err(err_create_heap(ERR_FILE_READ_ERROR,
            uv_strerror(status), NULL));
    }
    task->file_read = read;
    task->park_kind = LAMBDA_PARK_FILE_READ;
    lambda_task_park(task);
    log_debug("concurrency io.read: parked task=%llu path=%s",
        (unsigned long long)task->id, read->path);
    return ri_ok((Item){.item = ITEM_TASK_SUSPENDED});
}

extern "C" Item pn_self(void) {
    LambdaTask* task = lambda_current_task();
    return task ? task->handle : ItemNull;
}

extern "C" Item pn_cancel(Item handle) {
    LambdaTask* task = lambda_task_from_handle(handle);
    if (!task) return ItemError;
    lambda_task_cancel(task);
    return ItemNull;
}

extern "C" Item fn_to_promise(Item handle) {
    if (!handle_to_promise) {
        return task_error(ERR_INVALID_STATE,
            "toPromise requires an initialized JavaScript runtime");
    }
    return handle_to_promise(handle);
}

// MIR imports use an Item return because the platform ABI for the two-word
// RetItem struct is not stable across MIR's generated-call boundary.
extern "C" Item pn_send_mir(Item handle, Item message) {
    return ri_to_item(pn_send(handle, message));
}

extern "C" Item pn_receive_mir(void) {
    return ri_to_item(pn_receive());
}

extern "C" Item pn_wait1_mir(Item handle) {
    return ri_to_item(pn_wait1(handle));
}

extern "C" Item pn_wait2_mir(Item handle, Item timeout_ms) {
    return ri_to_item(pn_wait2(handle, timeout_ms));
}

extern "C" Item pn_select_mir(Item handles, Item timeout_ms) {
    return ri_to_item(pn_select(handles, timeout_ms));
}

extern "C" Item pn_sleep_mir(Item duration_ms) {
    return ri_to_item(pn_sleep(duration_ms));
}
