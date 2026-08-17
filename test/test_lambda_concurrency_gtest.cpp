#include <gtest/gtest.h>

#ifndef _WIN32
#include <pthread.h>
#include <time.h>
#endif

#include "../lambda/runtime/concurrency.h"
#include "../lambda/lambda.hpp"
#include "../lambda/runtime/lambda-error.h"
#include "../lambda/runtime/recovery_frame.h"
#include "../lambda/runtime/lambda-stack.h"
#include "../lambda/runtime/runtime-state.h"
#include "../lambda/runtime/transpiler.hpp"
#include "../lambda/runtime/gc/gc_heap.h"
#include "../lambda/js/js_runtime.h"
#include "../lambda/js/js_runtime_state.hpp"
#include "../lambda/jube/jube_interface.h"
#include "../lambda/jube/jube_registry.h"
#include "../lambda/core/lambda-decimal.hpp"
#include "../lambda/core/name_pool.hpp"
#include "../lambda/input/input.hpp"
#include "../lib/mem_factory.h"
#include "../lib/mempool.h"
#include "../lib/uv_loop.h"
#include "../lib/url.h"

extern __thread EvalContext* context;

extern "C" Item vmap_new(void);
extern const JubeTypeBinding radiant_dom_type_bindings[];
extern const int32_t radiant_dom_type_binding_count;

#ifndef _WIN32
extern __thread Context* input_context;
#endif

struct CallableRealmSnapshot {
    Item math_abs = ItemNull;
    Item array_constructor = ItemNull;
    bool stable_lookup = false;
    bool iterator_alias = false;
};

TEST(JubeDom4, OrdinalParityCoversDeclaredMembersAndHusks) {
    Runtime runtime = {};
    runtime_init(&runtime);
    uint64_t result_home = 0;
    Item warmup = transpile_js_to_mir(&runtime, "0;", "<dom4-parity>", &result_home);
    ASSERT_FALSE(item_is_error(warmup));

    const JubeModuleDef* radiant = jube_find_static_module("radiant");
    ASSERT_NE(radiant, nullptr);
    ASSERT_TRUE(jube_activate_module(radiant));

    for (int type_index = 0; type_index < radiant_dom_type_binding_count; type_index++) {
        const JubeTypeBinding* binding = &radiant_dom_type_bindings[type_index];
        const JubeTypeDef* type = jube_iface_type_by_name(
            binding->type_name, (uint32_t)strlen(binding->type_name));
        ASSERT_NE(type, nullptr) << binding->type_name;
        int slot = jube_iface_type_slot(type);
        ASSERT_GE(slot, 0) << binding->type_name;
        int member_count = jube_member_count(type);
        ASSERT_GE(member_count, 0) << binding->type_name;
        Item husk = vmap_new();
        ASSERT_EQ(get_type_id(husk), LMD_TYPE_VMAP);
        husk.vmap->host_type = type;
        husk.vmap->host_data = nullptr;

        for (int ordinal = 0; ordinal < member_count; ordinal++) {
            const char* snake_name = jube_member_name_at(type, ordinal, false);
            const char* camel_name = jube_member_name_at(type, ordinal, true);
            ASSERT_NE(snake_name, nullptr) << binding->type_name << " ordinal " << ordinal;
            ASSERT_NE(camel_name, nullptr) << binding->type_name << " ordinal " << ordinal;
            ASSERT_EQ(jube_member_ordinal(type, snake_name,
                (uint32_t)strlen(snake_name)), ordinal) << binding->type_name;
            ASSERT_EQ(jube_member_ordinal(type, camel_name,
                (uint32_t)strlen(camel_name)), ordinal) << binding->type_name;
            Item key = js_make_string(snake_name);
            Item camel_key = js_make_string(camel_name);
            const char* member_name = camel_name;
            Item by_name = ItemNull;
            Item by_ordinal = ItemNull;
            int name_handled = jube_member_get(husk, key, &by_name);
            int ordinal_handled = jube_member_get_by_ordinal(
                husk, slot, (uint32_t)ordinal, &by_ordinal);
            EXPECT_EQ(name_handled, ordinal_handled)
                << binding->type_name << "." << member_name;
            EXPECT_EQ(by_name.item, by_ordinal.item)
                << binding->type_name << "." << member_name;

            Item camel_by_name = ItemNull;
            EXPECT_EQ(jube_member_get(husk, camel_key, &camel_by_name), name_handled);
            EXPECT_EQ(camel_by_name.item, by_name.item)
                << binding->type_name << "." << member_name;

            Item name_set = ItemNull;
            Item ordinal_set = ItemNull;
            Item value = js_make_string("dom4-parity");
            EXPECT_EQ(jube_member_set(husk, key, value, &name_set),
                jube_member_set_by_ordinal(husk, slot, (uint32_t)ordinal,
                    value, &ordinal_set));
            EXPECT_EQ(name_set.item, ordinal_set.item)
                << binding->type_name << "." << member_name;

            Item ordinal_call = ItemNull;
            int ordinal_called = jube_member_call_by_ordinal(
                husk, slot, (uint32_t)ordinal, nullptr, 0, &ordinal_call);
            Item name_method = ItemNull;
            int name_method_handled = jube_member_get(husk, key, &name_method);
            int name_called = 0;
            if (name_method_handled && get_type_id(name_method) == LMD_TYPE_FUNC) {
                name_called = 1;
                name_method = js_call_function(name_method, husk, nullptr, 0);
            }
            EXPECT_EQ(name_called, ordinal_called)
                << binding->type_name << "." << member_name;
            if (jube_member_kind_at(type, ordinal) != JUBE_MEMBER_KIND_METHOD) {
                EXPECT_EQ(ordinal_called, 0);
            }
        }

        // A valid family slot with a non-method ordinal must never enter the
        // call binding; this is the mismatched kind × member guard sweep.
        for (int ordinal = 0; ordinal < member_count; ordinal++) {
            if (jube_member_kind_at(type, ordinal) == JUBE_MEMBER_KIND_METHOD) continue;
            Item out = ItemNull;
            EXPECT_EQ(jube_member_call_by_ordinal(
                husk, slot, (uint32_t)ordinal, nullptr, 0, &out), 0)
                << binding->type_name << " ordinal " << ordinal;
        }
    }

    runtime_cleanup(&runtime);
}

static bool capture_callable_realm(Runtime* runtime,
                                   CallableRealmSnapshot* snapshot) {
    if (!runtime || !snapshot) return false;
    runtime_init(runtime);
    uint64_t result_home = 0;
    Item result = transpile_js_to_mir(runtime, "0;", "<callable-realm>",
        &result_home);
    if (item_is_error(result)) {
        runtime_cleanup(runtime);
        return false;
    }

    bool captured = false;
    {
        RootFrame roots(7);
        Rooted<Item> global_root(roots, js_get_global_this());
        Rooted<Item> math_root(roots, js_get_key_default(global_root.get(),
            js_make_string("Math")));
        Rooted<Item> abs_root(roots, js_get_key_default(math_root.get(),
            js_make_string("abs")));
        Rooted<Item> array_root(roots, js_get_key_default(global_root.get(),
            js_make_string("Array")));
        Rooted<Item> prototype_root(roots, js_get_key_default(array_root.get(),
            js_make_string("prototype")));
        Rooted<Item> values_root(roots, js_get_key_default(prototype_root.get(),
            js_make_string("values")));
        Rooted<Item> iterator_root(roots, js_get_key_default(prototype_root.get(),
            js_well_known_symbol_key(1)));

        snapshot->math_abs = abs_root.get();
        snapshot->array_constructor = array_root.get();
        snapshot->stable_lookup = abs_root.get().item ==
            js_get_key_default(math_root.get(), js_make_string("abs")).item;
        snapshot->iterator_alias = values_root.get().item == iterator_root.get().item;
        captured = get_type_id(snapshot->math_abs) == LMD_TYPE_FUNC &&
            get_type_id(snapshot->array_constructor) == LMD_TYPE_FUNC;
    }

    EvalContext* owner = runtime_get_eval_context(runtime);
    // D5.2/D6.2.2v2: leave both heaps alive while comparing identity, but
    // detach every derived TLS cache before another realm becomes current.
    if (!js_runtime_state_thread_shutdown(owner) ||
            !eval_context_thread_shutdown(owner)) {
        return false;
    }
    return captured;
}

TEST(JsCallableRealmIdentity, IntrinsicsAreStableWithinAndDistinctAcrossContexts) {
    Runtime first = {};
    Runtime second = {};
    CallableRealmSnapshot first_snapshot = {};
    CallableRealmSnapshot second_snapshot = {};

    bool first_captured = capture_callable_realm(&first, &first_snapshot);
    bool second_captured = capture_callable_realm(&second, &second_snapshot);

    EXPECT_TRUE(first_captured);
    EXPECT_TRUE(second_captured);
    EXPECT_TRUE(first_snapshot.stable_lookup);
    EXPECT_TRUE(second_snapshot.stable_lookup);
    EXPECT_TRUE(first_snapshot.iterator_alias);
    EXPECT_TRUE(second_snapshot.iterator_alias);
    EXPECT_NE(first_snapshot.math_abs.item, second_snapshot.math_abs.item);
    EXPECT_NE(first_snapshot.array_constructor.item,
              second_snapshot.array_constructor.item);

    runtime_cleanup(&second);
    runtime_cleanup(&first);
}

static gc_heap_t* concurrency_test_gc;

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

static LambdaTaskPoll raise_task_fault(LambdaTask* task, void* data, Item* out) {
    (void)task;
    (void)data;
    (void)out;
    // The scheduler must own the native target for this poll; it cannot
    // survive into a later resumption after the task yields.
    if (!lambda_recovery_frame_raise_fault(
            LAMBDA_FAULT_EQUALITY_DEPTH_EXHAUSTION, ERR_TYPE_MISMATCH)) {
        ADD_FAILURE() << "task fault had no scheduler recovery target";
    }
    return LAMBDA_TASK_POLL_DONE;
}

class LambdaConcurrencyRuntime : public ::testing::Test {
protected:
    EvalContext eval = {};
    Heap heap = {};
    Pool* pool = NULL;
    LambdaScheduler* scheduler = NULL;

    void SetUp() override {
        concurrency_test_gc = gc_heap_create();
        ASSERT_NE(concurrency_test_gc, nullptr);
        heap.gc = concurrency_test_gc;
        pool = mem_pool_create(NULL, MEM_ROLE_RUNTIME_HEAP, "test.concurrency.runtime");
        ASSERT_NE(pool, nullptr);
        heap.pool = pool;
        eval.heap = &heap;
        ASSERT_TRUE(eval_context_thread_initialize(&eval));
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
        EXPECT_TRUE(eval_context_thread_shutdown(&eval));
        gc_heap_destroy(concurrency_test_gc);
        concurrency_test_gc = NULL;
        mem_pool_destroy(pool);
        pool = NULL;
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

TEST_F(LambdaConcurrencyRuntime, TaskPollFaultCompletesWithDurableStaticResult) {
    LambdaTask* first = lambda_task_create(scheduler, raise_task_fault, NULL, NULL);
    LambdaTask* second = lambda_task_create(scheduler, raise_task_fault, NULL, NULL);
    ASSERT_NE(first, nullptr);
    ASSERT_NE(second, nullptr);

    ASSERT_EQ(lambda_scheduler_run_one(scheduler), 1);
    Item first_result = lambda_task_result(first);
    ASSERT_EQ(get_type_id(first_result), LMD_TYPE_ERROR);
    EXPECT_EQ(it2err(first_result)->code, ERR_RUNTIME_ERROR);
    EXPECT_TRUE(it2err(first_result)->is_static);
    EXPECT_EQ(lambda_recovery_frame_current(), nullptr);

    ASSERT_EQ(lambda_scheduler_run_one(scheduler), 1);
    Item second_result = lambda_task_result(second);
    ASSERT_EQ(get_type_id(second_result), LMD_TYPE_ERROR);
    EXPECT_EQ(it2err(second_result)->code, ERR_RUNTIME_ERROR);
    EXPECT_TRUE(it2err(second_result)->is_static);
    // The second landing reuses the TLS handoff record, but it cannot alter
    // the first completed task's own static result.
    EXPECT_EQ(it2err(first_result)->code, ERR_RUNTIME_ERROR);
    EXPECT_EQ(lambda_recovery_frame_current(), nullptr);
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

#ifndef _WIN32

// The script code and its import cone are compiled once below.  Every worker
// receives the same sealed Script pointers, but owns its heap/module slabs/ICs.
// This is the runtime-globals invariant: only immutable code is shared.
typedef struct SharedModuleStressCase {
    const char* name;
    const char* source;
    Script* script;
} SharedModuleStressCase;

// mark.type reaches a string-annotated chart helper; Lambda single-quoted
// literals are symbols and must not be used for this fixture's mark names.
static const char* kSharedModuleStressChartBar =
    "import vega: lambda.package.chart.vega\n"
    "import chart: lambda.package.chart.chart\n"
    "let spec = vega.convert({width: 120, height: 80, data: {values: "
    "[{category: 'A', amount: 2}, {category: 'B', amount: 5}]}, mark: "
    "{type: \"bar\"}, encoding: {x: {field: 'category', type: 'nominal'}, "
    "y: {field: 'amount', type: 'quantitative'}}})\n"
    "len(format(chart.render_spec(spec), 'xml'))\n";

static const char* kSharedModuleStressChartLine =
    "import vega: lambda.package.chart.vega\n"
    "import chart: lambda.package.chart.chart\n"
    "let spec = vega.convert({width: 120, height: 80, data: {values: "
    "[{x: 0, y: 3}, {x: 1, y: 7}, {x: 2, y: 4}]}, mark: {type: \"line\"}, "
    "encoding: {x: {field: 'x', type: 'quantitative'}, y: {field: 'y', "
    "type: 'quantitative'}}})\n"
    "len(format(chart.render_spec(spec), 'xml'))\n";

static const char* kSharedModuleStressPdfPageCount =
    "import pdf: lambda.package.pdf.pdf\n"
    "let doc = input('test/input/test.pdf', 'pdf') ^ { null }\n"
    "pdf.pdf_page_count(doc)\n";

static const char* kSharedModuleStressPdfContent =
    "import resolve: lambda.package.pdf.resolve\n"
    "let doc = input('test/input/test.pdf', 'pdf') ^ { null }\n"
    "let page = resolve.page_at(doc, 0)\n"
    "len(resolve.page_content_bytes(doc, page))\n";

static SharedModuleStressCase shared_module_stress_cases[] = {
    {"chart-bar", kSharedModuleStressChartBar, NULL},
    {"chart-line", kSharedModuleStressChartLine, NULL},
    {"pdf-page-count", kSharedModuleStressPdfPageCount, NULL},
    {"pdf-content", kSharedModuleStressPdfContent, NULL},
};
static const int shared_module_stress_case_count =
    (int)(sizeof(shared_module_stress_cases) / sizeof(shared_module_stress_cases[0]));
static Runtime shared_module_stress_runtime = {};

static uint64_t shared_module_stress_now_ms(void) {
    struct timespec ts = {};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u;
}

typedef struct SharedModuleStressGate {
    pthread_mutex_t mutex;
    pthread_cond_t condition;
    int setup_turn;
    int ready_count;
    bool release_workers;
} SharedModuleStressGate;

typedef enum SharedModuleStressMode {
    SHARED_MODULE_STRESS_COLD_INSTANTIATE,
    SHARED_MODULE_STRESS_HOT_LOOP,
    SHARED_MODULE_STRESS_WARM_ONLY,
} SharedModuleStressMode;

typedef struct SharedModuleStressWorker {
    SharedModuleStressCase* cases;
    int case_count;
    int worker_id;
    SharedModuleStressGate* gate;
    SharedModuleStressMode mode;
    EvalContext eval;
    Pool* setup_pool;
    ArrayList* import_cone;
    ArrayList* visited_scripts;
    uint64_t evaluations;
    uint64_t started_ms;
    uint64_t stopped_ms;
    uintptr_t module_state_address;
    TypeId failed_result_type;
    int failed_error_code;
    bool module_uses_shared_consts;
    const char* failure;
} SharedModuleStressWorker;

extern "C" bool prepare_context_module_state(void* mir_ctx, void* consts,
                                               void* type_list);

static bool shared_module_stress_select_case(SharedModuleStressWorker* worker,
                                             SharedModuleStressCase* test_case) {
    if (!test_case || !test_case->script || !test_case->script->main_func ||
            !test_case->script->const_list) {
        return false;
    }
    worker->eval.consts = (void**)test_case->script->const_list->data;
    worker->eval.type_list = test_case->script->type_list;
    worker->eval.current_file = test_case->script->reference;
    return true;
}

static bool shared_module_stress_is_integral_result(Item result) {
    switch (get_type_id(result)) {
    case LMD_TYPE_INT:
    case LMD_TYPE_INT64:
    case LMD_TYPE_UINT64:
        return true;
    default:
        return false;
    }
}

static bool shared_module_stress_execute_case(SharedModuleStressWorker* worker,
                                              SharedModuleStressCase* test_case,
                                              int64_t* result_value) {
    if (!shared_module_stress_select_case(worker, test_case)) return false;
    worker->eval.result = ItemNull;
    Item result = test_case->script->main_func((Context*)&worker->eval);
    worker->eval.result = result;
    if (worker->eval.heap) worker->eval.heap->result_root = result.item;
    if (!shared_module_stress_is_integral_result(result) || worker->eval.last_error != NULL) {
        worker->failed_result_type = get_type_id(result);
        worker->failed_error_code = worker->eval.last_error ?
            worker->eval.last_error->code : 0;
        return false;
    }
    if (result_value) *result_value = 0;
    return true;
}

static bool shared_module_stress_collect_import_cone(SharedModuleStressWorker* worker,
                                                     Script* script, bool include_script) {
    if (!script || !script->main_func || !worker->import_cone ||
            !worker->visited_scripts) return false;
    for (int i = 0; i < worker->visited_scripts->length; i++) {
        if ((Script*)worker->visited_scripts->data[i] == script) return true;
    }
    // Mark before descending so circular imports match the runner's graph
    // walk rather than recursively re-entering the same sealed module.
    if (!arraylist_append(worker->visited_scripts, script)) return false;

    if (script->direct_imports) {
        for (int i = 0; i < script->direct_imports->length; i++) {
            Script* dependency = (Script*)script->direct_imports->data[i];
            if (!shared_module_stress_collect_import_cone(worker, dependency, true)) return false;
        }
    }
    return !include_script || arraylist_append(worker->import_cone, script) != 0;
}

static void shared_module_stress_record_module_state(SharedModuleStressWorker* worker,
                                                     SharedModuleStressCase* test_case) {
    for (uint32_t i = 0; i < worker->eval.module_state_capacity; i++) {
        LambdaModuleState* state = worker->eval.module_states[i];
        if (!state || state->consts != test_case->script->const_list->data) continue;
        worker->module_state_address = (uintptr_t)state;
        worker->module_uses_shared_consts = true;
        return;
    }
}

static bool shared_module_stress_init_worker(SharedModuleStressWorker* worker) {
    memset(&worker->eval, 0, sizeof(worker->eval));
    if (!eval_context_thread_matches(&worker->eval)) {
        worker->failure = "eval thread ownership";
        return false;
    }
    input_context = (Context*)&worker->eval;
    lambda_stack_init();
    worker->eval.stack_limit = _lambda_stack_limit;
    if (!lambda_side_stack_bind()) return false;
    // Compiled scripts share their AST pool; allocating per-thread runtime
    // names from it races the pool allocator during concurrent setup.
    worker->setup_pool = mem_pool_create(NULL, MEM_ROLE_AST, "concurrency.worker");
    if (!worker->setup_pool) {
        worker->failure = "worker setup pool";
        return false;
    }
    worker->eval.pool = worker->setup_pool;
    worker->eval.runtime = &shared_module_stress_runtime;
    worker->eval.name_pool = name_pool_create(worker->setup_pool, NULL);
    heap_init();
    if (!worker->eval.heap) return false;
    worker->eval.pool = worker->eval.heap->pool;
    worker->eval.type_info = type_info;
    // PDF input resolves relative paths through cwd; a hand-built EvalContext
    // must establish the same context-owned URL as runner_setup_context().
    worker->eval.cwd = get_current_dir();
    worker->eval.decimal_ctx = decimal_fixed_context();
    worker->eval.context_alloc = heap_alloc;
    worker->eval.scheduler = lambda_scheduler_create(LAMBDA_MAILBOX_DEFAULT_CAPACITY);
    worker->import_cone = arraylist_new(worker->case_count * 4);
    worker->visited_scripts = arraylist_new(worker->case_count * 4);
    if (!worker->eval.name_pool || !worker->eval.cwd || !worker->eval.scheduler ||
            !worker->import_cone || !worker->visited_scripts) {
        worker->failure = "context resource initialization";
        return false;
    }

    // Match the normal runner: initialize package imports, but leave each
    // selected test script for the evaluation phase below.
    for (int i = 0; i < worker->case_count; i++) {
        if (!shared_module_stress_collect_import_cone(worker, worker->cases[i].script, false)) {
            worker->failure = worker->cases[i].name;
            return false;
        }
    }
    for (int i = 0; i < worker->import_cone->length; i++) {
        Script* script = (Script*)worker->import_cone->data[i];
        if (script->jit_context && !prepare_context_module_state((void*)script->jit_context,
                script->const_list ? script->const_list->data : NULL, script->type_list)) {
            worker->failure = script->reference;
            return false;
        }
    }
    for (int i = 0; i < worker->case_count; i++) {
        Script* script = worker->cases[i].script;
        if (!script || !script->jit_context || !prepare_context_module_state(
                (void*)script->jit_context,
                script->const_list ? script->const_list->data : NULL, script->type_list)) {
            worker->failure = worker->cases[i].name;
            return false;
        }
    }
    for (int i = 0; i < worker->import_cone->length; i++) {
        Script* script = (Script*)worker->import_cone->data[i];
        worker->eval.consts = script->const_list ? (void**)script->const_list->data : NULL;
        worker->eval.type_list = script->type_list;
        worker->eval.current_file = script->reference;
        worker->eval.result = script->main_func((Context*)&worker->eval);
        if (worker->eval.heap) worker->eval.heap->result_root = worker->eval.result.item;
    }
    for (int i = 0; i < worker->case_count; i++) {
        if (!shared_module_stress_execute_case(worker, &worker->cases[i], NULL)) {
            worker->failure = worker->cases[i].name;
            return false;
        }
        if (i == 0) shared_module_stress_record_module_state(worker, &worker->cases[i]);
    }
    return true;
}

static void shared_module_stress_destroy_worker(SharedModuleStressWorker* worker) {
    if (!eval_context_thread_matches(&worker->eval)) {
        worker->failure = "eval thread ownership during teardown";
        return;
    }
    input_context = (Context*)&worker->eval;
    if (worker->eval.scheduler) {
        lambda_scheduler_destroy(worker->eval.scheduler);
        worker->eval.scheduler = NULL;
    }
    lambda_module_state_destroy();
    if (worker->import_cone) {
        arraylist_free(worker->import_cone);
        worker->import_cone = NULL;
    }
    if (worker->visited_scripts) {
        arraylist_free(worker->visited_scripts);
        worker->visited_scripts = NULL;
    }
    if (worker->eval.name_pool) {
        name_pool_release(worker->eval.name_pool);
        worker->eval.name_pool = NULL;
    }
    if (worker->setup_pool) {
        pool_destroy(worker->setup_pool);
        worker->setup_pool = NULL;
    }
    if (worker->eval.heap) heap_destroy();
    if (worker->eval.cwd) {
        url_destroy(worker->eval.cwd);
        worker->eval.cwd = NULL;
    }
    lambda_stack_cleanup();
    input_context = NULL;
}

static void* shared_module_stress_worker_main(void* arg) {
    SharedModuleStressWorker* worker = (SharedModuleStressWorker*)arg;
    SharedModuleStressGate* gate = worker->gate;
    // Generated code and runtime helpers resolve allocation/context state
    // through TLS for the entire evaluation, not only during setup.
    if (!eval_context_thread_initialize(&worker->eval)) {
        worker->failure = "eval thread initialization";
        return NULL;
    }
    input_context = (Context*)&worker->eval;
    worker->started_ms = shared_module_stress_now_ms();
    // The worker's budget starts at pthread entry.  Context construction is
    // deliberately included: the test must not hide setup cost behind a
    // separate, longer process-wide timing window.
    uint64_t deadline = worker->started_ms + 20000u;

    // Construction touches cold process/runtime setup; serialize it so the
    // following concurrent phase isolates shared-module execution correctness.
    pthread_mutex_lock(&gate->mutex);
    while (gate->setup_turn != worker->worker_id) {
        pthread_cond_wait(&gate->condition, &gate->mutex);
    }
    bool initialized = shared_module_stress_init_worker(worker);
    gate->setup_turn++;
    gate->ready_count++;
    pthread_cond_broadcast(&gate->condition);
    while (!gate->release_workers) {
        pthread_cond_wait(&gate->condition, &gate->mutex);
    }
    pthread_mutex_unlock(&gate->mutex);

    if (initialized) {
        uint64_t state = 0x9e3779b97f4a7c15ULL ^ (uint64_t)(worker->worker_id + 1);
        while (worker->mode == SHARED_MODULE_STRESS_HOT_LOOP &&
                shared_module_stress_now_ms() < deadline) {
            state = state * 6364136223846793005ULL + 1442695040888963407ULL;
            SharedModuleStressCase* test_case = &worker->cases[
                (int)((state >> 32) % (uint64_t)worker->case_count)];
            if (!shared_module_stress_execute_case(worker, test_case, NULL)) {
                worker->failure = test_case->name;
                break;
            }
            worker->evaluations++;
        }
        if (worker->mode == SHARED_MODULE_STRESS_COLD_INSTANTIATE) {
            for (int i = 0; i < worker->case_count; i++) {
                if (!shared_module_stress_execute_case(worker, &worker->cases[i], NULL)) {
                    worker->failure = worker->cases[i].name;
                    break;
                }
                if (i == 0) shared_module_stress_record_module_state(worker,
                    &worker->cases[i]);
                worker->evaluations++;
            }
        }
    } else {
        if (!worker->failure) worker->failure = "context initialization";
    }
    shared_module_stress_destroy_worker(worker);
    if (!eval_context_thread_shutdown(&worker->eval) && !worker->failure) {
        worker->failure = "eval thread shutdown";
    }
    worker->stopped_ms = shared_module_stress_now_ms();
    return NULL;
}

class RuntimeGlobalsConcurrency : public ::testing::Test {
protected:
    static void SetUpTestSuite() {
        runtime_init(&shared_module_stress_runtime);
        shared_module_stress_runtime.use_mir_direct = true;

        // Cache the shared package code before worker contexts exist.  Calling
        // load_script from a worker would test per-thread compilation instead
        // of shared immutable module execution.
        Script* chart_package = load_script_mir_direct(&shared_module_stress_runtime,
        "lambda/package/chart/chart.ls", NULL, true);
        Script* pdf_package = load_script_mir_direct(&shared_module_stress_runtime,
        "lambda/package/pdf/pdf.ls", NULL, true);
        ASSERT_NE(chart_package, nullptr);
        ASSERT_NE(pdf_package, nullptr);
        for (int i = 0; i < shared_module_stress_case_count; i++) {
            shared_module_stress_cases[i].script = load_script_mir_direct(
                &shared_module_stress_runtime, shared_module_stress_cases[i].name,
                shared_module_stress_cases[i].source, false);
            ASSERT_NE(shared_module_stress_cases[i].script, nullptr)
                << shared_module_stress_cases[i].name;
            ASSERT_NE(shared_module_stress_cases[i].script->main_func, nullptr)
                << shared_module_stress_cases[i].name;
        }
    }

    static void TearDownTestSuite() {
        runtime_cleanup(&shared_module_stress_runtime);
        memset(&shared_module_stress_runtime, 0, sizeof(shared_module_stress_runtime));
    }
};

static bool shared_module_stress_run_workers(SharedModuleStressWorker* workers,
                                             int worker_count) {
    SharedModuleStressGate gate = {};
    if (pthread_mutex_init(&gate.mutex, NULL) != 0) return false;
    if (pthread_cond_init(&gate.condition, NULL) != 0) {
        pthread_mutex_destroy(&gate.mutex);
        return false;
    }
    pthread_t threads[4] = {};
    if (worker_count > (int)(sizeof(threads) / sizeof(threads[0]))) return false;
    pthread_attr_t attributes = {};
    if (pthread_attr_init(&attributes) != 0) return false;
    // Chart/PDF MIR entrypoints have deep native frames; the platform default
    // worker stack is too small and faults in chkstk before Lambda runs.
    if (pthread_attr_setstacksize(&attributes, 8u * 1024u * 1024u) != 0) {
        pthread_attr_destroy(&attributes);
        return false;
    }

    for (int i = 0; i < worker_count; i++) {
        workers[i].cases = shared_module_stress_cases;
        workers[i].case_count = shared_module_stress_case_count;
        workers[i].worker_id = i;
        workers[i].gate = &gate;
        if (pthread_create(&threads[i], &attributes, shared_module_stress_worker_main,
                &workers[i]) != 0) {
            pthread_attr_destroy(&attributes);
            return false;
        }
    }
    pthread_attr_destroy(&attributes);

    pthread_mutex_lock(&gate.mutex);
    while (gate.ready_count != worker_count) {
        pthread_cond_wait(&gate.condition, &gate.mutex);
    }
    gate.release_workers = true;
    pthread_cond_broadcast(&gate.condition);
    pthread_mutex_unlock(&gate.mutex);

    for (int i = 0; i < worker_count; i++) {
        if (pthread_join(threads[i], NULL) != 0) return false;
    }

    pthread_cond_destroy(&gate.condition);
    pthread_mutex_destroy(&gate.mutex);
    return true;
}

TEST_F(RuntimeGlobalsConcurrency, SharedChartAndPdfModulesStayStableAcrossEvalThreads) {
    constexpr int worker_count = 4;
    SharedModuleStressWorker workers[worker_count] = {};
    for (int i = 0; i < worker_count; i++) {
        workers[i].mode = SHARED_MODULE_STRESS_HOT_LOOP;
    }

    ASSERT_TRUE(shared_module_stress_run_workers(workers, worker_count));
    for (int i = 0; i < worker_count; i++) {
        EXPECT_STREQ(workers[i].failure, nullptr) << "worker " << i
            << " result_type=" << workers[i].failed_result_type
            << " error=" << workers[i].failed_error_code;
        EXPECT_GT(workers[i].evaluations, 0u) << "worker " << i;
        EXPECT_GE(workers[i].stopped_ms - workers[i].started_ms, 20000u)
            << "worker " << i << " must run for its own full 20-second budget";
    }
}

TEST_F(RuntimeGlobalsConcurrency, ModuleStateSlabsArePrivateToEachEvalContext) {
    constexpr int worker_count = 4;
    SharedModuleStressWorker workers[worker_count] = {};
    for (int i = 0; i < worker_count; i++) {
        workers[i].mode = SHARED_MODULE_STRESS_WARM_ONLY;
    }

    ASSERT_TRUE(shared_module_stress_run_workers(workers, worker_count));
    for (int i = 0; i < worker_count; i++) {
        EXPECT_STREQ(workers[i].failure, nullptr) << "worker " << i
            << " result_type=" << workers[i].failed_result_type
            << " error=" << workers[i].failed_error_code;
        EXPECT_TRUE(workers[i].module_uses_shared_consts) << "worker " << i;
        EXPECT_NE(workers[i].module_state_address, 0u) << "worker " << i;
    }
    for (int i = 0; i < worker_count; i++) {
        for (int j = i + 1; j < worker_count; j++) {
            EXPECT_NE(workers[i].module_state_address, workers[j].module_state_address);
        }
    }
}

#else

TEST(RuntimeGlobalsConcurrency, SharedChartAndPdfModulesStayStableAcrossEvalThreads) {
    GTEST_SKIP() << "the native concurrency harness requires pthread support";
}

#endif
