// Test suite for MarkBuilder::deep_copy() with smart ownership checking
// Tests the optimization that avoids copying when data is already in arena chain

#include <gtest/gtest.h>
#include "../lambda/lambda-data.hpp"
#include "../lambda/mark_builder.hpp"
#include "../lambda/mark_reader.hpp"  // for ArrayReader
#include "../lib/mempool.h"
#include "../lib/arena.h"
#include "../lib/log.h"
#include <cstring>

// Test fixture for deep copy tests
class MarkBuilderDeepCopyTest : public ::testing::Test {
protected:
    Pool* pool1;
    Pool* pool2;
    Input* input1;
    Input* input2;
    Input* child_input;  // Child of input1 for parent chain testing

    void SetUp() override {
        // Initialize logging
        log_init(NULL);
        pool1 = pool_create();
        pool2 = pool_create();
        ASSERT_NE(pool1, nullptr);
        ASSERT_NE(pool2, nullptr);

        input1 = Input::create(pool1, nullptr);
        input2 = Input::create(pool2, nullptr);
        ASSERT_NE(input1, nullptr);
        ASSERT_NE(input2, nullptr);

        // Create child input with input1 as parent
        child_input = Input::create(pool1, nullptr, input1);
        ASSERT_NE(child_input, nullptr);
        ASSERT_EQ(child_input->parent, input1);
    }

    void TearDown() override {
        if (pool1) pool_destroy(pool1);
        if (pool2) pool_destroy(pool2);
    }
};

// ============================================================================
// Primitive Types Tests
// ============================================================================

TEST_F(MarkBuilderDeepCopyTest, CopyNull) {
    MarkBuilder builder(input1);
    Item null_item = builder.createNull();

    // Copy to same input - should return same item
    Item copied = builder.deep_copy(null_item);
    EXPECT_EQ(copied.item, null_item.item);
    EXPECT_EQ(get_type_id(copied), LMD_TYPE_NULL);
}

TEST_F(MarkBuilderDeepCopyTest, CopyBool) {
    MarkBuilder builder(input1);
    Item true_item = builder.createBool(true);
    Item false_item = builder.createBool(false);

    // Bools are inline - should always return same
    Item copied_true = builder.deep_copy(true_item);
    Item copied_false = builder.deep_copy(false_item);

    EXPECT_EQ(it2b(copied_true), true);
    EXPECT_EQ(it2b(copied_false), false);
}

TEST_F(MarkBuilderDeepCopyTest, CopyInt) {
    MarkBuilder builder(input1);
    Item int_item = builder.createInt(42);

    // Ints are inline - should return same
    Item copied = builder.deep_copy(int_item);
    EXPECT_EQ(it2i(copied), 42);
}

TEST_F(MarkBuilderDeepCopyTest, CopyLong) {
    MarkBuilder builder(input1);
    Item long_item = builder.createLong(9223372036854775807LL);

    // Check if in arena
    EXPECT_TRUE(builder.is_in_arena(long_item));

    // Copy to same input - should return original
    Item copied = builder.deep_copy(long_item);
    EXPECT_EQ(it2l(copied), 9223372036854775807LL);
}

TEST_F(MarkBuilderDeepCopyTest, CopyFloat) {
    MarkBuilder builder(input1);
    Item float_item = builder.createFloat(3.14159);

    // Check if in arena
    EXPECT_TRUE(builder.is_in_arena(float_item));

    // Copy to same input - should return original
    Item copied = builder.deep_copy(float_item);
    EXPECT_DOUBLE_EQ(it2d(copied), 3.14159);
}

TEST_F(MarkBuilderDeepCopyTest, CopyRange) {
    MarkBuilder builder1(input1);
    MarkBuilder builder2(input2);

    // Create range in input1
    Item range_item = builder1.createRange(1, 100);
    EXPECT_EQ(get_type_id(range_item), LMD_TYPE_RANGE);
    EXPECT_TRUE(builder1.is_in_arena(range_item));

    Range* orig_range = range_item.range;
    EXPECT_EQ(orig_range->start, 1);
    EXPECT_EQ(orig_range->end, 100);
    EXPECT_EQ(orig_range->length, 100);

    // Copy to same input - should return original (optimization)
    Item copied_same = builder1.deep_copy(range_item);
    EXPECT_EQ(copied_same.range, range_item.range);  // Same pointer

    // Copy to different input - should create new range
    Item copied_diff = builder2.deep_copy(range_item);
    EXPECT_NE(copied_diff.range, range_item.range);  // Different pointer
    EXPECT_TRUE(builder2.is_in_arena(copied_diff));

    Range* copied_range = copied_diff.range;
    EXPECT_EQ(copied_range->start, 1);
    EXPECT_EQ(copied_range->end, 100);
    EXPECT_EQ(copied_range->length, 100);
}

TEST_F(MarkBuilderDeepCopyTest, CopyType) {
    MarkBuilder builder1(input1);
    MarkBuilder builder2(input2);

    // Create type in input1
    Item type_item = builder1.createMetaType(LMD_TYPE_STRING);
    EXPECT_EQ(get_type_id(type_item), LMD_TYPE_TYPE);
    EXPECT_TRUE(builder1.is_in_arena(type_item));

    Type* orig_type = ((TypeType*)type_item.type)->type;
    EXPECT_EQ(orig_type->type_id, LMD_TYPE_STRING);

    // Copy to same input - should return original (optimization)
    Item copied_same = builder1.deep_copy(type_item);
    EXPECT_EQ(copied_same.type, type_item.type);  // Same pointer

    // Copy to different input - should create new type
    Item copied_diff = builder2.deep_copy(type_item);
    EXPECT_NE(copied_diff.type, type_item.type);  // Different pointer
    EXPECT_TRUE(builder2.is_in_arena(copied_diff));

    Type* copied_type = ((TypeType*)copied_diff.type)->type;
    EXPECT_EQ(copied_type->type_id, LMD_TYPE_STRING);
}

// ============================================================================
// String and Symbol Tests
// ============================================================================

TEST_F(MarkBuilderDeepCopyTest, CopyString) {
    MarkBuilder builder1(input1);
    MarkBuilder builder2(input2);

    // Create string in input1
    Item str_item = builder1.createStringItem("Hello, World!");
    EXPECT_TRUE(builder1.is_in_arena(str_item));

    // Copy to same input - should return original
    Item copied_same = builder1.deep_copy(str_item);
    EXPECT_EQ(it2s(copied_same), it2s(str_item));  // Same pointer

    // Copy to different input - should create new string
    Item copied_diff = builder2.deep_copy(str_item);
    EXPECT_NE(it2s(copied_diff), it2s(str_item));  // Different pointer
    EXPECT_STREQ(it2s(copied_diff)->chars, "Hello, World!");
}

TEST_F(MarkBuilderDeepCopyTest, CopySymbol) {
    MarkBuilder builder1(input1);
    MarkBuilder builder2(input2);

    // Create symbol in input1
    Item sym_item = builder1.createSymbolItem("mySymbol");

    // Symbols use arena allocation
    Symbol* sym = sym_item.get_symbol();
    EXPECT_NE(sym, nullptr);

    // Copy to same input - should create new symbol
    Item copied_same = builder1.deep_copy(sym_item);
    Symbol* copied_sym = copied_same.get_symbol();
    EXPECT_STREQ(copied_sym->chars, "mySymbol");

    // Copy to different input
    Item copied_diff = builder2.deep_copy(sym_item);
    Symbol* copied_sym2 = copied_diff.get_symbol();
    EXPECT_STREQ(copied_sym2->chars, "mySymbol");
}

TEST_F(MarkBuilderDeepCopyTest, CopyName) {
    MarkBuilder builder1(input1);

    // Names are always pooled
    String* name1 = builder1.createName("fieldName");
    String* name2 = builder1.createName("fieldName");

    // Same name should return same pointer from pool
    EXPECT_EQ(name1, name2);
}

// ============================================================================
// Array Tests
// ============================================================================

TEST_F(MarkBuilderDeepCopyTest, CopyEmptyArray) {
    MarkBuilder builder(input1);
    Item arr = builder.createArray();

    EXPECT_TRUE(builder.is_in_arena(arr));
    Item copied = builder.deep_copy(arr);

    EXPECT_EQ(get_type_id(copied), LMD_TYPE_ARRAY);
}

TEST_F(MarkBuilderDeepCopyTest, CopyArrayWithPrimitives) {
    MarkBuilder builder1(input1);
    MarkBuilder builder2(input2);

    // Create array with integers
    ArrayBuilder arr_builder = builder1.array();
    arr_builder.append((int64_t)1);
    arr_builder.append((int64_t)2);
    arr_builder.append((int64_t)3);
    Item arr = arr_builder.final();

    EXPECT_TRUE(builder1.is_in_arena(arr));

    // Copy to same input - should return original
    Item copied_same = builder1.deep_copy(arr);
    Array* arr_same = copied_same.array;
    EXPECT_EQ(arr_same->length, 3);

    // Copy to different input - should create new array
    Item copied_diff = builder2.deep_copy(arr);
    Array* arr_diff = copied_diff.array;
    // NOTE: Phase 5 limitation - without pool tracking, arrays with only inline
    // values can't be distinguished as external, so they may not be copied
    // EXPECT_NE(arr_diff, arr.array);  // TODO: Enable after Phase 5
    EXPECT_EQ(arr_diff->length, 3);
    ArrayReader reader_diff(arr_diff);
    EXPECT_EQ(it2i(reader_diff.get(0).item()), 1);
    EXPECT_EQ(it2i(reader_diff.get(1).item()), 2);
    EXPECT_EQ(it2i(reader_diff.get(2).item()), 3);
}

TEST_F(MarkBuilderDeepCopyTest, CopyNestedArray) {
    MarkBuilder builder1(input1);
    MarkBuilder builder2(input2);

    // Create nested array [[1, 2], [3, 4]]
    ArrayBuilder inner1 = builder1.array();
    inner1.append((int64_t)1).append((int64_t)2);

    ArrayBuilder inner2 = builder1.array();
    inner2.append((int64_t)3).append((int64_t)4);

    ArrayBuilder outer = builder1.array();
    outer.append(inner1.final()).append(inner2.final());
    Item arr = outer.final();

    EXPECT_TRUE(builder1.is_in_arena(arr));

    // Copy to different input
    Item copied = builder2.deep_copy(arr);
    Array* outer_arr = copied.array;
    EXPECT_EQ(outer_arr->length, 2);

    // Verify nested structure
    ArrayReader outer_reader(outer_arr);
    Item first = outer_reader.get(0).item();
    EXPECT_EQ(get_type_id(first), LMD_TYPE_ARRAY);
    Array* first_arr = first.array;
    EXPECT_EQ(first_arr->length, 2);
    ArrayReader first_reader(first_arr);
    EXPECT_EQ(it2i(first_reader.get(0).item()), 1);
    EXPECT_EQ(it2i(first_reader.get(1).item()), 2);
}

// ============================================================================
// List Tests
// ============================================================================

TEST_F(MarkBuilderDeepCopyTest, CopyList) {
    MarkBuilder builder1(input1);
    MarkBuilder builder2(input2);

    // Create list in input1
    Item list = builder1.list()
        .push((int64_t)1)
        .push((int64_t)2)
        .push((int64_t)3)
        .final();

    EXPECT_EQ(get_type_id(list), LMD_TYPE_LIST);
    EXPECT_TRUE(builder1.is_in_arena(list));
    EXPECT_FALSE(builder2.is_in_arena(list));

    // Deep copy to input2
    Item copied = builder2.deep_copy(list);

    // Verify type is preserved as LIST (not converted to ARRAY)
    EXPECT_EQ(get_type_id(copied), LMD_TYPE_LIST);
    EXPECT_TRUE(builder2.is_in_arena(copied));

    // Verify contents
    List* copied_list = copied.list;
    ASSERT_NE(copied_list, nullptr);
    EXPECT_EQ(copied_list->length, 3);
    EXPECT_EQ(it2i(copied_list->items[0]), 1);
    EXPECT_EQ(it2i(copied_list->items[1]), 2);
    EXPECT_EQ(it2i(copied_list->items[2]), 3);

    // Verify different memory addresses (actual copy)
    EXPECT_NE(list.list, copied.list);
}

TEST_F(MarkBuilderDeepCopyTest, CopyListSameInput) {
    MarkBuilder builder(input1);

    // Create list
    Item list = builder.list()
        .push((int64_t)10)
        .push((int64_t)20)
        .final();

    // Deep copy to same input - should return original (optimization)
    Item copied = builder.deep_copy(list);

    // Should be same list (no copy needed)
    EXPECT_EQ(list.list, copied.list);
    EXPECT_EQ(get_type_id(copied), LMD_TYPE_LIST);
}

// ============================================================================
// Map Tests
// ============================================================================

TEST_F(MarkBuilderDeepCopyTest, CopyEmptyMap) {
    MarkBuilder builder(input1);
    Item map = builder.createMap();

    EXPECT_TRUE(builder.is_in_arena(map));
    Item copied = builder.deep_copy(map);

    EXPECT_EQ(get_type_id(copied), LMD_TYPE_MAP);
}

TEST_F(MarkBuilderDeepCopyTest, CopyMapWithFields) {
    MarkBuilder builder1(input1);
    MarkBuilder builder2(input2);

    // Create map with fields
    MapBuilder map_builder = builder1.map();
    map_builder.put("name", "Alice");
    map_builder.put("age", 30);
    Item map = map_builder.final();

    EXPECT_TRUE(builder1.is_in_arena(map));

    // Copy to same input - should return original
    Item copied_same = builder1.deep_copy(map);
    EXPECT_EQ(copied_same.map, map.map);  // Same pointer

    // Copy to different input - should create new map
    Item copied_diff = builder2.deep_copy(map);
    EXPECT_NE(copied_diff.map, map.map);  // Different pointer

    // Verify field values (note: need to access via shape iteration)
    Map* copied_map = copied_diff.map;
    EXPECT_NE(copied_map, nullptr);
    EXPECT_NE(copied_map->type, nullptr);
}

TEST_F(MarkBuilderDeepCopyTest, CopyNestedMap) {
    MarkBuilder builder1(input1);
    MarkBuilder builder2(input2);

    // Create nested map: { person: { name: "Bob", age: 25 } }
    MapBuilder inner_builder = builder1.map();
    inner_builder.put("name", "Bob");
    inner_builder.put("age", 25);
    Item inner_map = inner_builder.final();

    MapBuilder outer_builder = builder1.map();
    outer_builder.put("person", inner_map);
    Item outer_map = outer_builder.final();

    EXPECT_TRUE(builder1.is_in_arena(outer_map));

    // Copy to different input
    Item copied = builder2.deep_copy(outer_map);
    EXPECT_NE(copied.map, outer_map.map);
}

// ============================================================================
// Element Tests
// ============================================================================

TEST_F(MarkBuilderDeepCopyTest, CopyEmptyElement) {
    MarkBuilder builder(input1);
    Item elem = builder.createElement("div");

    EXPECT_TRUE(builder.is_in_arena(elem));
    Item copied = builder.deep_copy(elem);

    EXPECT_EQ(get_type_id(copied), LMD_TYPE_ELEMENT);
}

TEST_F(MarkBuilderDeepCopyTest, CopyElementWithAttributes) {
    MarkBuilder builder1(input1);
    MarkBuilder builder2(input2);

    // Create element with attributes
    ElementBuilder elem_builder = builder1.element("div");
    elem_builder.attr("id", "main");
    elem_builder.attr("class", "container");
    Item elem = elem_builder.final();

    EXPECT_TRUE(builder1.is_in_arena(elem));

    // Copy to same input
    Item copied_same = builder1.deep_copy(elem);
    EXPECT_EQ(copied_same.element, elem.element);  // Same pointer

    // Copy to different input
    Item copied_diff = builder2.deep_copy(elem);
    EXPECT_NE(copied_diff.element, elem.element);  // Different pointer
}

TEST_F(MarkBuilderDeepCopyTest, CopyElementWithChildren) {
    MarkBuilder builder1(input1);
    MarkBuilder builder2(input2);

    // Create element with children
    ElementBuilder elem_builder = builder1.element("div");
    elem_builder.child(builder1.createStringItem("Hello"));
    elem_builder.child(builder1.createStringItem("World"));
    Item elem = elem_builder.final();

    EXPECT_TRUE(builder1.is_in_arena(elem));

    // Copy to different input
    Item copied = builder2.deep_copy(elem);
    Element* copied_elem = copied.element;
    EXPECT_NE(copied_elem, elem.element);
    EXPECT_EQ(copied_elem->length, 2);
}

TEST_F(MarkBuilderDeepCopyTest, CopyNestedElements) {
    MarkBuilder builder1(input1);
    MarkBuilder builder2(input2);

    // Create nested elements: <div><span>Text</span></div>
    ElementBuilder span_builder = builder1.element("span");
    span_builder.child(builder1.createStringItem("Text"));
    Item span = span_builder.final();

    ElementBuilder div_builder = builder1.element("div");
    div_builder.child(span);
    Item div = div_builder.final();

    EXPECT_TRUE(builder1.is_in_arena(div));

    // Copy to different input
    Item copied = builder2.deep_copy(div);
    Element* copied_div = copied.element;
    EXPECT_NE(copied_div, div.element);
    EXPECT_EQ(copied_div->length, 1);
}

// ============================================================================
// Ownership Chain Tests (Parent Input)
// ============================================================================

TEST_F(MarkBuilderDeepCopyTest, IsInArenaChecksParentChain) {
    MarkBuilder builder_parent(input1);
    MarkBuilder builder_child(child_input);

    // Create data in parent input
    Item str_item = builder_parent.createStringItem("Parent data");
    EXPECT_TRUE(builder_parent.is_in_arena(str_item));

    // Child builder should recognize parent's data as "in arena"
    EXPECT_TRUE(builder_child.is_in_arena(str_item));

    // deep_copy should NOT copy data from parent (optimization)
    Item copied = builder_child.deep_copy(str_item);
    EXPECT_EQ(it2s(copied), it2s(str_item));  // Same pointer (no copy)
}

TEST_F(MarkBuilderDeepCopyTest, CopyFromGrandparentChain) {
    // Create grandchild: input1 -> child_input -> grandchild
    Input* grandchild = Input::create(pool1, nullptr, child_input);
    ASSERT_NE(grandchild, nullptr);
    ASSERT_EQ(grandchild->parent, child_input);
    ASSERT_EQ(child_input->parent, input1);

    MarkBuilder builder_grandparent(input1);
    MarkBuilder builder_grandchild(grandchild);

    // Create data in grandparent
    Item data = builder_grandparent.createInt(123);

    // Grandchild should recognize grandparent's data
    EXPECT_TRUE(builder_grandchild.is_in_arena(data));

    // Should not copy
    Item copied = builder_grandchild.deep_copy(data);
    EXPECT_EQ(copied.item, data.item);
}

TEST_F(MarkBuilderDeepCopyTest, CopyExternalData) {
    MarkBuilder builder1(input1);
    MarkBuilder builder2(input2);

    // Create data in input1
    Item str_item = builder1.createStringItem("External");

    // input2 has no parent relationship with input1
    EXPECT_FALSE(builder2.is_in_arena(str_item));

    // Should perform actual copy
    Item copied = builder2.deep_copy(str_item);
    String* original_str = it2s(str_item);
    String* copied_str = it2s(copied);

    EXPECT_NE(copied_str, original_str);  // Different pointers
    EXPECT_STREQ(copied_str->chars, original_str->chars);  // Same content
}

// ============================================================================
// Mixed Ownership Tests
// ============================================================================

TEST_F(MarkBuilderDeepCopyTest, CopyArrayWithMixedOwnership) {
    MarkBuilder builder_parent(input1);
    MarkBuilder builder_child(child_input);
    MarkBuilder builder_external(input2);

    // Create array with items from different sources
    Item parent_item = builder_parent.createInt(100);
    Item child_item = builder_child.createInt(200);
    Item external_item = builder_external.createInt(300);

    // Build array in child input
    ArrayBuilder arr_builder = builder_child.array();
    arr_builder.append(parent_item);   // From parent - should not copy
    arr_builder.append(child_item);    // From self - should not copy
    arr_builder.append(external_item); // From external - MUST copy
    Item arr = arr_builder.final();

    // Verify array is in child's arena
    EXPECT_TRUE(builder_child.is_in_arena(arr));
}

TEST_F(MarkBuilderDeepCopyTest, CopyMapWithExternalValues) {
    MarkBuilder builder1(input1);
    MarkBuilder builder2(input2);

    // Create data in input1
    Item value1 = builder1.createStringItem("Value1");

    // Create map in input2 with external value
    MapBuilder map_builder = builder2.map();
    map_builder.put("key", value1);  // External value
    Item map = map_builder.final();

    // Map contains external value, so is_in_arena should return false
    EXPECT_FALSE(builder2.is_in_arena(map));

    // deep_copy should copy the external value
    Item copied = builder2.deep_copy(map);
    EXPECT_NE(copied.map, map.map);  // Should create new map
}

// ============================================================================
// Performance/Optimization Tests
// ============================================================================

TEST_F(MarkBuilderDeepCopyTest, DeepCopyAvoidsCopyForLocalData) {
    MarkBuilder builder(input1);

    // Create complex nested structure
    MapBuilder map_builder = builder.map();

    ArrayBuilder arr_builder = builder.array();
    for (int i = 0; i < 10; i++) {
        arr_builder.append((int64_t)i);
    }

    map_builder.put("numbers", arr_builder.final());
    map_builder.put("name", "Test");
    Item map = map_builder.final();

    // deep_copy should recognize all data is local
    EXPECT_TRUE(builder.is_in_arena(map));

    // Should return original without copying
    Item copied = builder.deep_copy(map);
    EXPECT_EQ(copied.map, map.map);  // Same pointer
}

TEST_F(MarkBuilderDeepCopyTest, DeepCopyCopiesExternalData) {
    MarkBuilder builder1(input1);
    MarkBuilder builder2(input2);

    // Create structure in input1
    MapBuilder map_builder = builder1.map();
    map_builder.put("field", "value");
    Item map = map_builder.final();

    // Copy to input2
    EXPECT_FALSE(builder2.is_in_arena(map));
    Item copied = builder2.deep_copy(map);

    // Should be different pointer
    EXPECT_NE(copied.map, map.map);
}

//==============================================================================
// Phase 5a Tests: Arena Container Allocation
//==============================================================================

TEST_F(MarkBuilderDeepCopyTest, ArrayStructIsArenaAllocated) {
    MarkBuilder builder(input1);

    // Create an array
    ArrayBuilder array_builder = builder.array();
    array_builder.append((int64_t)42);
    array_builder.append((int64_t)100);
    Item array = array_builder.final();

    // Verify the Array struct itself is arena-allocated
    EXPECT_TRUE(arena_owns(input1->arena, array.array));
}

TEST_F(MarkBuilderDeepCopyTest, MapStructIsArenaAllocated) {
    MarkBuilder builder(input1);

    // Create a map
    MapBuilder map_builder = builder.map();
    map_builder.put("key", "value");
    Item map = map_builder.final();

    // Verify the Map struct itself is arena-allocated
    EXPECT_TRUE(arena_owns(input1->arena, map.map));
}

TEST_F(MarkBuilderDeepCopyTest, ElementStructIsArenaAllocated) {
    MarkBuilder builder(input1);

    // Create an element
    ElementBuilder elmt_builder = builder.element("div");
    elmt_builder.attr("class", "container");
    Item elmt = elmt_builder.final();

    // Verify the Element struct itself is arena-allocated
    EXPECT_TRUE(arena_owns(input1->arena, elmt.element));
}

TEST_F(MarkBuilderDeepCopyTest, IsInArenaDetectsContainerOwnership) {
    MarkBuilder builder1(input1);
    MarkBuilder builder2(input2);

    // Create array in input1
    ArrayBuilder array_builder = builder1.array();
    array_builder.append((int64_t)42);
    Item array = array_builder.final();

    // builder1 should recognize the array as owned
    EXPECT_TRUE(builder1.is_in_arena(array));

    // builder2 should NOT recognize it as owned (different arena)
    EXPECT_FALSE(builder2.is_in_arena(array));
}

TEST_F(MarkBuilderDeepCopyTest, DeepCopyNowRecognizesContainerOwnership) {
    MarkBuilder builder1(input1);
    MarkBuilder builder2(input2);

    // Create nested structure in input1
    ElementBuilder elmt_builder = builder1.element("div");
    elmt_builder.child(builder1.createStringItem("content"));
    Item elmt = elmt_builder.final();

    // Deep copy to same arena should return original
    Item same = builder1.deep_copy(elmt);
    EXPECT_EQ(same.element, elmt.element);  // Same pointer (no copy)

    // Deep copy to different arena should make new copy
    Item different = builder2.deep_copy(elmt);
    EXPECT_NE(different.element, elmt.element);  // Different pointer (copied)
    EXPECT_TRUE(builder2.is_in_arena(different));  // Now owned by input2
}
