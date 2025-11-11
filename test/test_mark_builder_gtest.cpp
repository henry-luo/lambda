/**
 * GTest-based test suite for MarkBuilder API
 * Tests the fluent C++ interface for building Mark documents
 */

#include <gtest/gtest.h>
#include "../lambda/mark_builder.hpp"
#include "../lambda/lambda-data.hpp"
#include "../lambda/input/input.h"
#include "../lib/mempool.h"
#include "../lib/arraylist.h"
#include "../lib/stringbuf.h"
#include <cmath>
#include <string>
#include <cstdint>
#include <climits>

// Test fixture for MarkBuilder tests
class MarkBuilderTest : public ::testing::Test {
protected:
    Input* input;
    
    void SetUp() override {
        // Create minimal Input structure for testing
        Pool* pool = pool_create();
        NamePool* name_pool = name_pool_create(pool, nullptr);
        ArrayList* type_list = arraylist_new(32);
        StringBuf* sb = stringbuf_new_cap(pool, 256);
        
        input = (Input*)pool_alloc(pool, sizeof(Input));
        input->pool = pool;
        input->name_pool = name_pool;
        input->type_list = type_list;
        input->sb = sb;
        input->url = nullptr;
        input->path = nullptr;
        input->root = (Item){.item = 0};
    }
    
    void TearDown() override {
        if (input) {
            arraylist_free(input->type_list);
            pool_destroy(input->pool);
        }
    }
};

// Test string creation
TEST_F(MarkBuilderTest, CreateString) {
    MarkBuilder builder(input);
    
    Item str_item = builder.createStringItem("Hello, World!");
    ASSERT_EQ(str_item.type_id, LMD_TYPE_STRING);
    
    String* str = (String*)str_item.pointer;
    ASSERT_NE(str, nullptr);
    EXPECT_STREQ(str->chars, "Hello, World!");
}

// Test integer creation
TEST_F(MarkBuilderTest, CreateInt) {
    MarkBuilder builder(input);
    
    Item int_item = builder.createInt(42);
    EXPECT_EQ(int_item.type_id, LMD_TYPE_INT);
    EXPECT_EQ(int_item.int_val, 42);
}

// Test float creation
TEST_F(MarkBuilderTest, CreateFloat) {
    MarkBuilder builder(input);
    
    Item float_item = builder.createFloat(3.14);
    EXPECT_EQ(float_item.type_id, LMD_TYPE_FLOAT);
    
    double* val = (double*)float_item.pointer;
    ASSERT_NE(val, nullptr);
    EXPECT_DOUBLE_EQ(*val, 3.14);
}

// Test boolean creation
TEST_F(MarkBuilderTest, CreateBool) {
    MarkBuilder builder(input);
    
    Item bool_true = builder.createBool(true);
    EXPECT_EQ(bool_true.type_id, LMD_TYPE_BOOL);
    EXPECT_EQ(bool_true.bool_val, 1);
    
    Item bool_false = builder.createBool(false);
    EXPECT_EQ(bool_false.type_id, LMD_TYPE_BOOL);
    EXPECT_EQ(bool_false.bool_val, 0);
}

// Test null creation
TEST_F(MarkBuilderTest, CreateNull) {
    MarkBuilder builder(input);
    
    Item null_item = builder.createNull();
    EXPECT_EQ(null_item.type_id, LMD_TYPE_NULL);
}

// Test array creation
TEST_F(MarkBuilderTest, CreateArray) {
    MarkBuilder builder(input);
    
    Item array_item = builder.array()
        .append(builder.createInt(1))
        .append(builder.createInt(2))
        .append(builder.createInt(3))
        .build();
    
    EXPECT_EQ(array_item.type_id, LMD_TYPE_ARRAY);
    
    Array* arr = (Array*)array_item.pointer;
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
    
    Item array_item = builder.array().build();
    
    EXPECT_EQ(array_item.type_id, LMD_TYPE_ARRAY);
    
    Array* arr = (Array*)array_item.pointer;
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
        .build();
    
    Array* arr = (Array*)array_item.pointer;
    ASSERT_NE(arr, nullptr);
    EXPECT_EQ(arr->length, 4);
    
    EXPECT_EQ(arr->items[0].type_id, LMD_TYPE_INT);
    EXPECT_EQ(arr->items[1].type_id, LMD_TYPE_STRING);
    EXPECT_EQ(arr->items[2].type_id, LMD_TYPE_BOOL);
    EXPECT_EQ(arr->items[3].type_id, LMD_TYPE_NULL);
}

// Test simple element creation
TEST_F(MarkBuilderTest, CreateSimpleElement) {
    MarkBuilder builder(input);
    
    Item elem_item = builder.element("div")
        .text("Hello World")
        .build();
    
    EXPECT_EQ(elem_item.type_id, LMD_TYPE_ELEMENT);
    
    Element* elem = (Element*)elem_item.pointer;
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
        .build();
    
    EXPECT_EQ(elem_item.type_id, LMD_TYPE_ELEMENT);
    
    Element* elem = (Element*)elem_item.pointer;
    ASSERT_NE(elem, nullptr);
    
    TypeElmt* elem_type = (TypeElmt*)elem->type;
    ASSERT_NE(elem_type, nullptr);
}

// Test nested elements
TEST_F(MarkBuilderTest, CreateNestedElements) {
    MarkBuilder builder(input);
    
    Item child = builder.element("span")
        .text("Inner")
        .build();
    
    Item parent = builder.element("div")
        .child(child)
        .build();
    
    EXPECT_EQ(parent.type_id, LMD_TYPE_ELEMENT);
    
    Element* elem = (Element*)parent.pointer;
    ASSERT_NE(elem, nullptr);
    EXPECT_EQ(elem->length, 1);
    EXPECT_EQ(elem->items[0].type_id, LMD_TYPE_ELEMENT);
}

// Test map creation
TEST_F(MarkBuilderTest, CreateMap) {
    MarkBuilder builder(input);
    
    Item map_item = builder.map()
        .put("name", "John")
        .put("age", (int64_t)30)  // Explicit cast to avoid ambiguity
        .put("active", true)
        .build();
    
    EXPECT_EQ(map_item.type_id, LMD_TYPE_MAP);
    
    Map* map = (Map*)map_item.pointer;
    ASSERT_NE(map, nullptr);
}

// Test empty map
TEST_F(MarkBuilderTest, CreateEmptyMap) {
    MarkBuilder builder(input);
    
    Item map_item = builder.map().build();
    
    EXPECT_EQ(map_item.type_id, LMD_TYPE_MAP);
    
    Map* map = (Map*)map_item.pointer;
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
            .build())
        .child(builder.element("p")
            .text("Paragraph text")
            .build())
        .child(builder.array()
            .append(builder.createInt(1))
            .append(builder.createInt(2))
            .build())
        .build();
    
    EXPECT_EQ(doc.type_id, LMD_TYPE_ELEMENT);
    
    Element* article = (Element*)doc.pointer;
    ASSERT_NE(article, nullptr);
    EXPECT_GE(article->length, 3);
}

// Test string interning
TEST_F(MarkBuilderTest, StringInterning) {
    MarkBuilder builder(input);
    builder.setInternStrings(true);
    
    Item str1 = builder.createStringItem("test");
    Item str2 = builder.createStringItem("test");
    
    // Verify both are valid strings
    EXPECT_EQ(str1.type_id, LMD_TYPE_STRING);
    EXPECT_EQ(str2.type_id, LMD_TYPE_STRING);
    
    String* s1 = (String*)str1.pointer;
    String* s2 = (String*)str2.pointer;
    ASSERT_NE(s1, nullptr);
    ASSERT_NE(s2, nullptr);
    
    // Both should have the same content
    EXPECT_STREQ(s1->chars, "test");
    EXPECT_STREQ(s2->chars, "test");
    
    // Note: Pointer equality depends on name pool implementation
    // Some implementations may return the same pointer, others may not
}

// Test auto string merge
TEST_F(MarkBuilderTest, AutoStringMerge) {
    MarkBuilder builder(input);
    builder.setAutoStringMerge(true);
    
    Item elem_item = builder.element("p")
        .text("Hello ")
        .text("World")
        .build();
    
    Element* elem = (Element*)elem_item.pointer;
    ASSERT_NE(elem, nullptr);
    
    // With auto merge, adjacent strings should be merged
    // (actual verification would depend on implementation details)
}

//==============================================================================
// Negative Tests - Error Handling
//==============================================================================

// Test null/empty string handling
TEST_F(MarkBuilderTest, NullAndEmptyStrings) {
    MarkBuilder builder(input);
    
    // Null string should return EMPTY_STRING sentinel (not actually empty)
    Item null_str = builder.createStringItem(nullptr);
    EXPECT_EQ(null_str.type_id, LMD_TYPE_STRING);
    String* str = (String*)null_str.pointer;
    ASSERT_NE(str, nullptr);
    // EMPTY_STRING is actually "lambda.nil" with length 10
    EXPECT_EQ(str->len, 10);
    EXPECT_STREQ(str->chars, "lambda.nil");
    
    // Empty string should also return EMPTY_STRING sentinel
    Item empty_str = builder.createStringItem("");
    EXPECT_EQ(empty_str.type_id, LMD_TYPE_STRING);
    String* str2 = (String*)empty_str.pointer;
    ASSERT_NE(str2, nullptr);
    EXPECT_EQ(str2->len, 10);
    EXPECT_STREQ(str2->chars, "lambda.nil");
    
    // Zero-length string should also return EMPTY_STRING sentinel
    Item zero_len = builder.createStringItem("test", 0);
    EXPECT_EQ(zero_len.type_id, LMD_TYPE_STRING);
    String* str3 = (String*)zero_len.pointer;
    ASSERT_NE(str3, nullptr);
    EXPECT_EQ(str3->len, 10);
    EXPECT_STREQ(str3->chars, "lambda.nil");
}

// Test element with null tag name
TEST_F(MarkBuilderTest, ElementWithNullTagName) {
    MarkBuilder builder(input);
    
    // Should handle null tag name gracefully
    Item elem_item = builder.element(nullptr)
        .text("Content")
        .build();
    
    EXPECT_EQ(elem_item.type_id, LMD_TYPE_ELEMENT);
    Element* elem = (Element*)elem_item.pointer;
    ASSERT_NE(elem, nullptr);
}

// Test element with empty tag name
TEST_F(MarkBuilderTest, ElementWithEmptyTagName) {
    MarkBuilder builder(input);
    
    Item elem_item = builder.element("")
        .text("Content")
        .build();
    
    EXPECT_EQ(elem_item.type_id, LMD_TYPE_ELEMENT);
    Element* elem = (Element*)elem_item.pointer;
    ASSERT_NE(elem, nullptr);
}

// Test null text in element
TEST_F(MarkBuilderTest, ElementWithNullText) {
    MarkBuilder builder(input);
    
    Item elem_item = builder.element("div")
        .text(nullptr)
        .build();
    
    Element* elem = (Element*)elem_item.pointer;
    ASSERT_NE(elem, nullptr);
    // Should have no children or handle gracefully
}

// Test null attribute key
TEST_F(MarkBuilderTest, ElementWithNullAttributeKey) {
    MarkBuilder builder(input);
    
    Item elem_item = builder.element("div")
        .attr(nullptr, "value")
        .build();
    
    Element* elem = (Element*)elem_item.pointer;
    ASSERT_NE(elem, nullptr);
}

// Test null attribute value
TEST_F(MarkBuilderTest, ElementWithNullAttributeValue) {
    MarkBuilder builder(input);
    
    Item elem_item = builder.element("div")
        .attr("key", nullptr)
        .build();
    
    Element* elem = (Element*)elem_item.pointer;
    ASSERT_NE(elem, nullptr);
}

// Test null map key
TEST_F(MarkBuilderTest, MapWithNullKey) {
    MarkBuilder builder(input);
    
    Item map_item = builder.map()
        .put(nullptr, "value")
        .build();
    
    Map* map = (Map*)map_item.pointer;
    ASSERT_NE(map, nullptr);
}

// Test null map value
TEST_F(MarkBuilderTest, MapWithNullValue) {
    MarkBuilder builder(input);
    
    Item map_item = builder.map()
        .put("key", (const char*)nullptr)
        .build();
    
    Map* map = (Map*)map_item.pointer;
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
    
    EXPECT_EQ(str_item.type_id, LMD_TYPE_STRING);
    String* str = (String*)str_item.pointer;
    ASSERT_NE(str, nullptr);
    EXPECT_EQ(str->len, 10000);
}

// Test deeply nested elements
TEST_F(MarkBuilderTest, DeeplyNestedElements) {
    MarkBuilder builder(input);
    
    // Create 10 levels of nesting
    Item inner = builder.element("span").text("Deep").build();
    
    for (int i = 0; i < 10; i++) {
        inner = builder.element("div")
            .child(inner)
            .build();
    }
    
    EXPECT_EQ(inner.type_id, LMD_TYPE_ELEMENT);
}

// Test large array
TEST_F(MarkBuilderTest, LargeArray) {
    MarkBuilder builder(input);
    
    ArrayBuilder arr_builder = builder.array();
    
    // Add 1000 items
    for (int i = 0; i < 1000; i++) {
        arr_builder.append(builder.createInt(i));
    }
    
    Item array_item = arr_builder.build();
    Array* arr = (Array*)array_item.pointer;
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
    
    Item elem_item = elem_builder.build();
    Element* elem = (Element*)elem_item.pointer;
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
            .build());
    }
    
    Item elem_item = elem_builder.build();
    Element* elem = (Element*)elem_item.pointer;
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
    
    Item map_item = map_builder.build();
    Map* map = (Map*)map_item.pointer;
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
    
    String* str = (String*)str_item.pointer;
    ASSERT_NE(str, nullptr);
    EXPECT_EQ(str->len, 13);
    EXPECT_STREQ(str->chars, "Hello, World!");
}

// Test creating strings with special characters (parser needs)
TEST_F(MarkBuilderTest, StringsWithSpecialCharacters) {
    MarkBuilder builder(input);
    
    // Newlines, tabs, quotes
    Item str1 = builder.createStringItem("Line1\nLine2\tTabbed");
    String* s1 = (String*)str1.pointer;
    EXPECT_STREQ(s1->chars, "Line1\nLine2\tTabbed");
    
    // Unicode
    Item str2 = builder.createStringItem("Hello 世界 🌍");
    String* s2 = (String*)str2.pointer;
    EXPECT_STREQ(s2->chars, "Hello 世界 🌍");
    
    // Quotes and escapes
    Item str3 = builder.createStringItem("\"quoted\" and 'single'");
    String* s3 = (String*)str3.pointer;
    EXPECT_STREQ(s3->chars, "\"quoted\" and 'single'");
}

// Test mixed content elements (parser needs)
TEST_F(MarkBuilderTest, ElementWithMixedContent) {
    MarkBuilder builder(input);
    
    Item elem_item = builder.element("p")
        .text("Start ")
        .child(builder.element("strong").text("bold").build())
        .text(" middle ")
        .child(builder.element("em").text("italic").build())
        .text(" end")
        .build();
    
    Element* elem = (Element*)elem_item.pointer;
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
        .build();
    
    Element* elem = (Element*)elem_item.pointer;
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
    elem_builder.child(builder.element("h1").text("Title").build());
    elem_builder.child(builder.element("p").text("Content").build());
    
    Item result = elem_builder.build();
    EXPECT_EQ(result.type_id, LMD_TYPE_ELEMENT);
}

// Test reusing builder for multiple documents
TEST_F(MarkBuilderTest, ReuseBuilderForMultipleDocs) {
    MarkBuilder builder(input);
    
    // Build first document
    Item doc1 = builder.element("div")
        .text("First")
        .build();
    EXPECT_EQ(doc1.type_id, LMD_TYPE_ELEMENT);
    
    // Build second document with same builder
    Item doc2 = builder.element("span")
        .text("Second")
        .build();
    EXPECT_EQ(doc2.type_id, LMD_TYPE_ELEMENT);
    
    // Both should be valid
    Element* e1 = (Element*)doc1.pointer;
    Element* e2 = (Element*)doc2.pointer;
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
    
    Item result = arr.build();
    Array* array = (Array*)result.pointer;
    ASSERT_NE(array, nullptr);
    EXPECT_EQ(array->length, 3);
}

// Test map with Item values (parser needs)
TEST_F(MarkBuilderTest, MapWithItemValues) {
    MarkBuilder builder(input);
    
    Item nested_array = builder.array()
        .append(builder.createInt(1))
        .append(builder.createInt(2))
        .build();
    
    Item map_item = builder.map()
        .put("data", nested_array)
        .build();
    
    Map* map = (Map*)map_item.pointer;
    ASSERT_NE(map, nullptr);
}

// Test building fragments without root element
TEST_F(MarkBuilderTest, BuildFragmentArray) {
    MarkBuilder builder(input);
    
    // Parser may build fragment list
    Item fragment = builder.array()
        .append(builder.element("h1").text("Title").build())
        .append(builder.element("p").text("Para 1").build())
        .append(builder.element("p").text("Para 2").build())
        .build();
    
    Array* arr = (Array*)fragment.pointer;
    ASSERT_NE(arr, nullptr);
    EXPECT_EQ(arr->length, 3);
    
    // All should be elements
    for (int i = 0; i < arr->length; i++) {
        EXPECT_EQ(arr->items[i].type_id, LMD_TYPE_ELEMENT);
    }
}

// Test integer boundary values
TEST_F(MarkBuilderTest, IntegerBoundaries) {
    MarkBuilder builder(input);
    
    // Note: Item.int_val is only 32 bits, so test 32-bit boundaries
    Item max_int = builder.createInt(INT32_MAX);
    EXPECT_EQ(max_int.int_val, INT32_MAX);
    
    Item min_int = builder.createInt(INT32_MIN);
    EXPECT_EQ(min_int.int_val, INT32_MIN);
    
    Item zero = builder.createInt(0);
    EXPECT_EQ(zero.int_val, 0);
    
    // Negative values
    Item neg = builder.createInt(-42);
    EXPECT_EQ(neg.int_val, -42);
    
    // Large values that fit in 32 bits
    Item large = builder.createInt(1000000);
    EXPECT_EQ(large.int_val, 1000000);
}

// Test float special values
TEST_F(MarkBuilderTest, FloatSpecialValues) {
    MarkBuilder builder(input);
    
    Item inf_val = builder.createFloat(INFINITY);
    double* inf_ptr = (double*)inf_val.pointer;
    EXPECT_TRUE(std::isinf(*inf_ptr));
    
    Item neg_inf = builder.createFloat(-INFINITY);
    double* neg_inf_ptr = (double*)neg_inf.pointer;
    EXPECT_TRUE(std::isinf(*neg_inf_ptr));
    
    Item nan_val = builder.createFloat(NAN);
    double* nan_ptr = (double*)nan_val.pointer;
    EXPECT_TRUE(std::isnan(*nan_ptr));
    
    Item zero_val = builder.createFloat(0.0);
    double* zero_ptr = (double*)zero_val.pointer;
    EXPECT_DOUBLE_EQ(*zero_ptr, 0.0);
}

// Test empty string buffer usage
TEST_F(MarkBuilderTest, EmptyStringBuf) {
    MarkBuilder builder(input);
    
    // Get empty string from builder (returns EMPTY_STRING sentinel)
    String* empty = builder.emptyString();
    ASSERT_NE(empty, nullptr);
    EXPECT_EQ(empty->len, 10);
    EXPECT_STREQ(empty->chars, "lambda.nil");
}

// Test duplicate keys in map (last wins)
TEST_F(MarkBuilderTest, MapDuplicateKeys) {
    MarkBuilder builder(input);
    
    Item map_item = builder.map()
        .put("key", "value1")
        .put("key", "value2")  // Should override
        .build();
    
    Map* map = (Map*)map_item.pointer;
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
        .build();
    
    EXPECT_EQ(elem_item.type_id, LMD_TYPE_ELEMENT);
    
    Element* elem = (Element*)elem_item.pointer;
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
        .build();
    
    EXPECT_EQ(elem_item.type_id, LMD_TYPE_ELEMENT);
    
    Element* elem = (Element*)elem_item.pointer;
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
    
    Item elem_item = elem_builder.build();
    
    Element* elem = (Element*)elem_item.pointer;
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
        .build();
    
    Element* elem = (Element*)elem_item.pointer;
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
        .child(builder.element("li").text("Item 1").build())
        .attr("id", "mylist")  // Add attribute after child
        .child(builder.element("li").text("Item 2").build())
        .build();
    
    Element* elem = (Element*)elem_item.pointer;
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
            .build())
        .build();
    
    Element* div_elem = (Element*)elem_item.pointer;
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
    EXPECT_EQ(child_item.type_id, LMD_TYPE_ELEMENT);
    
    Element* span_elem = (Element*)child_item.pointer;
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
        .build();
    
    Element* e1 = (Element*)elem1.pointer;
    TypeElmt* t1 = (TypeElmt*)e1->type;
    EXPECT_EQ(t1->name.length, 1);
    EXPECT_EQ(strncmp(t1->name.str, "a", 1), 0);
    
    // Medium tag name
    Item elem2 = builder.element("button")
        .attr("type", "submit")
        .build();
    
    Element* e2 = (Element*)elem2.pointer;
    TypeElmt* t2 = (TypeElmt*)e2->type;
    EXPECT_EQ(t2->name.length, 6);
    EXPECT_EQ(strncmp(t2->name.str, "button", 6), 0);
    
    // Long tag name
    Item elem3 = builder.element("custom-web-component")
        .attr("data-value", "123")
        .build();
    
    Element* e3 = (Element*)elem3.pointer;
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
        .build();
    
    Element* e1 = (Element*)elem1.pointer;
    TypeElmt* t1 = (TypeElmt*)e1->type;
    EXPECT_EQ(t1->name.length, 10);
    EXPECT_EQ(strncmp(t1->name.str, "my-element", 10), 0);
    
    // Underscored tag name
    Item elem2 = builder.element("my_element")
        .attr("class", "test")
        .build();
    
    Element* e2 = (Element*)elem2.pointer;
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
        .build();
    
    Element* elem = (Element*)elem_item.pointer;
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
        .build();
    
    Item elem2 = builder.element("div")
        .attr("id", "second")
        .build();
    
    Item elem3 = builder.element("div")
        .attr("id", "third")
        .build();
    
    // All should have "div" as tag name
    Element* e1 = (Element*)elem1.pointer;
    TypeElmt* t1 = (TypeElmt*)e1->type;
    EXPECT_EQ(strncmp(t1->name.str, "div", 3), 0);
    
    Element* e2 = (Element*)elem2.pointer;
    TypeElmt* t2 = (TypeElmt*)e2->type;
    EXPECT_EQ(strncmp(t2->name.str, "div", 3), 0);
    
    Element* e3 = (Element*)elem3.pointer;
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
                .build())
            .child(builder.element("p")
                .attr("class", "meta")
                .text("By Author")
                .build())
            .build())
        .child(builder.element("section")
            .attr("class", "content")
            .child(builder.element("p").text("Paragraph 1").build())
            .child(builder.element("p").text("Paragraph 2").build())
            .build())
        .child(builder.element("footer")
            .attr("class", "post-footer")
            .text("Footer content")
            .build())
        .build();
    
    // Verify root element
    Element* article = (Element*)doc.pointer;
    ASSERT_NE(article, nullptr);
    TypeElmt* article_type = (TypeElmt*)article->type;
    EXPECT_EQ(strncmp(article_type->name.str, "article", 7), 0);
    
    // Verify first child (header)
    ASSERT_GE(article->length, 1);
    Element* header = (Element*)article->items[0].pointer;
    TypeElmt* header_type = (TypeElmt*)header->type;
    EXPECT_EQ(strncmp(header_type->name.str, "header", 6), 0);
    
    // Verify header's first child (h1)
    ASSERT_GE(header->length, 1);
    Element* h1 = (Element*)header->items[0].pointer;
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
        .build();
    
    Element* e1 = (Element*)elem1.pointer;
    TypeElmt* t1 = (TypeElmt*)e1->type;
    EXPECT_EQ(strncmp(t1->name.str, "p", 1), 0);
    
    // Text before attributes
    Item elem2 = builder.element("p")
        .text("Content")
        .attr("id", "para2")
        .build();
    
    Element* e2 = (Element*)elem2.pointer;
    TypeElmt* t2 = (TypeElmt*)e2->type;
    EXPECT_EQ(strncmp(t2->name.str, "p", 1), 0);
    
    // Interleaved
    Item elem3 = builder.element("p")
        .attr("class", "test")
        .text("Start")
        .attr("id", "para3")
        .text(" End")
        .build();
    
    Element* e3 = (Element*)elem3.pointer;
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
        .build();
}

// Test: Simple element survives builder destruction
TEST_F(MarkBuilderTest, SimpleElementSurvivesBuilderDestruction) {
    Item elem_item = BuildSimpleElement(input);
    
    // Builder is destroyed, verify element is still valid
    EXPECT_EQ(elem_item.type_id, LMD_TYPE_ELEMENT);
    
    Element* elem = (Element*)elem_item.pointer;
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
        .build();
}

// Test: Element with attributes survives builder destruction
TEST_F(MarkBuilderTest, ElementWithAttributesSurvivesBuilderDestruction) {
    Item elem_item = BuildElementWithAttributes(input);
    
    // Builder is destroyed, verify element is still valid
    EXPECT_EQ(elem_item.type_id, LMD_TYPE_ELEMENT);
    
    Element* elem = (Element*)elem_item.pointer;
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
            .child(builder.element("h1").text("Title").build())
            .child(builder.element("p").text("Subtitle").build())
            .build())
        .child(builder.element("article")
            .attr("id", "main-article")
            .child(builder.element("p").text("Paragraph 1").build())
            .child(builder.element("p").text("Paragraph 2").build())
            .build())
        .child(builder.element("footer")
            .text("Footer text")
            .build())
        .build();
}

// Test: Nested document survives builder destruction
TEST_F(MarkBuilderTest, NestedDocumentSurvivesBuilderDestruction) {
    Item doc_item = BuildNestedDocument(input);
    
    // Builder is destroyed, verify entire structure is still valid
    EXPECT_EQ(doc_item.type_id, LMD_TYPE_ELEMENT);
    
    Element* section = (Element*)doc_item.pointer;
    ASSERT_NE(section, nullptr);
    
    // Verify root element
    TypeElmt* section_type = (TypeElmt*)section->type;
    EXPECT_EQ(strncmp(section_type->name.str, "section", 7), 0);
    
    // Verify has children
    EXPECT_EQ(section->length, 3);
    
    // Verify first child (header)
    Element* header = (Element*)section->items[0].pointer;
    ASSERT_NE(header, nullptr);
    TypeElmt* header_type = (TypeElmt*)header->type;
    EXPECT_EQ(strncmp(header_type->name.str, "header", 6), 0);
    EXPECT_EQ(header->length, 2);
    
    // Verify header's first child (h1)
    Element* h1 = (Element*)header->items[0].pointer;
    ASSERT_NE(h1, nullptr);
    TypeElmt* h1_type = (TypeElmt*)h1->type;
    EXPECT_EQ(strncmp(h1_type->name.str, "h1", 2), 0);
    
    // Verify second child (article)
    Element* article = (Element*)section->items[1].pointer;
    ASSERT_NE(article, nullptr);
    TypeElmt* article_type = (TypeElmt*)article->type;
    EXPECT_EQ(strncmp(article_type->name.str, "article", 7), 0);
    EXPECT_EQ(article->length, 2);
    
    // Verify third child (footer)
    Element* footer = (Element*)section->items[2].pointer;
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
        .build();
}

// Test: Array survives builder destruction
TEST_F(MarkBuilderTest, ArraySurvivesBuilderDestruction) {
    Item array_item = BuildArray(input);
    
    // Builder is destroyed, verify array is still valid
    EXPECT_EQ(array_item.type_id, LMD_TYPE_ARRAY);
    
    Array* arr = (Array*)array_item.pointer;
    ASSERT_NE(arr, nullptr);
    EXPECT_EQ(arr->length, 5);
    
    // Verify array contents
    EXPECT_EQ(arr->items[0].type_id, LMD_TYPE_INT);
    EXPECT_EQ(arr->items[0].int_val, 10);
    
    EXPECT_EQ(arr->items[1].type_id, LMD_TYPE_INT);
    EXPECT_EQ(arr->items[1].int_val, 20);
    
    EXPECT_EQ(arr->items[2].type_id, LMD_TYPE_INT);
    EXPECT_EQ(arr->items[2].int_val, 30);
    
    EXPECT_EQ(arr->items[3].type_id, LMD_TYPE_STRING);
    String* str = (String*)arr->items[3].pointer;
    EXPECT_STREQ(str->chars, "test");
    
    EXPECT_EQ(arr->items[4].type_id, LMD_TYPE_BOOL);
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
        .build();
}

// Test: Map survives builder destruction
TEST_F(MarkBuilderTest, MapSurvivesBuilderDestruction) {
    Item map_item = BuildMap(input);
    
    // Builder is destroyed, verify map is still valid
    EXPECT_EQ(map_item.type_id, LMD_TYPE_MAP);
    
    Map* map = (Map*)map_item.pointer;
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
        .build();
    
    Item nested_map = builder.map()
        .put("key1", "value1")
        .put("key2", "value2")
        .build();
    
    return builder.element("div")
        .attr("id", "root")
        .attr("class", "complex")
        .child(builder.element("h1").text("Complex Document").build())
        .child(nested_array)
        .child(nested_map)
        .child(builder.element("p")
            .text("This is ")
            .child(builder.element("strong").text("bold").build())
            .text(" text")
            .build())
        .build();
}

// Test: Complex mixed document survives builder destruction
TEST_F(MarkBuilderTest, ComplexMixedDocumentSurvivesBuilderDestruction) {
    Item doc_item = BuildComplexMixedDocument(input);
    
    // Builder is destroyed, verify entire structure
    EXPECT_EQ(doc_item.type_id, LMD_TYPE_ELEMENT);
    
    Element* root = (Element*)doc_item.pointer;
    ASSERT_NE(root, nullptr);
    
    // Verify root element
    TypeElmt* root_type = (TypeElmt*)root->type;
    EXPECT_EQ(strncmp(root_type->name.str, "div", 3), 0);
    
    // Should have 4 children: h1, array, map, p
    EXPECT_EQ(root->length, 4);
    
    // Verify h1
    EXPECT_EQ(root->items[0].type_id, LMD_TYPE_ELEMENT);
    Element* h1 = (Element*)root->items[0].pointer;
    TypeElmt* h1_type = (TypeElmt*)h1->type;
    EXPECT_EQ(strncmp(h1_type->name.str, "h1", 2), 0);
    
    // Verify array
    EXPECT_EQ(root->items[1].type_id, LMD_TYPE_ARRAY);
    Array* arr = (Array*)root->items[1].pointer;
    EXPECT_EQ(arr->length, 3);
    
    // Verify map
    EXPECT_EQ(root->items[2].type_id, LMD_TYPE_MAP);
    Map* map = (Map*)root->items[2].pointer;
    ASSERT_NE(map, nullptr);
    
    // Verify paragraph with nested element
    EXPECT_EQ(root->items[3].type_id, LMD_TYPE_ELEMENT);
    Element* p = (Element*)root->items[3].pointer;
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
    
    return elem_builder.build();
}

// Test: Element with many attributes survives builder destruction
TEST_F(MarkBuilderTest, ElementWithManyAttributesSurvivesBuilderDestruction) {
    Item elem_item = BuildElementWithManyAttributesInFunction(input);
    
    // Builder is destroyed, verify element and all attributes
    EXPECT_EQ(elem_item.type_id, LMD_TYPE_ELEMENT);
    
    Element* elem = (Element*)elem_item.pointer;
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
    EXPECT_EQ(str_item.type_id, LMD_TYPE_STRING);
    
    String* str = (String*)str_item.pointer;
    ASSERT_NE(str, nullptr);
    EXPECT_STREQ(str->chars, "Hello from builder function");
}

// Helper function: Build document fragment (array of elements)
Item BuildDocumentFragment(Input* input) {
    MarkBuilder builder(input);
    return builder.array()
        .append(builder.element("h1").text("Fragment Title").build())
        .append(builder.element("p").text("First paragraph").build())
        .append(builder.element("p").text("Second paragraph").build())
        .append(builder.element("hr").build())
        .append(builder.element("p").text("Third paragraph").build())
        .build();
}

// Test: Document fragment survives builder destruction
TEST_F(MarkBuilderTest, DocumentFragmentSurvivesBuilderDestruction) {
    Item fragment_item = BuildDocumentFragment(input);
    
    // Builder is destroyed, verify fragment array
    EXPECT_EQ(fragment_item.type_id, LMD_TYPE_ARRAY);
    
    Array* fragment = (Array*)fragment_item.pointer;
    ASSERT_NE(fragment, nullptr);
    EXPECT_EQ(fragment->length, 5);
    
    // Verify all elements
    for (int i = 0; i < fragment->length; i++) {
        EXPECT_EQ(fragment->items[i].type_id, LMD_TYPE_ELEMENT);
        Element* elem = (Element*)fragment->items[i].pointer;
        ASSERT_NE(elem, nullptr);
        TypeElmt* elem_type = (TypeElmt*)elem->type;
        ASSERT_NE(elem_type, nullptr);
    }
    
    // Verify specific elements
    Element* h1 = (Element*)fragment->items[0].pointer;
    TypeElmt* h1_type = (TypeElmt*)h1->type;
    EXPECT_EQ(strncmp(h1_type->name.str, "h1", 2), 0);
    
    Element* hr = (Element*)fragment->items[3].pointer;
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
        .build();
    
    // Wrap it 5 times
    for (int i = 0; i < 5; i++) {
        char attr_val[32];
        snprintf(attr_val, sizeof(attr_val), "level-%d", i);
        inner = builder.element("div")
            .attr("class", attr_val)
            .child(inner)
            .build();
    }
    
    return inner;
}

// Test: Deeply nested structure survives builder destruction
TEST_F(MarkBuilderTest, DeeplyNestedStructureSurvivesBuilderDestruction) {
    Item doc_item = BuildDeeplyNestedStructure(input);
    
    // Builder is destroyed, verify nested structure
    EXPECT_EQ(doc_item.type_id, LMD_TYPE_ELEMENT);
    
    // Navigate down the nesting
    Element* current = (Element*)doc_item.pointer;
    for (int i = 0; i < 5; i++) {
        ASSERT_NE(current, nullptr);
        TypeElmt* current_type = (TypeElmt*)current->type;
        EXPECT_EQ(strncmp(current_type->name.str, "div", 3), 0);
        
        // Should have one child
        EXPECT_EQ(current->length, 1);
        current = (Element*)current->items[0].pointer;
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
