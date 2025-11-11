/*
 * Comprehensive ElementReader Test Suite (GTest Version)
 * ======================================================
 *
 * Test Coverage:
 * - ElementReader creation and basic operations
 * - Element property access (tag names, counts, flags)
 * - Child access and navigation
 * - Text content extraction (immediate and recursive)
 * - Attribute access and type conversion
 * - Element tree iteration (multiple modes)
 * - Search operations (by ID, class, attribute, tag)
 * - Utility functions (tree statistics, debug output)
 * - Edge cases and error handling
 * - Memory management and pool integration
 * - Performance characteristics
 * - Thread safety considerations
 */

#include <gtest/gtest.h>
#include <cstring>
#include <cstdio>

extern "C" {
#include "../lib/mempool.h"
#include "../lib/arraylist.h"
#include "../lib/strbuf.h"
#include "../lib/url.h"
#include "../lambda/lambda-data.hpp"
#include "../lambda/element_reader.h"
#include "../lambda/input/input.h"
}
#include "../lambda/mark_reader.hpp"



// ========================================================================
// Test Fixture and Helper Functions
// ========================================================================

class ElementReaderTest : public ::testing::Test {
protected:
    Pool* pool;
    Input* input;

    void SetUp() override {
        pool = pool_create();
        ASSERT_NE(pool, nullptr);
        input = nullptr;
    }

    void TearDown() override {
        if (input) {
            // Input cleanup would be handled by the Lambda runtime
            input = nullptr;
        }
        if (pool) {
            pool_destroy(pool);
            pool = nullptr;
        }
    }

    // Helper: Create Lambda string
    String* create_lambda_string(const char* text) {
        if (!text) return nullptr;
        
        size_t len = strlen(text);
        String* result = (String*)pool_alloc(pool, sizeof(String) + len + 1);
        if (!result) return nullptr;
        
        result->len = len;
        result->ref_cnt = 1;
        memcpy(result->chars, text, len);
        result->chars[len] = '\0';
        return result;
    }

    // Helper: Create a mock element with specified tag and attributes
    Element* create_mock_element(const char* tag_name, Pool* element_pool = nullptr) {
        Pool* use_pool = element_pool ? element_pool : pool;
        
        Element* element = (Element*)pool_calloc(use_pool, sizeof(Element));
        if (!element) return nullptr;
        
        // Set up as both List (for children) and Element
        element->type_id = LMD_TYPE_ELEMENT;
        element->ref_cnt = 1;
        element->items = nullptr;
        element->length = 0;
        element->extra = 0;
        element->capacity = 0;
        
        // Create element type with tag name
        TypeElmt* elem_type = (TypeElmt*)pool_alloc(use_pool, sizeof(TypeElmt));
        if (!elem_type) return nullptr;
        
        elem_type->type_id = LMD_TYPE_ELEMENT;
        elem_type->length = 0;  // No attributes initially
        elem_type->byte_size = 0;
        elem_type->type_index = 0;
        elem_type->shape = nullptr;
        elem_type->last = nullptr;
        elem_type->content_length = 0;
        
        // Set tag name
        if (tag_name) {
            size_t name_len = strlen(tag_name);
            char* name_copy = (char*)pool_alloc(use_pool, name_len + 1);
            if (name_copy) {
                memcpy(name_copy, tag_name, name_len + 1);
                elem_type->name = strview_init(name_copy, name_len);
            }
        } else {
            elem_type->name.str = nullptr;
            elem_type->name.length = 0;
        }
        
        element->type = elem_type;
        element->data = nullptr;
        element->data_cap = 0;
        
        return element;
    }

    // Helper: Add child to element
    void add_child_to_element(Element* parent, Item child) {
        if (!parent) return;
        
        // Extend capacity if needed
        if (parent->length >= parent->capacity) {
            int64_t new_capacity = parent->capacity > 0 ? parent->capacity * 2 : 4;
            Item* new_items = (Item*)pool_alloc(pool, sizeof(Item) * new_capacity);
            if (!new_items) return;
            
            if (parent->items && parent->length > 0) {
                memcpy(new_items, parent->items, sizeof(Item) * parent->length);
            }
            
            parent->items = new_items;
            parent->capacity = new_capacity;
        }
        
        parent->items[parent->length] = child;
        parent->length++;
        
        // Update content length in type
        TypeElmt* elem_type = (TypeElmt*)parent->type;
        if (elem_type) {
            elem_type->content_length = parent->length;
        }
    }

    // Helper: Create text item
    Item create_text_item(const char* text) {
        String* str = create_lambda_string(text);
        if (!str) return ItemNull;
        
        Item item;
        item.type_id = LMD_TYPE_STRING;
        item.pointer = (uint64_t)str;
        return item;
    }

    // Helper: Create element item
    Item create_element_item(Element* element) {
        if (!element) return ItemNull;
        
        Item item;
        item.type_id = LMD_TYPE_ELEMENT;
        item.element = element;
        return item;
    }

    // Helper: Add attribute to element
    void add_attribute_to_element(Element* element, const char* attr_name, const char* attr_value) {
        if (!element || !attr_name || !attr_value) return;
        
        TypeElmt* elem_type = (TypeElmt*)element->type;
        if (!elem_type) return;
        
        // Create new shape entry
        ShapeEntry* entry = (ShapeEntry*)pool_alloc(pool, sizeof(ShapeEntry));
        if (!entry) return;
        
        // Set attribute name
        entry->name = (StrView*)pool_alloc(pool, sizeof(StrView));
        if (!entry->name) return;
        
        size_t name_len = strlen(attr_name);
        char* name_copy = (char*)pool_alloc(pool, name_len + 1);
        if (!name_copy) return;
        
        memcpy(name_copy, attr_name, name_len + 1);
        *entry->name = strview_init(name_copy, name_len);
        
        // Create string type for the attribute
        Type* string_type = (Type*)pool_alloc(pool, sizeof(Type));
        if (!string_type) return;
        string_type->type_id = LMD_TYPE_STRING;
        entry->type = string_type;
        
        // Set byte offset (simple linear layout)
        entry->byte_offset = elem_type->byte_size;
        elem_type->byte_size += sizeof(String*);
        
        // Link into shape list
        entry->next = nullptr;
        if (elem_type->last) {
            elem_type->last->next = entry;
        } else {
            elem_type->shape = entry;
        }
        elem_type->last = entry;
        elem_type->length++;
        
        // Allocate/extend attribute data
        if (!element->data) {
            element->data = pool_alloc(pool, elem_type->byte_size);
            element->data_cap = elem_type->byte_size;
        } else if (element->data_cap < elem_type->byte_size) {
            void* new_data = pool_alloc(pool, elem_type->byte_size);
            if (new_data && element->data) {
                memcpy(new_data, element->data, element->data_cap);
            }
            element->data = new_data;
            element->data_cap = elem_type->byte_size;
        }
        
        // Store attribute value
        if (element->data) {
            String* value_str = create_lambda_string(attr_value);
            String** attr_slot = (String**)((char*)element->data + entry->byte_offset);
            *attr_slot = value_str;
        }
    }

    // Helper: Parse simple HTML for testing
    Input* parse_simple_html(const char* html_content) {
        String* type_str = create_lambda_string("html");
        Url* url = url_parse("file://test.html");
        
        char* content_copy = strdup(html_content);
        Input* result = input_from_source(content_copy, url, type_str, nullptr);
        free(content_copy);
        
        return result;
    }
};

// ========================================================================
// Basic ElementReader Creation and Properties
// ========================================================================

TEST_F(ElementReaderTest, CreateFromValidElement) {
    Element* element = create_mock_element("div");
    ASSERT_NE(element, nullptr);
    
    ElementReader* reader = element_reader_create(element, pool);
    ASSERT_NE(reader, nullptr);
    
    EXPECT_EQ(reader->element, element);
    EXPECT_NE(reader->element_type, nullptr);
    EXPECT_STREQ(reader->tag_name, "div");
    EXPECT_EQ(reader->tag_name_len, 3);
    EXPECT_EQ(reader->child_count, 0);
    EXPECT_EQ(reader->attr_count, 0);
}

TEST_F(ElementReaderTest, CreateFromNullElement) {
    ElementReader* reader = element_reader_create(nullptr, pool);
    EXPECT_EQ(reader, nullptr);
}

TEST_F(ElementReaderTest, CreateFromNullPool) {
    Element* element = create_mock_element("div");
    ElementReader* reader = element_reader_create(element, nullptr);
    EXPECT_EQ(reader, nullptr);
}

TEST_F(ElementReaderTest, CreateFromItem) {
    Element* element = create_mock_element("span");
    Item element_item = create_element_item(element);
    
    ElementReader* reader = element_reader_from_item(element_item, pool);
    ASSERT_NE(reader, nullptr);
    
    EXPECT_STREQ(element_reader_tag_name(reader), "span");
}

TEST_F(ElementReaderTest, CreateFromNonElementItem) {
    Item text_item = create_text_item("Hello");
    
    ElementReader* reader = element_reader_from_item(text_item, pool);
    EXPECT_EQ(reader, nullptr);
}

// ========================================================================
// Element Property Access Tests
// ========================================================================

TEST_F(ElementReaderTest, TagNameAccess) {
    Element* element = create_mock_element("paragraph");
    ElementReader* reader = element_reader_create(element, pool);
    ASSERT_NE(reader, nullptr);
    
    const char* tag = element_reader_tag_name(reader);
    EXPECT_STREQ(tag, "paragraph");
    
    int64_t tag_len = element_reader_tag_name_len(reader);
    EXPECT_EQ(tag_len, 9);
    
    EXPECT_TRUE(element_reader_has_tag(reader, "paragraph"));
    EXPECT_FALSE(element_reader_has_tag(reader, "div"));
    
    EXPECT_TRUE(element_reader_has_tag_n(reader, "paragraph", 9));
    EXPECT_FALSE(element_reader_has_tag_n(reader, "paragraph", 8));
    EXPECT_FALSE(element_reader_has_tag_n(reader, "para", 4));
}

TEST_F(ElementReaderTest, TagNameAccessWithNullReader) {
    EXPECT_EQ(element_reader_tag_name(nullptr), nullptr);
    EXPECT_EQ(element_reader_tag_name_len(nullptr), 0);
    EXPECT_FALSE(element_reader_has_tag(nullptr, "div"));
    EXPECT_FALSE(element_reader_has_tag_n(nullptr, "div", 3));
}

TEST_F(ElementReaderTest, ChildAndAttrCounts) {
    Element* parent = create_mock_element("div");
    Element* child1 = create_mock_element("p");
    Element* child2 = create_mock_element("span");
    
    add_child_to_element(parent, create_element_item(child1));
    add_child_to_element(parent, create_text_item("Hello"));
    add_child_to_element(parent, create_element_item(child2));
    
    add_attribute_to_element(parent, "id", "main");
    add_attribute_to_element(parent, "class", "container");
    
    ElementReader* reader = element_reader_create(parent, pool);
    ASSERT_NE(reader, nullptr);
    
    EXPECT_EQ(element_reader_child_count(reader), 3);
    EXPECT_EQ(element_reader_attr_count(reader), 2);
}

TEST_F(ElementReaderTest, IsEmptyCheck) {
    // Empty element
    Element* empty = create_mock_element("div");
    ElementReader* empty_reader = element_reader_create(empty, pool);
    EXPECT_TRUE(element_reader_is_empty(empty_reader));
    
    // Element with only empty text
    Element* empty_text = create_mock_element("div");
    add_child_to_element(empty_text, create_text_item(""));
    ElementReader* empty_text_reader = element_reader_create(empty_text, pool);
    EXPECT_TRUE(element_reader_is_empty(empty_text_reader));
    
    // Element with non-empty text
    Element* with_text = create_mock_element("div");
    add_child_to_element(with_text, create_text_item("Hello"));
    ElementReader* with_text_reader = element_reader_create(with_text, pool);
    EXPECT_FALSE(element_reader_is_empty(with_text_reader));
    
    // Element with child elements
    Element* with_child = create_mock_element("div");
    add_child_to_element(with_child, create_element_item(create_mock_element("p")));
    ElementReader* with_child_reader = element_reader_create(with_child, pool);
    EXPECT_FALSE(element_reader_is_empty(with_child_reader));
}

TEST_F(ElementReaderTest, IsTextOnlyCheck) {
    // Empty element
    Element* empty = create_mock_element("div");
    ElementReader* empty_reader = element_reader_create(empty, pool);
    EXPECT_FALSE(element_reader_is_text_only(empty_reader));
    
    // Text only
    Element* text_only = create_mock_element("div");
    add_child_to_element(text_only, create_text_item("Hello"));
    add_child_to_element(text_only, create_text_item(" World"));
    ElementReader* text_only_reader = element_reader_create(text_only, pool);
    EXPECT_TRUE(element_reader_is_text_only(text_only_reader));
    
    // Mixed content
    Element* mixed = create_mock_element("div");
    add_child_to_element(mixed, create_text_item("Hello"));
    add_child_to_element(mixed, create_element_item(create_mock_element("span")));
    ElementReader* mixed_reader = element_reader_create(mixed, pool);
    EXPECT_FALSE(element_reader_is_text_only(mixed_reader));
}

// ========================================================================
// Child Access Tests
// ========================================================================

TEST_F(ElementReaderTest, ChildAccess) {
    Element* parent = create_mock_element("div");
    Element* child1 = create_mock_element("p");
    Element* child2 = create_mock_element("span");
    
    add_child_to_element(parent, create_element_item(child1));
    add_child_to_element(parent, create_text_item("Hello"));
    add_child_to_element(parent, create_element_item(child2));
    
    ElementReader* reader = element_reader_create(parent, pool);
    ASSERT_NE(reader, nullptr);
    
    // Test valid indices
    Item first_child = element_reader_child_at(reader, 0);
    EXPECT_EQ(get_type_id(first_child), LMD_TYPE_ELEMENT);
    
    Item second_child = element_reader_child_at(reader, 1);
    EXPECT_EQ(get_type_id(second_child), LMD_TYPE_STRING);
    
    Item third_child = element_reader_child_at(reader, 2);
    EXPECT_EQ(get_type_id(third_child), LMD_TYPE_ELEMENT);
    
    // Test invalid indices
    Item invalid1 = element_reader_child_at(reader, -1);
    EXPECT_EQ(invalid1.item, ItemNull.item);
    
    Item invalid2 = element_reader_child_at(reader, 10);
    EXPECT_EQ(invalid2.item, ItemNull.item);
}

TEST_F(ElementReaderTest, ChildTypedAccess) {
    Element* parent = create_mock_element("div");
    add_child_to_element(parent, create_text_item("Hello"));
    
    ElementReader* reader = element_reader_create(parent, pool);
    ASSERT_NE(reader, nullptr);
    
    TypedItem typed_child = element_reader_child_typed_at(reader, 0);
    EXPECT_EQ(typed_child.type_id, LMD_TYPE_STRING);
    EXPECT_NE(typed_child.string, nullptr);
}

TEST_F(ElementReaderTest, FindChild) {
    Element* parent = create_mock_element("div");
    Element* p_child = create_mock_element("p");
    Element* span_child = create_mock_element("span");
    Element* another_p = create_mock_element("p");
    
    add_child_to_element(parent, create_element_item(p_child));
    add_child_to_element(parent, create_element_item(span_child));
    add_child_to_element(parent, create_element_item(another_p));
    
    ElementReader* reader = element_reader_create(parent, pool);
    ASSERT_NE(reader, nullptr);
    
    // Find first matching child
    Item found_p = element_reader_find_child(reader, "p");
    EXPECT_EQ(get_type_id(found_p), LMD_TYPE_ELEMENT);
    EXPECT_EQ(found_p.element, p_child);  // Should be first one
    
    Item found_span = element_reader_find_child(reader, "span");
    EXPECT_EQ(get_type_id(found_span), LMD_TYPE_ELEMENT);
    EXPECT_EQ(found_span.element, span_child);
    
    // Non-existent tag
    Item not_found = element_reader_find_child(reader, "div");
    EXPECT_EQ(not_found.item, ItemNull.item);
}

TEST_F(ElementReaderTest, FindChildren) {
    Element* parent = create_mock_element("ul");
    Element* li1 = create_mock_element("li");
    Element* li2 = create_mock_element("li");
    Element* li3 = create_mock_element("li");
    Element* span_child = create_mock_element("span");
    
    add_child_to_element(parent, create_element_item(li1));
    add_child_to_element(parent, create_element_item(span_child));
    add_child_to_element(parent, create_element_item(li2));
    add_child_to_element(parent, create_element_item(li3));
    
    ElementReader* reader = element_reader_create(parent, pool);
    ASSERT_NE(reader, nullptr);
    
    ArrayList* li_children = element_reader_find_children(reader, "li", pool);
    ASSERT_NE(li_children, nullptr);
    EXPECT_EQ(li_children->length, 3);
    
    ArrayList* span_children = element_reader_find_children(reader, "span", pool);
    ASSERT_NE(span_children, nullptr);
    EXPECT_EQ(span_children->length, 1);
    
    ArrayList* div_children = element_reader_find_children(reader, "div", pool);
    ASSERT_NE(div_children, nullptr);
    EXPECT_EQ(div_children->length, 0);
}

// ========================================================================
// Text Content Extraction Tests
// ========================================================================

TEST_F(ElementReaderTest, ImmediateTextContent) {
    Element* parent = create_mock_element("div");
    add_child_to_element(parent, create_text_item("Hello "));
    add_child_to_element(parent, create_text_item("World"));
    
    // Add nested element (should be ignored for immediate text)
    Element* nested = create_mock_element("span");
    add_child_to_element(nested, create_text_item("Nested"));
    add_child_to_element(parent, create_element_item(nested));
    
    ElementReader* reader = element_reader_create(parent, pool);
    ASSERT_NE(reader, nullptr);
    
    String* immediate = element_reader_immediate_text(reader, pool);
    ASSERT_NE(immediate, nullptr);
    EXPECT_EQ(immediate->len, 11);  // "Hello World"
    EXPECT_STREQ(immediate->chars, "Hello World");
}

TEST_F(ElementReaderTest, RecursiveTextContent) {
    Element* parent = create_mock_element("div");
    add_child_to_element(parent, create_text_item("Hello "));
    
    Element* span = create_mock_element("span");
    add_child_to_element(span, create_text_item("Beautiful "));
    add_child_to_element(parent, create_element_item(span));
    
    add_child_to_element(parent, create_text_item("World"));
    
    ElementReader* reader = element_reader_create(parent, pool);
    ASSERT_NE(reader, nullptr);
    
    String* recursive = element_reader_text_content(reader, pool);
    ASSERT_NE(recursive, nullptr);
    EXPECT_EQ(recursive->len, 21);  // "Hello Beautiful World"
    EXPECT_STREQ(recursive->chars, "Hello Beautiful World");
}

TEST_F(ElementReaderTest, EmptyTextContent) {
    Element* empty = create_mock_element("div");
    ElementReader* reader = element_reader_create(empty, pool);
    ASSERT_NE(reader, nullptr);
    
    String* immediate = element_reader_immediate_text(reader, pool);
    ASSERT_NE(immediate, nullptr);
    EXPECT_EQ(immediate->len, 0);
    
    String* recursive = element_reader_text_content(reader, pool);
    ASSERT_NE(recursive, nullptr);
    EXPECT_EQ(recursive->len, 0);
}

// ========================================================================
// Attribute Access Tests
// ========================================================================

TEST_F(ElementReaderTest, AttributeReaderCreation) {
    Element* element = create_mock_element("div");
    add_attribute_to_element(element, "id", "main");
    add_attribute_to_element(element, "class", "container");
    
    ElementReader* reader = element_reader_create(element, pool);
    ASSERT_NE(reader, nullptr);
    
    AttributeReader* attr_reader = element_reader_attributes(reader, pool);
    ASSERT_NE(attr_reader, nullptr);
    
    EXPECT_EQ(attr_reader->element_reader, reader);
    EXPECT_NE(attr_reader->map_type, nullptr);
    EXPECT_NE(attr_reader->attr_data, nullptr);
    EXPECT_NE(attr_reader->shape, nullptr);
}

TEST_F(ElementReaderTest, AttributeExistence) {
    Element* element = create_mock_element("div");
    add_attribute_to_element(element, "id", "main");
    add_attribute_to_element(element, "class", "container");
    
    ElementReader* reader = element_reader_create(element, pool);
    AttributeReader* attr_reader = element_reader_attributes(reader, pool);
    ASSERT_NE(attr_reader, nullptr);
    
    EXPECT_TRUE(attribute_reader_has(attr_reader, "id"));
    EXPECT_TRUE(attribute_reader_has(attr_reader, "class"));
    EXPECT_FALSE(attribute_reader_has(attr_reader, "style"));
    EXPECT_FALSE(attribute_reader_has(attr_reader, "nonexistent"));
}

TEST_F(ElementReaderTest, AttributeStringAccess) {
    Element* element = create_mock_element("div");
    add_attribute_to_element(element, "id", "main-content");
    add_attribute_to_element(element, "class", "highlight active");
    
    ElementReader* reader = element_reader_create(element, pool);
    AttributeReader* attr_reader = element_reader_attributes(reader, pool);
    ASSERT_NE(attr_reader, nullptr);
    
    const String* id_str = attribute_reader_get_string(attr_reader, "id");
    ASSERT_NE(id_str, nullptr);
    EXPECT_STREQ(id_str->chars, "main-content");
    
    const char* id_cstr = attribute_reader_get_cstring(attr_reader, "id");
    EXPECT_STREQ(id_cstr, "main-content");
    
    const String* class_str = attribute_reader_get_string(attr_reader, "class");
    ASSERT_NE(class_str, nullptr);
    EXPECT_STREQ(class_str->chars, "highlight active");
    
    // Non-existent attribute
    const String* nonexistent = attribute_reader_get_string(attr_reader, "style");
    EXPECT_EQ(nonexistent, nullptr);
    
    const char* nonexistent_cstr = attribute_reader_get_cstring(attr_reader, "style");
    EXPECT_EQ(nonexistent_cstr, nullptr);
}

TEST_F(ElementReaderTest, AttributeTypedAccess) {
    Element* element = create_mock_element("div");
    add_attribute_to_element(element, "title", "Main Content");
    
    ElementReader* reader = element_reader_create(element, pool);
    AttributeReader* attr_reader = element_reader_attributes(reader, pool);
    ASSERT_NE(attr_reader, nullptr);
    
    TypedItem typed = attribute_reader_get_typed(attr_reader, "title");
    EXPECT_EQ(typed.type_id, LMD_TYPE_STRING);
    EXPECT_NE(typed.string, nullptr);
    EXPECT_STREQ(typed.string->chars, "Main Content");
    
    // Non-existent attribute
    TypedItem null_typed = attribute_reader_get_typed(attr_reader, "nonexistent");
    EXPECT_EQ(null_typed.type_id, 0);
}

TEST_F(ElementReaderTest, AttributeNames) {
    Element* element = create_mock_element("div");
    add_attribute_to_element(element, "id", "main");
    add_attribute_to_element(element, "class", "container");
    add_attribute_to_element(element, "style", "color: red");
    
    ElementReader* reader = element_reader_create(element, pool);
    AttributeReader* attr_reader = element_reader_attributes(reader, pool);
    ASSERT_NE(attr_reader, nullptr);
    
    ArrayList* names = attribute_reader_names(attr_reader, pool);
    ASSERT_NE(names, nullptr);
    EXPECT_EQ(names->length, 3);
    
    // Check that all expected names are present
    bool found_id = false, found_class = false, found_style = false;
    for (int i = 0; i < names->length; i++) {
        StrView* name = (StrView*)names->data[i];
        if (name) {
            if (strncmp(name->str, "id", name->length) == 0) found_id = true;
            else if (strncmp(name->str, "class", name->length) == 0) found_class = true;
            else if (strncmp(name->str, "style", name->length) == 0) found_style = true;
        }
    }
    
    EXPECT_TRUE(found_id);
    EXPECT_TRUE(found_class);
    EXPECT_TRUE(found_style);
}

// ========================================================================
// Element Iterator Tests
// ========================================================================

TEST_F(ElementReaderTest, ChildrenOnlyIterator) {
    Element* parent = create_mock_element("div");
    Element* child1 = create_mock_element("p");
    Element* child2 = create_mock_element("span");
    
    add_child_to_element(parent, create_element_item(child1));
    add_child_to_element(parent, create_text_item("Text"));
    add_child_to_element(parent, create_element_item(child2));
    
    ElementReader* reader = element_reader_create(parent, pool);
    ASSERT_NE(reader, nullptr);
    
    ElementIterator* iter = element_iterator_create(reader, ITER_CHILDREN_ONLY, pool);
    ASSERT_NE(iter, nullptr);
    
    // Should iterate through 3 children
    EXPECT_TRUE(element_iterator_has_next(iter));
    Item item1 = element_iterator_next(iter);
    EXPECT_EQ(get_type_id(item1), LMD_TYPE_ELEMENT);
    
    EXPECT_TRUE(element_iterator_has_next(iter));
    Item item2 = element_iterator_next(iter);
    EXPECT_EQ(get_type_id(item2), LMD_TYPE_STRING);
    
    EXPECT_TRUE(element_iterator_has_next(iter));
    Item item3 = element_iterator_next(iter);
    EXPECT_EQ(get_type_id(item3), LMD_TYPE_ELEMENT);
    
    EXPECT_FALSE(element_iterator_has_next(iter));
    Item item4 = element_iterator_next(iter);
    EXPECT_EQ(item4.item, ItemNull.item);
}

TEST_F(ElementReaderTest, ElementsOnlyIterator) {
    Element* parent = create_mock_element("div");
    Element* child1 = create_mock_element("p");
    Element* child2 = create_mock_element("span");
    
    add_child_to_element(parent, create_element_item(child1));
    add_child_to_element(parent, create_text_item("Text"));
    add_child_to_element(parent, create_element_item(child2));
    
    ElementReader* reader = element_reader_create(parent, pool);
    ASSERT_NE(reader, nullptr);
    
    ElementIterator* iter = element_iterator_create(reader, ITER_ELEMENTS_ONLY, pool);
    ASSERT_NE(iter, nullptr);
    
    // Should only iterate through element children
    int element_count = 0;
    while (element_iterator_has_next(iter)) {
        ElementReader* elem = element_iterator_next_element(iter);
        if (elem) {
            element_count++;
        }
    }
    
    EXPECT_EQ(element_count, 2);  // Only the two element children
}

TEST_F(ElementReaderTest, TextOnlyIterator) {
    Element* parent = create_mock_element("div");
    Element* child = create_mock_element("p");
    
    add_child_to_element(parent, create_text_item("Hello"));
    add_child_to_element(parent, create_element_item(child));
    add_child_to_element(parent, create_text_item("World"));
    
    ElementReader* reader = element_reader_create(parent, pool);
    ASSERT_NE(reader, nullptr);
    
    ElementIterator* iter = element_iterator_create(reader, ITER_TEXT_ONLY, pool);
    ASSERT_NE(iter, nullptr);
    
    // Should only iterate through text children
    int text_count = 0;
    while (element_iterator_has_next(iter)) {
        Item item = element_iterator_next(iter);
        if (get_type_id(item) == LMD_TYPE_STRING) {
            text_count++;
        }
    }
    
    EXPECT_EQ(text_count, 2);  // Only the two text children
}

TEST_F(ElementReaderTest, IteratorReset) {
    Element* parent = create_mock_element("div");
    add_child_to_element(parent, create_text_item("Child1"));
    add_child_to_element(parent, create_text_item("Child2"));
    
    ElementReader* reader = element_reader_create(parent, pool);
    ASSERT_NE(reader, nullptr);
    
    ElementIterator* iter = element_iterator_create(reader, ITER_CHILDREN_ONLY, pool);
    ASSERT_NE(iter, nullptr);
    
    // First iteration
    EXPECT_TRUE(element_iterator_has_next(iter));
    element_iterator_next(iter);
    EXPECT_TRUE(element_iterator_has_next(iter));
    element_iterator_next(iter);
    EXPECT_FALSE(element_iterator_has_next(iter));
    
    // Reset and iterate again
    element_iterator_reset(iter);
    EXPECT_TRUE(element_iterator_has_next(iter));
    element_iterator_next(iter);
    EXPECT_TRUE(element_iterator_has_next(iter));
    element_iterator_next(iter);
    EXPECT_FALSE(element_iterator_has_next(iter));
}

// ========================================================================
// Search Operations Tests
// ========================================================================

TEST_F(ElementReaderTest, FindByIdBasic) {
    Element* root = create_mock_element("div");
    Element* target = create_mock_element("p");
    Element* other = create_mock_element("span");
    
    add_attribute_to_element(target, "id", "target-element");
    add_attribute_to_element(other, "class", "normal");
    
    add_child_to_element(root, create_element_item(target));
    add_child_to_element(root, create_element_item(other));
    
    ElementReader* reader = element_reader_create(root, pool);
    ASSERT_NE(reader, nullptr);
    
    ElementReader* found = element_reader_find_by_id(reader, "target-element", pool);
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->element, target);
    
    ElementReader* not_found = element_reader_find_by_id(reader, "nonexistent", pool);
    EXPECT_EQ(not_found, nullptr);
}

TEST_F(ElementReaderTest, FindByClass) {
    Element* root = create_mock_element("div");
    Element* elem1 = create_mock_element("p");
    Element* elem2 = create_mock_element("span");
    Element* elem3 = create_mock_element("div");
    
    add_attribute_to_element(elem1, "class", "highlight");
    add_attribute_to_element(elem2, "class", "highlight active");
    add_attribute_to_element(elem3, "class", "normal");
    
    add_child_to_element(root, create_element_item(elem1));
    add_child_to_element(root, create_element_item(elem2));
    add_child_to_element(root, create_element_item(elem3));
    
    ElementReader* reader = element_reader_create(root, pool);
    ASSERT_NE(reader, nullptr);
    
    ArrayList* found = element_reader_find_by_class(reader, "highlight", pool);
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->length, 2);  // elem1 and elem2
    
    ArrayList* not_found = element_reader_find_by_class(reader, "nonexistent", pool);
    ASSERT_NE(not_found, nullptr);
    EXPECT_EQ(not_found->length, 0);
}

TEST_F(ElementReaderTest, FindByAttribute) {
    Element* root = create_mock_element("div");
    Element* link1 = create_mock_element("a");
    Element* link2 = create_mock_element("a");
    Element* other = create_mock_element("p");
    
    add_attribute_to_element(link1, "href", "http://example.com");
    add_attribute_to_element(link2, "href", "http://test.com");
    add_attribute_to_element(other, "title", "No link");
    
    add_child_to_element(root, create_element_item(link1));
    add_child_to_element(root, create_element_item(link2));
    add_child_to_element(root, create_element_item(other));
    
    ElementReader* reader = element_reader_create(root, pool);
    ASSERT_NE(reader, nullptr);
    
    // Find by attribute existence
    ArrayList* with_href = element_reader_find_by_attribute(reader, "href", nullptr, pool);
    ASSERT_NE(with_href, nullptr);
    EXPECT_EQ(with_href->length, 2);
    
    // Find by specific attribute value
    ArrayList* specific = element_reader_find_by_attribute(reader, "href", "http://example.com", pool);
    ASSERT_NE(specific, nullptr);
    EXPECT_EQ(specific->length, 1);
    
    // Non-existent attribute
    ArrayList* none = element_reader_find_by_attribute(reader, "data-test", nullptr, pool);
    ASSERT_NE(none, nullptr);
    EXPECT_EQ(none->length, 0);
}

// ========================================================================
// Utility Functions Tests
// ========================================================================

TEST_F(ElementReaderTest, CountElements) {
    Element* root = create_mock_element("div");
    Element* child1 = create_mock_element("p");
    Element* child2 = create_mock_element("span");
    Element* grandchild = create_mock_element("strong");
    
    add_child_to_element(child1, create_element_item(grandchild));
    add_child_to_element(root, create_element_item(child1));
    add_child_to_element(root, create_element_item(child2));
    
    ElementReader* reader = element_reader_create(root, pool);
    ASSERT_NE(reader, nullptr);
    
    int64_t count = element_reader_count_elements(reader);
    EXPECT_EQ(count, 4);  // root + child1 + child2 + grandchild
}

TEST_F(ElementReaderTest, TreeDepth) {
    Element* root = create_mock_element("div");
    Element* level1 = create_mock_element("p");
    Element* level2 = create_mock_element("span");
    Element* level3 = create_mock_element("strong");
    
    add_child_to_element(level2, create_element_item(level3));
    add_child_to_element(level1, create_element_item(level2));
    add_child_to_element(root, create_element_item(level1));
    
    ElementReader* reader = element_reader_create(root, pool);
    ASSERT_NE(reader, nullptr);
    
    int64_t depth = element_reader_tree_depth(reader);
    EXPECT_EQ(depth, 4);  // root -> level1 -> level2 -> level3
}

TEST_F(ElementReaderTest, DebugString) {
    Element* root = create_mock_element("div");
    Element* child = create_mock_element("p");
    
    add_child_to_element(child, create_text_item("Hello"));
    add_child_to_element(root, create_element_item(child));
    
    ElementReader* reader = element_reader_create(root, pool);
    ASSERT_NE(reader, nullptr);
    
    String* debug = element_reader_debug_string(reader, pool);
    ASSERT_NE(debug, nullptr);
    EXPECT_GT(debug->len, 0);
    
    // Should contain element tags
    EXPECT_NE(strstr(debug->chars, "<div>"), nullptr);
    EXPECT_NE(strstr(debug->chars, "<p>"), nullptr);
    EXPECT_NE(strstr(debug->chars, "Hello"), nullptr);
}

// ========================================================================
// Error Handling and Edge Cases
// ========================================================================

TEST_F(ElementReaderTest, NullInputHandling) {
    // Most functions should handle NULL gracefully
    EXPECT_EQ(element_reader_tag_name(nullptr), nullptr);
    EXPECT_EQ(element_reader_child_count(nullptr), 0);
    EXPECT_EQ(element_reader_attr_count(nullptr), 0);
    EXPECT_TRUE(element_reader_is_empty(nullptr));
    EXPECT_FALSE(element_reader_is_text_only(nullptr));
    
    EXPECT_EQ(element_reader_child_at(nullptr, 0).item, ItemNull.item);
    EXPECT_EQ(element_reader_find_child(nullptr, "div").item, ItemNull.item);
    EXPECT_EQ(element_reader_find_children(nullptr, "div", pool), nullptr);
    
    EXPECT_EQ(element_reader_text_content(nullptr, pool), nullptr);
    EXPECT_EQ(element_reader_immediate_text(nullptr, pool), nullptr);
    
    EXPECT_EQ(element_reader_attributes(nullptr, pool), nullptr);
    EXPECT_EQ(element_iterator_create(nullptr, ITER_CHILDREN_ONLY, pool), nullptr);
}

TEST_F(ElementReaderTest, EmptyElementHandling) {
    Element* empty = create_mock_element("div");
    ElementReader* reader = element_reader_create(empty, pool);
    ASSERT_NE(reader, nullptr);
    
    EXPECT_EQ(element_reader_child_count(reader), 0);
    EXPECT_EQ(element_reader_child_at(reader, 0).item, ItemNull.item);
    EXPECT_EQ(element_reader_find_child(reader, "p").item, ItemNull.item);
    
    ArrayList* children = element_reader_find_children(reader, "p", pool);
    ASSERT_NE(children, nullptr);
    EXPECT_EQ(children->length, 0);
    
    String* text = element_reader_text_content(reader, pool);
    ASSERT_NE(text, nullptr);
    EXPECT_EQ(text->len, 0);
}

TEST_F(ElementReaderTest, AttributeReaderNullHandling) {
    EXPECT_FALSE(attribute_reader_has(nullptr, "id"));
    EXPECT_EQ(attribute_reader_get_string(nullptr, "id"), nullptr);
    EXPECT_EQ(attribute_reader_get_cstring(nullptr, "id"), nullptr);
    
    TypedItem typed = attribute_reader_get_typed(nullptr, "id");
    EXPECT_EQ(typed.type_id, 0);
    
    EXPECT_EQ(attribute_reader_names(nullptr, pool), nullptr);
}

// ========================================================================
// Performance and Memory Tests
// ========================================================================

TEST_F(ElementReaderTest, LargeTreePerformance) {
    // Create a moderately large tree for performance testing
    Element* root = create_mock_element("root");
    
    // Add multiple levels of children
    for (int i = 0; i < 10; i++) {
        Element* level1 = create_mock_element("level1");
        for (int j = 0; j < 5; j++) {
            Element* level2 = create_mock_element("level2");
            add_child_to_element(level2, create_text_item("Text content"));
            add_child_to_element(level1, create_element_item(level2));
        }
        add_child_to_element(root, create_element_item(level1));
    }
    
    ElementReader* reader = element_reader_create(root, pool);
    ASSERT_NE(reader, nullptr);
    
    // Test that operations complete successfully on large tree
    int64_t count = element_reader_count_elements(reader);
    EXPECT_EQ(count, 61);  // 1 root + 10 level1 + 50 level2
    
    int64_t depth = element_reader_tree_depth(reader);
    EXPECT_EQ(depth, 3);
    
    String* text = element_reader_text_content(reader, pool);
    ASSERT_NE(text, nullptr);
    EXPECT_GT(text->len, 0);
}

TEST_F(ElementReaderTest, MemoryUsagePatterns) {
    // Test that reader creation doesn't leak memory
    Element* element = create_mock_element("div");
    add_attribute_to_element(element, "id", "test");
    add_child_to_element(element, create_text_item("Content"));
    
    // Create and use multiple readers
    for (int i = 0; i < 100; i++) {
        ElementReader* reader = element_reader_create(element, pool);
        ASSERT_NE(reader, nullptr);
        
        // Use various functions
        element_reader_tag_name(reader);
        element_reader_child_count(reader);
        element_reader_text_content(reader, pool);
        
        AttributeReader* attrs = element_reader_attributes(reader, pool);
        if (attrs) {
            attribute_reader_has(attrs, "id");
        }
        
        // Pool-based allocation means no explicit cleanup needed
    }
    
    // If we reach here without crashes, memory management is working
    SUCCEED();
}

// ========================================================================
// Integration Tests
// ========================================================================

TEST_F(ElementReaderTest, RealWorldHTML) {
    // Skip if input parsing is not available
    const char* html = "<div id='main' class='container'>"
                      "<h1>Title</h1>"
                      "<p>Paragraph with <span>nested</span> content.</p>"
                      "</div>";
    
    Input* parsed = parse_simple_html(html);
    if (!parsed) {
        GTEST_SKIP() << "HTML parsing not available in test environment";
    }
    
    ElementReader* root_reader = element_reader_from_input_root(parsed, pool);
    if (!root_reader) {
        GTEST_SKIP() << "Could not create reader from parsed HTML";
    }
    
    // Test that we can navigate the parsed structure
    EXPECT_STREQ(element_reader_tag_name(root_reader), "div");
    EXPECT_GT(element_reader_child_count(root_reader), 0);
    
    AttributeReader* attrs = element_reader_attributes(root_reader, pool);
    if (attrs) {
        EXPECT_TRUE(attribute_reader_has(attrs, "id"));
        EXPECT_TRUE(attribute_reader_has(attrs, "class"));
        
        const char* id = attribute_reader_get_cstring(attrs, "id");
        if (id) {
            EXPECT_STREQ(id, "main");
        }
    }
    
    String* text = element_reader_text_content(root_reader, pool);
    if (text) {
        EXPECT_GT(text->len, 0);
    }
}

// ========================================================================
// C++ API Iteration Tests
// ========================================================================

TEST_F(ElementReaderTest, CppApiChildIteration) {
    // Build element using C++ API
    Element* parent = create_mock_element("ul");
    Element* li1 = create_mock_element("li");
    Element* li2 = create_mock_element("li");
    
    add_child_to_element(parent, create_element_item(li1));
    add_child_to_element(parent, create_text_item("Text"));
    add_child_to_element(parent, create_element_item(li2));
    
    // Use C++ ElementReaderWrapper API
    Item parent_item = create_element_item(parent);
    ItemReader item_reader(parent_item, pool);
    ASSERT_TRUE(item_reader.isElement());
    
    ElementReaderWrapper elem_reader = item_reader.asElement();
    ASSERT_TRUE(elem_reader.isValid());
    EXPECT_STREQ(elem_reader.tagName(), "ul");
    
    // Test iterating all children (any type)
    int child_count = 0;
    auto child_iter = elem_reader.children();
    ItemReader child;
    while (child_iter.next(&child)) {
        child_count++;
        EXPECT_FALSE(child.isNull());
    }
    EXPECT_EQ(child_count, 3);  // 2 elements + 1 text
    
    // Test iterating only element children
    int element_count = 0;
    auto elem_iter = elem_reader.children();
    ItemReader child_item;
    while (elem_iter.next(&child_item)) {
        if (child_item.isElement()) {
            element_count++;
            ElementReaderWrapper child_elem = child_item.asElement();
            EXPECT_TRUE(child_elem.isValid());
            EXPECT_STREQ(child_elem.tagName(), "li");
        }
    }
    EXPECT_EQ(element_count, 2);  // Only the li elements
}