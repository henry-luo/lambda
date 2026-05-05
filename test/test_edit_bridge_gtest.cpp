// Test file for EditSession bridge API
#include <gtest/gtest.h>
#include "../lambda/edit_bridge.h"
#include "../lambda/mark_builder.hpp"
#include "../lambda/mark_reader.hpp"
#include "../lambda/input/input.hpp"
#include "../lib/mempool.h"
#include "../lib/log.h"

extern "C" Context* _lambda_rt = nullptr;

typedef struct SessionCounters {
    int change_count;
    int selection_count;
    Item last_payload;
} SessionCounters;

static void count_change(EditSession* session, EditEventKind kind, Item payload, void* user_data) {
    (void)session;
    SessionCounters* counters = (SessionCounters*)user_data;
    if (kind == EDIT_EVENT_CHANGE) {
        counters->change_count++;
        counters->last_payload = payload;
    }
}

static void count_selection(EditSession* session, EditEventKind kind, Item payload, void* user_data) {
    (void)session;
    (void)payload;
    SessionCounters* counters = (SessionCounters*)user_data;
    if (kind == EDIT_EVENT_SELECTION) {
        counters->selection_count++;
    }
}

class EditBridgeSessionTest : public ::testing::Test {
protected:
    Pool* pool;
    Input* input;

    void SetUp() override {
        log_init(NULL);
        pool = pool_create();
        ASSERT_NE(pool, nullptr);
        input = Input::create(pool);
        ASSERT_NE(input, nullptr);
    }

    void TearDown() override {
        if (pool) {
            pool_destroy(pool);
        }
    }

    int counter_value(Item item) {
        MarkReader reader(item);
        MapReader map_reader = reader.getRoot().asMap();
        return map_reader.get("counter").asInt32();
    }
};

TEST_F(EditBridgeSessionTest, SessionLifecycleAndHistoryCommands) {
    MarkBuilder builder(input);
    Item doc0 = builder.map().put("counter", 0).final();
    Item doc1 = builder.map().put("counter", 1).final();
    Item doc2 = builder.map().put("counter", 2).final();

    EditSchema* schema = (EditSchema*)input;
    EditSession* session = edit_session_new_with_input(input, doc0, schema);
    ASSERT_NE(session, nullptr);
    EXPECT_EQ(edit_session_schema(session), schema);
    EXPECT_EQ(counter_value(edit_session_current(session)), 0);

    SessionCounters counters;
    counters.change_count = 0;
    counters.selection_count = 0;
    counters.last_payload = ItemNull;
    edit_session_subscribe(session, EDIT_EVENT_CHANGE, count_change, &counters);

    EXPECT_TRUE(edit_session_exec(session, "set_root", doc1));
    EXPECT_EQ(counter_value(edit_session_current(session)), 1);
    EXPECT_EQ(counters.change_count, 1);
    EXPECT_EQ(counter_value(counters.last_payload), 1);
    EXPECT_TRUE(edit_session_exec(session, "commit", ItemNull));

    EXPECT_TRUE(edit_session_exec(session, "replace_root", doc2));
    EXPECT_TRUE(edit_session_exec(session, "commit", ItemNull));
    EXPECT_EQ(counter_value(edit_session_current(session)), 2);

    EXPECT_TRUE(edit_session_exec(session, "undo", ItemNull));
    EXPECT_EQ(counter_value(edit_session_current(session)), 1);
    EXPECT_TRUE(edit_session_exec(session, "redo", ItemNull));
    EXPECT_EQ(counter_value(edit_session_current(session)), 2);

    edit_session_destroy(session);
}

TEST_F(EditBridgeSessionTest, SelectionIsSessionOwnedAndObservable) {
    MarkBuilder builder(input);
    Item doc = builder.map().put("counter", 0).final();
    EditSession* session = edit_session_new_with_input(input, doc, nullptr);
    ASSERT_NE(session, nullptr);

    SessionCounters counters;
    counters.change_count = 0;
    counters.selection_count = 0;
    counters.last_payload = ItemNull;
    edit_session_subscribe(session, EDIT_EVENT_SELECTION, count_selection, &counters);

    uint32_t anchor_indices[2] = {0, 1};
    uint32_t head_indices[2] = {0, 2};
    SourcePos anchor = {{2, anchor_indices}, 3};
    SourcePos head = {{2, head_indices}, 7};
    EXPECT_TRUE(edit_session_set_selection(session, anchor, head));
    EXPECT_EQ(counters.selection_count, 1);

    anchor_indices[1] = 9;
    head_indices[1] = 9;
    SourcePos stored_anchor = edit_session_selection_anchor(session);
    SourcePos stored_head = edit_session_selection_head(session);
    ASSERT_EQ(stored_anchor.path.len, 2u);
    ASSERT_EQ(stored_head.path.len, 2u);
    EXPECT_EQ(stored_anchor.path.indices[1], 1u);
    EXPECT_EQ(stored_head.path.indices[1], 2u);
    EXPECT_EQ(stored_anchor.offset, 3u);
    EXPECT_EQ(stored_head.offset, 7u);

    edit_session_destroy(session);
}

