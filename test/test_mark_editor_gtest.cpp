// Test file for MarkEditor - CRUD operations on Lambda documents
#include <gtest/gtest.h>
#include "../lambda/mark_editor.hpp"
#include "../lambda/mark_reader.hpp"
#include "../lambda/input/input.hpp"
#include "../lambda/format/format.h"
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
    ConstItem counter_val2 = current.map->get("counter");
    ASSERT_EQ(counter_val2.item & 0xFFFFFFFF, 2u);
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
// COMPOSITE VALUE TESTS
//==============================================================================

TEST_F(MarkEditorTest, MapWithNestedMap) {
    MarkBuilder builder(input);

    // Create nested map
    Item address = builder.map()
        .put("street", "123 Main St")
        .put("city", "Boston")
        .final();

    // Create parent map with nested map
    Item doc = builder.map()
        .put("name", "Alice")
        .put("address", address)
        .final();

    input->root = doc;

    MarkEditor editor(input, EDIT_MODE_INLINE);

    // Update nested map
    Item new_address = editor.builder()->map()
        .put("street", "456 Elm St")
        .put("city", "Cambridge")
        .put("zip", "02139")
        .final();

    Item updated = editor.map_update(doc, "address", new_address);

    ASSERT_NE(updated.map, nullptr);
    ASSERT_EQ(updated.map->type_id, LMD_TYPE_MAP);

    // Verify nested map using MarkReader
    MarkReader reader(updated);
    MapReader map_reader = reader.getRoot().asMap();
    ItemReader addr_reader = map_reader.get("address");
    ASSERT_TRUE(addr_reader.isMap());

    MapReader addr_map = addr_reader.asMap();
    ASSERT_EQ(addr_map.get("city").cstring(), std::string("Cambridge"));
    ASSERT_EQ(addr_map.get("zip").cstring(), std::string("02139"));
}

TEST_F(MarkEditorTest, MapWithArray) {
    MarkBuilder builder(input);

    // Create array
    Item tags = builder.array()
        .append("cpp")
        .append("lambda")
        .append("functional")
        .final();

    // Create map with array
    Item doc = builder.map()
        .put("name", "Project")
        .put("tags", tags)
        .final();

    input->root = doc;

    MarkEditor editor(input, EDIT_MODE_INLINE);

    // Update the array
    Item new_tags = editor.builder()->array()
        .append("cpp")
        .append("lambda")
        .append("scripting")
        .final();

    Item updated = editor.map_update(doc, "tags", new_tags);

    ASSERT_NE(updated.map, nullptr);
    ASSERT_EQ(updated.map->type_id, LMD_TYPE_MAP);

    // Verify array using MarkReader
    MarkReader reader(updated);
    MapReader map_reader = reader.getRoot().asMap();
    ItemReader tags_reader = map_reader.get("tags");
    ASSERT_TRUE(tags_reader.isArray());

    ArrayReader arr = tags_reader.asArray();
    ASSERT_EQ(arr.length(), 3);
}

TEST_F(MarkEditorTest, ArrayOfMaps) {
    MarkBuilder builder(input);

    // Create array of maps
    Item user1 = builder.map().put("name", "Alice").put("age", (int64_t)30).final();
    Item user2 = builder.map().put("name", "Bob").put("age", (int64_t)25).final();

    Item arr = builder.array()
        .append(user1)
        .append(user2)
        .final();

    input->root = arr;

    MarkEditor editor(input, EDIT_MODE_INLINE);

    // Update first element
    Item new_user = editor.builder()->map()
        .put("name", "Alice Updated")
        .put("age", (int64_t)31)
        .final();

    Item updated = editor.array_set(arr, 0, new_user);

    ASSERT_NE(updated.array, nullptr);
    ASSERT_EQ(updated.array->type_id, LMD_TYPE_ARRAY);
    ASSERT_EQ(updated.array->length, 2);

    // Verify using MarkReader
    MarkReader reader(updated);
    ArrayReader arr_reader = reader.getRoot().asArray();
    ItemReader first = arr_reader.get(0);
    ASSERT_TRUE(first.isMap());

    MapReader first_map = first.asMap();
    ASSERT_EQ(first_map.get("name").cstring(), std::string("Alice Updated"));
    ASSERT_EQ(first_map.get("age").asInt(), 31);
}

TEST_F(MarkEditorTest, DeepNestedStructure) {
    MarkBuilder builder(input);

    // Create deeply nested structure: map -> array -> map -> array
    Item inner_array = builder.array()
        .append((int64_t)1)
        .append((int64_t)2)
        .final();

    Item inner_map = builder.map()
        .put("values", inner_array)
        .final();

    Item outer_array = builder.array()
        .append(inner_map)
        .final();

    Item doc = builder.map()
        .put("data", outer_array)
        .final();

    input->root = doc;

    MarkEditor editor(input, EDIT_MODE_IMMUTABLE);

    // Update the nested structure
    Item new_inner_array = editor.builder()->array()
        .append((int64_t)10)
        .append((int64_t)20)
        .append((int64_t)30)
        .final();

    Item new_inner_map = editor.builder()->map()
        .put("values", new_inner_array)
        .put("count", (int64_t)3)
        .final();

    Item new_outer_array = editor.builder()->array()
        .append(new_inner_map)
        .final();

    Item updated = editor.map_update(doc, "data", new_outer_array);

    ASSERT_NE(updated.map, nullptr);
    ASSERT_NE(updated.map, doc.map);  // Immutable mode creates new instance

    // Verify deep structure
    MarkReader reader(updated);
    MapReader root = reader.getRoot().asMap();
    ItemReader data = root.get("data");
    ASSERT_TRUE(data.isArray());

    ArrayReader outer_arr = data.asArray();
    ASSERT_EQ(outer_arr.length(), 1);

    ItemReader first_elem = outer_arr.get(0);
    ASSERT_TRUE(first_elem.isMap());

    MapReader inner = first_elem.asMap();
    ASSERT_EQ(inner.get("count").asInt(), 3);
}

//==============================================================================
// NEGATIVE TESTS
//==============================================================================

TEST_F(MarkEditorTest, MapUpdateNullMap) {
    MarkEditor editor(input, EDIT_MODE_INLINE);

    Item null_map = ItemNull;  // Use predefined null item
    Item result = editor.map_update(null_map, "key", editor.builder()->createInt(42));

    // Should return error
    ASSERT_EQ(result._type_id, LMD_TYPE_ERROR);
}

TEST_F(MarkEditorTest, MapUpdateWrongType) {
    MarkBuilder builder(input);

    // Create array, not map
    Item arr = builder.array().append((int64_t)1).final();
    input->root = arr;

    MarkEditor editor(input, EDIT_MODE_INLINE);

    // Try to update as map
    Item result = editor.map_update(arr, "key", editor.builder()->createInt(42));

    // Should return error
    ASSERT_EQ(result._type_id, LMD_TYPE_ERROR);
}

TEST_F(MarkEditorTest, MapDeleteNonexistentField) {
    MarkBuilder builder(input);
    Item doc = builder.map()
        .put("name", "Alice")
        .final();

    input->root = doc;

    MarkEditor editor(input, EDIT_MODE_INLINE);

    // Delete non-existent field
    Item result = editor.map_delete(doc, "nonexistent");

    // Should succeed but return unchanged map
    ASSERT_NE(result.map, nullptr);
    ASSERT_EQ(result.map->type_id, LMD_TYPE_MAP);

    MarkReader reader(result);
    MapReader map_reader = reader.getRoot().asMap();
    ASSERT_TRUE(map_reader.has("name"));
    ASSERT_FALSE(map_reader.has("nonexistent"));
}

TEST_F(MarkEditorTest, MapBatchUpdateZeroCount) {
    MarkBuilder builder(input);
    Item doc = builder.map()
        .put("name", "Alice")
        .final();

    input->root = doc;

    MarkEditor editor(input, EDIT_MODE_INLINE);

    // Batch update with count=0
    Item result = editor.map_update_batch(doc, 0);

    // Should return original map unchanged
    ASSERT_EQ(result.map, doc.map);
}

TEST_F(MarkEditorTest, ArraySetOutOfBounds) {
    MarkBuilder builder(input);
    Item arr = builder.array()
        .append((int64_t)1)
        .append((int64_t)2)
        .final();

    input->root = arr;

    MarkEditor editor(input, EDIT_MODE_INLINE);

    // Try to set element beyond bounds
    Item result = editor.array_set(arr, 10, editor.builder()->createInt(42));

    // Should return error
    ASSERT_EQ(result._type_id, LMD_TYPE_ERROR);
}

TEST_F(MarkEditorTest, ArraySetNegativeIndex) {
    MarkBuilder builder(input);
    Item arr = builder.array()
        .append((int64_t)1)
        .final();

    input->root = arr;

    MarkEditor editor(input, EDIT_MODE_INLINE);

    // Try negative index
    Item result = editor.array_set(arr, -1, editor.builder()->createInt(42));

    // Should return error
    ASSERT_EQ(result._type_id, LMD_TYPE_ERROR);
}

TEST_F(MarkEditorTest, ArrayInsertOutOfBounds) {
    MarkBuilder builder(input);
    Item arr = builder.array()
        .append((int64_t)1)
        .final();

    input->root = arr;

    MarkEditor editor(input, EDIT_MODE_INLINE);

    // Insert at index > length should fail
    Item result = editor.array_insert(arr, 10, editor.builder()->createInt(42));

    // Should return error
    ASSERT_EQ(result._type_id, LMD_TYPE_ERROR);
}

TEST_F(MarkEditorTest, ArrayDeleteOutOfBounds) {
    MarkBuilder builder(input);
    Item arr = builder.array()
        .append((int64_t)1)
        .final();

    input->root = arr;

    MarkEditor editor(input, EDIT_MODE_INLINE);

    // Delete beyond bounds
    Item result = editor.array_delete(arr, 5);

    // Should return error
    ASSERT_EQ(result._type_id, LMD_TYPE_ERROR);
}

TEST_F(MarkEditorTest, ArrayAppendToNonArray) {
    MarkBuilder builder(input);

    // Create map, not array
    Item map = builder.map().put("key", "value").final();
    input->root = map;

    MarkEditor editor(input, EDIT_MODE_INLINE);

    // Try to append to map
    Item result = editor.array_append(map, editor.builder()->createInt(42));

    // Should return error
    ASSERT_EQ(result._type_id, LMD_TYPE_ERROR);
}

TEST_F(MarkEditorTest, ElementUpdateAttributeNonElement) {
    MarkBuilder builder(input);

    // Create map, not element
    Item map = builder.map().put("key", "value").final();
    input->root = map;

    MarkEditor editor(input, EDIT_MODE_INLINE);

    // Try to update attribute on map
    Item result = editor.elmt_update_attr(map, "attr", editor.builder()->createInt(42));

    // Should return error
    ASSERT_EQ(result._type_id, LMD_TYPE_ERROR);
}

TEST_F(MarkEditorTest, ImmutableModePreservesOriginal) {
    MarkBuilder builder(input);
    Item doc = builder.map()
        .put("count", (int64_t)10)
        .final();

    input->root = doc;

    MarkEditor editor(input, EDIT_MODE_IMMUTABLE);

    // Save original value
    MarkReader orig_reader(doc);
    int64_t orig_value = orig_reader.getRoot().asMap().get("count").asInt();
    ASSERT_EQ(orig_value, 10);

    // Update in immutable mode
    Item updated = editor.map_update(doc, "count", editor.builder()->createLong(20));

    // Original should be unchanged
    MarkReader check_orig(doc);
    ASSERT_EQ(check_orig.getRoot().asMap().get("count").asInt(), 10);

    // Updated should have new value
    MarkReader check_updated(updated);
    ASSERT_EQ(check_updated.getRoot().asMap().get("count").asInt(), 20);

    // Should be different instances
    ASSERT_NE(doc.map, updated.map);
}

TEST_F(MarkEditorTest, ImmutableModeSerializationVerification) {
    // Create a complex document with nested structures
    MarkBuilder builder(input);

    Item address = builder.map()
        .put("street", "123 Main St")
        .put("city", "Boston")
        .put("zip", "02101")
        .final();

    Item tags = builder.array()
        .append("developer")
        .append("engineer")
        .final();

    Item doc = builder.map()
        .put("name", "Alice")
        .put("age", (int64_t)30)
        .put("address", address)
        .put("tags", tags)
        .final();

    input->root = doc;

    // Serialize v1 to string s1
    String* s1 = format_json(pool, doc);
    ASSERT_NE(s1, nullptr);
    ASSERT_GT(s1->len, 0u);

    // Edit in immutable mode
    MarkEditor editor(input, EDIT_MODE_IMMUTABLE);

    // Perform multiple edits
    Item updated1 = editor.map_update(doc, "age", editor.builder()->createLong(31));
    Item updated2 = editor.map_update(updated1, "name", editor.builder()->createStringItem("Alice Updated"));

    Item new_address = editor.builder()->map()
        .put("street", "456 Elm St")
        .put("city", "Cambridge")
        .put("zip", "02139")
        .final();
    Item updated3 = editor.map_update(updated2, "address", new_address);

    // Serialize original doc again to string s2
    String* s2 = format_json(pool, doc);
    ASSERT_NE(s2, nullptr);
    ASSERT_GT(s1->len, 0u);

    // Verify that s1 == s2 (original unchanged)
    ASSERT_EQ(s1->len, s2->len);
    ASSERT_EQ(strncmp(s1->chars, s2->chars, s1->len), 0);

    // Verify that updated document is different
    String* s3 = format_json(pool, updated3);
    ASSERT_NE(s3, nullptr);
    ASSERT_GT(s1->len, 0u);
    ASSERT_NE(strncmp(s1->chars, s3->chars, s1->len), 0);  // s1 != s3
}

TEST_F(MarkEditorTest, ImmutableModeElementSerializationVerification) {
    // Create a complex element structure with nested children
    MarkBuilder builder(input);

    Item child1 = builder.element("span")
        .attr("class", "highlight")
        .text("Hello")
        .final();

    Item child2 = builder.element("strong")
        .text("World")
        .final();

    Item doc = builder.element("div")
        .attr("id", "container")
        .attr("class", "box")
        .child(child1)
        .child(child2)
        .final();

    input->root = doc;

    // Serialize v1 to string s1
    String* s1 = format_html(pool, doc);
    ASSERT_NE(s1, nullptr);
    ASSERT_GT(s1->len, 0u);

    // Edit in immutable mode
    MarkEditor editor(input, EDIT_MODE_IMMUTABLE);

    // Perform multiple element edits
    Item updated1 = editor.elmt_update_attr(doc, "class", editor.builder()->createStringItem("container"));
    Item updated2 = editor.elmt_update_attr(updated1, "data-value", editor.builder()->createInt(42));

    Item new_child = editor.builder()->element("em")
        .text("New Text")
        .final();
    Item updated3 = editor.elmt_insert_child(updated2, 1, new_child);

    // Serialize original doc again to string s2
    String* s2 = format_html(pool, doc);
    ASSERT_NE(s2, nullptr);
    ASSERT_GT(s1->len, 0u);

    // Verify that s1 == s2 (original unchanged)
    ASSERT_EQ(s1->len, s2->len);
    ASSERT_EQ(strncmp(s1->chars, s2->chars, s1->len), 0);

    // Verify that updated document is different
    String* s3 = format_html(pool, updated3);
    ASSERT_NE(s3, nullptr);
    ASSERT_GT(s1->len, 0u);
    ASSERT_NE(strncmp(s1->chars, s3->chars, s1->len), 0);  // s1 != s3

    // Verify original element structure is intact
    MarkReader orig_reader(doc);
    ElementReader orig_elem = orig_reader.getRoot().asElement();
    ASSERT_EQ(orig_elem.get_attr("class").cstring(), std::string("box"));
    ASSERT_FALSE(orig_elem.has_attr("data-value"));
    ASSERT_EQ(orig_elem.childCount(), 2);  // Still 2 children, not 3
}

//==============================================================================
// EXTERNAL VALUE HANDLING - Phase 5b Tests
//==============================================================================

TEST_F(MarkEditorTest, MapUpdateWithExternalValue) {
    // Create two separate Inputs (simulating data from different sources)
    Pool* external_pool = pool_create();
    ASSERT_NE(external_pool, nullptr);

    Input* external_input = Input::create(external_pool);
    ASSERT_NE(external_input, nullptr);

    // Create external value in external_input
    MarkBuilder external_builder(external_input);
    Item external_value = external_builder.map()
        .put("street", "123 Main St")
        .put("city", "Boston")
        .final();

    // Create target document in our input
    MarkBuilder builder(input);
    Item doc = builder.map()
        .put("name", "Alice")
        .put("age", (int64_t)30)
        .final();

    input->root = doc;

    // Update with external value - should deep copy
    MarkEditor editor(input, EDIT_MODE_INLINE);
    Item updated = editor.map_update(doc, "address", external_value);

    ASSERT_NE(updated.map, nullptr);

    // Verify the value was copied to target arena
    ConstItem address_val = updated.map->get("address");
    ASSERT_EQ(address_val.type_id(), LMD_TYPE_MAP);

    // The copied map should be in our arena (not external arena)
    ASSERT_TRUE(arena_owns(input->arena, address_val.map));
    ASSERT_FALSE(arena_owns(external_input->arena, address_val.map));

    // Verify nested fields were also copied
    MarkReader reader(updated);
    MapReader map_reader = reader.getRoot().asMap();
    MapReader address_reader = map_reader.get("address").asMap();
    ASSERT_EQ(address_reader.get("street").cstring(), std::string("123 Main St"));
    ASSERT_EQ(address_reader.get("city").cstring(), std::string("Boston"));

    // Clean up external input
    pool_destroy(external_pool);
}

TEST_F(MarkEditorTest, ElementInsertChildWithExternalValue) {
    // TODO: Deep copy of elements with external shapes/names needs debugging
    // The shape pooling and NamePool interactions make this more complex.
    // For now, test basic functionality without destroying external pool.

    // Create external Input with data
    Pool* external_pool = pool_create();
    Input* external_input = Input::create(external_pool);

    MarkBuilder external_builder(external_input);
    Item external_child = external_builder.element("span")
        .attr("class", "highlight")
        .child(external_builder.createStringItem("External Text"))
        .final();

    // Create target element in our input
    MarkBuilder builder(input);
    Item doc = builder.element("div")
        .child(builder.createStringItem("Child 1"))
        .final();

    input->root = doc;

    // Insert external child - should trigger deep copy
    MarkEditor editor(input, EDIT_MODE_INLINE);
    Item updated = editor.elmt_insert_child(doc, -1, external_child);  // append

    ASSERT_NE(updated.element, nullptr);
    ASSERT_EQ(updated.element->length, 2);

    // Verify the child was copied to our arena
    Item child2 = updated.element->items[1];
    ASSERT_EQ(get_type_id(child2), LMD_TYPE_ELEMENT);
    ASSERT_TRUE(arena_owns(input->arena, child2.element));

    // Clean up external pool
    pool_destroy(external_pool);
}

TEST_F(MarkEditorTest, ArraySetWithExternalValue) {
    // Create external Input with data
    Pool* external_pool = pool_create();
    Input* external_input = Input::create(external_pool);

    MarkBuilder external_builder(external_input);
    Item external_map = external_builder.map()
        .put("x", (int64_t)100)
        .put("y", (int64_t)200)
        .final();

    // Create target array in our input
    MarkBuilder builder(input);
    Item doc = builder.array()
        .append(builder.map().put("x", (int64_t)1).put("y", (int64_t)2).final())
        .append(builder.map().put("x", (int64_t)3).put("y", (int64_t)4).final())
        .final();

    input->root = doc;

    // Set array element with external value - should deep copy
    MarkEditor editor(input, EDIT_MODE_INLINE);
    Item updated = editor.array_set(doc, 1, external_map);

    ASSERT_NE(updated.array, nullptr);
    ASSERT_EQ(updated.array->length, 2);

    // Verify the value was copied to our arena
    Item item1 = updated.array->items[1];
    ASSERT_EQ(get_type_id(item1), LMD_TYPE_MAP);
    ASSERT_TRUE(arena_owns(input->arena, item1.map));
    ASSERT_FALSE(arena_owns(external_input->arena, item1.map));

    // Verify values
    MarkReader reader(updated);
    ArrayReader arr_reader = reader.getRoot().asArray();
    MapReader map1_reader = arr_reader.get(1).asMap();
    ASSERT_EQ(map1_reader.get("x").asInt(), 100);
    ASSERT_EQ(map1_reader.get("y").asInt(), 200);

    pool_destroy(external_pool);
}

TEST_F(MarkEditorTest, ArrayInsertWithExternalValue) {
    // Create external Input
    Pool* external_pool = pool_create();
    Input* external_input = Input::create(external_pool);

    MarkBuilder external_builder(external_input);
    Item external_item = external_builder.createStringItem("External String");

    // Create target array
    MarkBuilder builder(input);
    Item doc = builder.array()
        .append(builder.createStringItem("A"))
        .append(builder.createStringItem("B"))
        .final();

    input->root = doc;

    // Insert external value - should deep copy
    MarkEditor editor(input, EDIT_MODE_INLINE);
    Item updated = editor.array_insert(doc, 1, external_item);

    ASSERT_NE(updated.array, nullptr);
    ASSERT_EQ(updated.array->length, 3);

    // Verify insertion
    MarkReader reader(updated);
    ArrayReader arr_reader = reader.getRoot().asArray();
    ASSERT_EQ(arr_reader.get(0).cstring(), std::string("A"));
    ASSERT_EQ(arr_reader.get(1).cstring(), std::string("External String"));
    ASSERT_EQ(arr_reader.get(2).cstring(), std::string("B"));

    // Verify the string was copied to our arena
    Item item1 = updated.array->items[1];
    ASSERT_EQ(get_type_id(item1), LMD_TYPE_STRING);
    String* str = it2s(item1);
    ASSERT_TRUE(arena_owns(input->arena, str));
    ASSERT_FALSE(arena_owns(external_input->arena, str));

    pool_destroy(external_pool);
}

TEST_F(MarkEditorTest, NestedExternalStructureDeepCopy) {
    // Create complex nested external structure
    Pool* external_pool = pool_create();
    Input* external_input = Input::create(external_pool);

    MarkBuilder external_builder(external_input);
    Item external_nested = external_builder.map()
        .put("users", external_builder.array()
            .append(external_builder.map().put("id", (int64_t)1).put("name", "User1").final())
            .append(external_builder.map().put("id", (int64_t)2).put("name", "User2").final())
            .final())
        .put("count", (int64_t)2)
        .final();

    // Create target document
    MarkBuilder builder(input);
    Item doc = builder.map()
        .put("title", "Document")
        .final();

    input->root = doc;

    // Add external nested structure
    MarkEditor editor(input, EDIT_MODE_INLINE);
    Item updated = editor.map_update(doc, "data", external_nested);

    ASSERT_NE(updated.map, nullptr);

    // Verify entire nested structure was deep copied to our arena
    ConstItem data_val = updated.map->get("data");
    ASSERT_EQ(data_val.type_id(), LMD_TYPE_MAP);
    ASSERT_TRUE(arena_owns(input->arena, data_val.map));

    // Check nested array
    ConstItem users_val = data_val.map->get("users");
    ASSERT_EQ(users_val.type_id(), LMD_TYPE_ARRAY);
    ASSERT_TRUE(arena_owns(input->arena, users_val.array));

    // Check nested maps in array
    const Array* users_arr = users_val.array;
    ASSERT_EQ(users_arr->length, 2);

    Item user1 = users_arr->items[0];
    ASSERT_EQ(get_type_id(user1), LMD_TYPE_MAP);
    ASSERT_TRUE(arena_owns(input->arena, user1.map));

    // Verify data integrity
    MarkReader reader(updated);
    MapReader map_reader = reader.getRoot().asMap();
    MapReader data_reader = map_reader.get("data").asMap();
    ArrayReader users_reader = data_reader.get("users").asArray();
    ASSERT_EQ(users_reader.length(), 2);
    ASSERT_EQ(users_reader.get(0).asMap().get("name").cstring(), std::string("User1"));
    ASSERT_EQ(users_reader.get(1).asMap().get("name").cstring(), std::string("User2"));

    pool_destroy(external_pool);
}

//==============================================================================
// EXTERNAL INPUT TESTS - Deep copy with NamePool/ShapePool lifecycle
//==============================================================================

// Test deep copying an element from external Input that gets destroyed
TEST(ExternalInputTest, DeepCopyExternalElement) {
    // Initialize logging
    log_init(NULL);

    // Create parent pool (will remain alive)
    Pool* parent_pool = pool_create();
    ASSERT_NE(parent_pool, nullptr);
    Input* parent_input = Input::create(parent_pool);

    // Create external Input with a simple element
    Pool* external_pool = pool_create();
    Input* external_input = Input::create(external_pool);
    MarkBuilder external_builder(external_input);

    // Build an element in the external Input
    Item external_elem = external_builder
        .element("div")
            .attr("class", "container")
            .attr("id", "main")
        .final();

    ASSERT_EQ(external_elem.type_id(), LMD_TYPE_ELEMENT);
    ASSERT_NE(external_elem.element, nullptr);

    // Create target Input (where we'll deep copy)
    Pool* target_pool = pool_create();
    Input* target_input = Input::create(target_pool);
    MarkBuilder target_builder(target_input);

    // Deep copy the external element into target Input
    Item copied_elem = target_builder.deep_copy(external_elem);

    ASSERT_EQ(copied_elem.type_id(), LMD_TYPE_ELEMENT);
    ASSERT_NE(copied_elem.element, nullptr);

    // CRITICAL: Destroy the external pool (frees its NamePool and shape_pool)
    pool_destroy(external_pool);

    // Now try to access the copied element's attributes
    // This MUST NOT crash - the copied element should have all its data
    // stored in target_input's pools, not the external pool
    Element* elem = copied_elem.element;
    TypeElmt* elem_type = (TypeElmt*)elem->type;

    // Check tag name - should be accessible
    ASSERT_NE(elem_type->name.str, nullptr);
    ASSERT_STREQ(elem_type->name.str, "div");

    // Try to get attributes via ElementReader - this should work now with the fix
    ElementReader copied_reader(elem);
    EXPECT_EQ(copied_reader.get_attr("class").cstring(), std::string("container"));
    EXPECT_EQ(copied_reader.get_attr("id").cstring(), std::string("main"));

    // Cleanup
    pool_destroy(target_pool);
    pool_destroy(parent_pool);
}

// Test for DOM CRUD scenario: IMMUTABLE mode for adding attributes
TEST_F(MarkEditorTest, DomCrudScenario_AddAttributeWithImmutableMode) {
    // Step 1: Create element with initial attribute
    MarkBuilder builder(input);
    Item elem_item = builder.element("div")
        .attr("_init", "placeholder")
        .final();

    ASSERT_NE(elem_item.element, nullptr);
    input->root = elem_item;

    // Verify initial attribute
    ElementReader reader1(elem_item.element);
    EXPECT_STREQ(reader1.get_attr("_init").cstring(), "placeholder");

    // Step 2: Use IMMUTABLE mode to add new attribute
    MarkEditor editor(input, EDIT_MODE_IMMUTABLE);
    Item value_item = editor.builder()->createStringItem("value1");

    Item updated = editor.elmt_update_attr(elem_item, "data-test", value_item);
    ASSERT_NE(updated.element, nullptr);

    // In IMMUTABLE mode, a NEW element is created
    EXPECT_NE(updated.element, elem_item.element) << "IMMUTABLE mode should create new element";

    // Verify both attributes exist in new element
    ElementReader reader2(updated.element);
    EXPECT_STREQ(reader2.get_attr("_init").cstring(), "placeholder");
    EXPECT_STREQ(reader2.get_attr("data-test").cstring(), "value1");

    // Original element unchanged
    ElementReader reader_orig(elem_item.element);
    EXPECT_STREQ(reader_orig.get_attr("_init").cstring(), "placeholder");
    EXPECT_EQ(reader_orig.get_attr("data-test").cstring(), nullptr) << "Original should not have new attribute";

    // Step 3: Add another attribute (use updated element as base)
    Item value_item2 = editor.builder()->createStringItem("value2");
    Item updated2 = editor.elmt_update_attr(updated, "data-test2", value_item2);
    ASSERT_NE(updated2.element, nullptr);

    // Verify all three attributes
    ElementReader reader3(updated2.element);
    EXPECT_STREQ(reader3.get_attr("_init").cstring(), "placeholder");
    EXPECT_STREQ(reader3.get_attr("data-test").cstring(), "value1");
    EXPECT_STREQ(reader3.get_attr("data-test2").cstring(), "value2");
}

// Test for DOM CRUD scenario: INLINE mode for adding attributes (after fix)
TEST_F(MarkEditorTest, DomCrudScenario_AddAttributeWithInlineMode) {
    // Step 1: Create element with initial attribute
    MarkBuilder builder(input);
    Item elem_item = builder.element("div")
        .attr("_init", "placeholder")
        .final();

    ASSERT_NE(elem_item.element, nullptr);
    input->root = elem_item;

    // Verify initial attribute
    ElementReader reader1(elem_item.element);
    EXPECT_STREQ(reader1.get_attr("_init").cstring(), "placeholder");

    Element* original_ptr = elem_item.element;

    // Step 2: Use INLINE mode to add new attribute (should work now!)
    MarkEditor editor(input, EDIT_MODE_INLINE);
    Item value_item = editor.builder()->createStringItem("value1");

    Item updated = editor.elmt_update_attr(elem_item, "data-test", value_item);
    ASSERT_NE(updated.element, nullptr);

    // In INLINE mode, element pointer stays the same
    EXPECT_EQ(updated.element, original_ptr) << "INLINE mode should modify in-place";

    // Verify both attributes exist
    ElementReader reader2(updated.element);
    EXPECT_STREQ(reader2.get_attr("_init").cstring(), "placeholder");
    EXPECT_STREQ(reader2.get_attr("data-test").cstring(), "value1");

    // Step 3: Add another attribute
    Item value_item2 = editor.builder()->createStringItem("value2");
    Item updated2 = editor.elmt_update_attr(updated, "data-test2", value_item2);
    ASSERT_NE(updated2.element, nullptr);
    EXPECT_EQ(updated2.element, original_ptr) << "Should still be same element";

    // Verify all three attributes
    ElementReader reader3(updated2.element);
    EXPECT_STREQ(reader3.get_attr("_init").cstring(), "placeholder");
    EXPECT_STREQ(reader3.get_attr("data-test").cstring(), "value1");
    EXPECT_STREQ(reader3.get_attr("data-test2").cstring(), "value2");

    // Step 4: Update an existing attribute
    Item value_item3 = editor.builder()->createStringItem("updated_value");
    Item updated3 = editor.elmt_update_attr(updated2, "data-test", value_item3);
    ASSERT_NE(updated3.element, nullptr);

    // Verify update worked
    ElementReader reader4(updated3.element);
    EXPECT_STREQ(reader4.get_attr("_init").cstring(), "placeholder");
    EXPECT_STREQ(reader4.get_attr("data-test").cstring(), "updated_value");
    EXPECT_STREQ(reader4.get_attr("data-test2").cstring(), "value2");
}

//==============================================================================
// MAIN
//==============================================================================

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
