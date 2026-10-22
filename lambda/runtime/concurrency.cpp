#include "../lambda-data.hpp"
#include "../lambda.hpp"
#include "concurrency.h"
#include "activation.h"
#include "lambda-root-frame.hpp"
#include "lambda-error.h"
#include "recovery_frame.h"
#include "transpiler.hpp"
#include "io_file_search.hpp"
#include "re2_wrapper.hpp"
#include "../js/js_runtime_state.hpp"
#include "lambda/runtime/gc/gc_heap.h"
#include "../../lib/log.h"
#include "../../lib/memtrack.h"
#include "../../lib/strbuf.h"
#include "../../lib/queue.h"
#include "../../lib/uv_loop.h"
#include "../../lib/shell.h"
#include "../../lib/time_util.h"

#include <assert.h>
#include <fcntl.h>
#include <limits.h>
#include <string.h>
#include <uv.h>

extern __thread EvalContext* context;
extern "C" Item lambda_concurrency_fn_call_procedure_into(Function* fn, List* args,
    uint64_t* result_home);
extern "C" void heap_register_gc_root(uint64_t* slot);
extern "C" void heap_unregister_gc_root(uint64_t* slot);
extern "C" void heap_register_gc_root_range(uint64_t* base, int count);
extern "C" void heap_unregister_gc_root_range(uint64_t* base);
extern "C" StrBuf* lambda_get_local_path_from_item(Item item);
Item _map_get(TypeMap* type, void* data, const char* key, bool* found);

typedef enum LambdaParkKind {
    LAMBDA_PARK_NONE = 0,
    LAMBDA_PARK_RECEIVE,
    LAMBDA_PARK_WAIT,
    LAMBDA_PARK_SELECT,
    LAMBDA_PARK_SLEEP,
    LAMBDA_PARK_FILE_READ,
    LAMBDA_PARK_OFFLOAD,
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

// A blocking job on the libuv thread pool while its task parks: io.exec, and
// io.fetch (AR-E3v2). The worker thread sees only `data` and this record;
// progress reaches Lambda on the task's own stack, between parks, through
// ops->progress — the runtime itself never sends a message (AR-O11).
struct LambdaOffload {
    uv_work_t work;
    uv_async_t wake;
    LambdaTask* task;
    const LambdaOffloadOps* ops;
    void* data;
    int64_t progress_ms;
    uint64_t last_notify_ms;   // worker thread, atomic
    int cancel_flag;           // atomic
    bool progress_pending;     // loop thread
    bool done;
    bool wake_closed;
    bool free_on_close;
};

struct LambdaProcess {
    char** args;
    int arg_count;
    ShellEnvEntry* env;
    int env_count;
    char* cwd;
    ShellOptions options;
    ShellResult result;
    // io.exec {progress} (AR-E5v2): the capture thread keeps the latest
    // complete stdout line; the task delivers it between parks
    LambdaOffload* job;
    Item progress_item;         // the option as given; rooted by pn_io_exec3
    Rooted<Item>* callback;     // non-NULL once the option named a function
    int64_t progress_ms;
    uv_mutex_t lock;
    bool lock_ready;
    char* line;                 // guarded by lock
    size_t line_len;
    bool line_fresh;
    char* partial;              // bytes after the last newline (capture thread only)
    size_t partial_len;
    size_t partial_cap;
};

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
    // The task runs on its own activation; the root task is the thread's base
    // stack itself and has none (RA8).
    Activation* activation;
    bool is_root;
    Item handle;
    // owned_item_slot_store keeps a wide scalar's payload in the word right
    // after its slot, so each owned Item is immediately followed by its own.
    Item result;
    uint64_t result_scalar;
    // Delivered as the return value of the park being resumed; rooted while
    // the task waits in the run queue.
    Item resume_value;
    uint64_t resume_value_scalar;
    // A task owns the static fault view published after one of its polls
    // lands. The TLS fallback is only a handoff buffer, so a later task fault
    // must not rewrite an already-completed task's observable result.
    LambdaFaultRecord fault;
    bool cancel_requested;
    bool cleanup_masked;
    bool started;
    bool queued;
    bool handle_rooted;
    bool values_rooted;
    uint64_t last_send_sequence;
    uint64_t completion_sequence;
    LambdaMailbox mailbox;
    LambdaWaitLink* waiters;
    LambdaWaitGroup* wait_group;
    LambdaTaskTimer* timer;
    LambdaFileRead* file_read;
    LambdaOffload* offload;
    LambdaTaskScope* scope_top;
    LambdaTaskScope* owner_scope;
    LambdaTask* next_scope_child;
    // S13.3.1v2: the document that owns this task (`start(..., {owner:
    // 'document'})`), an opaque token from the document probe; null otherwise
    void* owner_document;
    LambdaTaskObserver* observers;
    LambdaTask* next_all;
    QueueNode run_link;
};

struct LambdaScheduler {
    EvalContext* owner;
    LambdaTask* all_tasks;
    Queue run_queue;
    LambdaTask* current;
    // Code running on the base stack belongs to this task (created on demand),
    // so task builtins work at every entry point without wrapping it.
    LambdaTask* root;
    uint64_t next_task_id;
    uint64_t event_sequence;
    int mailbox_capacity;
    int live_count;
    // the live tasks a document owns, with their descendants (S13.3.1v2): the
    // document joins them, so a script run's end-of-run drain does not wait
    int document_live;
    // weakly registered JS async activations (JSCU25), and this scheduler's
    // serial, which their tokens name
    int weak_count;
    uint64_t serial;
    bool draining;
    uv_async_t wake;
    bool wake_initialized;
    bool wake_refed;
    bool wake_closed;
};

static char task_handle_brand;
static LambdaScheduler* attached_scheduler;
// The UI layer's answer to "which document is running Lambda code now", null
// when none is (a script, the REPL); S13.3.1v2's `owner: 'document'` needs one.
static void* (*document_probe)(void);
static uint64_t scheduler_serial_next = 1;
static LambdaPromiseIsFn promise_is;
static LambdaPromiseWaitFn promise_wait;
static LambdaHandleToPromiseFn handle_to_promise;
static bool scheduler_has_heap(void);
static LambdaTask* lambda_current_task(void);
static void offload_cancel(LambdaOffload* job);
static void offload_release(LambdaOffload* job, bool free_on_close);

static void task_handle_invalidate(LambdaTask* task) {
    if (!task || get_type_id(task->handle) != LMD_TYPE_VMAP ||
            !task->handle.vmap ||
            task->handle.vmap->host_type != &task_handle_brand) return;
    // JS Promise reactions retain the handle, not the task allocation.  A
    // reaction may run in the final microtask checkpoint after scheduler
    // teardown; clear this non-managed edge before releasing the task.
    task->handle.vmap->host_data = NULL;
    task->handle.vmap->data = NULL;
}

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
    LambdaTask* task = (LambdaTask*)data;
    if (!task) return;
    // completed handles own their result graph; scheduler bookkeeping keeps only scalar/fault homes.
    task->handle = ItemNull;
    task->result = ItemNull;
    task->resume_value = ItemNull;
    mem_free(task->mailbox.items);
    task->mailbox.items = NULL;
    task->mailbox.count = 0;
}

static void task_handle_trace(void* data, gc_heap_t* gc) {
    LambdaTask* task = (LambdaTask*)data;
    if (!task) return;
    gc_mark_item(gc, task->result.item);
    gc_mark_item(gc, task->resume_value.item);
    for (int i = 0; task->mailbox.items && i < task->mailbox.capacity; i++)
        gc_mark_item(gc, task->mailbox.items[i].item);
}

static void task_release_handle_root(LambdaTask* task) {
    if (task && task->handle_rooted) {
        heap_unregister_gc_root(&task->handle.item);
        task->handle_rooted = false;
    }
}

static void task_scopes_destroy(LambdaTask* task) {
    LambdaTaskScope* scope = task ? task->scope_top : NULL;
    while (scope) {
        LambdaTaskScope* parent = scope->parent;
        for (LambdaTask* child = scope->children; child; child = child->next_scope_child) {
            child->owner_scope = NULL;
            if (child->state == LAMBDA_TASK_DONE) task_release_handle_root(child);
        }
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
    task_handle_trace,
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
    // Inline int values store their payload in the Item itself; only INT64
    // carries a pointer that is valid for get_int64().
    if (type_id == LMD_TYPE_INT) {
        *out = lambda_int_item_to_i64(item);
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
    if (task->is_root) {
        // The root continues on the base stack once its loop turn observes
        // the state change; wake that turn instead of queueing a poll.
        if (task->scheduler->wake_initialized) uv_async_send(&task->scheduler->wake);
        return;
    }
    task->queued = true;
    queue_push(&task->scheduler->run_queue, &task->run_link);
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
            !scheduler->run_queue.first) {
        uv_unref((uv_handle_t*)&scheduler->wake);
        scheduler->wake_refed = false;
    }
}

static LambdaTask* scheduler_dequeue(LambdaScheduler* scheduler) {
    QueueNode* node = scheduler ? queue_pop(&scheduler->run_queue) : NULL;
    if (!node) return NULL;
    LambdaTask* task = QUEUE_CONTAINER_OF(node, LambdaTask, run_link);
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
    task->park_kind = LAMBDA_PARK_NONE;
    scheduler_enqueue(task);
}

static void process_release(LambdaProcess* process) {
    if (!process) return;
    for (int i = 0; process->args && i < process->arg_count; i++) mem_free(process->args[i]);
    for (int i = 0; process->env && i < process->env_count; i++) {
        mem_free((void*)process->env[i].key);
        mem_free((void*)process->env[i].value);
    }
    mem_free(process->args);
    mem_free(process->env);
    mem_free(process->cwd);
    if (process->lock_ready) uv_mutex_destroy(&process->lock);
    mem_free(process->line);
    mem_free(process->partial);
    shell_result_free(&process->result);
    mem_free(process);
}

static void process_release_orphan(void* data) {
    process_release((LambdaProcess*)data);
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
            // A waiter can already be done when its target completes. Removing
            // only this target link leaves the group's next link dangling; tear
            // down the whole settled group before either task is reclaimed.
            LambdaWaitLink* stale = target->waiters;
            if (group) {
                if (group->waiter && group->waiter->wait_group == group) {
                    group->waiter->wait_group = NULL;
                }
                wait_group_unlink(group);
                wait_group_free(group);
            } else {
                target->waiters = stale->next_target;
                mem_free(stale);
            }
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
    scheduler->owner = context;
    queue_init(&scheduler->run_queue);
    scheduler->mailbox_capacity = mailbox_capacity > 0
        ? mailbox_capacity : LAMBDA_MAILBOX_DEFAULT_CAPACITY;
    scheduler->next_task_id = 1;
    scheduler->serial = scheduler_serial_next++;
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

static bool scheduler_poll_teardown(EvalContext* owner, uv_run_mode mode) {
    uv_run(lambda_uv_loop(), mode);
    // shared-loop callbacks can bind another document; teardown must return to its precise root owner.
    if (owner && context != owner && !js_runtime_context_enter_turn(owner->runtime, owner)) {
        log_error("scheduler-teardown: could not restore the retiring evaluator");
        return false;
    }
    return true;
}

extern "C" void lambda_scheduler_destroy(LambdaScheduler* scheduler) {
    if (!scheduler) return;
    EvalContext* teardown_owner = context;
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
        offload_cancel(task->offload);
    }
    // File-system requests retain their task pointer through the libuv
    // callback. Finish cancellation before releasing task records.
    bool pending_file_read = true;
    while (pending_file_read) {
        pending_file_read = false;
        for (LambdaTask* pending = scheduler->all_tasks; pending; pending = pending->next_all) {
            if ((pending->file_read && !pending->file_read->done) ||
                    (pending->offload && !pending->offload->done)) {
                pending_file_read = true;
                break;
            }
        }
        if (pending_file_read && !scheduler_poll_teardown(teardown_owner, UV_RUN_ONCE)) return;
    }
    LambdaTask* task = scheduler->all_tasks;
    while (task) {
        LambdaTask* next = task->next_all;
        // D6.3.1: a JS microtask can only request a later Lambda macrotask.
        // Once teardown starts, retained Promise reactions must become no-ops
        // rather than dereferencing the task after this record is released.
        task_handle_invalidate(task);
        if (scheduler_has_heap()) {
            task_release_handle_root(task);
            if (task->values_rooted) {
                heap_unregister_gc_root(&task->result.item);
                heap_unregister_gc_root(&task->resume_value.item);
                heap_unregister_gc_root_range((uint64_t*)task->mailbox.items);
            }
        }
        activation_destroy(task->activation);
        task->activation = NULL;
        task_scopes_destroy(task);
        LambdaTaskObserver* observer = task->observers;
        while (observer) {
            LambdaTaskObserver* next_observer = observer->next;
            if (observer->destroy_data) observer->destroy_data(observer->data);
            mem_free(observer);
            observer = next_observer;
        }
        file_read_release(task->file_read);
        if (task->offload) {
            // the task never resumes, so free the job's data for it and let
            // libuv release the wake handle before the record goes
            LambdaOffload* job = task->offload;
            if (job->ops->release) job->ops->release(job->data);
            offload_release(job, false);
            while (!job->wake_closed) {
                if (!scheduler_poll_teardown(teardown_owner, UV_RUN_NOWAIT)) {
                    job->free_on_close = true;
                    job = NULL;
                    break;
                }
            }
            mem_free(job);
        }
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
        while (!scheduler->wake_closed)
            if (!scheduler_poll_teardown(teardown_owner, UV_RUN_NOWAIT)) return;
    }
    // Every task's activation is gone; keep no pooled stacks past the scheduler.
    activation_release_pool();
    mem_free(scheduler);
    log_debug("concurrency scheduler: destroyed");
}

static LambdaTask* task_record_create(LambdaScheduler* scheduler, bool is_root) {
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
    handle->data = task;

    task->scheduler = scheduler;
    task->id = scheduler->next_task_id++;
    task->state = LAMBDA_TASK_RUNNABLE;
    task->is_root = is_root;
    task->started = is_root;
    task->handle = (Item){.vmap = handle};
    task->result = ItemNull;
    lambda_fault_record_init(&task->fault);
    task->resume_value = ItemNull;
    task->mailbox.capacity = scheduler->mailbox_capacity;
    task->next_all = scheduler->all_tasks;
    scheduler->all_tasks = task;
    // The root is the base stack: it never completes, so it never holds a
    // drain open.
    if (!is_root) scheduler->live_count++;

    heap_register_gc_root(&task->handle.item);
    heap_register_gc_root(&task->result.item);
    heap_register_gc_root(&task->resume_value.item);
    heap_register_gc_root_range((uint64_t*)task->mailbox.items, task->mailbox.capacity);
    task->handle_rooted = task->values_rooted = true;
    log_debug("concurrency task: created id=%llu%s", (unsigned long long)task->id,
        is_root ? " (root)" : "");
    return task;
}

extern "C" LambdaTask* lambda_task_create(LambdaScheduler* scheduler,
    ActivationEntry entry, Item arg) {
    if (!entry) return NULL;
    // The handle allocation below may collect; the argument has no other
    // owner until the activation's own segment holds it.
    RootFrame roots(1);
    Rooted<Item> arg_root(roots, arg);
    LambdaTask* task = task_record_create(scheduler, false);
    if (!task) return NULL;
    task->activation = activation_create(entry, arg_root.get(), true, task);
    if (!task->activation) {
        lambda_task_complete(task, task_error(ERR_OUT_OF_MEMORY,
            "task activation allocation failed"));
        return task;
    }
    scheduler_enqueue(task);
    return task;
}

extern "C" int lambda_scheduler_run_one(LambdaScheduler* scheduler) {
    // shared-loop wakeups during another document's teardown must leave this owner's task queued (D5.4.1).
    if (!scheduler || scheduler->owner != context) return 0;
    LambdaTask* task = scheduler_dequeue(scheduler);
    if (!task) return 0;
    if (task->cancel_requested && !task->cleanup_masked && !task->started) {
        // A task cancelled before its first poll has no lexical scopes yet.
        // Once started, cancellation is delivered at its park point so `^`
        // propagation runs structured cancel-then-join cleanup.
        lambda_task_complete(task, task_error(ERR_CANCELLED, "task cancelled"));
        return 1;
    }
    if (!task->activation) {
        lambda_task_complete(task, ItemNull);
        return 1;
    }
    task->started = true;
    LambdaTask* previous = scheduler->current;
    scheduler->current = task;
    Item input = task->resume_value;
    task->resume_value = ItemNull;
    // Faults are contained by the activation's own execution boundary; the
    // poll returns here whether the task parked, finished or faulted.
    ActivationStatus status = activation_resume(task->activation, input);
    scheduler->current = previous;
    if (status == ACTIVATION_DONE) {
        const LambdaFaultRecord* fault = activation_fault(task->activation);
        Item result;
        if (fault) {
            // The task owns the static fault view it publishes; the
            // activation's copy is released with it.
            task->fault = *fault;
            result = err2it(&task->fault.error);
        } else {
            // the activation owns returned wide scalars until its record is destroyed.
            result = lambda_item_adopt_scalar_home(activation_value(task->activation), &task->result_scalar);
        }
        activation_destroy(task->activation);
        task->activation = NULL;
        lambda_task_complete(task, result);
    }
    return 1;
}

extern "C" int lambda_scheduler_run_ready(LambdaScheduler* scheduler) {
    if (!scheduler || scheduler->owner != context || scheduler->draining) return 0;
    scheduler->draining = true;
    int ran = 0;
    QueueNode* boundary = queue_last(&scheduler->run_queue);
    while (scheduler->run_queue.first) {
        QueueNode* node = scheduler->run_queue.first;
        ran += lambda_scheduler_run_one(scheduler);
        if (node == boundary) break;
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

// the live tasks a drain waits for: those no document owns
static int scheduler_drain_live(const LambdaScheduler* scheduler) {
    return scheduler->live_count - scheduler->document_live;
}

static bool scheduler_run_satisfied(const LambdaScheduler* scheduler,
                                    const LambdaTask* awaited) {
    return awaited ? awaited->state != LAMBDA_TASK_PARKED
        : scheduler_drain_live(scheduler) == 0 && scheduler->weak_count == 0;
}

// Whether anything other than `watchdog` keeps the loop alive.
static bool scheduler_loop_busy(uv_loop_t* loop, uv_timer_t* watchdog) {
    if (!watchdog) return uv_loop_alive(loop) != 0;
    uv_unref((uv_handle_t*)watchdog);
    bool busy = uv_loop_alive(loop) != 0;
    uv_ref((uv_handle_t*)watchdog);
    return busy;
}

// Only weak registrations remain and nothing can run: give queued promise
// jobs their checkpoint, then collect, so a carrier nobody can reach any more
// leaves. True if either made progress.
static bool scheduler_settle_weak(LambdaScheduler* scheduler) {
    if (scheduler_drain_live(scheduler) != 0 || scheduler->weak_count == 0) return false;
    int before = scheduler->weak_count;
    lambda_uv_checkpoint();
    if (scheduler->weak_count != before || scheduler->run_queue.first) return true;
    if (scheduler_has_heap()) heap_gc_collect();
    return scheduler->weak_count != before || scheduler->run_queue.first;
}

// Drive the loop on the base stack until `awaited` is woken, or, when it is
// NULL, until no task is live. Tasks run on their own activations, so any of
// them can resume in any order; nothing is buried beneath this loop.
static int scheduler_run_until(LambdaScheduler* scheduler, LambdaTask* awaited) {
    if (!scheduler) return 0;
    uv_loop_t* loop = lambda_uv_loop();
    LambdaDrainWatchdogState watchdog_state = {false, false};
    uv_timer_t watchdog;
    bool watchdog_initialized = false;
    bool watchdog_started = false;
    int ran = 0;
    while (!scheduler_run_satisfied(scheduler, awaited) && !watchdog_state.fired) {
        int step = lambda_scheduler_run_ready(scheduler);
        ran += step;
        if (scheduler_run_satisfied(scheduler, awaited)) break;
        // run_ready intentionally stops at its initial FIFO boundary. A task
        // may enqueue a child behind that boundary; poll only after the next
        // macrotask batch has had a chance to start its I/O/timer wait.
        if (scheduler->run_queue.first) continue;
        if (!loop) break;
        // Only weak registrations remain: wait for the loop only while
        // something besides this drain's own watchdog keeps it alive.
        if (!awaited && scheduler_drain_live(scheduler) == 0 &&
                !scheduler_loop_busy(loop, watchdog_started ? &watchdog : NULL)) {
            if (scheduler_settle_weak(scheduler)) continue;
            break;
        }
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
        if (scheduler_run_satisfied(scheduler, awaited)) break;
        // A root task's wait is stuck only when nothing but this watchdog
        // keeps the loop alive. A timer, an offloaded job or a child process
        // still pending will wake a task however long it takes, so a sleep or
        // download past 5 s failed with E502. The end-of-run drain (awaited
        // == NULL) keeps its documented bound (Lambda_Design_Concurrency §12.4).
        if (awaited && watchdog_state.fired && scheduler_loop_busy(loop, &watchdog)) {
            watchdog_state.fired = false;
            watchdog_state.grace_turn = false;
            uv_timer_start(&watchdog, scheduler_drain_watchdog_cb, 5000, 0);
        }
        if (step == 0 && !uv_loop_alive(loop) && !scheduler->run_queue.first) break;
    }
    if (watchdog_initialized) {
        if (watchdog_started) uv_timer_stop(&watchdog);
        if (!uv_is_closing((uv_handle_t*)&watchdog)) {
            uv_close((uv_handle_t*)&watchdog, NULL);
        }
        uv_run(loop, UV_RUN_NOWAIT);
    }
    // Weak registrations that nothing can settle never fail a drain.
    if (!awaited && scheduler_drain_live(scheduler) == 0 && scheduler->weak_count > 0) {
        log_debug("concurrency scheduler: drain left %d unsettled JS activation(s)",
            scheduler->weak_count);
        return ran;
    }
    if (!scheduler_run_satisfied(scheduler, awaited)) {
        log_error("concurrency scheduler: %s stopped with %d live task(s)%s",
            awaited ? "root wait" : "drain", awaited ? scheduler->live_count : scheduler_drain_live(scheduler),
            watchdog_state.fired ? " after watchdog timeout" : " without runnable work");
        return -1;
    }
    return ran;
}

extern "C" int lambda_scheduler_drain(LambdaScheduler* scheduler) {
    return scheduler_run_until(scheduler, NULL);
}

extern "C" int lambda_scheduler_live_count(const LambdaScheduler* scheduler) {
    return scheduler ? scheduler->live_count + scheduler->weak_count : 0;
}

extern "C" uint64_t lambda_scheduler_weak_enter(void) {
    if (!attached_scheduler) return 0;
    attached_scheduler->weak_count++;
    return attached_scheduler->serial;
}

extern "C" void lambda_scheduler_weak_leave(uint64_t token) {
    LambdaScheduler* scheduler = attached_scheduler;
    if (!token || !scheduler || scheduler->serial != token ||
            scheduler->weak_count <= 0) return;
    scheduler->weak_count--;
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
    RootFrame roots(1);
    Rooted<Item> handle(roots, task->handle);
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
    if (task->owner_document) task->scheduler->document_live--;
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
    // D5.3.3: once complete, reachability through the handle retains the result, not the scheduler.
    if (task->values_rooted) {
        heap_unregister_gc_root(&task->result.item);
        heap_unregister_gc_root(&task->resume_value.item);
        heap_unregister_gc_root_range((uint64_t*)task->mailbox.items);
        task->values_rooted = false;
    }
    if (!task->owner_scope) task_release_handle_root(task);
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

static LambdaTask* scheduler_root_task(LambdaScheduler* scheduler) {
    if (!scheduler->root) scheduler->root = task_record_create(scheduler, true);
    return scheduler->root;
}

static void task_wait_abandon(LambdaTask* task) {
    if (task->wait_group) {
        LambdaWaitGroup* group = task->wait_group;
        task->wait_group = NULL;
        wait_group_unlink(group);
        wait_group_free(group);
    }
    task_timer_close(task);
    task->park_kind = LAMBDA_PARK_NONE;
    task->state = LAMBDA_TASK_RUNNABLE;
}

// Park the running task and return the value it is resumed with. The caller
// has already registered whatever wakes it. A task on its own activation
// switches away; the root task drives the loop on the base stack instead.
extern "C" Item lambda_task_suspend(LambdaTask* task) {
    if (!task || task->state == LAMBDA_TASK_DONE) {
        return task_error(ERR_INVALID_STATE, "suspension requires a running task");
    }
    lambda_task_park(task);
    if (task->is_root) {
        if (scheduler_run_until(task->scheduler, task) < 0) {
            task_wait_abandon(task);
            return task_error(ERR_INVALID_STATE,
                "task wait cannot complete: nothing else can run");
        }
        Item resumed = task->resume_value;
        task->resume_value = ItemNull;
        return resumed;
    }
    if (activation_current() != task->activation) {
        // A nested activation (a generator body) cannot park its task.
        task_wait_abandon(task);
        return task_error(ERR_INVALID_STATE,
            "a task can only park on its own activation");
    }
    // The scheduler delivers task->resume_value as this call's result.
    return activation_suspend(ItemNull);
}

extern "C" Item lambda_task_start_function(Item function, List* args) {
    return lambda_task_start_function_scoped(function, args, false);
}

// Launch data travels as one Item, [function, arg0, ...], rooted in the
// activation's own segment for the entry's lifetime.
static Item task_entry(Activation* self, Item launch) {
    LambdaTask* task = (LambdaTask*)activation_user(self);
    Array* items = get_type_id(launch) == LMD_TYPE_ARRAY ? launch.array : NULL;
    Function* function = items && items->length > 0 &&
        get_type_id(items->items[0]) == LMD_TYPE_FUNC ? items->items[0].function : NULL;
    int arg_count = items ? (int)items->length - 1 : 0;
    // Copy the arguments into this stack's roots: the array's buffer may move
    // during a collection while the call is still consuming it.
    RootSpan span((size_t)(arg_count > 0 ? arg_count : 0));
    Item* slots = span.items();
    for (int i = 0; i < arg_count; i++) slots[i] = items->items[i + 1];
    List args = {};
    args.type_id = LMD_TYPE_ARRAY;
    args.items = slots;
    args.length = arg_count;
    args.capacity = arg_count;
    // A task publishes its result after this entry returns. Its companion
    // word is therefore the stable home for a MIR public wrapper's wide scalar.
    return lambda_concurrency_fn_call_procedure_into(function, &args,
        task ? &task->result_scalar : NULL);
}

extern "C" Item lambda_task_start_function_scoped(Item function, List* args, bool escapes) {
    if (!context || !context->scheduler || get_type_id(function) != LMD_TYPE_FUNC) {
        return task_error(ERR_INVALID_OPERATION, "start requires a procedure value and scheduler");
    }
    // A caller may hand over an unrooted argument list (the JS membrane), so
    // root it before the launch array allocation can collect it.
    RootFrame roots(3);
    Rooted<Item> function_root(roots, function);
    Rooted<Item> args_root(roots, args ? (Item){.array = (Array*)args} : ItemNull);
    Rooted<Item> launch_root(roots, ItemNull);
    Array* launch = array();
    if (!launch) return task_error(ERR_OUT_OF_MEMORY, "task launch allocation failed");
    launch_root.set((Item){.array = launch});
    array_push_verbatim(launch, function_root.get());
    for (int i = 0; args && i < (int)args_root.get().array->length; i++) {
        // re-read through the root: a push may grow (and move) the buffers
        array_push_verbatim(launch_root.get().array, args_root.get().array->items[i]);
    }
    LambdaTask* task = lambda_task_create(context->scheduler, task_entry,
        launch_root.get());
    if (!task) return task_error(ERR_OUT_OF_MEMORY, "task creation failed");
    LambdaTask* parent = lambda_current_task();
    // a document-owned task's descendants belong to that document too
    if (parent && parent->owner_document) {
        task->owner_document = parent->owner_document;
        context->scheduler->document_live++;
    }
    if (!escapes && parent && parent->scope_top) {
        task->owner_scope = parent->scope_top;
        task->next_scope_child = parent->scope_top->children;
        parent->scope_top->children = task;
    }
    return lambda_task_handle(task);
}

extern "C" void lambda_set_document_probe(void* (*probe)(void)) {
    document_probe = probe;
}

// S13.3.1v2: a task the running document owns. No block joins it, whatever
// scope starts it; the document cancels then joins it when it is discarded
// (lambda_document_tasks_release). Outside a document there is no owner.
extern "C" Item lambda_task_start_document(Item function, List* args) {
    void* document = document_probe ? document_probe() : NULL;
    if (!document) {
        return task_error(ERR_INVALID_OPERATION,
            "start: owner 'document' needs a running document (a view's handler or template)");
    }
    Item handle = lambda_task_start_function_scoped(function, args, true);
    LambdaTask* task = lambda_task_from_handle(handle);
    if (task && !task->owner_document) context->scheduler->document_live++;
    if (task) task->owner_document = document;
    return handle;
}

static bool document_tasks_pending(LambdaScheduler* scheduler, void* document) {
    for (LambdaTask* task = scheduler->all_tasks; task; task = task->next_all) {
        if (task->owner_document == document && task->state != LAMBDA_TASK_DONE) return true;
    }
    return false;
}

// Cancels every task `document` owns, then drives the loop until they have
// finished: their cleanup (a child process stopped, a staging folder removed)
// runs while the document's state and DOM still exist. Bounded, so a task that
// ignores cancellation cannot hold the window open; it is reported and left to
// the scheduler's teardown. Returns the number of tasks released.
extern "C" int lambda_document_tasks_release(void* document) {
    LambdaScheduler* scheduler = context ? context->scheduler : NULL;
    if (!scheduler || !document) return 0;
    int released = 0;
    for (LambdaTask* task = scheduler->all_tasks; task; task = task->next_all) {
        if (task->owner_document != document || task->state == LAMBDA_TASK_DONE) continue;
        lambda_task_cancel(task);
        released++;
    }
    if (!released) return 0;
    uv_loop_t* loop = lambda_uv_loop();
    uint64_t deadline = time_now_ms() + 5000;
    while (document_tasks_pending(scheduler, document) && time_now_ms() < deadline) {
        int ran = lambda_scheduler_run_ready(scheduler);
        if (!document_tasks_pending(scheduler, document)) break;
        if (scheduler->run_queue.first) continue;
        if (!loop || (!ran && !uv_loop_alive(loop))) break;
        uv_run(loop, ran > 0 ? UV_RUN_NOWAIT : UV_RUN_ONCE);
    }
    for (LambdaTask* task = scheduler->all_tasks; task; task = task->next_all) {
        if (task->owner_document != document) continue;
        if (task->state != LAMBDA_TASK_DONE) {
            log_error("concurrency document: task %llu did not finish after cancellation",
                (unsigned long long)task->id);
            scheduler->document_live--;  // ownerless now: the scheduler's teardown frees it
        }
        task->owner_document = NULL;
    }
    return released;
}

static LambdaTask* lambda_current_task(void) {
    LambdaScheduler* scheduler = context ? context->scheduler : NULL;
    if (!scheduler) return NULL;
    if (scheduler->current) return scheduler->current;
    // Base-stack code is the root task; another activation that is not a
    // task (a generator body) has no task identity of its own.
    return activation_current() ? NULL : scheduler_root_task(scheduler);
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

    if (error_exit && !scope->cancelling) {
        scope->cancelling = true;
        scope->masked_cleanup = true;
        lambda_task_set_cleanup_masked(task, true);
        for (LambdaTask* child = scope->children; child; child = child->next_scope_child) {
            lambda_task_cancel(child);
        }
    }

    for (LambdaTask* child = scope->children; child; child = child->next_scope_child) {
        while (child->state != LAMBDA_TASK_DONE) {
            LambdaTask* targets[1] = {child};
            task->park_kind = LAMBDA_PARK_WAIT;
            if (!task_wait_targets(task, targets, 1, false, 0)) {
                return task_error(ERR_INVALID_STATE, "failed to join scoped task");
            }
            log_debug("concurrency scope: park owner=%llu child=%llu",
                (unsigned long long)task->id, (unsigned long long)child->id);
            // A cancellation wake does not end the join; every child is
            // awaited before the scope closes.
            (void)lambda_task_suspend(task);
        }
    }

    task->scope_top = scope->parent;
    for (LambdaTask* child = scope->children; child; child = child->next_scope_child) {
        child->owner_scope = NULL;
        task_release_handle_root(child);
    }
    if (scope->masked_cleanup) lambda_task_set_cleanup_masked(task, false);
    log_debug("concurrency scope: leave task=%llu", (unsigned long long)task->id);
    mem_free(scope);
    return ItemNull;
}

extern "C" Item lambda_task_scope_unwind(LambdaTaskScope* base, bool error_exit) {
    LambdaTask* task = lambda_current_task();
    if (!task) return ItemNull;
    while (task->scope_top && task->scope_top != base) {
        Item result = lambda_task_scope_leave(task->scope_top, error_exit);
        if (get_type_id(result) == LMD_TYPE_ERROR) return result;
    }
    return ItemNull;
}

extern "C" Item pn_send(Item handle, Item message) {
    LambdaTask* target = lambda_task_from_handle(handle);
    LambdaTask* sender = lambda_current_task();
    LambdaSendStatus status = lambda_task_send(sender, target, message);
    if (status == LAMBDA_SEND_OK) return ItemNull;
    if (status == LAMBDA_SEND_FULL) {
        LambdaError* error = err_create_heap(ERR_MAILBOX_FULL, "task mailbox full", NULL);
        return err2it_or_error(error);
    }
    LambdaError* error = err_create_heap(ERR_INVALID_OPERATION,
        status == LAMBDA_SEND_CLOSED ? "cannot send to completed task" : "invalid task handle", NULL);
    return err2it_or_error(error);
}

static Item task_cancelled_error(void) {
    return err2it_or_error(err_create_heap(ERR_CANCELLED, "task cancelled", NULL));
}

static bool task_cancel_pending(const LambdaTask* task) {
    return task->cancel_requested && !task->cleanup_masked;
}

extern "C" Item pn_receive(void) {
    LambdaTask* task = lambda_current_task();
    if (!task) return err2it_or_error(err_create_heap(ERR_INVALID_STATE,
        "receive requires a running task", NULL));
    if (task_cancel_pending(task)) return task_cancelled_error();
    Item message = ItemNull;
    if (lambda_task_mailbox_receive(task, &message)) return message;
    task->park_kind = LAMBDA_PARK_RECEIVE;
    return lambda_task_suspend(task);
}

static Item wait_handle(Item handle, uint64_t timeout_ms) {
    LambdaTask* waiter = lambda_current_task();
    LambdaTask* target = lambda_task_from_handle(handle);
    if (!waiter || !target || waiter->scheduler != target->scheduler) {
        return err2it_or_error(err_create_heap(ERR_INVALID_OPERATION, "invalid task handle", NULL));
    }
    if (task_cancel_pending(waiter)) return task_cancelled_error();
    if (target->state == LAMBDA_TASK_DONE) return target->result;
    LambdaTask* targets[1] = {target};
    waiter->park_kind = LAMBDA_PARK_WAIT;
    if (!task_wait_targets(waiter, targets, 1, false, timeout_ms)) {
        return err2it_or_error(err_create_heap(ERR_INVALID_STATE, "failed to wait for task", NULL));
    }
    return lambda_task_suspend(waiter);
}

extern "C" Item pn_wait1(Item handle) {
    if (promise_is && promise_wait && promise_is(handle)) {
        return promise_wait(handle, lambda_current_task());
    }
    return wait_handle(handle, 0);
}

extern "C" Item pn_wait2(Item handle, Item timeout_ms) {
    if (promise_is && promise_is(handle)) {
        return err2it_or_error(err_create_heap(ERR_INVALID_OPERATION,
            "wait timeout is not supported for JavaScript Promises", NULL));
    }
    int64_t value = -1;
    task_read_milliseconds(timeout_ms, &value);
    if (value < 0) return err2it_or_error(err_create_heap(ERR_INVALID_OPERATION,
        "wait timeout must be a non-negative integer", NULL));
    return wait_handle(handle, (uint64_t)value);
}

extern "C" Item pn_select(Item handles, Item timeout_ms) {
    LambdaTask* waiter = lambda_current_task();
    if (!waiter || get_type_id(handles) != LMD_TYPE_ARRAY || !handles.array) {
        return err2it_or_error(err_create_heap(ERR_INVALID_OPERATION,
            "select requires an array of task handles", NULL));
    }
    if (task_cancel_pending(waiter)) return task_cancelled_error();
    int count = (int)handles.array->length;
    if (count <= 0) return err2it_or_error(err_create_heap(ERR_INVALID_OPERATION,
        "select requires at least one task handle", NULL));
    LambdaTask** targets = (LambdaTask**)mem_calloc((size_t)count,
        sizeof(LambdaTask*), MEM_CAT_EVAL);
    if (!targets) return err2it_or_error(err_create_heap(ERR_OUT_OF_MEMORY, "select allocation failed", NULL));
    for (int i = 0; i < count; i++) {
        targets[i] = lambda_task_from_handle(handles.array->items[i]);
        if (!targets[i] || targets[i]->scheduler != waiter->scheduler) {
            mem_free(targets);
            return err2it_or_error(err_create_heap(ERR_INVALID_OPERATION, "invalid task handle", NULL));
        }
        if (targets[i]->state == LAMBDA_TASK_DONE) {
            Item result = targets[i]->handle;
            mem_free(targets);
            return result;
        }
    }
    int64_t timeout = 0;
    task_read_milliseconds(timeout_ms, &timeout);
    waiter->park_kind = LAMBDA_PARK_SELECT;
    bool parked = task_wait_targets(waiter, targets, count, true,
        timeout > 0 ? (uint64_t)timeout : 0);
    mem_free(targets);
    return parked ? lambda_task_suspend(waiter)
                  : err2it_or_error(err_create_heap(ERR_INVALID_STATE, "failed to select tasks", NULL));
}

extern "C" Item pn_sleep(Item duration_ms) {
    LambdaTask* task = lambda_current_task();
    int64_t value = -1;
    task_read_milliseconds(duration_ms, &value);
    if (!task || value < 0) return err2it_or_error(err_create_heap(ERR_INVALID_OPERATION,
        "sleep requires a running task and non-negative duration", NULL));
    if (task_cancel_pending(task)) return task_cancelled_error();
    if (value == 0) return ItemNull;
    task->park_kind = LAMBDA_PARK_SLEEP;
    lambda_task_park(task);
    if (!task_timer_start(task, (uint64_t)value, false)) {
        task->state = LAMBDA_TASK_RUNNABLE;
        task->park_kind = LAMBDA_PARK_NONE;
        return err2it_or_error(err_create_heap(ERR_INVALID_STATE, "failed to start sleep timer", NULL));
    }
    return lambda_task_suspend(task);
}

static Item file_read_result(LambdaTask* task, LambdaFileRead* read) {
    task->file_read = NULL;
    int error = read->error;
    ssize_t length = read->length;
    char* bytes = read->bytes;
    read->bytes = NULL;
    file_read_release(read);
    if (task_cancel_pending(task)) {
        mem_free(bytes);
        return task_cancelled_error();
    }
    if (error) {
        mem_free(bytes);
        LambdaErrorCode code = error == UV_ENOENT
            ? ERR_FILE_NOT_FOUND
            : (error == UV_EACCES ? ERR_FILE_ACCESS_DENIED : ERR_FILE_READ_ERROR);
        return err2it_or_error(err_create_heap(code, uv_strerror(error), NULL));
    }
    String* string = heap_strcpy(bytes ? bytes : "", length > 0 ? length : 0);
    mem_free(bytes);
    return string ? (Item){.item = s2it(string)}
                  : err2it_or_error(err_create_heap(ERR_OUT_OF_MEMORY,
                        "io.read result allocation failed", NULL));
}

extern "C" Item pn_io_read(Item target) {
    LambdaTask* task = lambda_current_task();
    if (!task) return err2it_or_error(err_create_heap(ERR_INVALID_STATE,
        "io.read requires a running task", NULL));
    if (task_cancel_pending(task)) return task_cancelled_error();
    StrBuf* path = lambda_get_local_path_from_item(target);
    if (!path) return err2it_or_error(err_create_heap(ERR_INVALID_OPERATION,
        "io.read requires a local file target", NULL));
    LambdaFileRead* read = (LambdaFileRead*)mem_calloc(1, sizeof(LambdaFileRead), MEM_CAT_EVAL);
    if (!read) {
        strbuf_free(path);
        return err2it_or_error(err_create_heap(ERR_OUT_OF_MEMORY,
            "io.read request allocation failed", NULL));
    }
    read->task = task;
    read->file = -1;
    read->path = mem_strdup(path->str, MEM_CAT_EVAL);
    strbuf_free(path);
    if (!read->path) {
        file_read_release(read);
        return err2it_or_error(err_create_heap(ERR_OUT_OF_MEMORY,
            "io.read path allocation failed", NULL));
    }
    read->request.data = read;
    int status = uv_fs_open(lambda_uv_loop(), &read->request, read->path,
        O_RDONLY, 0, file_read_open_cb);
    if (status < 0) {
        uv_fs_req_cleanup(&read->request);
        file_read_release(read);
        return err2it_or_error(err_create_heap(ERR_FILE_READ_ERROR,
            uv_strerror(status), NULL));
    }
    task->file_read = read;
    log_debug("concurrency io.read: parked task=%llu path=%s",
        (unsigned long long)task->id, read->path);
    while (!read->done) {
        task->park_kind = LAMBDA_PARK_FILE_READ;
        (void)lambda_task_suspend(task);
        if (!read->done && task_cancel_pending(task)) {
            // The cancellation wake races with the in-flight fs request; stay
            // parked until libuv releases the request storage.
            uv_cancel((uv_req_t*)&read->request);
        }
    }
    return file_read_result(task, read);
}

// ---------------------------------------------------------------------------
// Offload: a blocking job on the thread pool while the task parks (AR-E3v2)
// ---------------------------------------------------------------------------

static void offload_cancel(LambdaOffload* job) {
    if (!job || job->done) return;
    __atomic_store_n(&job->cancel_flag, 1, __ATOMIC_RELEASE);
    uv_cancel((uv_req_t*)&job->work);
}

// Closes the wake handle; the record is freed once libuv lets go of it.
static void offload_release(LambdaOffload* job, bool free_on_close) {
    if (!job) return;
    job->free_on_close = free_on_close;
    if (uv_is_closing((uv_handle_t*)&job->wake)) return;
    uv_close((uv_handle_t*)&job->wake, [](uv_handle_t* handle) {
        LambdaOffload* job = (LambdaOffload*)handle->data;
        job->wake_closed = true;
        if (job->free_on_close) mem_free(job);
    });
}

extern "C" const int* lambda_offload_cancel_flag(const LambdaOffload* job) {
    return job ? &job->cancel_flag : NULL;
}

// Worker thread. Progress is a level, not a queue: a tick inside the rate
// window is dropped, and the next one carries the latest counters.
extern "C" void lambda_offload_notify(LambdaOffload* job) {
    if (!job) return;
    uint64_t now = time_now_ms();
    if (!now) now = 1;
    uint64_t last = __atomic_load_n(&job->last_notify_ms, __ATOMIC_RELAXED);
    if (last && job->progress_ms > 0 && now - last < (uint64_t)job->progress_ms) return;
    __atomic_store_n(&job->last_notify_ms, now, __ATOMIC_RELAXED);
    uv_async_send(&job->wake);
}

// Only a task parked on this job may be woken: parked anywhere else (its
// progress callback sleeping, say) it finds `done` or `progress_pending`
// when it returns to the offload loop.
static void offload_wake_task(LambdaOffload* job) {
    LambdaTask* task = job->task;
    if (task && task->state == LAMBDA_TASK_PARKED && task->park_kind == LAMBDA_PARK_OFFLOAD)
        lambda_task_resume(task);
}

extern "C" Item lambda_task_offload(const LambdaOffloadOps* ops, void* data, int64_t progress_ms) {
    LambdaTask* task = lambda_current_task();
    if (!task || !ops || !ops->run) {
        return task_error(ERR_INVALID_OPERATION, "offload requires a running procedure");
    }
    if (task_cancel_pending(task)) return task_cancelled_error();
    if (!lambda_uv_loop() && lambda_uv_init() != 0) {
        return task_error(ERR_INVALID_STATE, "offload requires the event loop");
    }
    LambdaOffload* job = (LambdaOffload*)mem_calloc(1, sizeof(LambdaOffload), MEM_CAT_EVAL);
    if (!job) return task_error(ERR_OUT_OF_MEMORY, "offload allocation failed");
    job->task = task;
    job->ops = ops;
    job->data = data;
    job->progress_ms = progress_ms;
    job->work.data = job;
    job->wake.data = job;
    if (uv_async_init(lambda_uv_loop(), &job->wake, [](uv_async_t* handle) {
            LambdaOffload* job = (LambdaOffload*)handle->data;
            if (!job || job->done) return;
            job->progress_pending = true;
            offload_wake_task(job);
        }) != 0) {
        mem_free(job);
        return task_error(ERR_INVALID_STATE, "offload wake handle failed");
    }
    // the work request keeps the loop alive; the wake handle must not outlast it
    uv_unref((uv_handle_t*)&job->wake);
    int status = uv_queue_work(lambda_uv_loop(), &job->work, [](uv_work_t* request) {
        LambdaOffload* job = (LambdaOffload*)request->data;
        job->ops->run(job, job->data);
    }, [](uv_work_t* request, int status) {
        LambdaOffload* job = (LambdaOffload*)request->data;
        job->done = true;
        if (status == UV_ECANCELED) __atomic_store_n(&job->cancel_flag, 1, __ATOMIC_RELEASE);
        offload_wake_task(job);
    });
    if (status < 0) {
        offload_release(job, true);
        return task_error(ERR_INVALID_STATE, uv_strerror(status));
    }
    task->offload = job;
    RootFrame roots(1);
    Rooted<Item> abort_error(roots, ItemNull);
    while (!job->done) {
        if (job->progress_pending && ops->progress && abort_error.get().item == ITEM_NULL &&
                !task_cancel_pending(task)) {
            job->progress_pending = false;
            // on the task's own stack: the callback may allocate, send, or park
            Item outcome = ops->progress(data, false);
            if (get_type_id(outcome) == LMD_TYPE_ERROR) {
                // the callback refused: stop the work and raise its error (AR-E3v2)
                abort_error.set(outcome);
                offload_cancel(job);
            }
            continue;
        }
        task->park_kind = LAMBDA_PARK_OFFLOAD;
        (void)lambda_task_suspend(task);
        if (task_cancel_pending(task)) offload_cancel(job);
    }
    task->offload = NULL;
    Item result = ItemNull;
    if (task_cancel_pending(task)) result = task_cancelled_error();
    else if (abort_error.get().item != ITEM_NULL) result = abort_error.get();
    else if (ops->progress) result = ops->progress(data, true);   // the final level
    if (get_type_id(result) != LMD_TYPE_ERROR) result = ItemNull;
    offload_release(job, true);
    return result;
}

// ---------------------------------------------------------------------------
// io.exec
// ---------------------------------------------------------------------------

#define PROCESS_LINE_CAP ((size_t)64 * 1024)

// Capture thread (AR-E5v2): keep the latest complete stdout line and ask for
// a tick. Bytes after the last newline wait in `partial` for the next chunk.
static void process_on_chunk(const char* data, size_t len, bool is_stderr, void* user_data) {
    LambdaProcess* process = (LambdaProcess*)user_data;
    if (is_stderr || !process->job || !len) return;
    const char* end = data + len;
    const char* last_nl = NULL;
    for (const char* p = end; p > data;) {
        if (*--p == '\n') { last_nl = p; break; }
    }
    if (last_nl) {
        const char* start = last_nl;
        while (start > data && start[-1] != '\n') start--;
        size_t tail = (size_t)(last_nl - start);
        size_t head = start == data ? process->partial_len : 0;   // the line began in an earlier chunk
        if (head + tail > PROCESS_LINE_CAP) { head = head > PROCESS_LINE_CAP ? PROCESS_LINE_CAP : head; tail = PROCESS_LINE_CAP - head; }
        char* line = (char*)mem_alloc(head + tail + 1, MEM_CAT_EVAL);
        if (line) {
            if (head) memcpy(line, process->partial, head);
            memcpy(line + head, start, tail);
            size_t total = head + tail;
            if (total && line[total - 1] == '\r') total--;
            line[total] = '\0';
            uv_mutex_lock(&process->lock);
            mem_free(process->line);
            process->line = line;
            process->line_len = total;
            process->line_fresh = true;
            uv_mutex_unlock(&process->lock);
        }
        process->partial_len = 0;
        data = last_nl + 1;
        len = (size_t)(end - data);
    }
    if (len && process->partial_len < PROCESS_LINE_CAP) {
        size_t keep = len < PROCESS_LINE_CAP - process->partial_len ? len : PROCESS_LINE_CAP - process->partial_len;
        if (process->partial_len + keep > process->partial_cap) {
            size_t cap = process->partial_cap ? process->partial_cap * 2 : 4096;
            while (cap < process->partial_len + keep) cap *= 2;
            char* grown = (char*)mem_realloc(process->partial, cap, MEM_CAT_EVAL);
            if (!grown) return;
            process->partial = grown;
            process->partial_cap = cap;
        }
        memcpy(process->partial + process->partial_len, data, keep);
        process->partial_len += keep;
    }
    if (last_nl) lambda_offload_notify(process->job);
}

static void process_run(LambdaOffload* job, void* data) {
    LambdaProcess* process = (LambdaProcess*)data;
    process->job = job;
    process->options.cancel_flag = lambda_offload_cancel_flag(job);
    process->result = shell_exec(process->args[0], (const char**)process->args, &process->options);
}

// Loop thread, on the task's stack: hand the callback `{line}`. At the end a
// trailing unterminated line counts as complete.
static Item process_progress(void* data, bool final) {
    LambdaProcess* process = (LambdaProcess*)data;
    if (!process->callback) return ItemNull;
    char* text = NULL;
    size_t length = 0;
    uv_mutex_lock(&process->lock);
    if (final && process->partial_len) {
        text = mem_dup_n(process->partial, process->partial_len, MEM_CAT_EVAL);
        length = process->partial_len;
        process->partial_len = 0;
        process->line_fresh = false;
    } else if (process->line_fresh) {
        text = mem_dup_n(process->line, process->line_len, MEM_CAT_EVAL);
        length = process->line_len;
        process->line_fresh = false;
    }
    uv_mutex_unlock(&process->lock);
    if (!text) return ItemNull;
    RootFrame roots(2);
    Rooted<Item> line(roots, ItemNull);
    Rooted<Map*> record(roots, (Map*)NULL);
    String* string = heap_strcpy(text, (int64_t)length);
    mem_free(text);
    if (!string) return ItemNull;
    line.set((Item){.item = s2it(string)});
    static const char* names[] = {"line"};
    static const TypeId types[] = {LMD_TYPE_STRING};
    record.set(map_alloc_for_type(runtime_result_shape(names, types, 1), NULL, 0));
    if (!record.get()) return ItemNull;
    Item values[] = {line.get()};
    map_fill_items(record.get(), values, 1);
    Item callback = process->callback->get();
    Item outcome = fn_call1((Function*)(uintptr_t)callback.item, (Item){.map = record.get()});
    return get_type_id(outcome) == LMD_TYPE_ERROR ? outcome : ItemNull;
}

static char* process_argument(Item value) {
    TypeId type = get_type_id(value);
    const char* chars = NULL;
    size_t length = 0;
    if (is_text_type_id(type)) {
        chars = value.get_chars();
        length = value.get_len();
    } else if (type == LMD_TYPE_BINARY) {
        Binary* bytes = value.get_safe_binary();
        chars = bytes ? (const char*)binary_data(bytes) : NULL;
        length = bytes ? binary_length(bytes) : 0;
    } else return NULL;
    if ((!chars && length) || (chars && memchr(chars, 0, length))) return NULL;
    return mem_dup_n(chars ? chars : "", length, MEM_CAT_EVAL);
}

static LambdaProcess* process_create(Item program, Item arguments, Item options, Item* error) {
    *error = ItemError;
    if (get_type_id(arguments) != LMD_TYPE_ARRAY || arguments.array->length > INT_MAX - 2) {
        *error = io_fs_error(ERR_TYPE_MISMATCH, "io.exec: args must be an array of text or byte arguments");
        return NULL;
    }
    IoFsOptionMap opts;
    if (!io_fs_options_open(options, "io.exec", SYSPROC_IO_EXEC, &opts, error)) return NULL;
    int64_t timeout = 30000, limit = SHELL_DEFAULT_MAX_OUTPUT_BYTES;
    if (!io_fs_option_count(&opts, "timeout_ms", &timeout, error) ||
            !io_fs_option_count(&opts, "max_output_bytes", &limit, error)) return NULL;
    if (timeout > INT_MAX || limit <= 0 || (uint64_t)limit > SIZE_MAX) {
        *error = io_fs_error(ERR_TYPE_MISMATCH, "io.exec: timeout_ms is too large or max_output_bytes is not positive");
        return NULL;
    }
    LambdaProcess* process = (LambdaProcess*)mem_calloc(1, sizeof(LambdaProcess), MEM_CAT_EVAL);
    if (!process) return NULL;
    process->result.exit_code = -1;
    process->arg_count = (int)arguments.array->length + 1;
    process->args = (char**)mem_calloc((size_t)process->arg_count + 1, sizeof(char*), MEM_CAT_EVAL);
    if (!process->args) { process_release(process); return NULL; }
    process->args[0] = process_argument(program);
    bool valid = process->args[0] && process->args[0][0];
    for (int i = 1; valid && i < process->arg_count; i++) {
        process->args[i] = process_argument(arguments.array->items[i - 1]);
        valid = process->args[i] != NULL;
    }
    Item cwd = io_fs_option_get(&opts, "cwd");
    if (get_type_id(cwd) != LMD_TYPE_NULL) {
        if (get_type_id(cwd) == LMD_TYPE_BINARY) process->cwd = process_argument(cwd);
        else {
            StrBuf* path = lambda_get_local_path_from_item(cwd);
            if (path) { process->cwd = mem_dup_n(path->str, path->length, MEM_CAT_EVAL); strbuf_free(path); }
        }
        valid = valid && process->cwd;
    }
    Item env = io_fs_option_get(&opts, "env");
    if (get_type_id(env) != LMD_TYPE_NULL) {
        if (get_type_id(env) != LMD_TYPE_MAP) valid = false;
        else {
            TypeMap* shape = lambda_attr_shape(LMD_TYPE_MAP, env.map);
            process->env_count = (int)fn_len(env);
            process->env = (ShellEnvEntry*)mem_calloc((size_t)process->env_count + 1,
                sizeof(ShellEnvEntry), MEM_CAT_EVAL);
            if (!process->env) valid = false;
            int index = 0;
            if (valid) FOR_EACH_MAP_FIELD(shape, field) {
                if (!field->name || index >= process->env_count) continue;
                char* key = mem_dup_n(field->name->str, field->name->length, MEM_CAT_EVAL);
                bool found = false;
                Item value = _map_get(shape, env.map->data, key, &found);
                char* text = get_type_id(value) == LMD_TYPE_NULL ? NULL : process_argument(value);
                if (!key || !key[0] || strchr(key, '=') ||
                        (get_type_id(value) != LMD_TYPE_NULL && !text)) valid = false;
                process->env[index++] = {key, text};
            }
        }
    }
    if (!valid) {
        *error = io_fs_error(ERR_TYPE_MISMATCH, "io.exec: invalid program, argument, cwd, or environment entry");
        process_release(process);
        return NULL;
    }
    process->options.cwd = process->cwd;
    process->options.env = process->env;
    process->options.timeout_ms = (int)timeout;
    process->options.max_output_bytes = (size_t)limit;
#ifdef _WIN32
    process->options.stdin_path = "NUL";
#else
    process->options.stdin_path = "/dev/null";
#endif
    // AR-E5v2: progress is a callback; the capture thread only splits lines
    process->progress_item = io_fs_option_get(&opts, "progress");
    process->progress_ms = 250;
    TypeId progress_type = get_type_id(process->progress_item);
    if ((progress_type != LMD_TYPE_NULL && progress_type != LMD_TYPE_FUNC) ||
            !io_fs_option_count(&opts, "progress_ms", &process->progress_ms, error)) {
        if (progress_type != LMD_TYPE_NULL && progress_type != LMD_TYPE_FUNC)
            *error = io_fs_error(ERR_TYPE_MISMATCH, "io.exec: option 'progress' must be a function");
        process_release(process);
        return NULL;
    }
    if (progress_type == LMD_TYPE_FUNC) {
        if (uv_mutex_init(&process->lock) != 0) {
            *error = io_fs_error(ERR_INVALID_STATE, "io.exec: progress lock allocation failed");
            process_release(process);
            return NULL;
        }
        process->lock_ready = true;
        process->options.on_chunk = process_on_chunk;
        process->options.user_data = process;
    }
    return process;
}

static Item process_result(LambdaProcess* process) {
    RootFrame roots(3);
    Rooted<Item> output(roots, ItemNull), errors(roots, ItemNull);
    Rooted<Map*> result(roots, (Map*)NULL);
    // S2.2.2v2: an empty captured byte stream is absent binary, represented by null.
    if (process->result.stdout_len) output.set((Item){.item = x2it(heap_binary_from_bytes(
        process->result.stdout_buf, process->result.stdout_len))});
    if (process->result.stderr_len) errors.set((Item){.item = x2it(heap_binary_from_bytes(
        process->result.stderr_buf, process->result.stderr_len))});
    static const char* names[] = {"stdout", "stderr", "exit_code", "timed_out", "cancelled", "truncated"};
    static const TypeId types[] = {LMD_TYPE_ANY, LMD_TYPE_ANY, LMD_TYPE_INT,
        LMD_TYPE_BOOL, LMD_TYPE_BOOL, LMD_TYPE_BOOL};
    result.set(map_alloc_for_type(runtime_result_shape(names, types, 6), NULL, 0));
    Item values[] = {output.get(), errors.get(), (Item){.item = i2it(process->result.exit_code)},
        (Item){.item = b2it(process->result.timed_out)}, (Item){.item = b2it(process->result.cancelled)},
        (Item){.item = b2it(process->result.output_limit_exceeded)}};
    map_fill_items(result.get(), values, 6);
    return (Item){.map = result.get()};
}

extern "C" Item pn_io_exec3(Item program, Item arguments, Item options) {
    GUARD_ERROR3(program, arguments, options);
    if (g_dry_run) {
        LambdaProcess fabricated = {};
        return process_result(&fabricated);
    }
    RootFrame roots(4);
    Rooted<Item> rooted_program(roots, program), rooted_arguments(roots, arguments), rooted_options(roots, options);
    Rooted<Item> rooted_callback(roots, ItemNull);
    LambdaTask* task = lambda_current_task();
    if (!task) return task_error(ERR_INVALID_OPERATION, "io.exec requires a running procedure");
    if (task_cancel_pending(task)) return task_cancelled_error();
    Item error = ItemError;
    LambdaProcess* process = process_create(program, arguments, options, &error);
    if (!process) return error;
    if (get_type_id(process->progress_item) == LMD_TYPE_FUNC) {
        // the callback is read through this root at each call: the heap may move it while the task parks
        rooted_callback.set(process->progress_item);
        process->callback = &rooted_callback;
    }
    static const LambdaOffloadOps ops = {process_run, process_progress, process_release_orphan};
    Item outcome = lambda_task_offload(&ops, process, process->progress_ms);
    // cancellation waits for the worker/pipe owners before releasing their request memory.
    Item result = get_type_id(outcome) == LMD_TYPE_ERROR ? outcome : process_result(process);
    process_release(process);
    return result;
}

extern "C" Item pn_io_exec2(Item program, Item arguments) {
    return pn_io_exec3(program, arguments, ItemNull);
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
// Item struct is not stable across MIR's generated-call boundary.
