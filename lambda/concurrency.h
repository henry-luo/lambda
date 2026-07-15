#pragma once

#include "lambda.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LAMBDA_MAILBOX_DEFAULT_CAPACITY 1024

typedef struct LambdaScheduler LambdaScheduler;
typedef struct LambdaTask LambdaTask;
typedef struct LambdaAsyncFrame LambdaAsyncFrame;
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

typedef enum LambdaTaskPoll {
    LAMBDA_TASK_POLL_READY = 0,
    LAMBDA_TASK_POLL_PARKED,
    LAMBDA_TASK_POLL_DONE,
} LambdaTaskPoll;

typedef enum LambdaSendStatus {
    LAMBDA_SEND_OK = 0,
    LAMBDA_SEND_FULL,
    LAMBDA_SEND_CLOSED,
    LAMBDA_SEND_INVALID_HANDLE,
} LambdaSendStatus;

typedef LambdaTaskPoll (*LambdaTaskResumeFn)(LambdaTask* task, void* frame, Item* out);
typedef void (*LambdaTaskFrameDestroyFn)(void* frame);

LambdaScheduler* lambda_scheduler_create(int mailbox_capacity);
void lambda_scheduler_destroy(LambdaScheduler* scheduler);
int lambda_scheduler_run_one(LambdaScheduler* scheduler);
int lambda_scheduler_run_ready(LambdaScheduler* scheduler);
int lambda_scheduler_drain(LambdaScheduler* scheduler);
int lambda_scheduler_live_count(const LambdaScheduler* scheduler);
LambdaTask* lambda_scheduler_current(LambdaScheduler* scheduler);

LambdaTask* lambda_task_create(LambdaScheduler* scheduler, LambdaTaskResumeFn resume,
    void* frame, LambdaTaskFrameDestroyFn destroy_frame);
void lambda_task_set_frame_roots(LambdaTask* task, Item* roots, int count);
Item lambda_task_handle(LambdaTask* task);
bool lambda_task_handle_is(Item item);
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
bool lambda_task_take_resume_value(LambdaTask* task, Item* out);
bool lambda_task_on_complete(LambdaTask* task, LambdaTaskCompletionFn callback,
    void* data, LambdaTaskObserverDestroyFn destroy_data);
void lambda_concurrency_set_promise_bridge(LambdaPromiseIsFn is_promise,
    LambdaPromiseWaitFn wait_promise, LambdaHandleToPromiseFn handle_to_promise);

LambdaAsyncFrame* lambda_async_frame_enter_current(void);
int lambda_async_frame_state(LambdaAsyncFrame* frame);
void lambda_async_frame_set_state(LambdaAsyncFrame* frame, int state);
Item lambda_async_frame_get(LambdaAsyncFrame* frame, int slot);
void lambda_async_frame_set(LambdaAsyncFrame* frame, int slot, Item value);
void lambda_async_frame_complete(LambdaAsyncFrame* frame);
LambdaTaskScope* lambda_async_frame_scope_base(LambdaAsyncFrame* frame);
int lambda_task_has_current(void);

Item lambda_task_start_function(Item function, List* args);
Item lambda_task_start_function_scoped(Item function, List* args, bool escapes);
Item lambda_task_run_root_raw(void* function_ptr, void* env, int env_count, List* args);
LambdaTaskScope* lambda_task_scope_enter(void);
LambdaTaskScope* lambda_task_scope_current(void);
Item lambda_task_scope_leave(LambdaTaskScope* scope, bool error_exit);
Item lambda_task_scope_unwind(LambdaTaskScope* base, bool error_exit);

RetItem pn_send(Item handle, Item message);
RetItem pn_receive(void);
RetItem pn_wait1(Item handle);
RetItem pn_wait2(Item handle, Item timeout_ms);
RetItem pn_select(Item handles, Item timeout_ms);
RetItem pn_sleep(Item duration_ms);
RetItem pn_io_read(Item target);
Item pn_self(void);
Item pn_cancel(Item handle);
Item fn_to_promise(Item handle);

Item pn_send_mir(Item handle, Item message);
Item pn_receive_mir(void);
Item pn_wait1_mir(Item handle);
Item pn_wait2_mir(Item handle, Item timeout_ms);
Item pn_select_mir(Item handles, Item timeout_ms);
Item pn_sleep_mir(Item duration_ms);
Item pn_io_read_mir(Item target);

#ifdef __cplusplus
}
#endif
