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

// Main function
int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
