# DOM Refactoring Plan: Merge DomNode with DomElement/DomText/DomComment

## Current Architecture Analysis

### Existing Data Pipeline (HTML/CSS → Layout)

1. **HTML Parsing** (`lambda/input/html/`)
   - Lambda parser → `Element` tree (Lambda's native format)
   - Tree contains elements, text nodes, comments, DOCTYPE, etc.

2. **CSS System Integration** (`lambda/input/css/`)
   - `build_dom_tree_from_element()` converts `Element` → `DomElement` tree
   - Creates separate structures: `DomElement`, `DomText`, `DomComment`
   - Each has `node_type`, parent/child/sibling pointers, style trees
   - `DomElement` has CSS cascade (specified_style, computed_style)

3. **Radiant Wrapper Layer** (`radiant/dom.hpp`)
   - `DomNode` wraps around `DomElement`/`DomText`/`DomComment`
   - Uses union + type enum to hold pointers
   - Provides unified interface via helper methods
   - Caches `_child` and `_next` for traversal

4. **Layout Engine** (`radiant/layout.cpp`)
   - `layout_flow_node()` receives `DomNode*`
   - Traverses via `first_child()` / `next_sibling()`
   - Calls layout functions based on node type
   - Creates `View` hierarchy for rendering

### Current Problems

1. **Double Indirection**: `DomNode` → union → `DomElement*/DomText*/DomComment*`
   - Extra pointer dereference on every access
   - Cache inefficiency

2. **Memory Overhead**:
   - `DomNode` wrapper allocated separately (heap)
   - Each wrapper contains duplicate type info + 3 pointers
   - Cached child/sibling pointers consume memory

3. **Complexity**:
   - Two parallel type systems: `NodeType` enum and `DomNodeType` enum
   - Factory methods: `create_mark_element()`, `create_mark_text()`, `create_mark_comment()`
   - Memory management split: Pool for DOM nodes, malloc for wrappers

4. **Code Duplication**:
   - Sibling navigation logic duplicated in `DomNode::next_sibling()`
   - Type checking spread across multiple functions

## Refactoring Goal

**Merge `DomNode` into the base of `DomElement`/`DomText`/`DomComment` hierarchy.**

Instead of:
```
DomNode (wrapper) → union { DomElement*, DomText*, DomComment* }
```

Use inheritance:
```
DomNode (base struct)
  ├─ DomElement (extends DomNode)
  ├─ DomText (extends DomNode)
  └─ DomComment (extends DomNode)
```

## Design Proposal

### 1. New Base Structure: `DomNode`

**File**: `lambda/input/css/dom_node.h` (new file)

```c
typedef enum DomNodeType {
    DOM_NODE_ELEMENT = 1,     // Element node
    DOM_NODE_TEXT = 3,        // Text node
    DOM_NODE_COMMENT = 8,     // Comment node
    DOM_NODE_DOCTYPE = 10     // DOCTYPE declaration
} DomNodeType;

/**
 * DomNode - Base structure for all DOM nodes
 *
 * This is the common header for DomElement, DomText, and DomComment.
 * All three structures START with these fields in the same order,
 * enabling safe casting and polymorphic behavior.
 */
typedef struct DomNode {
    // Common header fields (must match all derived types)
    DomNodeType node_type;       // Discriminator for node type
    Pool* pool;                  // Memory pool for allocations

    // Tree relationships (using void* for flexibility)
    struct DomNode* parent;      // Parent node (usually DomElement)
    void* first_child;           // First child (DomNode* of any type)
    void* next_sibling;          // Next sibling (DomNode* of any type)
    void* prev_sibling;          // Previous sibling (DomNode* of any type)
} DomNode;
```

### 2. Refactored `DomElement` (Extends `DomNode`)

**File**: `lambda/input/css/dom_element.hpp` (now uses C++ inheritance)

```cpp
struct DomElement : public DomNodeBase {
    // === DomNodeBase inherited fields ===
    // node_type, pool, parent, first_child, next_sibling, prev_sibling

    // === Element-specific fields ===
    Element* native_element;     // Pointer to native Lambda Element
    const char* tag_name;        // Element tag name (cached string)
    void* tag_name_ptr;          // Tag name pointer from name_pool
    const char* id;              // Element ID attribute
    const char** class_names;    // Array of class names
    int class_count;             // Number of classes

    // Style trees (CSS cascade)
    StyleTree* specified_style;  // Specified values from CSS rules
    StyleTree* computed_style;   // Computed values (cached)

    // Version tracking
    uint32_t style_version;      // Incremented when styles change
    bool needs_style_recompute;  // Flag indicating stale computed values

    // Attributes
    AttributeStorage* attributes;  // Hybrid attribute storage

    // Pseudo-class state
    uint32_t pseudo_state;       // Bitmask of pseudo-class states

    // Document reference
    DocumentStyler* document;    // Parent document styler
} DomElement;
```

### 3. Refactored `DomText` (Extends `DomNode`)

```c
typedef struct DomText {
    // === DomNode base fields (MUST be first) ===
    DomNodeType node_type;       // Always DOM_NODE_TEXT
    Pool* pool;                  // Memory pool for allocations
    DomNode* parent;             // Parent element
    void* first_child;           // Always NULL for text nodes
    void* next_sibling;          // Next sibling
    void* prev_sibling;          // Previous sibling

    // === Text-specific fields ===
    const char* text;            // Text content
    size_t length;               // Text length
} DomText;
```

### 4. Refactored `DomComment` (Extends `DomNode`)

```c
typedef struct DomComment {
    // === DomNode base fields (MUST be first) ===
    DomNodeType node_type;       // DOM_NODE_COMMENT or DOM_NODE_DOCTYPE
    Pool* pool;                  // Memory pool for allocations
    DomNode* parent;             // Parent element
    void* first_child;           // Always NULL for comments
    void* next_sibling;          // Next sibling
    void* prev_sibling;          // Previous sibling

    // === Comment-specific fields ===
    const char* tag_name;        // Node name: "!--", "!DOCTYPE"
    const char* content;         // Full content/text
    size_t length;               // Content length
} DomComment;
```

### 5. Polymorphic Operations

**File**: `lambda/input/css/dom_node.c` (new file)

```c
/**
 * Get node type from any DomNode pointer
 */
static inline DomNodeType dom_node_get_type(void* node) {
    if (!node) return (DomNodeType)0;
    return ((DomNode*)node)->node_type;
}

/**
 * Get node name (element tag, "#text", "#comment", etc.)
 */
const char* dom_node_get_name(DomNode* node) {
    if (!node) return "#null";

    switch (node->node_type) {
    case DOM_NODE_ELEMENT:
        return ((DomElement*)node)->tag_name;
    case DOM_NODE_TEXT:
        return "#text";
    case DOM_NODE_COMMENT:
        return "#comment";
    case DOM_NODE_DOCTYPE:
        return "!DOCTYPE";
    default:
        return "#unknown";
    }
}

/**
 * Get first child (unified across all node types)
 */
DomNode* dom_node_first_child(DomNode* node) {
    if (!node) return NULL;

    void* child = node->first_child;

    // Skip comments in traversal if needed
    while (child && dom_node_get_type(child) == DOM_NODE_COMMENT) {
        child = ((DomNode*)child)->next_sibling;
    }

    return (DomNode*)child;
}

/**
 * Get next sibling (unified across all node types)
 */
DomNode* dom_node_next_sibling(DomNode* node) {
    if (!node) return NULL;

    void* sibling = node->next_sibling;

    // Skip comments in traversal if needed
    while (sibling && dom_node_get_type(sibling) == DOM_NODE_COMMENT) {
        sibling = ((DomNode*)sibling)->next_sibling;
    }

    return (DomNode*)sibling;
}

/**
 * Type-safe downcasts with runtime checking
 */
DomElement* dom_node_as_element(DomNode* node) {
    return (node && node->node_type == DOM_NODE_ELEMENT) ? (DomElement*)node : NULL;
}

DomText* dom_node_as_text(DomNode* node) {
    return (node && node->node_type == DOM_NODE_TEXT) ? (DomText*)node : NULL;
}

DomComment* dom_node_as_comment(DomNode* node) {
    return (node && (node->node_type == DOM_NODE_COMMENT ||
                     node->node_type == DOM_NODE_DOCTYPE)) ? (DomComment*)node : NULL;
}
```

## Migration Steps

### Phase 1: Create New Base Structure (No Breaking Changes)

**Week 1: Setup**
1. Create `lambda/input/css/dom_node.h` with new `DomNode` base definition
2. Create `lambda/input/css/dom_node.c` with polymorphic helper functions
3. Add to build system (`build_lambda_config.json`)
4. Run: `make build` to verify compilation

### Phase 2: Refactor DomElement/DomText/DomComment (Breaking Changes)

**Week 1-2: Restructure Types**
1. Update `DomElement` in `dom_element.hpp`:
   - Use C++ inheritance from `DomNodeBase`
   - Remove redundant `node_type`, `pool`, parent/child fields from old positions
   - Update all size calculations and offsets

2. Update `DomText` in `dom_element.hpp`:
   - Use C++ inheritance from `DomNodeBase`
   - Remove redundant fields

3. Update `DomComment` in `dom_element.hpp`:
   - Use C++ inheritance from `DomNodeBase`
   - Remove redundant fields

4. Update all creation functions in `dom_element.cpp`:
   ```cpp
   DomElement* dom_element_create(Pool* pool, const char* tag_name, Element* native) {
       void* mem = pool_calloc(pool, sizeof(DomElement));
       DomElement* elem = new (mem) DomElement();  // Placement new for vtable
       elem->pool = pool;
       // ... rest of initialization
   }
   ```

5. Test compilation: `make build`

### Phase 3: Update Radiant Integration Layer

**Week 2: Eliminate DomNode Wrapper**

1. Update `radiant/dom.hpp`:
   - Remove old `DomNode` wrapper struct
   - Add `using DomNode = ::DomNode;` typedef (C++ compatibility)
   - Remove factory methods: `create_mark_element()`, etc.
   - Remove `free_tree()` and caching logic

2. Update `radiant/dom.cpp`:
   - Remove wrapper allocation code
   - Use direct casting: `DomNode* → DomElement*` etc.
   - Simplify tree traversal (no more wrapper unwrapping)

3. Update `Document` struct in `dom.hpp`:
   ```cpp
   typedef struct {
       Url* url;
       DocumentType doc_type;
       DomNode* root_node;            // Changed: now points directly to DomElement
       Element* lambda_html_root;
       int html_version;
       ViewTree* view_tree;
       StateStore* state;
   } Document;
   ```

### Phase 4: Update Layout Engine

**Week 2-3: Simplify Layout Code**

1. Update `radiant/layout.hpp`:
   - Change all `DomNode*` parameters to use base `DomNode*` type
   - Remove wrapper-specific logic

2. Update `layout_flow_node()` in `layout.cpp`:
   ```cpp
   void layout_flow_node(LayoutContext* lycon, DomNode* node) {
       if (!node) return;

       switch (node->node_type) {  // Direct access, no wrapper
       case DOM_NODE_ELEMENT: {
           DomElement* elem = (DomElement*)node;
           DisplayValue display = resolve_display_value(elem);
           // ... layout logic
           break;
       }
       case DOM_NODE_TEXT: {
           DomText* text = (DomText*)node;
           layout_text(lycon, text);
           break;
       }
       case DOM_NODE_COMMENT:
           // Skip comments
           break;
       }
   }
   ```

3. Update all layout functions:
   - `layout_block()` → accept `DomElement*` directly
   - `layout_text()` → accept `DomText*` directly
   - `layout_inline()` → accept `DomElement*` directly

4. Update tree traversal in layout:
   ```cpp
   // Old way (wrapper):
   DomNode* child = node->first_child();  // Method call, caching

   // New way (direct):
   DomNode* child = dom_node_first_child((DomNode*)elem);  // Direct access
   ```

### Phase 5: Update CMD Layer

**Week 3: Update Commands**

1. Update `radiant/cmd_layout.cpp`:
   - Change `build_dom_tree_from_element()` to return `DomNode*`
   - Remove wrapper creation in tree building
   - Direct construction of `DomElement`/`DomText`/`DomComment`

2. Update `load_lambda_html_doc()`:
   ```cpp
   Document* load_lambda_html_doc(...) {
       // Build DOM tree directly (no wrappers)
       DomNode* dom_root = (DomNode*)build_dom_tree_from_element(...);

       doc->root_node = dom_root;  // Direct assignment
       // ... rest of function
   }
   ```

### Phase 6: Testing & Validation

**Week 3-4: Comprehensive Testing**

1. **Unit Tests**:
   - Test DOM tree construction
   - Test node type checking
   - Test traversal (first_child, next_sibling)
   - Test CSS cascade on refactored DomElement

2. **Integration Tests**:
   ```bash
   make build-test
   make test
   ```

3. **Layout Tests**:
   - Test HTML5 layout: `lambda layout test/input/html5_sample.html`
   - Test CSS cascade: `lambda layout test/input/css_cascade.html -c test/input/styles.css`
   - Compare output with baseline

4. **Performance Benchmarks**:
   - Measure layout time before/after refactoring
   - Track memory usage (should decrease due to removed wrappers)

## Benefits After Refactoring

### Performance Improvements

1. **Reduced Memory**:
   - Eliminate separate `DomNode` wrapper allocations
   - Remove cached `_child` and `_next` pointers
   - Estimate: 24-32 bytes saved per node

2. **Fewer Pointer Dereferences**:
   - Direct access to node data (no union indirection)
   - Better CPU cache locality

3. **Simplified Memory Management**:
   - All nodes allocated from same Pool
   - No mixed malloc/pool allocation

### Code Quality Improvements

1. **Cleaner API**:
   - Single type hierarchy (no wrapper)
   - Consistent casting: `(DomElement*)node`
   - Standard C struct inheritance pattern

2. **Less Code**:
   - Remove ~200 lines of wrapper code
   - Remove factory methods
   - Remove caching logic

3. **Easier Debugging**:
   - Direct node pointers in debugger
   - No union unwrapping confusion

## Risk Assessment

### Medium Risk Items

1. **C++ Compatibility**:
   - Risk: C++ code uses C structs, may have alignment issues
   - Mitigation: Use `#pragma pack` if needed, test thoroughly

2. **Pointer Casting**:
   - Risk: Incorrect casts may cause crashes
   - Mitigation: Use type-safe helper functions, add assertions

3. **Performance Regression**:
   - Risk: Changes may slow down layout unexpectedly
   - Mitigation: Run benchmarks before/after, profile hotspots

### Low Risk Items

1. **Build System**: Only need to add new files, no complex changes
2. **Tree Structure**: Parent/child relationships remain the same
3. **CSS Cascade**: No changes to StyleTree or cascade logic

## Timeline

- **Week 1**: Phase 1 + Phase 2 (base structure + type refactoring)
- **Week 2**: Phase 3 + Phase 4 (Radiant integration + layout updates)
- **Week 3**: Phase 5 + Phase 6 (CMD layer + testing)
- **Week 4**: Buffer for fixes, documentation, final validation

**Total Estimated Time**: 3-4 weeks

## Success Criteria

1. ✅ All tests pass (`make test`)
2. ✅ Layout output matches baseline (pixel-perfect)
3. ✅ No memory leaks (run with valgrind)
4. ✅ Performance equal or better than before
5. ✅ Code is simpler and more maintainable

## Future Enhancements (Post-Refactoring)

1. **Virtual Function Table**:
   - Add function pointers to `DomNode` for polymorphic behavior
   - Enable extensibility for custom node types

2. **Visitor Pattern**:
   - Implement DOM tree visitor for traversal operations
   - Simplify bulk operations (printing, validation, etc.)

3. **Smart Pointers** (C++):
   - Add reference counting for automatic memory management
   - Enable safer node manipulation

---

**Document Status**: Draft v1.0
**Author**: GitHub Copilot
**Date**: 2025-01-07
**Review**: Pending user approval before implementation
