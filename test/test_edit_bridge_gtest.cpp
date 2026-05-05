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

    int64_t array_value(Item item, int64_t index) {
        MarkReader reader(item);
        ArrayReader array_reader = reader.getRoot().asArray();
        return array_reader.get(index).asInt();
    }

    int64_t array_length(Item item) {
        MarkReader reader(item);
        ArrayReader array_reader = reader.getRoot().asArray();
        return array_reader.length();
    }

    int64_t element_child_count(Item item) {
        MarkReader reader(item);
        ElementReader element_reader = reader.getRoot().asElement();
        return element_reader.childCount();
    }

    const char* element_child_tag(Item item, int64_t index) {
        MarkReader reader(item);
        ElementReader element_reader = reader.getRoot().asElement();
        ItemReader child = element_reader.childAt(index);
        if (!child.isElement()) { return nullptr; }
        return child.asElement().tagName();
    }

    const char* nested_element_child_tag(Item item, int64_t block_index, int64_t child_index) {
        MarkReader reader(item);
        ElementReader root_reader = reader.getRoot().asElement();
        ItemReader block = root_reader.childAt(block_index);
        if (!block.isElement()) { return nullptr; }
        ItemReader child = block.asElement().childAt(child_index);
        if (!child.isElement()) { return nullptr; }
        return child.asElement().tagName();
    }

    const char* nested_text(Item item, int64_t block_index, int64_t text_index) {
        MarkReader reader(item);
        ElementReader root_reader = reader.getRoot().asElement();
        ItemReader block = root_reader.childAt(block_index);
        if (!block.isElement()) { return nullptr; }
        return block.asElement().childAt(text_index).cstring();
    }

    const char* element_attr(Item item, const char* key) {
        MarkReader reader(item);
        ElementReader element_reader = reader.getRoot().asElement();
        return element_reader.get_attr_string(key);
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

TEST_F(EditBridgeSessionTest, ExecAppliesRootMapAndArrayCommands) {
    MarkBuilder builder(input);
    Item doc0 = builder.map().put("counter", (int64_t)0).final();
    EditSession* session = edit_session_new_with_input(input, doc0, nullptr);
    ASSERT_NE(session, nullptr);

    SessionCounters counters;
    counters.change_count = 0;
    counters.selection_count = 0;
    counters.last_payload = ItemNull;
    edit_session_subscribe(session, EDIT_EVENT_CHANGE, count_change, &counters);

    Item update_args = builder.map()
        .put("key", "counter")
        .put("value", (int64_t)7)
        .final();
    EXPECT_TRUE(edit_session_exec(session, "map_update", update_args));
    EXPECT_EQ(counter_value(edit_session_current(session)), 7);
    EXPECT_EQ(counters.change_count, 1);

    EXPECT_TRUE(edit_session_exec(session, "commit", ItemNull));

    Item array0 = builder.array()
        .append((int64_t)1)
        .append((int64_t)2)
        .final();
    EXPECT_TRUE(edit_session_exec(session, "set_root", array0));

    Item append_args = builder.map().put("value", (int64_t)3).final();
    EXPECT_TRUE(edit_session_exec(session, "array_append", append_args));
    EXPECT_EQ(array_length(edit_session_current(session)), 3);
    EXPECT_EQ(array_value(edit_session_current(session), 2), 3);

    Item set_args = builder.map()
        .put("index", (int64_t)1)
        .put("value", (int64_t)20)
        .final();
    EXPECT_TRUE(edit_session_exec(session, "array_set", set_args));
    EXPECT_EQ(array_value(edit_session_current(session), 1), 20);

    EXPECT_TRUE(edit_session_exec(session, "commit", ItemNull));
    EXPECT_TRUE(edit_session_exec(session, "undo", ItemNull));
    EXPECT_EQ(counter_value(edit_session_current(session)), 7);

    edit_session_destroy(session);
}

TEST_F(EditBridgeSessionTest, ExecAppliesRootElementCommands) {
    MarkBuilder builder(input);
    Item doc0 = builder.element("div")
        .attr("class", "box")
        .text("Old")
        .final();
    EditSession* session = edit_session_new_with_input(input, doc0, nullptr);
    ASSERT_NE(session, nullptr);

    Item attr_args = builder.map()
        .put("attr", "class")
        .put("value", "container")
        .final();
    EXPECT_TRUE(edit_session_exec(session, "element_update_attr", attr_args));
    EXPECT_STREQ(element_attr(edit_session_current(session), "class"), "container");

    Item insert_args = builder.map()
        .put("index", (int64_t)-1)
        .put("child", builder.element("span").text("New").final())
        .final();
    EXPECT_TRUE(edit_session_exec(session, "element_insert_child", insert_args));
    EXPECT_EQ(element_child_count(edit_session_current(session)), 2);

    Item delete_args = builder.map().put("index", (int64_t)0).final();
    EXPECT_TRUE(edit_session_exec(session, "element_delete_child", delete_args));
    EXPECT_EQ(element_child_count(edit_session_current(session)), 1);

    edit_session_destroy(session);
}

TEST_F(EditBridgeSessionTest, HistoryRestoresSelectionSnapshots) {
    MarkBuilder builder(input);
    Item doc0 = builder.map().put("counter", (int64_t)0).final();
    Item doc1 = builder.map().put("counter", (int64_t)1).final();
    EditSession* session = edit_session_new_with_input(input, doc0, nullptr);
    ASSERT_NE(session, nullptr);

    SessionCounters counters;
    counters.change_count = 0;
    counters.selection_count = 0;
    counters.last_payload = ItemNull;
    edit_session_subscribe(session, EDIT_EVENT_SELECTION, count_selection, &counters);

    uint32_t anchor_a_indices[1] = {0};
    uint32_t head_a_indices[1] = {0};
    SourcePos anchor_a = {{1, anchor_a_indices}, 1};
    SourcePos head_a = {{1, head_a_indices}, 1};
    EXPECT_TRUE(edit_session_set_selection(session, anchor_a, head_a));
    EXPECT_TRUE(edit_session_exec(session, "commit", ItemNull));

    EXPECT_TRUE(edit_session_exec(session, "set_root", doc1));
    uint32_t anchor_b_indices[2] = {0, 1};
    uint32_t head_b_indices[2] = {0, 1};
    SourcePos anchor_b = {{2, anchor_b_indices}, 5};
    SourcePos head_b = {{2, head_b_indices}, 5};
    EXPECT_TRUE(edit_session_set_selection(session, anchor_b, head_b));
    EXPECT_TRUE(edit_session_exec(session, "commit", ItemNull));

    EXPECT_TRUE(edit_session_exec(session, "undo", ItemNull));
    EXPECT_EQ(counter_value(edit_session_current(session)), 0);
    SourcePos undo_anchor = edit_session_selection_anchor(session);
    EXPECT_EQ(undo_anchor.path.len, 1u);
    EXPECT_EQ(undo_anchor.path.indices[0], 0u);
    EXPECT_EQ(undo_anchor.offset, 1u);

    EXPECT_TRUE(edit_session_exec(session, "redo", ItemNull));
    EXPECT_EQ(counter_value(edit_session_current(session)), 1);
    SourcePos redo_anchor = edit_session_selection_anchor(session);
    EXPECT_EQ(redo_anchor.path.len, 2u);
    EXPECT_EQ(redo_anchor.path.indices[0], 0u);
    EXPECT_EQ(redo_anchor.path.indices[1], 1u);
    EXPECT_EQ(redo_anchor.offset, 5u);
    EXPECT_GE(counters.selection_count, 4);

    edit_session_destroy(session);
}

TEST_F(EditBridgeSessionTest, ExecAppliesRichTextBlockCommands) {
    MarkBuilder builder(input);
    Item doc0 = builder.element("doc")
        .child(builder.element("paragraph").text("A").final())
        .child(builder.element("paragraph").text("B").final())
        .final();
    EditSession* session = edit_session_new_with_input(input, doc0, nullptr);
    ASSERT_NE(session, nullptr);

    uint32_t caret_indices[2] = {0, 0};
    SourcePos caret = {{2, caret_indices}, 1};
    EXPECT_TRUE(edit_session_set_selection(session, caret, caret));

    EXPECT_TRUE(edit_session_exec(session, "insert_horizontal_rule", ItemNull));
    EXPECT_EQ(element_child_count(edit_session_current(session)), 3);
    EXPECT_STREQ(element_child_tag(edit_session_current(session), 1), "hr");
    SourcePos hr_selection = edit_session_selection_anchor(session);
    ASSERT_EQ(hr_selection.path.len, 1u);
    EXPECT_EQ(hr_selection.path.indices[0], 1u);

    Item code_args = builder.map()
        .put("index", (int64_t)2)
        .put("text", "let x = 1")
        .final();
    EXPECT_TRUE(edit_session_exec(session, "insert_code_block", code_args));
    EXPECT_EQ(element_child_count(edit_session_current(session)), 4);
    EXPECT_STREQ(element_child_tag(edit_session_current(session), 2), "code_block");
    EXPECT_STREQ(nested_text(edit_session_current(session), 2, 0), "let x = 1");

    Item pre_args = builder.map()
        .put("index", (int64_t)3)
        .put("tag", "pre")
        .put("text", "html")
        .final();
    EXPECT_TRUE(edit_session_exec(session, "insertCodeBlock", pre_args));
    EXPECT_STREQ(element_child_tag(edit_session_current(session), 3), "pre");
    EXPECT_STREQ(nested_text(edit_session_current(session), 3, 0), "html");

    Item wrap_args = builder.map()
        .put("start", (int64_t)0)
        .put("end", (int64_t)1)
        .final();
    EXPECT_TRUE(edit_session_exec(session, "wrap_blockquote", wrap_args));
    EXPECT_STREQ(element_child_tag(edit_session_current(session), 0), "blockquote");
    EXPECT_STREQ(nested_element_child_tag(edit_session_current(session), 0, 0), "paragraph");
    EXPECT_STREQ(nested_element_child_tag(edit_session_current(session), 0, 1), "hr");

    EXPECT_TRUE(edit_session_exec(session, "lift_blockquote", builder.map().put("index", (int64_t)0).final()));
    EXPECT_STREQ(element_child_tag(edit_session_current(session), 0), "paragraph");
    EXPECT_STREQ(element_child_tag(edit_session_current(session), 1), "hr");

    edit_session_destroy(session);
}

