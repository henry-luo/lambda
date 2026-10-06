#pragma once

#include "../lambda.h"
#include "activation.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LAMBDA_MAILBOX_DEFAULT_CAPACITY 1024

typedef struct LambdaScheduler LambdaScheduler;
typedef struct LambdaTask LambdaTask;
typedef struct LambdaTaskScope LambdaTaskScope;
typedef void (*LambdaTaskCompletionFn)(LambdaTask* task, Item result, void* data);
typedef void (*LambdaTaskObserverDestroyFn)(void* data);
typedef bool (*LambdaPromiseIsFn)(Item value);
typedef Item (*LambdaPromiseWaitFn)(Item promise, LambdaTask* waiter);
typedef Item (*LambdaHandleToPromiseFn)(Item handle);

typedef enum LambdaTaskState {
    LAMBDA_TASK_RUNNABLE = 0,
    LAMBDA_TASK_PARKED,
    LAMBDA_TASK_DONE,
} LambdaTaskState;

typedef enum LambdaSendStatus {
    LAMBDA_SEND_OK = 0,
    LAMBDA_SEND_FULL,
    LAMBDA_SEND_CLOSED,
    LAMBDA_SEND_INVALID_HANDLE,
} LambdaSendStatus;

LambdaScheduler* lambda_scheduler_create(int mailbox_capacity);
void lambda_scheduler_destroy(LambdaScheduler* scheduler);
int lambda_scheduler_run_one(LambdaScheduler* scheduler);
int lambda_scheduler_run_ready(LambdaScheduler* scheduler);
int lambda_scheduler_drain(LambdaScheduler* scheduler);
int lambda_scheduler_live_count(const LambdaScheduler* scheduler);
// JSCU25: a parked JS async activation registers weakly with the attached
// scheduler. It counts as live while it waits and leaves when it settles or
// its carrier is collected; it keeps a drain going only while the loop could
// still settle it. Enter returns a token (0 without a scheduler); leave
// ignores a token whose scheduler is gone.
uint64_t lambda_scheduler_weak_enter(void);
void lambda_scheduler_weak_leave(uint64_t token);
LambdaTask* lambda_scheduler_current(LambdaScheduler* scheduler);

// A task runs `entry(arg)` on its own activation; it is runnable at once.
LambdaTask* lambda_task_create(LambdaScheduler* scheduler, ActivationEntry entry,
    Item arg);
Item lambda_task_handle(LambdaTask* task);
#ifdef LAMBDA_NO_TASKS
// no task capability objects can be constructed in a synchronous profile.
static inline bool lambda_task_handle_is(Item item) { (void)item; return false; }
#else
bool lambda_task_handle_is(Item item);
#endif
LambdaTask* lambda_task_from_handle(Item item);
LambdaTaskState lambda_task_state(const LambdaTask* task);
Item lambda_task_result(const LambdaTask* task);
bool lambda_task_cancel_requested(const LambdaTask* task);
void lambda_task_set_cleanup_masked(LambdaTask* task, bool masked);

LambdaSendStatus lambda_task_send(LambdaTask* sender, LambdaTask* target, Item message);
bool lambda_task_mailbox_receive(LambdaTask* task, Item* out);
int lambda_task_mailbox_count(const LambdaTask* task);
int lambda_task_mailbox_capacity(const LambdaTask* task);
uint64_t lambda_task_last_send_sequence(const LambdaTask* task);
uint64_t lambda_task_completion_sequence(const LambdaTask* task);

void lambda_task_park(LambdaTask* task);
void lambda_task_resume(LambdaTask* task);
void lambda_task_resume_external(LambdaTask* task, Item result);
void lambda_task_complete(LambdaTask* task, Item result);
bool lambda_task_cancel(LambdaTask* task);
// The one park point: park the running task (its wakeup is already
// registered) and return the value it is resumed with.
Item lambda_task_suspend(LambdaTask* task);
bool lambda_task_on_complete(LambdaTask* task, LambdaTaskCompletionFn callback,
    void* data, LambdaTaskObserverDestroyFn destroy_data);
void lambda_concurrency_set_promise_bridge(LambdaPromiseIsFn is_promise,
    LambdaPromiseWaitFn wait_promise, LambdaHandleToPromiseFn handle_to_promise);

Item lambda_task_start_function(Item function, List* args);
Item lambda_task_start_function_scoped(Item function, List* args, bool escapes);
LambdaTaskScope* lambda_task_scope_enter(void);
LambdaTaskScope* lambda_task_scope_current(void);
Item lambda_task_scope_leave(LambdaTaskScope* scope, bool error_exit);
Item lambda_task_scope_unwind(LambdaTaskScope* base, bool error_exit);

Item pn_send(Item handle, Item message);
Item pn_receive(void);
Item pn_wait1(Item handle);
Item pn_wait2(Item handle, Item timeout_ms);
Item pn_select(Item handles, Item timeout_ms);
Item pn_sleep(Item duration_ms);
Item pn_io_read(Item target);
Item pn_self(void);
Item pn_cancel(Item handle);
Item fn_to_promise(Item handle);


#ifdef __cplusplus
}
#endif
