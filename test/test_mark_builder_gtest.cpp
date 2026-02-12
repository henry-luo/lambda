/**
 * GTest-based test suite for MarkBuilder API
 * Tests the fluent C++ interface for building Mark documents
 */

#include <gtest/gtest.h>
#include "../lambda/mark_builder.hpp"
#include "../lambda/lambda-data.hpp"
#include "../lambda/input/input.hpp"
#include "../lib/mempool.h"
#include "../lib/arraylist.h"
#include "../lib/stringbuf.h"
#include "../lib/log.h"
#include <cmath>
#include <string>
#include <cstdint>
#include <climits>

// Test fixture for MarkBuilder tests
class MarkBuilderTest : public ::testing::Test {
protected:
    Input* input;

    void SetUp() override {
        // Initialize logging
        log_init(NULL);
        // Create minimal Input structure for testing
        input = InputManager::create_input(nullptr);
    }

    void TearDown() override {
        // InputManager will handle cleanup
        // Don't manually free anything here
    }
};

// Test string creation
TEST_F(MarkBuilderTest, CreateString) {
    MarkBuilder builder(input);

    Item str_item = builder.createStringItem("Hello, World!");
    ASSERT_EQ(get_type_id(str_item), LMD_TYPE_STRING);

    String* str = str_item.get_string();
    ASSERT_NE(str, nullptr);
    EXPECT_STREQ(str->chars, "Hello, World!");
}

// Test integer creation
TEST_F(MarkBuilderTest, CreateInt) {
    MarkBuilder builder(input);

    Item int_item = builder.createInt(42);
    EXPECT_EQ(get_type_id(int_item), LMD_TYPE_INT);
    EXPECT_EQ(int_item.int_val, 42);
}

// Test float creation
TEST_F(MarkBuilderTest, CreateFloat) {
    MarkBuilder builder(input);
    Item float_item = builder.createFloat(3.14);
    EXPECT_EQ(get_type_id(float_item), LMD_TYPE_FLOAT);
    double val = float_item.get_double();
    EXPECT_DOUBLE_EQ(val, 3.14);
}

// Test boolean creation
TEST_F(MarkBuilderTest, CreateBool) {
    MarkBuilder builder(input);

    Item bool_true = builder.createBool(true);
    EXPECT_EQ(get_type_id(bool_true), LMD_TYPE_BOOL);
    EXPECT_EQ(bool_true.bool_val, 1);

    Item bool_false = builder.createBool(false);
    EXPECT_EQ(get_type_id(bool_false), LMD_TYPE_BOOL);
    EXPECT_EQ(bool_false.bool_val, 0);
}

// Test null creation
TEST_F(MarkBuilderTest, CreateNull) {
    MarkBuilder builder(input);

    Item null_item = builder.createNull();
    EXPECT_EQ(get_type_id(null_item), LMD_TYPE_NULL);
}

// Test range creation
TEST_F(MarkBuilderTest, CreateRange) {
    MarkBuilder builder(input);

    Item range_item = builder.createRange(1, 10);
    EXPECT_EQ(get_type_id(range_item), LMD_TYPE_RANGE);

    Range* range = range_item.range;
    ASSERT_NE(range, nullptr);
    EXPECT_EQ(range->start, 1);
    EXPECT_EQ(range->end, 10);
    EXPECT_EQ(range->length, 10);
}

// Test empty range
TEST_F(MarkBuilderTest, CreateEmptyRange) {
    MarkBuilder builder(input);

    Item range_item = builder.createRange(5, 3);  // end < start
    EXPECT_EQ(get_type_id(range_item), LMD_TYPE_RANGE);

    Range* range = range_item.range;
    ASSERT_NE(range, nullptr);
    EXPECT_EQ(range->start, 5);
    EXPECT_EQ(range->end, 3);
    EXPECT_EQ(range->length, 0);  // Empty range
}

// Test type creation
TEST_F(MarkBuilderTest, createMetaType) {
    MarkBuilder builder(input);

    Item type_item = builder.createMetaType(LMD_TYPE_STRING);
    EXPECT_EQ(get_type_id(type_item), LMD_TYPE_TYPE);

    TypeType* metatype = (TypeType*)type_item.type;
    Type* type = metatype->type;
    ASSERT_NE(type, nullptr);
    EXPECT_EQ(type->type_id, LMD_TYPE_STRING);
    EXPECT_EQ(type->is_literal, true);
    EXPECT_EQ(type->is_const, true);
}

// Test type creation with flags
TEST_F(MarkBuilderTest, CreateTypeWithFlags) {
    MarkBuilder builder(input);

    Item type_item = builder.createMetaType(LMD_TYPE_INT);
    EXPECT_EQ(get_type_id(type_item), LMD_TYPE_TYPE);

    Type* type = ((TypeType*)type_item.type)->type;
    ASSERT_NE(type, nullptr);
    EXPECT_EQ(type->type_id, LMD_TYPE_INT);
    EXPECT_EQ(type->is_literal, 1);
    EXPECT_EQ(type->is_const, 1);
}

// Test array creation
TEST_F(MarkBuilderTest, CreateArray) {
    MarkBuilder builder(input);

    Item array_item = builder.array()
        .append(builder.createInt(1))
        .append(builder.createInt(2))
        .append(builder.createInt(3))
        .final();

    EXPECT_EQ(get_type_id(array_item), LMD_TYPE_ARRAY);

    Array* arr = array_item.array;
    ASSERT_NE(arr, nullptr);
    EXPECT_EQ(arr->length, 3);

    // Check array contents
    EXPECT_EQ(arr->items[0].int_val, 1);
    EXPECT_EQ(arr->items[1].int_val, 2);
    EXPECT_EQ(arr->items[2].int_val, 3);
}

// Test empty array
TEST_F(MarkBuilderTest, CreateEmptyArray) {
    MarkBuilder builder(input);

    Item array_item = builder.array().final();

    EXPECT_EQ(get_type_id(array_item), LMD_TYPE_ARRAY);

    Array* arr = array_item.array;
    ASSERT_NE(arr, nullptr);
    EXPECT_EQ(arr->length, 0);
}

// Test mixed-type array
TEST_F(MarkBuilderTest, CreateMixedArray) {
    MarkBuilder builder(input);

    Item array_item = builder.array()
        .append(builder.createInt(42))
        .append(builder.createStringItem("test"))
        .append(builder.createBool(true))
        .append(builder.createNull())
        .final();

    Array* arr = array_item.array;
    ASSERT_NE(arr, nullptr);
    EXPECT_EQ(arr->length, 4);

    EXPECT_EQ(get_type_id(arr->items[0]), LMD_TYPE_INT);
    EXPECT_EQ(get_type_id(arr->items[1]), LMD_TYPE_STRING);
    EXPECT_EQ(get_type_id(arr->items[2]), LMD_TYPE_BOOL);
    EXPECT_EQ(get_type_id(arr->items[3]), LMD_TYPE_NULL);
}

// Test list creation
TEST_F(MarkBuilderTest, CreateList) {
    MarkBuilder builder(input);

    Item list_item = builder.list()
        .push(builder.createInt(1))
        .push(builder.createInt(2))
        .push(builder.createInt(3))
        .final();

    EXPECT_EQ(get_type_id(list_item), LMD_TYPE_LIST);

    List* lst = list_item.list;
    ASSERT_NE(lst, nullptr);
    EXPECT_EQ(lst->length, 3);

    // Check list contents
    EXPECT_EQ(lst->items[0].int_val, 1);
    EXPECT_EQ(lst->items[1].int_val, 2);
    EXPECT_EQ(lst->items[2].int_val, 3);
}

// Test empty list
TEST_F(MarkBuilderTest, CreateEmptyList) {
    MarkBuilder builder(input);

    Item list_item = builder.list().final();

    EXPECT_EQ(get_type_id(list_item), LMD_TYPE_LIST);

    List* lst = list_item.list;
    ASSERT_NE(lst, nullptr);
    EXPECT_EQ(lst->length, 0);
}

// Test list with null skipping (list_push behavior)
TEST_F(MarkBuilderTest, ListSkipsNulls) {
    MarkBuilder builder(input);

    Item list_item = builder.list()
        .push(builder.createInt(1))
        .push(builder.createNull())  // Should be skipped
        .push(builder.createInt(2))
        .push(builder.createNull())  // Should be skipped
        .push(builder.createInt(3))
        .final();

    List* lst = list_item.list;
    ASSERT_NE(lst, nullptr);
    EXPECT_EQ(lst->length, 3);  // Only 3 items, nulls skipped

    EXPECT_EQ(lst->items[0].int_val, 1);
    EXPECT_EQ(lst->items[1].int_val, 2);
    EXPECT_EQ(lst->items[2].int_val, 3);
}

// Test list flattening (nested lists are flattened by list_push)
TEST_F(MarkBuilderTest, ListFlattensNestedLists) {
    MarkBuilder builder(input);

    // Create inner list
    Item inner_list = builder.list()
        .push(builder.createInt(2))
        .push(builder.createInt(3))
        .final();

    // Create outer list with nested list
    Item outer_list = builder.list()
        .push(builder.createInt(1))
        .push(inner_list)  // Should be flattened
        .push(builder.createInt(4))
        .final();

    List* lst = outer_list.list;
    ASSERT_NE(lst, nullptr);
    EXPECT_EQ(lst->length, 4);  // Flattened: [1, 2, 3, 4]

    EXPECT_EQ(lst->items[0].int_val, 1);
    EXPECT_EQ(lst->items[1].int_val, 2);
    EXPECT_EQ(lst->items[2].int_val, 3);
    EXPECT_EQ(lst->items[3].int_val, 4);
}

// Test simple element creation
TEST_F(MarkBuilderTest, CreateSimpleElement) {
    MarkBuilder builder(input);

    Item elem_item = builder.element("div")
        .text("Hello World")
        .final();

    EXPECT_EQ(get_type_id(elem_item), LMD_TYPE_ELEMENT);

    Element* elem = elem_item.element;
    ASSERT_NE(elem, nullptr);
    EXPECT_GT(elem->length, 0);

    TypeElmt* elem_type = (TypeElmt*)elem->type;
    ASSERT_NE(elem_type, nullptr);
    EXPECT_GT(elem_type->name.length, 0);
    EXPECT_EQ(strncmp(elem_type->name.str, "div", 3), 0);
}

// Test element with attributes
TEST_F(MarkBuilderTest, CreateElementWithAttributes) {
    MarkBuilder builder(input);

    Item elem_item = builder.element("div")
        .attr("id", "main")
        .attr("class", "container")
        .text("Content")
        .final();

    EXPECT_EQ(get_type_id(elem_item), LMD_TYPE_ELEMENT);

    Element* elem = elem_item.element;
    ASSERT_NE(elem, nullptr);

    TypeElmt* elem_type = (TypeElmt*)elem->type;
    ASSERT_NE(elem_type, nullptr);
}

// Test nested elements
TEST_F(MarkBuilderTest, CreateNestedElements) {
    MarkBuilder builder(input);

    Item child = builder.element("span")
        .text("Inner")
        .final();

    Item parent = builder.element("div")
        .child(child)
        .final();

    EXPECT_EQ(get_type_id(parent), LMD_TYPE_ELEMENT);

    Element* elem = parent.element;
    ASSERT_NE(elem, nullptr);
    EXPECT_EQ(elem->length, 1);
    EXPECT_EQ(get_type_id(elem->items[0]), LMD_TYPE_ELEMENT);
}

// Test map creation
TEST_F(MarkBuilderTest, CreateMap) {
    MarkBuilder builder(input);

    Item map_item = builder.map()
        .put("name", "John")
        .put("age", (int64_t)30)  // Explicit cast to avoid ambiguity
        .put("active", true)
        .final();

    EXPECT_EQ(get_type_id(map_item), LMD_TYPE_MAP);

    Map* map = map_item.map;
    ASSERT_NE(map, nullptr);
}

// Test empty map
TEST_F(MarkBuilderTest, CreateEmptyMap) {
    MarkBuilder builder(input);

    Item map_item = builder.map().final();

    EXPECT_EQ(get_type_id(map_item), LMD_TYPE_MAP);

    Map* map = map_item.map;
    ASSERT_NE(map, nullptr);
}

// Test complex nested structure
TEST_F(MarkBuilderTest, CreateComplexStructure) {
    MarkBuilder builder(input);

    // Create a complex document structure
    Item doc = builder.element("article")
        .attr("id", "post-123")
        .child(builder.element("h1")
            .text("Title")
            .final())
        .child(builder.element("p")
            .text("Paragraph text")
            .final())
        .child(builder.array()
            .append(builder.createInt(1))
            .append(builder.createInt(2))
            .final())
        .final();

    EXPECT_EQ(get_type_id(doc), LMD_TYPE_ELEMENT);

    Element* article = doc.element;
    ASSERT_NE(article, nullptr);
    EXPECT_GE(article->length, 3);
}

// Test name/symbol/string separation
TEST_F(MarkBuilderTest, NameSymbolStringSeparation) {
    MarkBuilder builder(input);

    // Names created via createNameItem produce Symbol type (arena-allocated, NOT pooled)
    Item name1 = builder.createNameItem("element");
    Item name2 = builder.createNameItem("element");
    EXPECT_EQ(get_type_id(name1), LMD_TYPE_SYMBOL);
    EXPECT_EQ(get_type_id(name2), LMD_TYPE_SYMBOL);

    // Symbols are arena-allocated, not pooled - different pointers
    Symbol* sym_name1 = name1.get_symbol();
    Symbol* sym_name2 = name2.get_symbol();
    EXPECT_NE(sym_name1, sym_name2);  // Different instances (arena allocated)
    EXPECT_STREQ(sym_name1->chars, sym_name2->chars);  // Same content

    // Symbols created via createSymbolItem (arena-allocated, NOT pooled)
    Item sym1 = builder.createSymbolItem("short");
    Item sym2 = builder.createSymbolItem("short");
    EXPECT_EQ(get_type_id(sym1), LMD_TYPE_SYMBOL);
    EXPECT_EQ(get_type_id(sym2), LMD_TYPE_SYMBOL);

    Symbol* s1 = sym1.get_symbol();
    Symbol* s2 = sym2.get_symbol();
    EXPECT_NE(s1, s2);  // Different instances (arena allocated)
    EXPECT_STREQ(s1->chars, s2->chars);  // Same content

    // Long symbols also not pooled
    const char* long_sym = "this_is_a_very_long_symbol_name_exceeding_32_characters";
    Item long1 = builder.createSymbolItem(long_sym);
    Item long2 = builder.createSymbolItem(long_sym);
    EXPECT_EQ(get_type_id(long1), LMD_TYPE_SYMBOL);
    EXPECT_EQ(get_type_id(long2), LMD_TYPE_SYMBOL);
    EXPECT_NE(long1.symbol_ptr, long2.symbol_ptr);  // Different instances

    // Strings are never pooled (arena allocated)
    Item str1 = builder.createStringItem("test");
    Item str2 = builder.createStringItem("test");

    // Verify both are valid strings
    EXPECT_EQ(get_type_id(str1), LMD_TYPE_STRING);
    EXPECT_EQ(get_type_id(str2), LMD_TYPE_STRING);

    String* str_s1 = str1.get_string();
    String* str_s2 = str2.get_string();
    ASSERT_NE(str_s1, nullptr);
    ASSERT_NE(str_s2, nullptr);

    // Both should have the same content
    EXPECT_STREQ(str_s1->chars, "test");
    EXPECT_STREQ(str_s2->chars, "test");

    // Strings are NOT pooled - different pointers
    EXPECT_NE(str_s1, str_s2);
}

// Test auto string merge
TEST_F(MarkBuilderTest, AutoStringMerge) {
    MarkBuilder builder(input);
    builder.setAutoStringMerge(true);

    Item elem_item = builder.element("p")
        .text("Hello ")
        .text("World")
        .final();

    Element* elem = elem_item.element;
    ASSERT_NE(elem, nullptr);

    // With auto merge, adjacent strings should be merged
    // (actual verification would depend on implementation details)
}

//==============================================================================
// Negative Tests - Error Handling
//==============================================================================

// Test null/empty string handling
// Empty strings and null inputs now map to null Item (not EMPTY_STRING)
TEST_F(MarkBuilderTest, NullAndEmptyStrings) {
    MarkBuilder builder(input);

    // Null string input should return null Item
    Item null_str = builder.createStringItem(nullptr);
    EXPECT_EQ(get_type_id(null_str), LMD_TYPE_NULL);

    // Empty string ("") should also return null Item
    Item empty_str = builder.createStringItem("");
    EXPECT_EQ(get_type_id(empty_str), LMD_TYPE_NULL);

    // Zero-length string should also return null Item
    Item zero_len = builder.createStringItem("test", 0);
    EXPECT_EQ(get_type_id(zero_len), LMD_TYPE_NULL);
    
    // Non-empty string should return a proper string
    Item normal_str = builder.createStringItem("hello");
    EXPECT_EQ(get_type_id(normal_str), LMD_TYPE_STRING);
    String* str = normal_str.get_string();
    ASSERT_NE(str, nullptr);
    EXPECT_EQ(str->len, 5);
    EXPECT_STREQ(str->chars, "hello");
}

// Test element with null tag name
TEST_F(MarkBuilderTest, ElementWithNullTagName) {
    MarkBuilder builder(input);

    // Should handle null tag name gracefully
    Item elem_item = builder.element(nullptr)
        .text("Content")
        .final();

    EXPECT_EQ(get_type_id(elem_item), LMD_TYPE_ELEMENT);
    Element* elem = elem_item.element;
    ASSERT_NE(elem, nullptr);
}

// Test element with empty tag name
TEST_F(MarkBuilderTest, ElementWithEmptyTagName) {
    MarkBuilder builder(input);

    Item elem_item = builder.element("")
        .text("Content")
        .final();

    EXPECT_EQ(get_type_id(elem_item), LMD_TYPE_ELEMENT);
    Element* elem = elem_item.element;
    ASSERT_NE(elem, nullptr);
}

// Test null text in element
TEST_F(MarkBuilderTest, ElementWithNullText) {
    MarkBuilder builder(input);

    Item elem_item = builder.element("div")
        .text(nullptr)
        .final();

    Element* elem = elem_item.element;
    ASSERT_NE(elem, nullptr);
    // Should have no children or handle gracefully
}

// Test null attribute key
TEST_F(MarkBuilderTest, ElementWithNullAttributeKey) {
    MarkBuilder builder(input);

    Item elem_item = builder.element("div")
        .attr(nullptr, "value")
        .final();

    Element* elem = elem_item.element;
    ASSERT_NE(elem, nullptr);
}

// Test null attribute value
TEST_F(MarkBuilderTest, ElementWithNullAttributeValue) {
    MarkBuilder builder(input);

    Item elem_item = builder.element("div")
        .attr("key", nullptr)
        .final();

    Element* elem = elem_item.element;
    ASSERT_NE(elem, nullptr);
}

// Test null map key
TEST_F(MarkBuilderTest, MapWithNullKey) {
    MarkBuilder builder(input);

    Item map_item = builder.map()
        .put(nullptr, "value")
        .final();

    Map* map = map_item.map;
    ASSERT_NE(map, nullptr);
}

// Test null map value
TEST_F(MarkBuilderTest, MapWithNullValue) {
    MarkBuilder builder(input);

    Item map_item = builder.map()
        .put("key", (const char*)nullptr)
        .final();

    Map* map = map_item.map;
    ASSERT_NE(map, nullptr);
}

//==============================================================================
// Corner Cases - Boundary Conditions
//==============================================================================

// Test very long strings
TEST_F(MarkBuilderTest, VeryLongString) {
    MarkBuilder builder(input);

    // Create 10KB string
    std::string long_str(10000, 'x');
    Item str_item = builder.createStringItem(long_str.c_str());

    EXPECT_EQ(get_type_id(str_item), LMD_TYPE_STRING);
    String* str = str_item.get_string();
    ASSERT_NE(str, nullptr);
    EXPECT_EQ(str->len, 10000);
}

// Test deeply nested elements
TEST_F(MarkBuilderTest, DeeplyNestedElements) {
    MarkBuilder builder(input);

    // Create 10 levels of nesting
    Item inner = builder.element("span").text("Deep").final();

    for (int i = 0; i < 10; i++) {
        inner = builder.element("div")
            .child(inner)
            .final();
    }

    EXPECT_EQ(get_type_id(inner), LMD_TYPE_ELEMENT);
}

// Test large array
TEST_F(MarkBuilderTest, LargeArray) {
    MarkBuilder builder(input);

    ArrayBuilder arr_builder = builder.array();

    // Add 1000 items
    for (int i = 0; i < 1000; i++) {
        arr_builder.append(builder.createInt(i));
    }

    Item array_item = arr_builder.final();
    Array* arr = array_item.array;
    ASSERT_NE(arr, nullptr);
    EXPECT_EQ(arr->length, 1000);

    // Verify first and last
    EXPECT_EQ(arr->items[0].int_val, 0);
    EXPECT_EQ(arr->items[999].int_val, 999);
}

// Test many attributes
TEST_F(MarkBuilderTest, ElementWithManyAttributes) {
    MarkBuilder builder(input);

    ElementBuilder elem_builder = builder.element("div");

    // Add 50 attributes
    for (int i = 0; i < 50; i++) {
        char key[32], val[32];
        snprintf(key, sizeof(key), "attr%d", i);
        snprintf(val, sizeof(val), "value%d", i);
        elem_builder.attr(key, val);
    }

    Item elem_item = elem_builder.final();
    Element* elem = elem_item.element;
    ASSERT_NE(elem, nullptr);
}

// Test many children
TEST_F(MarkBuilderTest, ElementWithManyChildren) {
    MarkBuilder builder(input);

    ElementBuilder elem_builder = builder.element("div");

    // Add 100 children
    for (int i = 0; i < 100; i++) {
        elem_builder.child(builder.element("span")
            .text("Child")
            .final());
    }

    Item elem_item = elem_builder.final();
    Element* elem = elem_item.element;
    ASSERT_NE(elem, nullptr);
    EXPECT_EQ(elem->length, 100);
}

// Test large map
TEST_F(MarkBuilderTest, LargeMap) {
    MarkBuilder builder(input);

    MapBuilder map_builder = builder.map();

    // Add 100 key-value pairs
    for (int i = 0; i < 100; i++) {
        char key[32], val[32];
        snprintf(key, sizeof(key), "key%d", i);
        snprintf(val, sizeof(val), "value%d", i);
        map_builder.put(key, val);
    }

    Item map_item = map_builder.final();
    Map* map = map_item.map;
    ASSERT_NE(map, nullptr);
}

//==============================================================================
// Input Parser Use Cases
//==============================================================================

// Test creating string with explicit length (for parser use)
TEST_F(MarkBuilderTest, CreateStringWithLength) {
    MarkBuilder builder(input);

    const char* source = "Hello, World! Extra text";
    Item str_item = builder.createStringItem(source, 13); // Only "Hello, World!"

    String* str = str_item.get_string();
    ASSERT_NE(str, nullptr);
    EXPECT_EQ(str->len, 13);
    EXPECT_STREQ(str->chars, "Hello, World!");
}

// Test creating strings with special characters (parser needs)
TEST_F(MarkBuilderTest, StringsWithSpecialCharacters) {
    MarkBuilder builder(input);

    // Newlines, tabs, quotes
    Item str1 = builder.createStringItem("Line1\nLine2\tTabbed");
    String* s1 = str1.get_string();
    EXPECT_STREQ(s1->chars, "Line1\nLine2\tTabbed");

    // Unicode
    Item str2 = builder.createStringItem("Hello 世界 🌍");
    String* s2 = str2.get_string();
    EXPECT_STREQ(s2->chars, "Hello 世界 🌍");

    // Quotes and escapes
    Item str3 = builder.createStringItem("\"quoted\" and 'single'");
    String* s3 = str3.get_string();
    EXPECT_STREQ(s3->chars, "\"quoted\" and 'single'");
}

// Test mixed content elements (parser needs)
TEST_F(MarkBuilderTest, ElementWithMixedContent) {
    MarkBuilder builder(input);

    Item elem_item = builder.element("p")
        .text("Start ")
        .child(builder.element("strong").text("bold").final())
        .text(" middle ")
        .child(builder.element("em").text("italic").final())
        .text(" end")
        .final();

    Element* elem = elem_item.element;
    ASSERT_NE(elem, nullptr);
    EXPECT_EQ(elem->length, 5); // 3 strings + 2 elements
}

// Test attribute types (parsers need various types)
TEST_F(MarkBuilderTest, AttributeTypesForParsers) {
    MarkBuilder builder(input);

    Item elem_item = builder.element("input")
        .attr("type", "text")
        .attr("maxlength", (int64_t)100)
        .attr("required", true)
        .attr("disabled", false)
        .attr("step", 0.5)
        .final();

    Element* elem = elem_item.element;
    ASSERT_NE(elem, nullptr);
}

// Test building from parsed tokens (common parser pattern)
TEST_F(MarkBuilderTest, BuildingFromTokens) {
    MarkBuilder builder(input);

    // Simulate parser building incrementally
    ElementBuilder elem_builder = builder.element("article");

    // Add attributes as parsed
    elem_builder.attr("id", "post-1");
    elem_builder.attr("class", "blog-post");

    // Add children as parsed
    elem_builder.child(builder.element("h1").text("Title").final());
    elem_builder.child(builder.element("p").text("Content").final());

    Item result = elem_builder.final();
    EXPECT_EQ(get_type_id(result), LMD_TYPE_ELEMENT);
}

// Test reusing builder for multiple documents
TEST_F(MarkBuilderTest, ReuseBuilderForMultipleDocs) {
    MarkBuilder builder(input);

    // Build first document
    Item doc1 = builder.element("div")
        .text("First")
        .final();
    EXPECT_EQ(get_type_id(doc1), LMD_TYPE_ELEMENT);

    // Build second document with same builder
    Item doc2 = builder.element("span")
        .text("Second")
        .final();
    EXPECT_EQ(get_type_id(doc2), LMD_TYPE_ELEMENT);

    // Both should be valid
    Element* e1 = doc1.element;
    Element* e2 = doc2.element;
    ASSERT_NE(e1, nullptr);
    ASSERT_NE(e2, nullptr);
}

// Test creating Item array for parser use
TEST_F(MarkBuilderTest, CreateItemArrayForParser) {
    MarkBuilder builder(input);

    // Parser collects items then builds array
    ArrayBuilder arr = builder.array();

    arr.append(builder.createStringItem("token1"));
    arr.append(builder.createInt(42));
    arr.append(builder.createBool(true));

    Item result = arr.final();
    Array* array = result.array;
    ASSERT_NE(array, nullptr);
    EXPECT_EQ(array->length, 3);
}

// Test map with Item values (parser needs)
TEST_F(MarkBuilderTest, MapWithItemValues) {
    MarkBuilder builder(input);

    Item nested_array = builder.array()
        .append(builder.createInt(1))
        .append(builder.createInt(2))
        .final();

    Item map_item = builder.map()
        .put("data", nested_array)
        .final();

    Map* map = map_item.map;
    ASSERT_NE(map, nullptr);
}

// Test building fragments without root element
TEST_F(MarkBuilderTest, BuildFragmentArray) {
    MarkBuilder builder(input);

    // Parser may build fragment list
    Item fragment = builder.array()
        .append(builder.element("h1").text("Title").final())
        .append(builder.element("p").text("Para 1").final())
        .append(builder.element("p").text("Para 2").final())
        .final();

    Array* arr = fragment.array;
    ASSERT_NE(arr, nullptr);
    EXPECT_EQ(arr->length, 3);

    // All should be elements
    for (int i = 0; i < arr->length; i++) {
        EXPECT_EQ(get_type_id(arr->items[i]), LMD_TYPE_ELEMENT);
    }
}

// Test integer boundary values
TEST_F(MarkBuilderTest, IntegerBoundaries) {
    MarkBuilder builder(input);

    // Item.int_val is a 56-bit signed integer field
    Item max_int = builder.createInt(INT32_MAX);
    EXPECT_EQ(max_int.int_val, (int64_t)INT32_MAX);

    Item min_int = builder.createInt(INT32_MIN);
    EXPECT_EQ(min_int.int_val, (int64_t)INT32_MIN);

    Item zero = builder.createInt(0);
    EXPECT_EQ(zero.int_val, 0);

    // Negative values
    Item neg = builder.createInt(-42);
    EXPECT_EQ(neg.int_val, (int64_t)-42);

    // Large values that fit in 32 bits
    Item large = builder.createInt(1000000);
    EXPECT_EQ(large.int_val, (int64_t)1000000);
}

// Test float special values
TEST_F(MarkBuilderTest, FloatSpecialValues) {
    MarkBuilder builder(input);

    Item inf_val = builder.createFloat(INFINITY);
    double inf_ptr = inf_val.get_double();
    EXPECT_TRUE(std::isinf(inf_ptr));

    Item neg_inf = builder.createFloat(-INFINITY);
    double neg_inf_ptr = neg_inf.get_double();
    EXPECT_TRUE(std::isinf(neg_inf_ptr));

    Item nan_val = builder.createFloat(NAN);
    double nan_ptr = nan_val.get_double();
    EXPECT_TRUE(std::isnan(nan_ptr));
    Item zero_val = builder.createFloat(0.0);
    double zero_ptr = zero_val.get_double();
    EXPECT_DOUBLE_EQ(zero_ptr, 0.0);
}

// Test empty string buffer usage
TEST_F(MarkBuilderTest, EmptyStringBuf) {
    MarkBuilder builder(input);

    // emptyString() returns nullptr (no sentinel string)
    String* empty = builder.emptyString();
    EXPECT_EQ(empty, nullptr);
}

// Test duplicate keys in map (last wins)
TEST_F(MarkBuilderTest, MapDuplicateKeys) {
    MarkBuilder builder(input);

    Item map_item = builder.map()
        .put("key", "value1")
        .put("key", "value2")  // Should override
        .final();

    Map* map = map_item.map;
    ASSERT_NE(map, nullptr);
    // Last value should win (implementation dependent)
}

//==============================================================================
// Tag Name Handling Tests - Regression tests for tag name bug
//==============================================================================

// Test: Element tag name is preserved when no attributes
TEST_F(MarkBuilderTest, TagNamePreservedWithoutAttributes) {
    MarkBuilder builder(input);

    Item elem_item = builder.element("div")
        .text("Content")
        .final();

    EXPECT_EQ(get_type_id(elem_item), LMD_TYPE_ELEMENT);

    Element* elem = elem_item.element;
    ASSERT_NE(elem, nullptr);

    TypeElmt* elem_type = (TypeElmt*)elem->type;
    ASSERT_NE(elem_type, nullptr);
    ASSERT_NE(elem_type->name.str, nullptr);

    // Tag name should be "div"
    EXPECT_EQ(elem_type->name.length, 3);
    EXPECT_EQ(strncmp(elem_type->name.str, "div", 3), 0);
}

// Test: Element tag name is preserved WITH attributes (regression test for bug)
TEST_F(MarkBuilderTest, TagNamePreservedWithAttributes) {
    MarkBuilder builder(input);

    Item elem_item = builder.element("article")
        .attr("id", "main")
        .attr("class", "content")
        .text("Text")
        .final();

    EXPECT_EQ(get_type_id(elem_item), LMD_TYPE_ELEMENT);

    Element* elem = elem_item.element;
    ASSERT_NE(elem, nullptr);

    TypeElmt* elem_type = (TypeElmt*)elem->type;
    ASSERT_NE(elem_type, nullptr);
    ASSERT_NE(elem_type->name.str, nullptr);

    // Tag name should be "article", NOT garbage bytes
    EXPECT_EQ(elem_type->name.length, 7);
    EXPECT_EQ(strncmp(elem_type->name.str, "article", 7), 0);
}

// Test: Multiple attributes don't corrupt tag name
TEST_F(MarkBuilderTest, TagNamePreservedWithManyAttributes) {
    MarkBuilder builder(input);

    ElementBuilder elem_builder = builder.element("section");

    // Add many attributes to stress test the fix
    for (int i = 0; i < 20; i++) {
        char key[32], val[32];
        snprintf(key, sizeof(key), "attr%d", i);
        snprintf(val, sizeof(val), "value%d", i);
        elem_builder.attr(key, val);
    }

    Item elem_item = elem_builder.final();

    Element* elem = elem_item.element;
    ASSERT_NE(elem, nullptr);

    TypeElmt* elem_type = (TypeElmt*)elem->type;
    ASSERT_NE(elem_type, nullptr);
    ASSERT_NE(elem_type->name.str, nullptr);

    // Tag name should still be "section"
    EXPECT_EQ(elem_type->name.length, 7);
    EXPECT_EQ(strncmp(elem_type->name.str, "section", 7), 0);
}

// Test: Tag name preserved with attributes of various types
TEST_F(MarkBuilderTest, TagNameWithDifferentAttributeTypes) {
    MarkBuilder builder(input);

    Item elem_item = builder.element("input")
        .attr("type", "text")           // string
        .attr("maxlength", (int64_t)50) // int
        .attr("required", true)         // bool
        .attr("step", 0.1)              // float
        .final();

    Element* elem = elem_item.element;
    ASSERT_NE(elem, nullptr);

    TypeElmt* elem_type = (TypeElmt*)elem->type;
    ASSERT_NE(elem_type, nullptr);
    ASSERT_NE(elem_type->name.str, nullptr);

    // Tag name should be "input"
    EXPECT_EQ(elem_type->name.length, 5);
    EXPECT_EQ(strncmp(elem_type->name.str, "input", 5), 0);
}

// Test: Tag name preserved when mixing attributes and children
TEST_F(MarkBuilderTest, TagNameWithAttributesAndChildren) {
    MarkBuilder builder(input);

    Item elem_item = builder.element("ul")
        .attr("class", "list")
        .child(builder.element("li").text("Item 1").final())
        .attr("id", "mylist")  // Add attribute after child
        .child(builder.element("li").text("Item 2").final())
        .final();

    Element* elem = elem_item.element;
    ASSERT_NE(elem, nullptr);

    TypeElmt* elem_type = (TypeElmt*)elem->type;
    ASSERT_NE(elem_type, nullptr);
    ASSERT_NE(elem_type->name.str, nullptr);

    // Tag name should be "ul"
    EXPECT_EQ(elem_type->name.length, 2);
    EXPECT_EQ(strncmp(elem_type->name.str, "ul", 2), 0);
}

// Test: Nested elements all preserve their tag names
TEST_F(MarkBuilderTest, NestedElementsPreserveTagNames) {
    MarkBuilder builder(input);

    Item elem_item = builder.element("div")
        .attr("class", "outer")
        .child(builder.element("span")
            .attr("id", "inner")
            .text("Inner text")
            .final())
        .final();

    Element* div_elem = elem_item.element;
    ASSERT_NE(div_elem, nullptr);

    TypeElmt* div_type = (TypeElmt*)div_elem->type;
    ASSERT_NE(div_type, nullptr);
    ASSERT_NE(div_type->name.str, nullptr);

    // Outer element tag name should be "div"
    EXPECT_EQ(div_type->name.length, 3);
    EXPECT_EQ(strncmp(div_type->name.str, "div", 3), 0);

    // Check inner element
    ASSERT_GT(div_elem->length, 0);
    Item child_item = div_elem->items[0];
    EXPECT_EQ(get_type_id(child_item), LMD_TYPE_ELEMENT);

    Element* span_elem = child_item.element;
    ASSERT_NE(span_elem, nullptr);

    TypeElmt* span_type = (TypeElmt*)span_elem->type;
    ASSERT_NE(span_type, nullptr);
    ASSERT_NE(span_type->name.str, nullptr);

    // Inner element tag name should be "span"
    EXPECT_EQ(span_type->name.length, 4);
    EXPECT_EQ(strncmp(span_type->name.str, "span", 4), 0);
}

// Test: Various tag name lengths
TEST_F(MarkBuilderTest, VariousTagNameLengths) {
    MarkBuilder builder(input);

    // Short tag name
    Item elem1 = builder.element("a")
        .attr("href", "#")
        .final();

    Element* e1 = elem1.element;
    TypeElmt* t1 = (TypeElmt*)e1->type;
    EXPECT_EQ(t1->name.length, 1);
    EXPECT_EQ(strncmp(t1->name.str, "a", 1), 0);

    // Medium tag name
    Item elem2 = builder.element("button")
        .attr("type", "submit")
        .final();

    Element* e2 = elem2.element;
    TypeElmt* t2 = (TypeElmt*)e2->type;
    EXPECT_EQ(t2->name.length, 6);
    EXPECT_EQ(strncmp(t2->name.str, "button", 6), 0);

    // Long tag name
    Item elem3 = builder.element("custom-web-component")
        .attr("data-value", "123")
        .final();

    Element* e3 = elem3.element;
    TypeElmt* t3 = (TypeElmt*)e3->type;
    EXPECT_EQ(t3->name.length, 20);
    EXPECT_EQ(strncmp(t3->name.str, "custom-web-component", 20), 0);
}

// Test: Tag name with special characters
TEST_F(MarkBuilderTest, TagNameWithSpecialCharacters) {
    MarkBuilder builder(input);

    // Hyphenated tag name
    Item elem1 = builder.element("my-element")
        .attr("data-id", "123")
        .final();

    Element* e1 = elem1.element;
    TypeElmt* t1 = (TypeElmt*)e1->type;
    EXPECT_EQ(t1->name.length, 10);
    EXPECT_EQ(strncmp(t1->name.str, "my-element", 10), 0);

    // Underscored tag name
    Item elem2 = builder.element("my_element")
        .attr("class", "test")
        .final();

    Element* e2 = elem2.element;
    TypeElmt* t2 = (TypeElmt*)e2->type;
    EXPECT_EQ(t2->name.length, 10);
    EXPECT_EQ(strncmp(t2->name.str, "my_element", 10), 0);
}

// Test: Element with only attributes, no children
TEST_F(MarkBuilderTest, ElementWithOnlyAttributes) {
    MarkBuilder builder(input);

    Item elem_item = builder.element("img")
        .attr("src", "image.png")
        .attr("alt", "Description")
        .attr("width", (int64_t)100)
        .attr("height", (int64_t)200)
        .final();

    Element* elem = elem_item.element;
    ASSERT_NE(elem, nullptr);

    TypeElmt* elem_type = (TypeElmt*)elem->type;
    ASSERT_NE(elem_type, nullptr);
    ASSERT_NE(elem_type->name.str, nullptr);

    // Tag name should be "img"
    EXPECT_EQ(elem_type->name.length, 3);
    EXPECT_EQ(strncmp(elem_type->name.str, "img", 3), 0);

    // Should have no children
    EXPECT_EQ(elem->length, 0);
}

// Test: Building multiple elements with same tag name
TEST_F(MarkBuilderTest, MultipleElementsSameTagName) {
    MarkBuilder builder(input);

    Item elem1 = builder.element("div")
        .attr("id", "first")
        .final();

    Item elem2 = builder.element("div")
        .attr("id", "second")
        .final();

    Item elem3 = builder.element("div")
        .attr("id", "third")
        .final();

    // All should have "div" as tag name
    Element* e1 = elem1.element;
    TypeElmt* t1 = (TypeElmt*)e1->type;
    EXPECT_EQ(strncmp(t1->name.str, "div", 3), 0);

    Element* e2 = elem2.element;
    TypeElmt* t2 = (TypeElmt*)e2->type;
    EXPECT_EQ(strncmp(t2->name.str, "div", 3), 0);

    Element* e3 = elem3.element;
    TypeElmt* t3 = (TypeElmt*)e3->type;
    EXPECT_EQ(strncmp(t3->name.str, "div", 3), 0);
}

// Test: Complex document structure with all tag names preserved
TEST_F(MarkBuilderTest, ComplexDocumentAllTagNamesPreserved) {
    MarkBuilder builder(input);

    Item doc = builder.element("article")
        .attr("id", "post-456")
        .attr("class", "blog-post published")
        .child(builder.element("header")
            .attr("class", "post-header")
            .child(builder.element("h1")
                .attr("class", "title")
                .text("Article Title")
                .final())
            .child(builder.element("p")
                .attr("class", "meta")
                .text("By Author")
                .final())
            .final())
        .child(builder.element("section")
            .attr("class", "content")
            .child(builder.element("p").text("Paragraph 1").final())
            .child(builder.element("p").text("Paragraph 2").final())
            .final())
        .child(builder.element("footer")
            .attr("class", "post-footer")
            .text("Footer content")
            .final())
        .final();

    // Verify root element
    Element* article = doc.element;
    ASSERT_NE(article, nullptr);
    TypeElmt* article_type = (TypeElmt*)article->type;
    EXPECT_EQ(strncmp(article_type->name.str, "article", 7), 0);

    // Verify first child (header)
    ASSERT_GE(article->length, 1);
    Element* header = article->items[0].element;
    TypeElmt* header_type = (TypeElmt*)header->type;
    EXPECT_EQ(strncmp(header_type->name.str, "header", 6), 0);

    // Verify header's first child (h1)
    ASSERT_GE(header->length, 1);
    Element* h1 = header->items[0].element;
    TypeElmt* h1_type = (TypeElmt*)h1->type;
    EXPECT_EQ(strncmp(h1_type->name.str, "h1", 2), 0);
}

// Test: Attribute added before vs after setting text
TEST_F(MarkBuilderTest, AttributeOrderingWithText) {
    MarkBuilder builder(input);

    // Attributes before text
    Item elem1 = builder.element("p")
        .attr("id", "para1")
        .text("Content")
        .final();

    Element* e1 = elem1.element;
    TypeElmt* t1 = (TypeElmt*)e1->type;
    EXPECT_EQ(strncmp(t1->name.str, "p", 1), 0);

    // Text before attributes
    Item elem2 = builder.element("p")
        .text("Content")
        .attr("id", "para2")
        .final();

    Element* e2 = elem2.element;
    TypeElmt* t2 = (TypeElmt*)e2->type;
    EXPECT_EQ(strncmp(t2->name.str, "p", 1), 0);

    // Interleaved
    Item elem3 = builder.element("p")
        .attr("class", "test")
        .text("Start")
        .attr("id", "para3")
        .text(" End")
        .final();

    Element* e3 = elem3.element;
    TypeElmt* t3 = (TypeElmt*)e3->type;
    EXPECT_EQ(strncmp(t3->name.str, "p", 1), 0);
}

//==============================================================================
// Builder Lifetime Tests - Document validity after builder destruction
//==============================================================================

// Helper function: Build simple element in separate scope
Item BuildSimpleElement(Input* input) {
    MarkBuilder builder(input);
    return builder.element("div")
        .attr("id", "test")
        .text("Content")
        .final();
}

// Test: Simple element survives builder destruction
TEST_F(MarkBuilderTest, SimpleElementSurvivesBuilderDestruction) {
    Item elem_item = BuildSimpleElement(input);

    // Builder is destroyed, verify element is still valid
    EXPECT_EQ(get_type_id(elem_item), LMD_TYPE_ELEMENT);

    Element* elem = elem_item.element;
    ASSERT_NE(elem, nullptr);

    TypeElmt* elem_type = (TypeElmt*)elem->type;
    ASSERT_NE(elem_type, nullptr);
    ASSERT_NE(elem_type->name.str, nullptr);

    // Verify tag name
    EXPECT_EQ(elem_type->name.length, 3);
    EXPECT_EQ(strncmp(elem_type->name.str, "div", 3), 0);

    // Verify has content
    EXPECT_GT(elem->length, 0);
}

// Helper function: Build element with multiple attributes
Item BuildElementWithAttributes(Input* input) {
    MarkBuilder builder(input);
    return builder.element("article")
        .attr("id", "post-789")
        .attr("class", "featured")
        .attr("data-category", "tech")
        .text("Article content")
        .final();
}

// Test: Element with attributes survives builder destruction
TEST_F(MarkBuilderTest, ElementWithAttributesSurvivesBuilderDestruction) {
    Item elem_item = BuildElementWithAttributes(input);

    // Builder is destroyed, verify element is still valid
    EXPECT_EQ(get_type_id(elem_item), LMD_TYPE_ELEMENT);

    Element* elem = elem_item.element;
    ASSERT_NE(elem, nullptr);

    TypeElmt* elem_type = (TypeElmt*)elem->type;
    ASSERT_NE(elem_type, nullptr);
    ASSERT_NE(elem_type->name.str, nullptr);

    // Verify tag name is still correct
    EXPECT_EQ(elem_type->name.length, 7);
    EXPECT_EQ(strncmp(elem_type->name.str, "article", 7), 0);

    // Verify attributes exist (TypeElmt should have shape entries)
    EXPECT_GT(elem_type->length, 0);
}

// Helper function: Build nested document structure
Item BuildNestedDocument(Input* input) {
    MarkBuilder builder(input);
    return builder.element("section")
        .attr("class", "container")
        .child(builder.element("header")
            .child(builder.element("h1").text("Title").final())
            .child(builder.element("p").text("Subtitle").final())
            .final())
        .child(builder.element("article")
            .attr("id", "main-article")
            .child(builder.element("p").text("Paragraph 1").final())
            .child(builder.element("p").text("Paragraph 2").final())
            .final())
        .child(builder.element("footer")
            .text("Footer text")
            .final())
        .final();
}

// Test: Nested document survives builder destruction
TEST_F(MarkBuilderTest, NestedDocumentSurvivesBuilderDestruction) {
    Item doc_item = BuildNestedDocument(input);

    // Builder is destroyed, verify entire structure is still valid
    EXPECT_EQ(get_type_id(doc_item), LMD_TYPE_ELEMENT);

    Element* section = doc_item.element;
    ASSERT_NE(section, nullptr);

    // Verify root element
    TypeElmt* section_type = (TypeElmt*)section->type;
    EXPECT_EQ(strncmp(section_type->name.str, "section", 7), 0);

    // Verify has children
    EXPECT_EQ(section->length, 3);

    // Verify first child (header)
    Element* header = section->items[0].element;
    ASSERT_NE(header, nullptr);
    TypeElmt* header_type = (TypeElmt*)header->type;
    EXPECT_EQ(strncmp(header_type->name.str, "header", 6), 0);
    EXPECT_EQ(header->length, 2);

    // Verify header's first child (h1)
    Element* h1 = header->items[0].element;
    ASSERT_NE(h1, nullptr);
    TypeElmt* h1_type = (TypeElmt*)h1->type;
    EXPECT_EQ(strncmp(h1_type->name.str, "h1", 2), 0);

    // Verify second child (article)
    Element* article = section->items[1].element;
    ASSERT_NE(article, nullptr);
    TypeElmt* article_type = (TypeElmt*)article->type;
    EXPECT_EQ(strncmp(article_type->name.str, "article", 7), 0);
    EXPECT_EQ(article->length, 2);

    // Verify third child (footer)
    Element* footer = section->items[2].element;
    ASSERT_NE(footer, nullptr);
    TypeElmt* footer_type = (TypeElmt*)footer->type;
    EXPECT_EQ(strncmp(footer_type->name.str, "footer", 6), 0);
}

// Helper function: Build array
Item BuildArray(Input* input) {
    MarkBuilder builder(input);
    return builder.array()
        .append(builder.createInt(10))
        .append(builder.createInt(20))
        .append(builder.createInt(30))
        .append(builder.createStringItem("test"))
        .append(builder.createBool(true))
        .final();
}

// Test: Array survives builder destruction
TEST_F(MarkBuilderTest, ArraySurvivesBuilderDestruction) {
    Item array_item = BuildArray(input);

    // Builder is destroyed, verify array is still valid
    EXPECT_EQ(get_type_id(array_item), LMD_TYPE_ARRAY);

    Array* arr = array_item.array;
    ASSERT_NE(arr, nullptr);
    EXPECT_EQ(arr->length, 5);

    // Verify array contents
    EXPECT_EQ(get_type_id(arr->items[0]), LMD_TYPE_INT);
    EXPECT_EQ(arr->items[0].int_val, 10);

    EXPECT_EQ(get_type_id(arr->items[1]), LMD_TYPE_INT);
    EXPECT_EQ(arr->items[1].int_val, 20);

    EXPECT_EQ(get_type_id(arr->items[2]), LMD_TYPE_INT);
    EXPECT_EQ(arr->items[2].int_val, 30);

    EXPECT_EQ(get_type_id(arr->items[3]), LMD_TYPE_STRING);
    String* str = arr->items[3].get_string();
    EXPECT_STREQ(str->chars, "test");

    EXPECT_EQ(get_type_id(arr->items[4]), LMD_TYPE_BOOL);
    EXPECT_EQ(arr->items[4].bool_val, 1);
}

// Helper function: Build map
Item BuildMap(Input* input) {
    MarkBuilder builder(input);
    return builder.map()
        .put("name", "John Doe")
        .put("age", (int64_t)42)
        .put("active", true)
        .put("score", 95.5)
        .final();
}

// Test: Map survives builder destruction
TEST_F(MarkBuilderTest, MapSurvivesBuilderDestruction) {
    Item map_item = BuildMap(input);

    // Builder is destroyed, verify map is still valid
    EXPECT_EQ(get_type_id(map_item), LMD_TYPE_MAP);

    Map* map = map_item.map;
    ASSERT_NE(map, nullptr);

    TypeMap* map_type = (TypeMap*)map->type;
    ASSERT_NE(map_type, nullptr);
    EXPECT_GT(map_type->length, 0);
}

// Helper function: Build complex document with mixed content
Item BuildComplexMixedDocument(Input* input) {
    MarkBuilder builder(input);

    Item nested_array = builder.array()
        .append(builder.createInt(1))
        .append(builder.createInt(2))
        .append(builder.createInt(3))
        .final();

    Item nested_map = builder.map()
        .put("key1", "value1")
        .put("key2", "value2")
        .final();

    return builder.element("div")
        .attr("id", "root")
        .attr("class", "complex")
        .child(builder.element("h1").text("Complex Document").final())
        .child(nested_array)
        .child(nested_map)
        .child(builder.element("p")
            .text("This is ")
            .child(builder.element("strong").text("bold").final())
            .text(" text")
            .final())
        .final();
}

// Test: Complex mixed document survives builder destruction
TEST_F(MarkBuilderTest, ComplexMixedDocumentSurvivesBuilderDestruction) {
    Item doc_item = BuildComplexMixedDocument(input);

    // Builder is destroyed, verify entire structure
    EXPECT_EQ(get_type_id(doc_item), LMD_TYPE_ELEMENT);

    Element* root = doc_item.element;
    ASSERT_NE(root, nullptr);

    // Verify root element
    TypeElmt* root_type = (TypeElmt*)root->type;
    EXPECT_EQ(strncmp(root_type->name.str, "div", 3), 0);

    // Should have 4 children: h1, array, map, p
    EXPECT_EQ(root->length, 4);

    // Verify h1
    EXPECT_EQ(get_type_id(root->items[0]), LMD_TYPE_ELEMENT);
    Element* h1 = root->items[0].element;
    TypeElmt* h1_type = (TypeElmt*)h1->type;
    EXPECT_EQ(strncmp(h1_type->name.str, "h1", 2), 0);

    // Verify array
    EXPECT_EQ(get_type_id(root->items[1]), LMD_TYPE_ARRAY);
    Array* arr = root->items[1].array;
    EXPECT_EQ(arr->length, 3);

    // Verify map
    EXPECT_EQ(get_type_id(root->items[2]), LMD_TYPE_MAP);
    Map* map = root->items[2].map;
    ASSERT_NE(map, nullptr);

    // Verify paragraph with nested element
    EXPECT_EQ(get_type_id(root->items[3]), LMD_TYPE_ELEMENT);
    Element* p = root->items[3].element;
    TypeElmt* p_type = (TypeElmt*)p->type;
    EXPECT_EQ(strncmp(p_type->name.str, "p", 1), 0);
    EXPECT_GT(p->length, 0);
}

// Helper function: Build document with many attributes
Item BuildElementWithManyAttributesInFunction(Input* input) {
    MarkBuilder builder(input);
    ElementBuilder elem_builder = builder.element("video");

    for (int i = 0; i < 30; i++) {
        char key[32], val[32];
        snprintf(key, sizeof(key), "data-attr-%d", i);
        snprintf(val, sizeof(val), "value-%d", i);
        elem_builder.attr(key, val);
    }

    return elem_builder.final();
}

// Test: Element with many attributes survives builder destruction
TEST_F(MarkBuilderTest, ElementWithManyAttributesSurvivesBuilderDestruction) {
    Item elem_item = BuildElementWithManyAttributesInFunction(input);

    // Builder is destroyed, verify element and all attributes
    EXPECT_EQ(get_type_id(elem_item), LMD_TYPE_ELEMENT);

    Element* elem = elem_item.element;
    ASSERT_NE(elem, nullptr);

    TypeElmt* elem_type = (TypeElmt*)elem->type;
    ASSERT_NE(elem_type, nullptr);

    // Verify tag name
    EXPECT_EQ(strncmp(elem_type->name.str, "video", 5), 0);

    // Verify has attributes
    EXPECT_EQ(elem_type->length, 30);
}

// Helper function: Build string item
Item BuildStringItem(Input* input) {
    MarkBuilder builder(input);
    return builder.createStringItem("Hello from builder function");
}

// Test: String survives builder destruction
TEST_F(MarkBuilderTest, StringSurvivesBuilderDestruction) {
    Item str_item = BuildStringItem(input);

    // Builder is destroyed, verify string is still valid
    EXPECT_EQ(get_type_id(str_item), LMD_TYPE_STRING);

    String* str = str_item.get_string();
    ASSERT_NE(str, nullptr);
    EXPECT_STREQ(str->chars, "Hello from builder function");
}

// Helper function: Build document fragment (array of elements)
Item BuildDocumentFragment(Input* input) {
    MarkBuilder builder(input);
    return builder.array()
        .append(builder.element("h1").text("Fragment Title").final())
        .append(builder.element("p").text("First paragraph").final())
        .append(builder.element("p").text("Second paragraph").final())
        .append(builder.element("hr").final())
        .append(builder.element("p").text("Third paragraph").final())
        .final();
}

// Test: Document fragment survives builder destruction
TEST_F(MarkBuilderTest, DocumentFragmentSurvivesBuilderDestruction) {
    Item fragment_item = BuildDocumentFragment(input);

    // Builder is destroyed, verify fragment array
    EXPECT_EQ(get_type_id(fragment_item), LMD_TYPE_ARRAY);

    Array* fragment = fragment_item.array;
    ASSERT_NE(fragment, nullptr);
    EXPECT_EQ(fragment->length, 5);

    // Verify all elements
    for (int i = 0; i < fragment->length; i++) {
        EXPECT_EQ(get_type_id(fragment->items[i]), LMD_TYPE_ELEMENT);
        Element* elem = fragment->items[i].element;
        ASSERT_NE(elem, nullptr);
        TypeElmt* elem_type = (TypeElmt*)elem->type;
        ASSERT_NE(elem_type, nullptr);
    }

    // Verify specific elements
    Element* h1 = fragment->items[0].element;
    TypeElmt* h1_type = (TypeElmt*)h1->type;
    EXPECT_EQ(strncmp(h1_type->name.str, "h1", 2), 0);

    Element* hr = fragment->items[3].element;
    TypeElmt* hr_type = (TypeElmt*)hr->type;
    EXPECT_EQ(strncmp(hr_type->name.str, "hr", 2), 0);
}

// Helper function: Build deeply nested structure
Item BuildDeeplyNestedStructure(Input* input) {
    MarkBuilder builder(input);

    // Build innermost element
    Item inner = builder.element("span")
        .attr("class", "inner")
        .text("Deep content")
        .final();

    // Wrap it 5 times
    for (int i = 0; i < 5; i++) {
        char attr_val[32];
        snprintf(attr_val, sizeof(attr_val), "level-%d", i);
        inner = builder.element("div")
            .attr("class", attr_val)
            .child(inner)
            .final();
    }

    return inner;
}

// Test: Deeply nested structure survives builder destruction
TEST_F(MarkBuilderTest, DeeplyNestedStructureSurvivesBuilderDestruction) {
    Item doc_item = BuildDeeplyNestedStructure(input);

    // Builder is destroyed, verify nested structure
    EXPECT_EQ(get_type_id(doc_item), LMD_TYPE_ELEMENT);

    // Navigate down the nesting
    Element* current = doc_item.element;
    for (int i = 0; i < 5; i++) {
        ASSERT_NE(current, nullptr);
        TypeElmt* current_type = (TypeElmt*)current->type;
        EXPECT_EQ(strncmp(current_type->name.str, "div", 3), 0);

        // Should have one child
        EXPECT_EQ(current->length, 1);
        current = current->items[0].element;
    }

    // Verify innermost element
    ASSERT_NE(current, nullptr);
    TypeElmt* inner_type = (TypeElmt*)current->type;
    EXPECT_EQ(strncmp(inner_type->name.str, "span", 4), 0);
}

// Main function
int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
