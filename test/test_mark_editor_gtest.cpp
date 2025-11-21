// Test file for MarkEditor - CRUD operations on Lambda documents
#include <gtest/gtest.h>
#include "../lambda/mark_editor.hpp"
#include "../lambda/mark_reader.hpp"
#include "../lambda/input/input.hpp"
#include "../lib/mempool.h"
#include "../lib/log.h"

// Test fixture for MarkEditor tests
class MarkEditorTest : public ::testing::Test {
protected:
    Pool* pool;
    Input* input;
    
    void SetUp() override {
        // Initialize logging for debug output
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
};

//==============================================================================
// MAP OPERATIONS - INLINE MODE
//==============================================================================

TEST_F(MarkEditorTest, MapUpdateInlineMode_SameType) {
    // Create initial map
    MarkBuilder builder(input);
    Item doc = builder.map()
        .put("name", "Alice")
        .put("age", (int64_t)30)
        .final();
    
    input->root = doc;
    
    // Create editor in inline mode
    MarkEditor editor(input, EDIT_MODE_INLINE);
    
    // Update existing field with same type
    Item updated = editor.map_update(doc, "age", editor.builder()->createLong(31));
    
    ASSERT_NE(updated.map, nullptr);
    ASSERT_EQ(updated.map->type_id, LMD_TYPE_MAP);
    ASSERT_EQ(updated.map, doc.map);  // Same map instance (inline)
    
    // Verify value changed using MarkReader
    MarkReader reader(updated);
    MapReader map_reader = reader.getRoot().asMap();
    ItemReader age_reader = map_reader.get("age");
    ASSERT_EQ(age_reader.getType(), LMD_TYPE_INT64);
    ASSERT_EQ(age_reader.asInt(), 31);
}

TEST_F(MarkEditorTest, MapUpdateInlineMode_NewField) {
    MarkBuilder builder(input);
    Item doc = builder.map()
        .put("name", "Bob")
        .final();
    
    input->root = doc;
    
    MarkEditor editor(input, EDIT_MODE_INLINE);
    
    // Add new field
    Item updated = editor.map_update(doc, "age", editor.builder()->createInt(25));
    
    ASSERT_NE(updated.map, nullptr);
    ASSERT_EQ(updated.map->type_id, LMD_TYPE_MAP);
    
    // Verify both fields exist
    ConstItem name_val = updated.map->get("name");
    ASSERT_EQ(name_val.type_id(), LMD_TYPE_STRING);
    
    ConstItem age_val = updated.map->get("age");
    ASSERT_EQ(age_val.type_id(), LMD_TYPE_INT);
    ASSERT_EQ(age_val.item & 0xFFFFFFFF, 25u);
}

TEST_F(MarkEditorTest, MapDeleteInlineMode) {
    MarkBuilder builder(input);
    Item doc = builder.map()
        .put("name", "Charlie")
        .put("age", (int64_t)40)
        .put("city", "NYC")
        .final();
    
    input->root = doc;
    
    MarkEditor editor(input, EDIT_MODE_INLINE);
    
    // Delete field
    Item updated = editor.map_delete(doc, "age");
    
    ASSERT_NE(updated.map, nullptr);
    ASSERT_EQ(updated.map->type_id, LMD_TYPE_MAP);
    
    // Verify field was deleted
    ASSERT_FALSE(updated.map->has_field("age"));
    ASSERT_TRUE(updated.map->has_field("name"));
    ASSERT_TRUE(updated.map->has_field("city"));
}

TEST_F(MarkEditorTest, MapBatchUpdate) {
    MarkBuilder builder(input);
    Item doc = builder.map()
        .put("a", (int64_t)1)
        .put("b", (int64_t)2)
        .final();
    
    input->root = doc;
    
    MarkEditor editor(input, EDIT_MODE_INLINE);
    
    // Update multiple fields at once
    Item updated = editor.map_update_batch(doc, 3,
        "a", editor.builder()->createInt(10),
        "b", editor.builder()->createInt(20),
        "c", editor.builder()->createInt(30)
    );
    
    ASSERT_NE(updated.map, nullptr);
    ASSERT_EQ(updated.map->type_id, LMD_TYPE_MAP);
    
    // Verify all updates using MarkReader
    MarkReader reader(updated);
    MapReader map_reader = reader.getRoot().asMap();
    
    ItemReader a_reader = map_reader.get("a");
    ASSERT_EQ(a_reader.getType(), LMD_TYPE_INT);
    ASSERT_EQ(a_reader.asInt32(), 10);
    
    ItemReader c_reader = map_reader.get("c");
    ASSERT_EQ(c_reader.getType(), LMD_TYPE_INT);
    ASSERT_EQ(c_reader.asInt32(), 30);
}

//==============================================================================
// MAP OPERATIONS - IMMUTABLE MODE
//==============================================================================

TEST_F(MarkEditorTest, MapUpdateImmutableMode) {
    MarkBuilder builder(input);
    Item doc = builder.map()
        .put("name", "David")
        .put("age", (int64_t)35)
        .final();
    
    input->root = doc;
    
    MarkEditor editor(input, EDIT_MODE_IMMUTABLE);
    
    // Update field
    Item updated = editor.map_update(doc, "age", editor.builder()->createLong(36));
    
    ASSERT_NE(updated.map, nullptr);
    ASSERT_EQ(updated.map->type_id, LMD_TYPE_MAP);
    ASSERT_NE(updated.map, doc.map);  // Different map instance (immutable)
    
    // Verify old map unchanged using MarkReader
    MarkReader old_reader(doc);
    MapReader old_map = old_reader.getRoot().asMap();
    ASSERT_EQ(old_map.get("age").asInt(), 35);
    
    // Verify new map has updated value
    MarkReader new_reader(updated);
    MapReader new_map = new_reader.getRoot().asMap();
    ASSERT_EQ(new_map.get("age").asInt(), 36);
}

TEST_F(MarkEditorTest, ImmutableModeVersionControl) {
    MarkBuilder builder(input);
    Item doc = builder.map()
        .put("counter", (int64_t)0)
        .final();
    
    input->root = doc;
    
    MarkEditor editor(input, EDIT_MODE_IMMUTABLE);
    
    // Make changes and commit versions
    doc = editor.map_update(doc, "counter", editor.builder()->createInt(1));
    input->root = doc;
    int v1 = editor.commit("Incremented to 1");
    ASSERT_EQ(v1, 0);
    
    doc = editor.map_update(doc, "counter", editor.builder()->createInt(2));
    input->root = doc;
    int v2 = editor.commit("Incremented to 2");
    ASSERT_EQ(v2, 1);
    
    doc = editor.map_update(doc, "counter", editor.builder()->createInt(3));
    input->root = doc;
    int v3 = editor.commit("Incremented to 3");
    ASSERT_EQ(v3, 2);
    
    // Undo twice
    ASSERT_TRUE(editor.undo());
    ASSERT_TRUE(editor.undo());
    
    // Verify we're at version 0 (counter == 1)
    Item current = editor.current();
    ConstItem counter_val = current.map->get("counter");
    ASSERT_EQ(counter_val.item & 0xFFFFFFFF, 1u);
    
    // Redo once
    ASSERT_TRUE(editor.redo());
    
    // Verify we're at version 1 (counter == 2)
    current = editor.current();
    counter_val = current.map->get("counter");
    ASSERT_EQ(counter_val.item & 0xFFFFFFFF, 2u);
}

//==============================================================================
// ELEMENT OPERATIONS
//==============================================================================

TEST_F(MarkEditorTest, ElementUpdateAttribute) {
    MarkBuilder builder(input);
    Item div = builder.element("div")
        .attr("class", "box")
        .attr("id", "main")
        .text("Content")
        .final();
    
    input->root = div;
    
    MarkEditor editor(input, EDIT_MODE_INLINE);
    
    // Update attribute
    Item updated = editor.elmt_update_attr(div, "class", editor.builder()->createStringItem("container"));
    
    ASSERT_NE(updated.element, nullptr);
    ASSERT_EQ(updated.element->type_id, LMD_TYPE_ELEMENT);
    
    // Verify attribute changed
    ConstItem class_val = updated.element->get_attr("class");
    ASSERT_EQ(class_val.type_id(), LMD_TYPE_STRING);
    ASSERT_STREQ(class_val.string()->chars, "container");
}

TEST_F(MarkEditorTest, ElementDeleteAttribute) {
    MarkBuilder builder(input);
    Item div = builder.element("div")
        .attr("class", "box")
        .attr("id", "main")
        .final();
    
    input->root = div;
    
    MarkEditor editor(input, EDIT_MODE_INLINE);
    
    // Delete attribute
    Item updated = editor.elmt_delete_attr(div, "id");
    
    ASSERT_NE(updated.element, nullptr);
    ASSERT_EQ(updated.element->type_id, LMD_TYPE_ELEMENT);
    ASSERT_FALSE(updated.element->has_attr("id"));
    ASSERT_TRUE(updated.element->has_attr("class"));
}

TEST_F(MarkEditorTest, ElementInsertChild) {
    MarkBuilder builder(input);
    Item div = builder.element("div")
        .text("Hello")
        .final();
    
    input->root = div;
    
    MarkEditor editor(input, EDIT_MODE_INLINE);
    
    // Insert child
    Item span = editor.builder()->element("span").text("World").final();
    Item updated = editor.elmt_insert_child(div, 1, span);
    
    ASSERT_NE(updated.element, nullptr);
    ASSERT_EQ(updated.element->type_id, LMD_TYPE_ELEMENT);
    ASSERT_EQ(updated.element->length, 2);
}

TEST_F(MarkEditorTest, ElementDeleteChild) {
    MarkBuilder builder(input);
    Item div = builder.element("div")
        .text("First")
        .text("Second")
        .text("Third")
        .final();
    
    input->root = div;
    
    MarkEditor editor(input, EDIT_MODE_INLINE);
    
    // Delete middle child
    Item updated = editor.elmt_delete_child(div, 1);
    
    ASSERT_NE(updated.element, nullptr);
    ASSERT_EQ(updated.element->type_id, LMD_TYPE_ELEMENT);
    ASSERT_EQ(updated.element->length, 2);
}

TEST_F(MarkEditorTest, ElementReplaceChild) {
    MarkBuilder builder(input);
    Item div = builder.element("div")
        .text("Old")
        .final();
    
    input->root = div;
    
    MarkEditor editor(input, EDIT_MODE_INLINE);
    
    // Replace child
    Item new_text = editor.builder()->createStringItem("New");
    Item updated = editor.elmt_replace_child(div, 0, new_text);
    
    ASSERT_NE(updated.element, nullptr);
    ASSERT_EQ(updated.element->type_id, LMD_TYPE_ELEMENT);
    ASSERT_EQ(updated.element->length, 1);
}

//==============================================================================
// ARRAY OPERATIONS
//==============================================================================

TEST_F(MarkEditorTest, ArraySet) {
    MarkBuilder builder(input);
    Item arr = builder.array()
        .append((int64_t)1)
        .append((int64_t)2)
        .append((int64_t)3)
        .final();
    
    input->root = arr;
    
    MarkEditor editor(input, EDIT_MODE_INLINE);
    
    // Set element
    Item updated = editor.array_set(arr, 1, editor.builder()->createInt(20));
    
    ASSERT_NE(updated.array, nullptr);
    ASSERT_EQ(updated.array->type_id, LMD_TYPE_ARRAY);
}

TEST_F(MarkEditorTest, ArrayInsert) {
    MarkBuilder builder(input);
    Item arr = builder.array()
        .append((int64_t)1)
        .append((int64_t)3)
        .final();
    
    input->root = arr;
    
    MarkEditor editor(input, EDIT_MODE_INLINE);
    
    // Insert element
    Item updated = editor.array_insert(arr, 1, editor.builder()->createInt(2));
    
    ASSERT_NE(updated.array, nullptr);
    ASSERT_EQ(updated.array->type_id, LMD_TYPE_ARRAY);
    ASSERT_EQ(updated.array->length, 3);
}

TEST_F(MarkEditorTest, ArrayDelete) {
    MarkBuilder builder(input);
    Item arr = builder.array()
        .append((int64_t)1)
        .append((int64_t)2)
        .append((int64_t)3)
        .final();
    
    input->root = arr;
    
    MarkEditor editor(input, EDIT_MODE_INLINE);
    
    // Delete element
    Item updated = editor.array_delete(arr, 1);
    
    ASSERT_NE(updated.array, nullptr);
    ASSERT_EQ(updated.array->type_id, LMD_TYPE_ARRAY);
    ASSERT_EQ(updated.array->length, 2);
}

TEST_F(MarkEditorTest, ArrayAppend) {
    MarkBuilder builder(input);
    Item arr = builder.array()
        .append((int64_t)1)
        .append((int64_t)2)
        .final();
    
    input->root = arr;
    
    MarkEditor editor(input, EDIT_MODE_INLINE);
    
    // Append element
    Item updated = editor.array_append(arr, editor.builder()->createInt(3));
    
    ASSERT_NE(updated.array, nullptr);
    ASSERT_EQ(updated.array->type_id, LMD_TYPE_ARRAY);
    ASSERT_EQ(updated.array->length, 3);
}

//==============================================================================
// MAIN
//==============================================================================

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
