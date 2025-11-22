# DOM Element AttributeStorage Cleanup Plan

## Executive Summary

This document outlines the plan to remove the `AttributeStorage` hybrid data structure from `dom_element` and replace it with direct access to the underlying Lambda `Element` structure's attributes. This simplification eliminates redundant data structures and leverages the existing robust attribute management in Lambda's runtime.

## Current Architecture Analysis

### Current AttributeStorage Implementation

**Location:** `lambda/input/css/dom_element.cpp` and `dom_element.hpp`

**Structure:**
```cpp
typedef struct AttributeStorage {
    int count;             // Number of attributes
    bool use_hashmap;      // Whether using hashmap (true) or array (false)
    Pool* pool;            // Memory pool

    union {
        AttributePair* array;     // Array storage for count < 10
        struct hashmap* hashmap;  // HashMap storage for count >= 10
    } storage;
} AttributeStorage;
```

**Operations:**
- `attribute_storage_create()` - Creates new storage
- `attribute_storage_set()` - Sets/updates attribute
- `attribute_storage_get()` - Retrieves attribute value
- `attribute_storage_has()` - Checks attribute existence
- `attribute_storage_remove()` - Removes attribute
- `attribute_storage_get_names()` - Gets all attribute names

**Used in DomElement:**
```cpp
struct DomElement : public DomNode {
    // ... other fields ...
    AttributeStorage* attributes;  // Hybrid attribute storage
    Element* native_element;       // Pointer to native Lambda Element
};
```

**Issues:**
1. **Data Duplication:** Attributes are stored twice - once in `AttributeStorage` and once in the native Lambda `Element`
2. **Memory Overhead:** Additional heap allocations for array/hashmap storage
3. **Synchronization Risk:** Two copies can become out of sync
4. **Complexity:** Hybrid array/hashmap switching adds unnecessary complexity

### Lambda Element Attribute System

**Location:** `lambda/lambda.hpp` and `lambda/lambda-data.cpp`

**Element Structure:**
```cpp
struct Element : List {
    void* type;       // TypeElmt - contains shape (attribute schema)
    void* data;       // Packed data struct of the attributes
    int data_cap;     // Capacity of the data struct
    
    // Attribute operations (already implemented)
    bool has_attr(const char* attr_name);
    ConstItem get_attr(const Item attr_name) const;
    ConstItem get_attr(const char* attr_name) const;
};
```

**TypeElmt Structure:**
```cpp
typedef struct TypeElmt : TypeMap {
    StrView name;        // Element tag name
    ShapeEntry* shape;   // Linked list of attributes
    ShapeEntry* last;    // Last shape entry
    int64_t length;      // Number of attributes
    int64_t byte_size;   // Size of data buffer
    // ... other fields ...
} TypeElmt;
```

**Shape-Based Storage:**
- Attributes stored in a `ShapeEntry` linked list (type shape)
- Actual values stored in packed `data` buffer at specific offsets
- Efficient memory layout with O(n) lookup for small n (typical case)
- Type-safe with proper reference counting

**Read Operations (MarkReader):**
```cpp
class ElementReader {
    bool has_attr(const char* key) const;
    const char* get_attr_string(const char* key) const;
    ItemReader get_attr(const char* key) const;
};
```

**Write Operations (MarkEditor):**
```cpp
class MarkEditor {
    Item elmt_update_attr(Item element, const char* attr_name, Item value);
    Item elmt_update_attr(Item element, String* attr_name, Item value);
    Item elmt_delete_attr(Item element, const char* attr_name);
    Item elmt_delete_attr(Item element, String* attr_name);
};
```

**Advantages:**
1. Single source of truth for attributes
2. Type-safe with proper schema management
3. Memory efficient with packed layout
4. Supports both inline and immutable update modes
5. Built-in reference counting for garbage collection
6. Well-tested in existing Lambda runtime

## Test Coverage Analysis

### Existing Tests

**Test Files Covering DOM Attributes:**
1. `test/css/test_css_dom_integration.cpp` - Comprehensive DOM integration tests
   - `DomElementAttributes` - Basic set/get/has/remove operations
   - `DomElementIdAttribute` - ID attribute handling
   - Covers attribute operations extensively

2. `test/test_html_css_gtest.cpp` - End-to-end HTML/CSS integration
   - Uses `dom_element_set_attribute()` for HTML parsing
   - Tests attribute retrieval after parsing
   - Covers real-world HTML attribute scenarios

3. `test/test_lambda_domnode_gtest.cpp` - DomNode polymorphism tests
   - Tests attribute operations through DomNode interface
   - Validates attribute access methods

**Test Coverage Summary:**
- ✅ Basic CRUD operations (Create, Read, Update, Delete)
- ✅ Multiple attributes per element
- ✅ Special attributes (id, class, style)
- ✅ Integration with HTML parsing
- ✅ Attribute existence checks
- ⚠️ **Gap:** No tests for attribute iteration (get all names)
- ⚠️ **Gap:** No tests for large attribute counts (10+ attributes)
- ⚠️ **Gap:** No tests for attribute removal during iteration
- ⚠️ **Gap:** No tests for special characters in attribute names/values

### Additional Tests Needed

1. **Attribute Iteration Test:**
   - Create element with 5+ attributes
   - Iterate through all attribute names
   - Verify count and names match

2. **Large Attribute Set Test:**
   - Create element with 20+ attributes (beyond hashmap threshold)
   - Verify all attributes accessible
   - Test performance remains acceptable

3. **Attribute Value Types Test:**
   - Test string attributes
   - Test numeric attributes (if supported)
   - Test boolean attributes
   - Verify type preservation

4. **Edge Cases Test:**
   - Empty string attribute values
   - Attributes with special characters
   - Very long attribute values
   - Null/invalid inputs

5. **Synchronization Test:**
   - Modify element via MarkEditor
   - Verify DOM element reflects changes
   - Test bidirectional updates

## Implementation Plan

### Phase 1: Preparation (Analysis Complete ✓)

**Tasks:**
- [x] Analyze current AttributeStorage usage
- [x] Review Lambda Element attribute API
- [x] Identify all call sites using AttributeStorage
- [x] Document test coverage gaps

### Phase 2: Refactor Attribute Operations

**Goal:** Replace AttributeStorage operations with direct Lambda Element access

#### 2.1 Update dom_element_set_attribute()

**Current Implementation:**
```cpp
bool dom_element_set_attribute(DomElement* element, const char* name, const char* value) {
    if (!element->attributes) {
        element->attributes = attribute_storage_create(element->pool);
    }
    
    bool result = attribute_storage_set(element->attributes, name, value);
    
    // Update cached fields (id, class)
    if (strcmp(name, "id") == 0) {
        element->id = attribute_storage_get(element->attributes, "id");
    }
    
    return result;
}
```

**New Implementation:**
```cpp
bool dom_element_set_attribute(DomElement* element, const char* name, const char* value) {
    if (!element || !name || !value) return false;
    
    // If native_element exists, use MarkEditor for updates
    if (element->native_element) {
        MarkEditor editor(/* get input from pool */, EDIT_MODE_INLINE);
        Item value_item = editor.builder()->createStringItem(value);
        Item result = editor.elmt_update_attr(
            {.element = element->native_element}, 
            name, 
            value_item
        );
        
        if (result.element) {
            element->native_element = result.element;
            
            // Update cached fields
            if (strcmp(name, "id") == 0) {
                element->id = value;
            }
            
            element->style_version++;
            element->needs_style_recompute = true;
            return true;
        }
        return false;
    }
    
    // Fallback: element created without native_element (rare)
    // TODO: Consider creating native_element on-demand
    return false;
}
```

**Key Changes:**
- Use `MarkEditor::elmt_update_attr()` for in-place updates
- Remove dependency on `AttributeStorage`
- Maintain cache invalidation for id/class fields
- Handle inline mode updates efficiently

#### 2.2 Update dom_element_get_attribute()

**Current Implementation:**
```cpp
const char* dom_element_get_attribute(DomElement* element, const char* name) {
    if (!element || !element->attributes) return NULL;
    return attribute_storage_get(element->attributes, name);
}
```

**New Implementation:**
```cpp
const char* dom_element_get_attribute(DomElement* element, const char* name) {
    if (!element || !name) return nullptr;
    
    // Use ElementReader for read-only access
    if (element->native_element) {
        ElementReader reader(element->native_element);
        return reader.get_attr_string(name);
    }
    
    return nullptr;
}
```

**Key Changes:**
- Use `ElementReader::get_attr_string()` for efficient read-only access
- No pool allocation needed (stack-based reader)
- Direct access to native element data

#### 2.3 Update dom_element_has_attribute()

**Current Implementation:**
```cpp
bool dom_element_has_attribute(DomElement* element, const char* name) {
    return attribute_storage_has(element->attributes, name);
}
```

**New Implementation:**
```cpp
bool dom_element_has_attribute(DomElement* element, const char* name) {
    if (!element || !name) return false;
    
    if (element->native_element) {
        ElementReader reader(element->native_element);
        return reader.has_attr(name);
    }
    
    return false;
}
```

**Key Changes:**
- Use `ElementReader::has_attr()` for existence check
- Consistent with get_attribute pattern

#### 2.4 Update dom_element_remove_attribute()

**Current Implementation:**
```cpp
bool dom_element_remove_attribute(DomElement* element, const char* name) {
    if (!element || !element->attributes) return false;
    
    bool result = attribute_storage_remove(element->attributes, name);
    
    if (result) {
        // Clear cached fields
        if (strcmp(name, "id") == 0) {
            element->id = nullptr;
        }
        element->style_version++;
        element->needs_style_recompute = true;
    }
    
    return result;
}
```

**New Implementation:**
```cpp
bool dom_element_remove_attribute(DomElement* element, const char* name) {
    if (!element || !name) return false;
    
    if (element->native_element) {
        MarkEditor editor(/* get input from pool */, EDIT_MODE_INLINE);
        Item result = editor.elmt_delete_attr(
            {.element = element->native_element}, 
            name
        );
        
        if (result.element) {
            element->native_element = result.element;
            
            // Clear cached fields
            if (strcmp(name, "id") == 0) {
                element->id = nullptr;
            }
            
            element->style_version++;
            element->needs_style_recompute = true;
            return true;
        }
    }
    
    return false;
}
```

**Key Changes:**
- Use `MarkEditor::elmt_delete_attr()` for attribute removal
- Maintain cache invalidation logic
- Handle inline mode deletes

#### 2.5 Add Helper: Get All Attribute Names

**New Function:**
```cpp
/**
 * Get all attribute names from element
 * @param element Target element
 * @param count Output parameter for number of attributes
 * @return Array of attribute names (from element's shape, no allocation needed)
 */
const char** dom_element_get_attribute_names(DomElement* element, int* count) {
    if (!element || !count) {
        if (count) *count = 0;
        return nullptr;
    }
    
    *count = 0;
    if (!element->native_element) return nullptr;
    
    ElementReader reader(element->native_element);
    int attr_count = reader.attrCount();
    if (attr_count == 0) return nullptr;
    
    // Allocate array from pool
    const char** names = (const char**)pool_alloc(
        element->pool, 
        attr_count * sizeof(const char*)
    );
    if (!names) return nullptr;
    
    // Iterate through shape to collect names
    const TypeElmt* type = (const TypeElmt*)element->native_element->type;
    const ShapeEntry* field = type->shape;
    int index = 0;
    
    while (field && index < attr_count) {
        names[index++] = field->name->str;
        field = field->next;
    }
    
    *count = index;
    return names;
}
```

**Usage:**
```cpp
// Iterate through all attributes
int count;
const char** names = dom_element_get_attribute_names(element, &count);
for (int i = 0; i < count; i++) {
    const char* name = names[i];
    const char* value = dom_element_get_attribute(element, name);
    printf("%s=%s\n", name, value);
}
```

### Phase 3: Update DomElement Initialization

#### 3.1 Modify dom_element_init()

**Current Code (lines 430-466):**
```cpp
bool dom_element_init(DomElement* element, Pool* pool, const char* tag_name, Element* native_element) {
    // ... existing initialization ...
    
    // Create style trees
    element->specified_style = style_tree_create(pool);
    element->computed_style = style_tree_create(pool);
    
    // Create attribute storage
    element->attributes = attribute_storage_create(pool);  // ← REMOVE THIS
    if (!element->attributes) {
        return false;
    }
    
    return true;
}
```

**Updated Code:**
```cpp
bool dom_element_init(DomElement* element, Pool* pool, const char* tag_name, Element* native_element) {
    if (!element || !pool || !tag_name) {
        return false;
    }
    
    // Initialize base DomNode fields
    element->node_type = DOM_NODE_ELEMENT;
    element->parent = NULL;
    element->first_child = NULL;
    element->next_sibling = NULL;
    element->prev_sibling = NULL;
    
    // Initialize DomElement fields
    element->pool = pool;
    element->native_element = native_element;
    
    // If no native element provided, create one
    if (!native_element) {
        // Create a minimal Lambda Element
        // Note: We need Input* for proper element creation
        // Consider requiring Input* parameter or storing it in pool
        log_warn("dom_element_init: creating element without native backing - limited functionality");
    }
    
    // Copy tag name
    size_t tag_len = strlen(tag_name);
    char* tag_copy = (char*)pool_alloc(pool, tag_len + 1);
    if (!tag_copy) {
        return false;
    }
    strcpy(tag_copy, tag_name);
    element->tag_name = tag_copy;
    element->tag_name_ptr = (void*)tag_copy;
    
    // Convert tag name to Lexbor tag ID
    element->tag_id = DomNode::tag_name_to_id(tag_name);
    
    // Create style trees
    element->specified_style = style_tree_create(pool);
    if (!element->specified_style) {
        return false;
    }
    
    element->computed_style = style_tree_create(pool);
    if (!element->computed_style) {
        return false;
    }
    
    // Initialize cached attribute fields from native element (if exists)
    if (native_element) {
        ElementReader reader(native_element);
        element->id = reader.get_attr_string("id");
        
        // Parse class attribute into array
        const char* class_str = reader.get_attr_string("class");
        if (class_str) {
            // TODO: Parse space-separated classes
            // For now, store as single class
            element->class_names = (const char**)pool_alloc(pool, sizeof(const char*));
            element->class_names[0] = class_str;
            element->class_count = 1;
        }
    }
    
    // NO AttributeStorage creation!
    element->pseudo_state = 0;
    element->style_version = 0;
    element->needs_style_recompute = true;
    element->document = nullptr;
    
    return true;
}
```

**Key Changes:**
- Remove `attribute_storage_create()` call
- Initialize cached fields (id, class) from native element
- Set `attributes` field to `nullptr` (field should be removed entirely)

#### 3.2 Modify dom_element_clear()

**Current Code:**
```cpp
void dom_element_clear(DomElement* element) {
    if (!element) return;
    
    if (element->attributes) {
        attribute_storage_destroy(element->attributes);  // ← REMOVE THIS
        element->attributes = NULL;
    }
    
    // ... rest of cleanup ...
}
```

**Updated Code:**
```cpp
void dom_element_clear(DomElement* element) {
    if (!element) return;
    
    // No AttributeStorage to destroy
    
    if (element->specified_style) {
        style_tree_destroy(element->specified_style);
        element->specified_style = NULL;
    }
    
    if (element->computed_style) {
        style_tree_destroy(element->computed_style);
        element->computed_style = NULL;
    }
    
    // Clear cached fields (but don't free - owned by pool/element)
    element->id = NULL;
    element->class_names = NULL;
    element->class_count = 0;
    
    // Note: native_element is not freed here - managed by Input/Arena
}
```

### Phase 4: Update Header Files

#### 4.1 Remove from dom_element.hpp

**Lines to Remove:**
```cpp
// ============================================================================
// Attribute Storage (Hybrid Array/Tree)
// ============================================================================

typedef struct AttributePair {
    const char* name;
    const char* value;
} AttributePair;

typedef struct AttributeStorage {
    int count;
    bool use_hashmap;
    Pool* pool;
    union {
        AttributePair* array;
        struct hashmap* hashmap;
    } storage;
} AttributeStorage;

#define ATTRIBUTE_HASHMAP_THRESHOLD 10

// AttributeStorage API functions (all of them)
AttributeStorage* attribute_storage_create(Pool* pool);
void attribute_storage_destroy(AttributeStorage* storage);
bool attribute_storage_set(AttributeStorage* storage, const char* name, const char* value);
const char* attribute_storage_get(AttributeStorage* storage, const char* name);
bool attribute_storage_has(AttributeStorage* storage, const char* name);
bool attribute_storage_remove(AttributeStorage* storage, const char* name);
const char** attribute_storage_get_names(AttributeStorage* storage, int* count);
```

**Remove from DomElement struct:**
```cpp
struct DomElement : public DomNode {
    // ... other fields ...
    AttributeStorage* attributes;  // ← REMOVE THIS LINE
    // ... rest ...
};
```

#### 4.2 Add to dom_element.hpp (New Helper)

**Add after existing attribute management functions:**
```cpp
/**
 * Get all attribute names from element
 * @param element Target element
 * @param count Output parameter for number of attributes
 * @return Array of attribute names
 */
const char** dom_element_get_attribute_names(DomElement* element, int* count);
```

### Phase 5: Update Implementation Files

#### 5.1 Remove from dom_element.cpp

**Remove entire sections (lines ~130-400):**
- `attribute_hash()` function
- `attribute_compare()` function
- `attribute_storage_create()` function
- `attribute_storage_destroy()` function
- `pool_malloc_wrapper()` function
- `pool_realloc_wrapper()` function
- `pool_free_wrapper()` function
- `attribute_storage_convert_to_hashmap()` function
- `attribute_storage_set()` function
- `attribute_storage_get()` function
- `attribute_storage_has()` function
- `attribute_storage_remove()` function
- `get_names_iter()` function
- `GetNamesContext` struct
- `attribute_storage_get_names()` function

**Total removal: ~270 lines of code**

#### 5.2 Add Input* Access Helper

**Challenge:** MarkEditor requires an `Input*` pointer, but DomElement only stores `Pool*`

**Solution:** Store Input* in DomElement or Pool metadata

**Option A: Add Input* to DomElement**
```cpp
struct DomElement : public DomNode {
    Element* native_element;
    Input* input;  // ← Add this field
    Pool* pool;
    // ... rest ...
};
```

**Option B: Create Input wrapper on-the-fly**
```cpp
// Helper to get/create Input for editing
static Input* get_element_input(DomElement* element) {
    // Option: Store Input* in pool's user_data field
    // Option: Create temporary Input* wrapper
    // Option: Require Input* in dom_element_init()
    
    // For now, return nullptr and document limitation
    return nullptr;
}
```

**Recommended: Option A** - Store Input* in DomElement
- Most straightforward
- Slight memory overhead (8 bytes per element)
- Provides full editing capabilities

### Phase 6: Handle Special Cases

#### 6.1 Class Attribute Parsing

**Current Implementation:**
- `class` attribute stored as array of strings in `element->class_names`
- Parsed on demand from AttributeStorage

**New Implementation:**
- Parse from native element's `class` attribute
- Cache in `element->class_names` array
- Rebuild cache on class modifications

**Helper Function:**
```cpp
static void refresh_class_cache(DomElement* element) {
    if (!element || !element->native_element) return;
    
    ElementReader reader(element->native_element);
    const char* class_str = reader.get_attr_string("class");
    
    if (!class_str) {
        element->class_names = nullptr;
        element->class_count = 0;
        return;
    }
    
    // Count classes (space-separated)
    int count = 1;
    for (const char* p = class_str; *p; p++) {
        if (*p == ' ') count++;
    }
    
    // Allocate array
    element->class_names = (const char**)pool_alloc(
        element->pool, 
        count * sizeof(const char*)
    );
    
    // Parse and store (using strtok or manual parsing)
    // TODO: Implement space-separated parsing
}
```

#### 6.2 ID Attribute Caching

**Current Implementation:**
- ID stored in `element->id` for fast access
- Updated whenever `id` attribute changes

**New Implementation:**
- Cache remains valid approach
- Update cache in `dom_element_set_attribute()` and `dom_element_remove_attribute()`
- No change needed

#### 6.3 Style Attribute Handling

**Current Implementation:**
- Style attribute parsed and applied via `dom_element_apply_inline_style()`
- Stored in `specified_style` tree with inline specificity

**New Implementation:**
- Read from native element when needed
- No change to inline style parsing logic
- May need to refresh from native element on init

### Phase 7: Testing Strategy

#### 7.1 Unit Tests to Add

**File: `test/css/unit/test_dom_element_attributes_unit.cpp`** (NEW)

```cpp
#include <gtest/gtest.h>
#include "../../../lambda/input/css/dom_element.hpp"
#include "../../../lambda/mark_builder.hpp"

class DomElementAttributeTest : public ::testing::Test {
protected:
    Input* input;
    Pool* pool;
    MarkBuilder* builder;
    
    void SetUp() override {
        input = input_create(1024 * 1024);  // 1MB pool
        pool = input->pool;
        builder = new MarkBuilder(input);
    }
    
    void TearDown() override {
        delete builder;
        input_destroy(input);
    }
};

TEST_F(DomElementAttributeTest, BasicAttributeOperations) {
    // Create Lambda element with attributes
    Item elem_item = builder->element("div")
        .attr("id", "test")
        .attr("class", "container")
        .attr("data-value", "42")
        .final();
    
    // Create DomElement wrapper
    DomElement* dom_elem = dom_element_create(pool, "div", elem_item.element);
    ASSERT_NE(dom_elem, nullptr);
    
    // Test get
    EXPECT_STREQ(dom_element_get_attribute(dom_elem, "id"), "test");
    EXPECT_STREQ(dom_element_get_attribute(dom_elem, "class"), "container");
    EXPECT_STREQ(dom_element_get_attribute(dom_elem, "data-value"), "42");
    
    // Test has
    EXPECT_TRUE(dom_element_has_attribute(dom_elem, "id"));
    EXPECT_FALSE(dom_element_has_attribute(dom_elem, "nonexistent"));
}

TEST_F(DomElementAttributeTest, SetAttribute) {
    Item elem_item = builder->element("div").final();
    DomElement* dom_elem = dom_element_create(pool, "div", elem_item.element);
    
    // Add new attribute
    EXPECT_TRUE(dom_element_set_attribute(dom_elem, "title", "Hello"));
    EXPECT_STREQ(dom_element_get_attribute(dom_elem, "title"), "Hello");
    
    // Update existing
    EXPECT_TRUE(dom_element_set_attribute(dom_elem, "title", "World"));
    EXPECT_STREQ(dom_element_get_attribute(dom_elem, "title"), "World");
}

TEST_F(DomElementAttributeTest, RemoveAttribute) {
    Item elem_item = builder->element("div")
        .attr("id", "test")
        .attr("class", "box")
        .final();
    
    DomElement* dom_elem = dom_element_create(pool, "div", elem_item.element);
    
    EXPECT_TRUE(dom_element_remove_attribute(dom_elem, "class"));
    EXPECT_FALSE(dom_element_has_attribute(dom_elem, "class"));
    EXPECT_TRUE(dom_element_has_attribute(dom_elem, "id"));
}

TEST_F(DomElementAttributeTest, IterateAttributes) {
    Item elem_item = builder->element("div")
        .attr("id", "test")
        .attr("class", "container")
        .attr("data-x", "10")
        .attr("data-y", "20")
        .final();
    
    DomElement* dom_elem = dom_element_create(pool, "div", elem_item.element);
    
    int count;
    const char** names = dom_element_get_attribute_names(dom_elem, &count);
    
    EXPECT_EQ(count, 4);
    EXPECT_NE(names, nullptr);
    
    // Verify all names present (order not guaranteed)
    bool found_id = false, found_class = false, found_x = false, found_y = false;
    for (int i = 0; i < count; i++) {
        if (strcmp(names[i], "id") == 0) found_id = true;
        if (strcmp(names[i], "class") == 0) found_class = true;
        if (strcmp(names[i], "data-x") == 0) found_x = true;
        if (strcmp(names[i], "data-y") == 0) found_y = true;
    }
    
    EXPECT_TRUE(found_id && found_class && found_x && found_y);
}

TEST_F(DomElementAttributeTest, LargeAttributeSet) {
    // Test with 25 attributes (beyond old hashmap threshold)
    ElementBuilder eb = builder->element("div");
    
    for (int i = 0; i < 25; i++) {
        char key[32], val[32];
        snprintf(key, sizeof(key), "attr-%d", i);
        snprintf(val, sizeof(val), "value-%d", i);
        eb.attr(key, val);
    }
    
    Item elem_item = eb.final();
    DomElement* dom_elem = dom_element_create(pool, "div", elem_item.element);
    
    // Verify all attributes accessible
    for (int i = 0; i < 25; i++) {
        char key[32], expected[32];
        snprintf(key, sizeof(key), "attr-%d", i);
        snprintf(expected, sizeof(expected), "value-%d", i);
        
        EXPECT_TRUE(dom_element_has_attribute(dom_elem, key));
        EXPECT_STREQ(dom_element_get_attribute(dom_elem, key), expected);
    }
}

TEST_F(DomElementAttributeTest, EmptyAttributeValue) {
    Item elem_item = builder->element("div")
        .attr("empty", "")
        .final();
    
    DomElement* dom_elem = dom_element_create(pool, "div", elem_item.element);
    
    EXPECT_TRUE(dom_element_has_attribute(dom_elem, "empty"));
    const char* value = dom_element_get_attribute(dom_elem, "empty");
    EXPECT_NE(value, nullptr);
    EXPECT_STREQ(value, "");
}

TEST_F(DomElementAttributeTest, SpecialCharactersInAttributes) {
    Item elem_item = builder->element("div")
        .attr("data-json", "{\"key\": \"value\"}")
        .attr("onclick", "alert('test');")
        .final();
    
    DomElement* dom_elem = dom_element_create(pool, "div", elem_item.element);
    
    EXPECT_STREQ(dom_element_get_attribute(dom_elem, "data-json"), 
                 "{\"key\": \"value\"}");
    EXPECT_STREQ(dom_element_get_attribute(dom_elem, "onclick"), 
                 "alert('test');");
}
```

#### 7.2 Integration Tests to Update

**Files to verify:**
- `test/css/test_css_dom_integration.cpp` - Should pass without changes
- `test/test_html_css_gtest.cpp` - Should pass without changes
- `test/test_lambda_domnode_gtest.cpp` - Should pass without changes

#### 7.3 Regression Testing

**Run full test suite:**
```bash
make clean
make build
make test-baseline
```

**Expected results:**
- All existing tests pass
- No memory leaks (verify with valgrind if available)
- No performance regression

### Phase 8: Documentation Updates

#### 8.1 Update Code Comments

**In dom_element.hpp:**
- Remove AttributeStorage documentation
- Update DomElement struct comment to mention native_element as attribute source
- Add comment about Input* requirement for attribute modifications

**In dom_element.cpp:**
- Add file header comment explaining attribute access strategy
- Document relationship with MarkReader/MarkEditor

#### 8.2 Update README/Design Docs

**If exists:**
- Update architecture diagrams to remove AttributeStorage
- Document attribute access patterns
- Note performance characteristics (O(n) lookup, acceptable for typical DOM)

## Migration Checklist

### Pre-Implementation
- [x] Analyze current code
- [x] Document existing behavior
- [x] Identify all usage sites
- [x] Review test coverage
- [x] Create cleanup plan

### Implementation
- [ ] Add Input* field to DomElement struct
- [ ] Implement new attribute operations (set/get/has/remove)
- [ ] Implement dom_element_get_attribute_names()
- [ ] Update dom_element_init()
- [ ] Update dom_element_clear()
- [ ] Update dom_element_destroy()
- [ ] Remove AttributeStorage code from .cpp
- [ ] Remove AttributeStorage code from .hpp
- [ ] Update all call sites if needed

### Testing
- [ ] Create new unit test file
- [ ] Add attribute iteration tests
- [ ] Add large attribute set tests
- [ ] Add edge case tests
- [ ] Run existing integration tests
- [ ] Run full test suite (make test-baseline)
- [ ] Fix any test failures
- [ ] Verify no memory leaks

### Cleanup
- [ ] Remove unused includes
- [ ] Update documentation
- [ ] Review code for TODOs
- [ ] Code review
- [ ] Final testing

### Validation
- [ ] All tests pass
- [ ] No compiler warnings
- [ ] No memory leaks
- [ ] Performance acceptable
- [ ] Code review approved

## Risk Assessment

### Low Risk
- ✅ Lambda Element attribute API is mature and well-tested
- ✅ Existing tests provide good coverage
- ✅ Changes are localized to dom_element module
- ✅ No external API changes (public interface unchanged)

### Medium Risk
- ⚠️ Need to add Input* to DomElement (struct size increase)
- ⚠️ MarkEditor creates new Element instances (may affect pointer stability)
- ⚠️ Inline editing mode must be used carefully

### Mitigation Strategies
1. **Input* Storage:** Add as last field in struct to minimize layout impact
2. **Pointer Stability:** Update element->native_element after each edit operation
3. **Mode Selection:** Always use EDIT_MODE_INLINE for DOM mutations
4. **Testing:** Comprehensive unit tests for all operations
5. **Incremental:** Implement in phases, test at each step

## Performance Considerations

### Before (AttributeStorage)
- **Small elements (<10 attrs):** O(n) array scan
- **Large elements (>=10 attrs):** O(1) hashmap lookup
- **Memory:** Additional overhead for array/hashmap + string copies

### After (Native Element)
- **All elements:** O(n) shape traversal
- **Typical DOM:** n < 10 for most elements, acceptable performance
- **Memory:** No duplication, more efficient

### Benchmarks (Expected)
- Get attribute: ~same performance (both O(n) for typical case)
- Set attribute: Slightly slower (MarkEditor overhead)
- Memory usage: 30-50% reduction (no duplication)

### Optimization Opportunities (Future)
1. Cache frequently accessed attributes (id, class already cached)
2. Add shape index for O(1) lookup if needed
3. Batch attribute updates using MarkEditor's batch API

## Open Questions

### Q1: What if DomElement is created without Input*?
**Answer:** Require Input* in dom_element_create() parameters. Update all call sites.

### Q2: Does MarkEditor always return the same Element* in inline mode?
**Answer:** Need to verify. If not, must update element->native_element after each operation.

### Q3: Should we support attribute iteration with callback?
**Answer:** Add if needed. Current approach returns array, which is simple and sufficient.

### Q4: Performance impact of MarkEditor overhead?
**Answer:** Minimal for typical DOM. Measure if becomes issue.

## Success Criteria

1. ✅ All existing tests pass
2. ✅ New attribute iteration tests pass
3. ✅ No memory leaks
4. ✅ Code is simpler (fewer lines, less complexity)
5. ✅ No performance regression on typical use cases
6. ✅ Memory usage reduced
7. ✅ Single source of truth for attributes

## Timeline Estimate

- **Phase 1 (Analysis):** ✅ Complete
- **Phase 2-5 (Implementation):** 4-6 hours
- **Phase 6 (Special Cases):** 2-3 hours
- **Phase 7 (Testing):** 3-4 hours
- **Phase 8 (Documentation):** 1-2 hours

**Total Estimate:** 10-15 hours

## Conclusion

This cleanup removes unnecessary complexity and data duplication by leveraging Lambda's robust Element attribute system. The changes are well-scoped, low-risk, and should result in simpler, more maintainable code with better memory efficiency.

The key insight is that AttributeStorage was created to solve a problem that Lambda Element already solves efficiently. By using MarkReader for reads and MarkEditor for writes, we get the best of both worlds: efficient access and proper lifetime management.

## Next Steps

1. Review this plan with team
2. Get approval for Input* addition to DomElement
3. Begin implementation (Phase 2)
4. Proceed phase by phase with testing at each step
5. Final validation and merge

---

**Document Version:** 1.0  
**Date:** 2025-11-22  
**Author:** GitHub Copilot (AI Assistant)  
**Status:** Ready for Review
