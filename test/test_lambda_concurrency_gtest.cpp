#include <gtest/gtest.h>

#include "../lambda/concurrency.h"
#include "../lambda/lambda.hpp"
#include "../lambda/lambda-error.h"
#include "../lambda/transpiler.hpp"
#include "../lambda/runtime/gc/gc_heap.h"
#include "../lib/uv_loop.h"

extern __thread EvalContext* context;

// The isolated scheduler suite must satisfy the current result-home call ABI;
// otherwise the linker pulls the full evaluator into this white-box target.
extern "C" Item lambda_concurrency_fn_call_into(Function* fn, List* args,
    uint64_t* result_home) {
    (void)fn;
    (void)args;
    (void)result_home;
    return ItemError;
}

extern "C" Function* lambda_concurrency_to_closure(
    fn_ptr ptr, int arity, void* env) {
    (void)ptr;
    (void)arity;
    (void)env;
    return NULL;
}

// Async file I/O is exercised through the Lambda integration suite; these
// host helpers only satisfy the isolated scheduler target's link boundary.
extern "C" StrBuf* lambda_get_local_path_from_item(Item item) {
    (void)item;
    return NULL;
}

extern "C" String* heap_strcpy(const char* src, int64_t len) {
    (void)src;
    (void)len;
    return NULL;
}

static gc_heap_t* concurrency_test_gc;

extern "C" void* heap_calloc(size_t size, TypeId type_id) {
    return concurrency_test_gc
        ? gc_heap_calloc(concurrency_test_gc, size, (uint16_t)type_id) : NULL;
}

void* heap_alloc(int size, TypeId type_id) {
    return concurrency_test_gc
        ? gc_heap_alloc(concurrency_test_gc, (size_t)size, (uint16_t)type_id) : NULL;
}

extern "C" void heap_register_gc_root(uint64_t* slot) {
    if (concurrency_test_gc && slot) gc_register_root(concurrency_test_gc, slot);
}

extern "C" void heap_unregister_gc_root(uint64_t* slot) {
    if (concurrency_test_gc && slot) gc_unregister_root(concurrency_test_gc, slot);
}

extern "C" void heap_register_gc_root_range(uint64_t* base, int count) {
    if (concurrency_test_gc && base && count > 0) {
        gc_register_root_range(concurrency_test_gc, base, count);
    }
}

extern "C" void heap_unregister_gc_root_range(uint64_t* base) {
    if (concurrency_test_gc && base) gc_unregister_root_range(concurrency_test_gc, base);
}

typedef struct RecordFrame {
    int id;
    int* order;
    int* order_count;
    Item result;
    LambdaTask* send_target;
    Item send_value;
} RecordFrame;

static LambdaTaskPoll record_and_complete(
    LambdaTask* task, void* data, Item* out) {
    RecordFrame* frame = (RecordFrame*)data;
    frame->order[(*frame->order_count)++] = frame->id;
    if (frame->send_target) {
        EXPECT_EQ(lambda_task_send(task, frame->send_target, frame->send_value),
            LAMBDA_SEND_OK);
    }
    *out = frame->result;
    return LAMBDA_TASK_POLL_DONE;
}

typedef struct ParkFrame {
    int runs;
    Item resumed;
} ParkFrame;

typedef struct WaitFrame {
    Item handle;
    Item timeout;
} WaitFrame;

static LambdaTaskPoll park_then_finish(LambdaTask* task, void* data, Item* out) {
    ParkFrame* frame = (ParkFrame*)data;
    frame->runs++;
    if (frame->runs == 1) {
        lambda_task_park(task);
        return LAMBDA_TASK_POLL_PARKED;
    }
    if (!lambda_task_take_resume_value(task, &frame->resumed)) {
        frame->resumed = ItemNull;
    }
    *out = frame->resumed;
    return LAMBDA_TASK_POLL_DONE;
}

static LambdaTaskPoll wait_with_timeout(LambdaTask* task, void* data, Item* out) {
    (void)task;
    WaitFrame* frame = (WaitFrame*)data;
    RetItem result = pn_wait2(frame->handle, frame->timeout);
    if (result.value.item == ITEM_TASK_SUSPENDED) {
        return LAMBDA_TASK_POLL_PARKED;
    }
    *out = ri_to_item(result);
    return LAMBDA_TASK_POLL_DONE;
}

class LambdaConcurrencyRuntime : public ::testing::Test {
protected:
    EvalContext eval = {};
    Heap heap = {};
    LambdaScheduler* scheduler = NULL;

    void SetUp() override {
        concurrency_test_gc = gc_heap_create();
        ASSERT_NE(concurrency_test_gc, nullptr);
        heap.gc = concurrency_test_gc;
        heap.pool = concurrency_test_gc->pool;
        eval.heap = &heap;
        context = &eval;
        err_set_heap_allocator(heap_calloc);
        scheduler = lambda_scheduler_create(3);
        ASSERT_NE(scheduler, nullptr);
        eval.scheduler = scheduler;
    }

    void TearDown() override {
        lambda_scheduler_destroy(scheduler);
        scheduler = NULL;
        eval.scheduler = NULL;
        lambda_uv_cleanup();
        err_set_heap_allocator(NULL);
        context = NULL;
        gc_heap_destroy(concurrency_test_gc);
        concurrency_test_gc = NULL;
    }
};

TEST_F(LambdaConcurrencyRuntime, SchedulerRunsRunnableTasksInFifoOrder) {
    int order[3] = {};
    int count = 0;
    RecordFrame frames[3] = {
        {1, order, &count, {.item = i2it(11)}, NULL, ItemNull},
        {2, order, &count, {.item = i2it(22)}, NULL, ItemNull},
        {3, order, &count, {.item = i2it(33)}, NULL, ItemNull},
    };
    LambdaTask* first = lambda_task_create(scheduler, record_and_complete, &frames[0], NULL);
    LambdaTask* second = lambda_task_create(scheduler, record_and_complete, &frames[1], NULL);
    LambdaTask* third = lambda_task_create(scheduler, record_and_complete, &frames[2], NULL);

    ASSERT_NE(first, nullptr);
    ASSERT_NE(second, nullptr);
    ASSERT_NE(third, nullptr);
    EXPECT_EQ(lambda_scheduler_run_ready(scheduler), 3);
    ASSERT_EQ(count, 3);
    EXPECT_EQ(order[0], 1);
    EXPECT_EQ(order[1], 2);
    EXPECT_EQ(order[2], 3);
    EXPECT_EQ(lambda_scheduler_live_count(scheduler), 0);
}

TEST_F(LambdaConcurrencyRuntime, OwnedSlotsPreserveWideIntegersWithoutGcScalarCells) {
    int64_t source_i = INT64_MAX;
    uint64_t source_u = UINT64_MAX;
    uint64_t alloc_i = gc_scalar_tag_allocation_count(LMD_TYPE_INT64);
    uint64_t alloc_u = gc_scalar_tag_allocation_count(LMD_TYPE_UINT64);
    uint64_t alloc_f = gc_scalar_tag_allocation_count(LMD_TYPE_FLOAT);
    uint64_t alloc_f64 = gc_scalar_tag_allocation_count(LMD_TYPE_FLOAT64);
    Item slots[4] = {};

    EXPECT_FALSE(gc_is_managed(concurrency_test_gc, &source_i));
    EXPECT_FALSE(gc_is_managed(concurrency_test_gc, &source_u));

    owned_item_slot_store(slots, 2, 0, (Item){.item = l2it(&source_i)});
    owned_item_slot_store(slots, 2, 1, (Item){.item = u2it(&source_u)});
    source_i = 0;
    source_u = 0;

    ASSERT_EQ(get_type_id(slots[0]), LMD_TYPE_INT64);
    ASSERT_EQ(get_type_id(slots[1]), LMD_TYPE_UINT64);
    EXPECT_FALSE(gc_is_managed(concurrency_test_gc,
        (void*)(uintptr_t)slots[0].int64_ptr));
    EXPECT_FALSE(gc_is_managed(concurrency_test_gc,
        (void*)(uintptr_t)slots[1].uint64_ptr));
    EXPECT_EQ(gc_scalar_tag_allocation_count(LMD_TYPE_INT64), alloc_i);
    EXPECT_EQ(gc_scalar_tag_allocation_count(LMD_TYPE_UINT64), alloc_u);
    EXPECT_EQ(gc_scalar_tag_allocation_count(LMD_TYPE_FLOAT), alloc_f);
    EXPECT_EQ(gc_scalar_tag_allocation_count(LMD_TYPE_FLOAT64), alloc_f64);

    gc_register_root(concurrency_test_gc, &slots[0].item);
    gc_register_root(concurrency_test_gc, &slots[1].item);
    gc_set_poison_freed(concurrency_test_gc, 1);
    gc_collect(concurrency_test_gc, NULL, 0);
    Item owned_i = owned_item_slot_read(slots, 2, 0, false);
    Item owned_u = owned_item_slot_read(slots, 2, 1, false);
    EXPECT_EQ(owned_i.get_int64(), INT64_MAX);
    EXPECT_EQ(owned_u.get_uint64(), UINT64_MAX);
    gc_unregister_root(concurrency_test_gc, &slots[0].item);
    gc_unregister_root(concurrency_test_gc, &slots[1].item);
}

TEST_F(LambdaConcurrencyRuntime, GcRejectsEveryScalarObjectAllocationRoute) {
    EXPECT_DEATH({
        gc_heap_alloc(concurrency_test_gc, sizeof(int64_t), LMD_TYPE_INT64);
    }, "gc-scalar-invariant");
    EXPECT_DEATH({
        gc_heap_calloc_class(concurrency_test_gc, sizeof(uint64_t),
            LMD_TYPE_UINT64, 0);
    }, "gc-scalar-invariant");
    EXPECT_DEATH({
        gc_heap_bump_alloc(concurrency_test_gc, sizeof(uint64_t) * 2,
            sizeof(double), LMD_TYPE_FLOAT, 0);
    }, "gc-scalar-invariant");
}

TEST_F(LambdaConcurrencyRuntime, MailboxIsBoundedAndDequeuesOnlyFromFifoHead) {
    ParkFrame target_frame = {};
    LambdaTask* target = lambda_task_create(scheduler, park_then_finish, &target_frame, NULL);
    ASSERT_NE(target, nullptr);

    EXPECT_EQ(lambda_task_mailbox_capacity(target), 3);
    EXPECT_EQ(lambda_task_send(NULL, target, (Item){.item = i2it(10)}), LAMBDA_SEND_OK);
    EXPECT_EQ(lambda_task_send(NULL, target, (Item){.item = i2it(20)}), LAMBDA_SEND_OK);
    EXPECT_EQ(lambda_task_send(NULL, target, (Item){.item = i2it(30)}), LAMBDA_SEND_OK);
    EXPECT_EQ(lambda_task_send(NULL, target, (Item){.item = i2it(40)}), LAMBDA_SEND_FULL);

    Item value = ItemNull;
    ASSERT_TRUE(lambda_task_mailbox_receive(target, &value));
    EXPECT_EQ(it2i(value), 10);
    ASSERT_TRUE(lambda_task_mailbox_receive(target, &value));
    EXPECT_EQ(it2i(value), 20);
    ASSERT_TRUE(lambda_task_mailbox_receive(target, &value));
    EXPECT_EQ(it2i(value), 30);
    EXPECT_FALSE(lambda_task_mailbox_receive(target, &value));
}

TEST_F(LambdaConcurrencyRuntime, TaskPersistentSlotsOwnWideIntegerPayloads) {
    int order[1] = {};
    int count = 0;
    int64_t result_source = INT64_MAX;
    RecordFrame result_frame = {
        1, order, &count, {.item = l2it(&result_source)}, NULL, ItemNull};
    LambdaTask* completed = lambda_task_create(
        scheduler, record_and_complete, &result_frame, NULL);
    ASSERT_NE(completed, nullptr);
    ASSERT_EQ(lambda_scheduler_run_one(scheduler), 1);
    result_source = 0;

    ParkFrame parked_frame = {};
    LambdaTask* parked = lambda_task_create(
        scheduler, park_then_finish, &parked_frame, NULL);
    ASSERT_NE(parked, nullptr);
    ASSERT_EQ(lambda_scheduler_run_one(scheduler), 1);
    uint64_t message_source = UINT64_MAX;
    ASSERT_EQ(lambda_task_send(NULL, parked,
        (Item){.item = u2it(&message_source)}), LAMBDA_SEND_OK);
    message_source = 0;

    gc_set_poison_freed(concurrency_test_gc, 1);
    gc_collect(concurrency_test_gc, NULL, 0);

    EXPECT_EQ(lambda_task_result(completed).get_int64(), INT64_MAX);
    Item message = ItemNull;
    ASSERT_TRUE(lambda_task_mailbox_receive(parked, &message));
    EXPECT_EQ(message.get_uint64(), UINT64_MAX);

    uint64_t resume_source = UINT64_MAX - 1;
    lambda_task_resume_external(parked,
        (Item){.item = u2it(&resume_source)});
    resume_source = 0;
    ASSERT_EQ(lambda_scheduler_run_one(scheduler), 1);
    EXPECT_EQ(lambda_task_result(parked).get_uint64(), UINT64_MAX - 1);
}

TEST_F(LambdaConcurrencyRuntime, CompletionIsPublishedAfterFinalSend) {
    int order[1] = {};
    int count = 0;
    RecordFrame sender_frame = {
        7, order, &count, {.item = i2it(99)}, NULL, {.item = i2it(42)}};
    LambdaTask* sender = lambda_task_create(
        scheduler, record_and_complete, &sender_frame, NULL);
    ASSERT_NE(sender, nullptr);
    LambdaTask* receiver = lambda_task_create(scheduler, NULL, NULL, NULL);
    ASSERT_NE(receiver, nullptr);
    sender_frame.send_target = receiver;
    ASSERT_EQ(lambda_scheduler_run_one(scheduler), 1);

    EXPECT_GT(lambda_task_last_send_sequence(sender), 0u);
    EXPECT_GT(lambda_task_completion_sequence(sender),
        lambda_task_last_send_sequence(sender));
    Item received = ItemNull;
    ASSERT_TRUE(lambda_task_mailbox_receive(receiver, &received));
    EXPECT_EQ(it2i(received), 42);
}

TEST_F(LambdaConcurrencyRuntime, CancellationUnparksAndIsIdempotent) {
    ParkFrame frame = {};
    LambdaTask* task = lambda_task_create(scheduler, park_then_finish, &frame, NULL);
    ASSERT_NE(task, nullptr);
    ASSERT_EQ(lambda_scheduler_run_one(scheduler), 1);
    ASSERT_EQ(lambda_task_state(task), LAMBDA_TASK_PARKED);

    EXPECT_TRUE(lambda_task_cancel(task));
    EXPECT_TRUE(lambda_task_cancel(task));
    EXPECT_TRUE(lambda_task_cancel_requested(task));
    EXPECT_EQ(lambda_task_state(task), LAMBDA_TASK_RUNNABLE);
    ASSERT_EQ(lambda_scheduler_run_one(scheduler), 1);
    EXPECT_EQ(lambda_task_state(task), LAMBDA_TASK_DONE);
    EXPECT_EQ(get_type_id(lambda_task_result(task)), LMD_TYPE_ERROR);
    EXPECT_TRUE(lambda_task_cancel(task));
}

TEST_F(LambdaConcurrencyRuntime, CleanupMaskDefersCancellationUntilUnmasked) {
    ParkFrame frame = {};
    LambdaTask* task = lambda_task_create(scheduler, park_then_finish, &frame, NULL);
    ASSERT_NE(task, nullptr);
    ASSERT_EQ(lambda_scheduler_run_one(scheduler), 1);
    ASSERT_EQ(lambda_task_state(task), LAMBDA_TASK_PARKED);

    lambda_task_set_cleanup_masked(task, true);
    EXPECT_TRUE(lambda_task_cancel(task));
    EXPECT_EQ(lambda_task_state(task), LAMBDA_TASK_PARKED);
    lambda_task_set_cleanup_masked(task, false);
    EXPECT_EQ(lambda_task_state(task), LAMBDA_TASK_RUNNABLE);
    ASSERT_EQ(lambda_scheduler_run_one(scheduler), 1);
    EXPECT_EQ(get_type_id(lambda_task_result(task)), LMD_TYPE_ERROR);
}

TEST_F(LambdaConcurrencyRuntime, WaitTimeoutDoesNotCancelObservedTask) {
    ParkFrame target_frame = {};
    LambdaTask* target = lambda_task_create(scheduler, park_then_finish, &target_frame, NULL);
    ASSERT_NE(target, nullptr);
    ASSERT_EQ(lambda_scheduler_run_one(scheduler), 1);
    ASSERT_EQ(lambda_task_state(target), LAMBDA_TASK_PARKED);

    WaitFrame waiter_frame = {
        lambda_task_handle(target), (Item){.item = i2it(1)}};
    LambdaTask* waiter = lambda_task_create(scheduler, wait_with_timeout, &waiter_frame, NULL);
    ASSERT_NE(waiter, nullptr);
    ASSERT_EQ(lambda_scheduler_run_one(scheduler), 1);
    ASSERT_EQ(lambda_task_state(waiter), LAMBDA_TASK_PARKED);

    ASSERT_NE(lambda_uv_loop(), nullptr);
    // A previously queued async wake may satisfy UV_RUN_ONCE before the 1 ms
    // timeout; keep polling until the timeout resumes and runs the waiter.
    for (int i = 0; i < 16 && lambda_task_state(waiter) != LAMBDA_TASK_DONE; i++) {
        uv_run(lambda_uv_loop(), UV_RUN_ONCE);
        lambda_scheduler_run_ready(scheduler);
    }
    EXPECT_EQ(lambda_task_state(waiter), LAMBDA_TASK_DONE);
    ASSERT_EQ(get_type_id(lambda_task_result(waiter)), LMD_TYPE_ERROR);
    LambdaError* error = it2err(lambda_task_result(waiter));
    ASSERT_NE(error, nullptr);
    EXPECT_EQ(error->code, ERR_TIMEOUT);
    EXPECT_EQ(lambda_task_state(target), LAMBDA_TASK_PARKED);
    EXPECT_FALSE(lambda_task_cancel_requested(target));
}

TEST_F(LambdaConcurrencyRuntime, SchedulerOwnsEveryTaskGcEdge) {
    int initial_slots = concurrency_test_gc->root_slot_count;
    int initial_ranges = concurrency_test_gc->root_range_count;
    Item frame_roots[2] = {ItemNull, ItemNull};
    ParkFrame frame = {};
    LambdaTask* task = lambda_task_create(scheduler, park_then_finish, &frame, NULL);
    ASSERT_NE(task, nullptr);
    lambda_task_set_frame_roots(task, frame_roots, 2);

    EXPECT_EQ(concurrency_test_gc->root_slot_count, initial_slots + 3);
    EXPECT_EQ(concurrency_test_gc->root_range_count, initial_ranges + 2);

    lambda_scheduler_destroy(scheduler);
    scheduler = NULL;
    eval.scheduler = NULL;
    EXPECT_EQ(concurrency_test_gc->root_slot_count, initial_slots);
    EXPECT_EQ(concurrency_test_gc->root_range_count, initial_ranges);
}

TEST_F(LambdaConcurrencyRuntime, ParkedFramesAndMailboxesSurviveCollection) {
    ParkFrame frame = {};
    LambdaTask* task = lambda_task_create(scheduler, park_then_finish, &frame, NULL);
    ASSERT_NE(task, nullptr);
    ASSERT_EQ(lambda_scheduler_run_one(scheduler), 1);

    VMap* frame_value = (VMap*)heap_calloc(sizeof(VMap), LMD_TYPE_VMAP);
    VMap* message_value = (VMap*)heap_calloc(sizeof(VMap), LMD_TYPE_VMAP);
    ASSERT_NE(frame_value, nullptr);
    ASSERT_NE(message_value, nullptr);
    Item frame_roots[1] = {{.vmap = frame_value}};
    lambda_task_set_frame_roots(task, frame_roots, 1);
    ASSERT_EQ(lambda_task_send(NULL, task, (Item){.vmap = message_value}), LAMBDA_SEND_OK);

    gc_collect(concurrency_test_gc, NULL, 0);

    EXPECT_TRUE(gc_is_managed(concurrency_test_gc, frame_roots[0].vmap));
    Item message = ItemNull;
    ASSERT_TRUE(lambda_task_mailbox_receive(task, &message));
    EXPECT_EQ(message.vmap, message_value);
    EXPECT_TRUE(gc_is_managed(concurrency_test_gc, message.vmap));
}
