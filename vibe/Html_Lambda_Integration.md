# HTML/CSS DOM and Lambda Tree Integration

## Document Overview

This document describes how the HTML/CSS DOM system connects with Lambda's native tree structures, and outlines a refactoring plan to establish bidirectional synchronization for text nodes.

**Status**: Planning Phase  
**Date**: 2025-01-23  
**Author**: Lambda Development Team

---

## Current Architecture

### 1. DomElement Integration (IMPLEMENTED ✅)

DomElement has **full bidirectional synchronization** with Lambda's Element tree through the MarkEditor API.

#### Data Structure

```cpp
struct DomElement : public DomNode {
    // Lambda backing
    Element* native_element;     // Pointer to backing Lambda Element
    Input* input;                // Input context (contains arena, pool, name_pool, shape_pool)
    
    // DOM tree relationships (managed separately)
    DomNode* parent;
    DomNode* first_child;
    DomNode* next_sibling;
    DomNode* prev_sibling;
    
    // Element metadata
    const char* tag_name;
    const void* tag_name_ptr;    // Unique pointer for fast comparison
    uint32_t tag_id;             // Lexbor tag ID for fast comparison
    
    // Cached attributes (extracted from native_element)
    const char* id;
    const char** class_names;
    int class_count;
    
    // CSS styling
    StyleTree* specified_style;
    StyleTree* computed_style;
    uint64_t style_version;
    bool needs_style_recompute;
    uint32_t pseudo_state;
    
    // Memory
    Pool* pool;
};
```

#### Lambda Element Structure

Lambda Elements are structured as typed containers with attributes and children:

```cpp
struct Element : List {
    void* type;        // TypeElmt* - contains tag name and shape (attribute schema)
    void* data;        // Map of attributes (field name -> value)
    int data_cap;      // Capacity of data buffer
    
    // Inherited from List:
    Item* items;       // Children array (Elements and Strings)
    int64_t length;    // Number of children
    int64_t capacity;  // Array capacity
    int ref_cnt;       // Reference count
    TypeId type_id;    // LMD_TYPE_ELEMENT
};

struct TypeElmt {
    Str name;          // Tag name (e.g., "div", "p")
    ShapeEntry* shape; // Attribute schema (linked list of name->type)
    int64_t content_length;
};

// Children are stored as Item union:
typedef union Item {
    Element* element;  // For element children
    String* string;    // For text children
    void* pointer;     // Generic pointer
    // ... other types
} Item;
```

#### CRUD Operations

All attribute modifications flow through **MarkEditor** which maintains consistency:

**1. Set Attribute** (`dom_element_set_attribute`)
```cpp
bool dom_element_set_attribute(DomElement* element, const char* name, const char* value) {
    if (element->native_element && element->input) {
        MarkEditor editor(element->input, EDIT_MODE_INLINE);
        
        // Create string value
        Item value_item = editor.builder()->createStringItem(value);
        
        // Update via MarkEditor - modifies native_element in-place
        Item result = editor.elmt_update_attr(
            {.element = element->native_element}, 
            name, 
            value_item
        );
        
        if (result.element) {
            // In INLINE mode: element pointer unchanged (in-place mutation)
            element->native_element = result.element;
            
            // Update DOM caches
            if (strcmp(name, "id") == 0) {
                element->id = extract_from_native(element->native_element);
            }
            element->style_version++;
            return true;
        }
    }
    return false;
}
```

**2. Remove Attribute** (`dom_element_remove_attribute`)
```cpp
bool dom_element_remove_attribute(DomElement* element, const char* name) {
    if (element->native_element && element->input) {
        MarkEditor editor(element->input, EDIT_MODE_INLINE);
        
        // Delete attribute via MarkEditor
        Item result = editor.elmt_delete_attr(
            {.element = element->native_element}, 
            name
        );
        
        if (result.element) {
            // In INLINE mode: element pointer unchanged
            element->native_element = result.element;
            element->style_version++;
            return true;
        }
    }
    return false;
}
```

**3. MarkEditor Operations**

MarkEditor provides in-place mutation (INLINE mode) or copy-on-write (IMMUTABLE mode):

- `elmt_update_attr()` - Add or update attribute
- `elmt_delete_attr()` - Remove attribute
- `elmt_insert_child()` - Insert child at index
- `elmt_append_child()` - Append child to end
- `elmt_delete_child()` - Remove child at index
- `elmt_replace_child()` - Replace child at index

In INLINE mode, the Element pointer remains stable (in-place mutation):
```cpp
// mark_editor.cpp line 1038
Element* result_elmt = old_elmt;  // Same pointer in INLINE mode
```

#### Flow: DOM → Lambda

```
User API Call
    ↓
dom_element_set_attribute()
    ↓
MarkEditor::elmt_update_attr()
    ↓
elmt_update_attr_inline()          [INLINE mode]
    ↓
elmt_rebuild_with_new_shape()      [if attribute doesn't exist]
    ↓
Lambda Element updated in-place
    ↓
Attribute persists in Lambda tree  ✅
```

#### Flow: Lambda → DOM

```
HTML Parser (build_dom_tree_from_element)
    ↓
Creates DomElement with native_element pointer
    ↓
ElementReader reads attributes from native_element
    ↓
Caches id, class_names from Lambda Element
    ↓
DOM reflects Lambda state  ✅
```

---

### 2. DomText Integration (CURRENT - READ-ONLY ❌)

DomText is currently **standalone** with **one-way synchronization** (Lambda → DOM only).

#### Data Structure

```cpp
struct DomText : public DomNode {
    // Text content (COPIED from Lambda)
    const char* text;        // Standalone copy - NOT backed by Lambda String
    size_t length;           // Text length
    
    // Memory
    Pool* pool;              // Memory pool
    
    // DOM tree relationships
    DomNode* parent;
    DomNode* first_child;    // Always nullptr for text nodes
    DomNode* next_sibling;
    DomNode* prev_sibling;
    
    // NO Lambda backing:
    // - No String* native_string pointer
    // - No Input* input context
    // - No parent Element* reference
    // - No child index tracking
};
```

#### Current Operations (Standalone)

**1. Create Text Node** (`dom_text_create`)
```cpp
DomText* dom_text_create(Pool* pool, const char* text) {
    DomText* text_node = (DomText*)pool_alloc(pool, sizeof(DomText));
    
    // COPY text content (loses Lambda String* reference)
    size_t len = strlen(text);
    char* text_copy = (char*)pool_alloc(pool, len + 1);
    strcpy(text_copy, text);
    
    text_node->text = text_copy;      // Standalone copy
    text_node->length = len;
    text_node->pool = pool;
    // No native_string, no input, no parent tracking
    
    return text_node;
}
```

**2. Set Text Content** (`dom_text_set_content`)
```cpp
bool dom_text_set_content(DomText* text, const char* new_content) {
    if (!text || !new_content) return false;
    
    // Allocate new copy (doesn't update Lambda tree)
    size_t new_len = strlen(new_content);
    char* new_copy = (char*)pool_alloc(text->pool, new_len + 1);
    strcpy(new_copy, new_content);
    
    text->text = new_copy;   // Update local copy only
    text->length = new_len;
    
    // Lambda String* is NOT updated ❌
    // Parent Element's children array is NOT modified ❌
    
    return true;
}
```

#### Flow: Lambda → DOM (One-Way Only)

```
HTML Parser (build_dom_tree_from_element)
    ↓
Finds String child in Element's items array
    ↓
Item child_item = elem->items[i];
String* text_str = (String*)child_item.pointer;
    ↓
dom_text_create(pool, text_str->chars)  [COPIES chars, DISCARDS String*]
    ↓
DomText created with standalone copy
    ↓
Lambda String* reference lost  ❌
```

#### Flow: DOM → Lambda (NOT IMPLEMENTED ❌)

```
User calls dom_text_set_content()
    ↓
Updates local text copy
    ↓
❌ Lambda String NOT updated
❌ Parent Element's items array NOT modified
❌ Changes lost on serialization
```

---

## Problem Statement

### Current Issues

1. **Architectural Inconsistency**
   - DomElement: Full bidirectional sync via MarkEditor
   - DomText: Read-only snapshot, no sync back to Lambda

2. **Data Loss**
   - Text modifications via `dom_text_set_content()` don't propagate to Lambda tree
   - When Lambda tree is serialized, text changes are lost
   - Inconsistent behavior: element attributes sync, text content doesn't

3. **Limited Functionality**
   - Cannot dynamically update text content in documents
   - Cannot implement text editing features
   - Cannot support contenteditable or rich text scenarios

4. **Memory Inefficiency**
   - Text content duplicated (Lambda String + DomText copy)
   - Reference to original Lambda String is discarded

### Design Goals

1. **Consistency**: DomText should have same integration level as DomElement
2. **Bidirectional Sync**: Changes flow both Lambda → DOM and DOM → Lambda
3. **Correctness**: DOM tree operations must update Lambda tree structure
4. **Performance**: Minimize copying, use references where possible
5. **Safety**: Maintain memory pool ownership and reference counting

---

## Refactoring Plan

### Phase 1: Update DomText Structure

Add Lambda backing fields to DomText:

```cpp
struct DomText : public DomNode {
    // Text content
    const char* text;            // Text content (points to native_string->chars)
    size_t length;               // Text length
    
    // Lambda backing (NEW)
    String* native_string;       // Pointer to backing Lambda String
    Input* input;                // Input context (for MarkEditor)
    DomElement* parent_element;  // Parent DomElement (for child array updates)
    int64_t child_index;         // Index in parent's native_element->items array
    
    // Memory
    Pool* pool;
    
    // DOM tree relationships
    DomNode* parent;
    DomNode* first_child;
    DomNode* next_sibling;
    DomNode* prev_sibling;
};
```

**Key Changes**:
- Add `String* native_string` - points to Lambda String in parent Element's children
- Add `Input* input` - provides access to MarkEditor dependencies
- Add `DomElement* parent_element` - tracks parent (needed to access native_element)
- Add `int64_t child_index` - tracks position in parent's items array
- Modify `text` pointer - should reference `native_string->chars` instead of copying

### Phase 2: Update Text Node Creation

Modify `build_dom_tree_from_element()` to preserve Lambda backing:

```cpp
// Current (loses Lambda reference):
else if (child_type == LMD_TYPE_STRING) {
    String* text_str = (String*)child_item.pointer;
    DomText* text_node = dom_text_create(pool, text_str->chars);  // ❌ Copies and discards String*
    text_node->parent = dom_elem;
}

// NEW (preserves Lambda reference):
else if (child_type == LMD_TYPE_STRING) {
    String* text_str = (String*)child_item.pointer;
    
    // Create backed text node
    DomText* text_node = dom_text_create_backed(
        pool,
        text_str,           // Preserve String* reference
        dom_elem,           // Parent DomElement
        i                   // Child index in native_element->items
    );
    
    if (text_node) {
        text_node->parent = dom_elem;
        // Add to DOM sibling chain...
    }
}
```

**New Function**:
```cpp
DomText* dom_text_create_backed(Pool* pool, String* native_string, 
                                DomElement* parent_element, int64_t child_index) {
    if (!pool || !native_string || !parent_element) return nullptr;
    
    DomText* text_node = (DomText*)pool_calloc(pool, sizeof(DomText));
    if (!text_node) return nullptr;
    
    // Initialize base DomNode
    text_node->node_type = DOM_NODE_TEXT;
    text_node->parent = parent_element;
    text_node->pool = pool;
    
    // Set Lambda backing
    text_node->native_string = native_string;
    text_node->text = native_string->chars;  // Reference, not copy
    text_node->length = native_string->len;
    text_node->input = parent_element->input;
    text_node->parent_element = parent_element;
    text_node->child_index = child_index;
    
    return text_node;
}
```

### Phase 3: Implement Backed CRUD Operations

Add new functions that synchronize with Lambda tree:

**1. Set Text Content (Backed)**

```cpp
bool dom_text_set_content_backed(DomText* text, const char* new_content) {
    if (!text || !new_content) return false;
    if (!text->native_string || !text->input || !text->parent_element) {
        log_error("dom_text_set_content_backed: text node not backed by Lambda");
        return false;
    }
    
    // Create new String via MarkBuilder
    MarkEditor editor(text->input, EDIT_MODE_INLINE);
    Item new_string_item = editor.builder()->createStringItem(new_content);
    
    if (!new_string_item.string) {
        log_error("dom_text_set_content_backed: failed to create string");
        return false;
    }
    
    // Replace child in parent Element's items array
    Item result = editor.elmt_replace_child(
        {.element = text->parent_element->native_element},
        text->child_index,
        new_string_item
    );
    
    if (!result.element) {
        log_error("dom_text_set_content_backed: failed to replace child");
        return false;
    }
    
    // Update DomText to point to new String
    text->native_string = new_string_item.string;
    text->text = new_string_item.string->chars;
    text->length = new_string_item.string->len;
    
    // In INLINE mode, parent element pointer unchanged
    // But update reference for consistency
    text->parent_element->native_element = result.element;
    
    return true;
}
```

**2. Append Text to Element (Backed)**

```cpp
DomText* dom_element_append_text_backed(DomElement* parent, const char* text_content) {
    if (!parent || !text_content) return nullptr;
    if (!parent->native_element || !parent->input) {
        log_error("dom_element_append_text_backed: parent not backed");
        return nullptr;
    }
    
    // Create String item
    MarkEditor editor(parent->input, EDIT_MODE_INLINE);
    Item string_item = editor.builder()->createStringItem(text_content);
    
    // Append to parent Element's children
    Item result = editor.elmt_append_child(
        {.element = parent->native_element},
        string_item
    );
    
    if (!result.element) {
        log_error("dom_element_append_text_backed: failed to append");
        return nullptr;
    }
    
    // Create DomText wrapper
    int64_t child_index = parent->native_element->length - 1;
    DomText* text_node = dom_text_create_backed(
        parent->pool,
        string_item.string,
        parent,
        child_index
    );
    
    if (!text_node) return nullptr;
    
    // Add to DOM sibling chain
    text_node->parent = parent;
    if (!parent->first_child) {
        parent->first_child = text_node;
    } else {
        DomNode* last = parent->first_child;
        while (last->next_sibling) last = last->next_sibling;
        last->next_sibling = text_node;
        text_node->prev_sibling = last;
    }
    
    parent->native_element = result.element;
    return text_node;
}
```

**3. Remove Text Node (Backed)**

```cpp
bool dom_text_remove_backed(DomText* text) {
    if (!text) return false;
    if (!text->native_string || !text->input || !text->parent_element) {
        log_error("dom_text_remove_backed: text node not backed");
        return false;
    }
    
    // Remove from Lambda parent Element's children array
    MarkEditor editor(text->input, EDIT_MODE_INLINE);
    Item result = editor.elmt_delete_child(
        {.element = text->parent_element->native_element},
        text->child_index
    );
    
    if (!result.element) {
        log_error("dom_text_remove_backed: failed to delete child");
        return false;
    }
    
    // Update parent
    text->parent_element->native_element = result.element;
    
    // Update sibling child indices (shifted after removal)
    // This requires tracking all DomText siblings - may need parent child list
    
    // Remove from DOM sibling chain
    if (text->prev_sibling) {
        text->prev_sibling->next_sibling = text->next_sibling;
    } else if (text->parent) {
        text->parent->first_child = text->next_sibling;
    }
    
    if (text->next_sibling) {
        text->next_sibling->prev_sibling = text->prev_sibling;
    }
    
    // Clear references
    text->parent = nullptr;
    text->native_string = nullptr;
    
    return true;
}
```

### Phase 4: Handle Child Index Tracking

**Challenge**: When children are added/removed from Lambda Element, child indices change.

**Solutions**:

1. **Option A - Lazy Update**: Recompute child_index on demand
   - Pro: Simple, no bookkeeping
   - Con: O(n) traversal on each operation

2. **Option B - Maintain Child Map**: Parent tracks DomNode* → child_index mapping
   - Pro: O(1) lookup
   - Con: Additional memory, needs updates on structure changes

3. **Option C - Traverse on Each Operation**: Find child_index by scanning native_element->items
   - Pro: No extra storage
   - Con: O(n) for each text update

**Recommended**: Option C with optimization - cache index but validate before use:

```cpp
int64_t dom_text_get_child_index(DomText* text) {
    if (!text->parent_element || !text->native_string) return -1;
    
    Element* parent_elem = text->parent_element->native_element;
    
    // Try cached index first (optimization)
    if (text->child_index >= 0 && text->child_index < parent_elem->length) {
        Item cached_item = parent_elem->items[text->child_index];
        if (cached_item.string == text->native_string) {
            return text->child_index;  // Cache hit
        }
    }
    
    // Cache miss - scan for correct index
    for (int64_t i = 0; i < parent_elem->length; i++) {
        Item item = parent_elem->items[i];
        if (get_type_id(item) == LMD_TYPE_STRING && item.string == text->native_string) {
            text->child_index = i;  // Update cache
            return i;
        }
    }
    
    log_error("dom_text_get_child_index: native_string not found in parent");
    return -1;
}
```

### Phase 5: Update Unit Tests

#### 5.1 Add New Tests to `test_css_dom_crud.cpp`

```cpp
// ============================================================================
// DomText Backed Tests (Lambda Integration)
// ============================================================================

TEST_F(DomCrudTest, DomText_CreateBacked) {
    // Create parent element with backing
    DomElement* parent = create_backed_element("div");
    
    // Append backed text node
    DomText* text = dom_element_append_text_backed(parent, "Hello World");
    
    ASSERT_NE(text, nullptr);
    EXPECT_NE(text->native_string, nullptr);
    EXPECT_NE(text->input, nullptr);
    EXPECT_EQ(text->parent_element, parent);
    EXPECT_STREQ(text->text, "Hello World");
    
    // Verify Lambda backing
    ASSERT_EQ(parent->native_element->length, 1);
    Item child = parent->native_element->items[0];
    EXPECT_EQ(get_type_id(child), LMD_TYPE_STRING);
    EXPECT_EQ(child.string, text->native_string);
    EXPECT_STREQ(child.string->chars, "Hello World");
}

TEST_F(DomCrudTest, DomText_SetContentBacked_UpdatesLambda) {
    DomElement* parent = create_backed_element("p");
    DomText* text = dom_element_append_text_backed(parent, "Original");
    
    // Update text content
    EXPECT_TRUE(dom_text_set_content_backed(text, "Updated"));
    
    // Verify DomText updated
    EXPECT_STREQ(text->text, "Updated");
    EXPECT_EQ(text->length, 7);
    
    // Verify Lambda String updated
    Item child = parent->native_element->items[text->child_index];
    EXPECT_EQ(get_type_id(child), LMD_TYPE_STRING);
    EXPECT_STREQ(child.string->chars, "Updated");
    EXPECT_EQ(child.string->len, 7);
}

TEST_F(DomCrudTest, DomText_RemoveBacked_UpdatesLambda) {
    DomElement* parent = create_backed_element("div");
    DomText* text1 = dom_element_append_text_backed(parent, "First");
    DomText* text2 = dom_element_append_text_backed(parent, "Second");
    
    EXPECT_EQ(parent->native_element->length, 2);
    
    // Remove first text node
    EXPECT_TRUE(dom_text_remove_backed(text1));
    
    // Verify Lambda updated
    EXPECT_EQ(parent->native_element->length, 1);
    Item remaining = parent->native_element->items[0];
    EXPECT_EQ(get_type_id(remaining), LMD_TYPE_STRING);
    EXPECT_STREQ(remaining.string->chars, "Second");
    
    // Verify text2 index updated
    EXPECT_EQ(text2->child_index, 0);
}

TEST_F(DomCrudTest, DomText_MultipleOperations_MaintainsSync) {
    DomElement* parent = create_backed_element("div");
    
    // Add multiple text nodes
    DomText* text1 = dom_element_append_text_backed(parent, "One");
    DomText* text2 = dom_element_append_text_backed(parent, "Two");
    DomText* text3 = dom_element_append_text_backed(parent, "Three");
    
    EXPECT_EQ(parent->native_element->length, 3);
    
    // Update middle text
    dom_text_set_content_backed(text2, "TWO");
    
    // Verify all strings
    EXPECT_STREQ(parent->native_element->items[0].string->chars, "One");
    EXPECT_STREQ(parent->native_element->items[1].string->chars, "TWO");
    EXPECT_STREQ(parent->native_element->items[2].string->chars, "Three");
    
    // Remove middle text
    dom_text_remove_backed(text2);
    
    EXPECT_EQ(parent->native_element->length, 2);
    EXPECT_STREQ(parent->native_element->items[0].string->chars, "One");
    EXPECT_STREQ(parent->native_element->items[1].string->chars, "Three");
    
    // Verify indices updated
    EXPECT_EQ(text1->child_index, 0);
    EXPECT_EQ(text3->child_index, 1);
}

TEST_F(DomCrudTest, DomText_MixedChildren_ElementsAndText) {
    DomElement* parent = create_backed_element("div");
    
    // Add mixed children
    DomText* text1 = dom_element_append_text_backed(parent, "Before");
    DomElement* child_elem = create_backed_element("span");
    dom_element_append_child(parent, child_elem);
    DomText* text2 = dom_element_append_text_backed(parent, "After");
    
    // Verify structure
    EXPECT_EQ(parent->native_element->length, 3);
    EXPECT_EQ(get_type_id(parent->native_element->items[0]), LMD_TYPE_STRING);
    EXPECT_EQ(get_type_id(parent->native_element->items[1]), LMD_TYPE_ELEMENT);
    EXPECT_EQ(get_type_id(parent->native_element->items[2]), LMD_TYPE_STRING);
    
    // Update text around element
    dom_text_set_content_backed(text1, "BEFORE");
    dom_text_set_content_backed(text2, "AFTER");
    
    // Verify Lambda tree
    EXPECT_STREQ(parent->native_element->items[0].string->chars, "BEFORE");
    EXPECT_STREQ(parent->native_element->items[2].string->chars, "AFTER");
}

TEST_F(DomCrudTest, DomText_ChildIndexTracking) {
    DomElement* parent = create_backed_element("p");
    
    DomText* t0 = dom_element_append_text_backed(parent, "Zero");
    DomText* t1 = dom_element_append_text_backed(parent, "One");
    DomText* t2 = dom_element_append_text_backed(parent, "Two");
    
    EXPECT_EQ(t0->child_index, 0);
    EXPECT_EQ(t1->child_index, 1);
    EXPECT_EQ(t2->child_index, 2);
    
    // Remove middle - indices should update
    dom_text_remove_backed(t1);
    
    EXPECT_EQ(t0->child_index, 0);
    EXPECT_EQ(t2->child_index, 1);
    
    // Get child index should validate
    EXPECT_EQ(dom_text_get_child_index(t0), 0);
    EXPECT_EQ(dom_text_get_child_index(t2), 1);
}

TEST_F(DomCrudTest, DomText_EmptyString_Backed) {
    DomElement* parent = create_backed_element("div");
    DomText* text = dom_element_append_text_backed(parent, "");
    
    ASSERT_NE(text, nullptr);
    EXPECT_STREQ(text->text, "");
    EXPECT_EQ(text->length, 0);
    
    // Verify Lambda has empty string
    Item child = parent->native_element->items[0];
    EXPECT_EQ(get_type_id(child), LMD_TYPE_STRING);
    EXPECT_STREQ(child.string->chars, "");
}

TEST_F(DomCrudTest, DomText_LongString_Backed) {
    const char* long_text = "Lorem ipsum dolor sit amet, consectetur adipiscing elit. "
                           "Sed do eiusmod tempor incididunt ut labore et dolore magna aliqua.";
    
    DomElement* parent = create_backed_element("div");
    DomText* text = dom_element_append_text_backed(parent, long_text);
    
    ASSERT_NE(text, nullptr);
    EXPECT_STREQ(text->text, long_text);
    EXPECT_EQ(text->length, strlen(long_text));
    
    // Update to even longer string
    const char* longer = "This is an even longer string that tests memory handling...";
    EXPECT_TRUE(dom_text_set_content_backed(text, longer));
    EXPECT_STREQ(text->text, longer);
    
    // Verify Lambda
    EXPECT_STREQ(parent->native_element->items[0].string->chars, longer);
}
```

#### 5.2 Update Existing Tests in `test_css_dom_integration.cpp`

Update tests to verify Lambda backing:

```cpp
TEST_F(DomIntegrationTest, DomText_SetContent_VerifyLambdaBacking) {
    // Parse HTML to get backed structure
    const char* html = "<div>Original Text</div>";
    Element* root = parse_html_fragment(html);
    DomElement* dom_div = build_dom_tree_from_element(root, pool, nullptr);
    
    // Get text node
    DomText* text = static_cast<DomText*>(dom_div->first_child);
    ASSERT_NE(text, nullptr);
    ASSERT_NE(text->native_string, nullptr);
    
    // Update content
    EXPECT_TRUE(dom_text_set_content_backed(text, "Updated Text"));
    
    // Verify Lambda String updated
    EXPECT_STREQ(dom_div->native_element->items[0].string->chars, "Updated Text");
}
```

---

## Implementation Checklist

### Phase 1: Structure Updates
- [ ] Add fields to DomText: `native_string`, `input`, `parent_element`, `child_index`
- [ ] Update DomText constructor
- [ ] Update `dom_text_create()` signature (add backing parameters)

### Phase 2: Creation Functions
- [ ] Implement `dom_text_create_backed()`
- [ ] Update `build_dom_tree_from_element()` to preserve String* references
- [ ] Update `build_dom_tree_from_element()` to pass parent and index

### Phase 3: CRUD Operations
- [ ] Implement `dom_text_set_content_backed()`
- [ ] Implement `dom_element_append_text_backed()`
- [ ] Implement `dom_text_remove_backed()`
- [ ] Implement `dom_text_get_child_index()` (validation helper)

### Phase 4: Child Index Management
- [ ] Add child index update logic after removals
- [ ] Add child index validation in CRUD operations
- [ ] Test index tracking with mixed children

### Phase 5: Testing
- [ ] Add 8-10 new tests to `test_css_dom_crud.cpp`
- [ ] Update existing tests in `test_css_dom_integration.cpp`
- [ ] Test edge cases: empty strings, long strings, mixed children
- [ ] Test index tracking after additions/removals
- [ ] Verify memory management (no leaks)

### Phase 6: Documentation
- [ ] Update function documentation in `dom_element.hpp`
- [ ] Document backed vs unbacked text nodes
- [ ] Add migration guide for existing code

---

## Migration Strategy

### Backward Compatibility

Keep existing unbacked functions for compatibility:

```cpp
// OLD API (unbacked) - DEPRECATED but still available
DomText* dom_text_create(Pool* pool, const char* text);
bool dom_text_set_content(DomText* text, const char* new_content);

// NEW API (backed) - RECOMMENDED
DomText* dom_text_create_backed(Pool* pool, String* native_string, 
                                DomElement* parent, int64_t index);
bool dom_text_set_content_backed(DomText* text, const char* new_content);
DomText* dom_element_append_text_backed(DomElement* parent, const char* text);
```

### Detection

```cpp
bool dom_text_is_backed(DomText* text) {
    return text && text->native_string && text->input && text->parent_element;
}
```

### Automatic Routing

```cpp
bool dom_text_set_content_auto(DomText* text, const char* new_content) {
    if (dom_text_is_backed(text)) {
        return dom_text_set_content_backed(text, new_content);
    } else {
        return dom_text_set_content(text, new_content);  // Unbacked fallback
    }
}
```

---

## Performance Considerations

1. **Memory**:
   - Before: Text copied (String + copy in DomText)
   - After: Text referenced (String only, DomText points to chars)
   - Savings: ~50% for text content

2. **Speed**:
   - Text updates: O(1) String replacement via MarkEditor
   - Child index lookup: O(n) worst case, O(1) with caching
   - Overall: Comparable or better than unbacked

3. **Reference Counting**:
   - Lambda String reference counts managed by MarkEditor
   - DomText lifetime must not exceed parent Element

---

## Testing Strategy

### Unit Tests (test_css_dom_crud.cpp)
- ✅ Create backed text nodes
- ✅ Update text content (verify Lambda sync)
- ✅ Remove text nodes (verify Lambda sync)
- ✅ Multiple operations (add/update/remove)
- ✅ Mixed children (elements and text)
- ✅ Child index tracking
- ✅ Empty and long strings
- ✅ Edge cases (null checks, bounds)

### Integration Tests (test_css_dom_integration.cpp)
- ✅ Parse HTML with text nodes
- ✅ Update text after parsing
- ✅ Verify Lambda backing preserved
- ✅ Serialize and verify text persists

### Manual Testing
- Parse complex HTML documents
- Modify text dynamically
- Serialize back to HTML
- Verify no memory leaks (valgrind)

---

## Benefits After Refactoring

1. **Consistency**: DomText and DomElement have same integration level
2. **Correctness**: Text changes persist in Lambda tree
3. **Completeness**: Full DOM CRUD operations supported
4. **Performance**: Reference instead of copy, less memory
5. **Maintainability**: Single source of truth (Lambda tree)
6. **Extensibility**: Foundation for contenteditable, text editing features

---

## Risks and Mitigations

| Risk | Impact | Mitigation |
|------|--------|-----------|
| Child index desync | Data corruption | Validate index before each use, add assertions |
| Memory lifetime issues | Crashes | Document ownership, add lifetime checks |
| Breaking existing code | Build failures | Keep unbacked API, add detection helpers |
| Performance regression | Slow operations | Profile and optimize child index lookup |
| Complex testing | Hard to verify | Comprehensive test suite, incremental rollout |

---

## Timeline Estimate

- Phase 1 (Structure): 2 hours
- Phase 2 (Creation): 3 hours
- Phase 3 (CRUD): 4 hours
- Phase 4 (Index Tracking): 2 hours
- Phase 5 (Testing): 4 hours
- Phase 6 (Documentation): 1 hour

**Total**: ~16 hours (2 days)

---

## Future Enhancements

1. **Efficient Index Tracking**: Consider maintaining parent's child map for O(1) lookups
2. **Text Manipulation**: Add substring operations, insert/delete at offset
3. **Text Ranges**: Support DOM Range API for selections
4. **Rich Text**: Support mixed formatting (inline elements within text)
5. **Event System**: Notify observers of text changes

---

## Conclusion

This refactoring establishes **full bidirectional synchronization** between DomText and Lambda String, bringing text nodes to the same integration level as elements. The result is a consistent, correct, and complete DOM/Lambda integration that forms a solid foundation for document manipulation and editing features.

**Status**: Ready for implementation
**Priority**: High (architectural consistency)
**Dependencies**: None (MarkEditor API already supports child operations)
