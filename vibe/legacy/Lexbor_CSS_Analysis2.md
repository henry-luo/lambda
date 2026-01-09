# Lexbor CSS Style Handling: AVL Tree Architecture

## Overview

Lexbor uses an AVL (Adelson-Velsky and Landis) tree to store CSS property declarations for each DOM element. This document analyzes how Lexbor builds and manages these style trees, along with the architectural limitations.

---

## 1. Core Data Structures

### 1.1 `lxb_style_node_t` - AVL Tree Node

```c
typedef struct {
    lexbor_avl_node_t              entry;        // AVL tree structure (left, right, parent)
    lxb_style_weak_t               *weak;        // Linked list of overridden declarations
    lxb_css_selector_specificity_t sp;           // Specificity of winning declaration
} lxb_style_node_t;
```

**Fields:**
- **`entry.type`**: CSS Property ID (e.g., `LXB_CSS_PROPERTY_COLOR`, `LXB_CSS_PROPERTY_FONT_WEIGHT`)
- **`entry.value`**: Pointer to `lxb_css_rule_declaration_t` (the winning CSS declaration after cascade)
- **`sp`**: Specificity value used for CSS cascade resolution (format: `0xAABBCC` where AA=ID, BB=class, CC=element)
- **`weak`**: Linked list of lower-specificity declarations preserved for debugging/devtools

### 1.2 `lxb_style_weak_t` - Overridden Declarations

```c
struct lxb_style_weak {
    void                           *value;       // Pointer to declaration
    lxb_css_selector_specificity_t sp;           // Specificity of this declaration
    lxb_style_weak_t               *next;        // Next in linked list
};
```

Stores CSS declarations that were overridden by higher-specificity rules but kept for reference.

### 1.3 Document-Level Style Storage

```c
typedef struct {
    lxb_css_memory_t      *memory;       // Memory allocator
    lxb_css_parser_t      *parser;       // CSS parser
    lexbor_avl_t          *styles;       // Global AVL tree pool
    lexbor_array_t        *stylesheets;  // Array of attached stylesheets
    // ... other fields
} lxb_dom_document_css_t;
```

**Key insight:** Lexbor uses a **shared global AVL tree pool** (`styles`) for all elements in the document. Each element's `element->style` is merely a **root pointer** into this shared pool.

---

## 2. AVL Tree Construction Flow

### 2.1 Initialization

```c
lxb_dom_document_css_init(document)
├─ css->styles = lexbor_avl_create()
├─ lexbor_avl_init(css->styles, 2048, sizeof(lxb_style_node_t))
└─ css->stylesheets = lexbor_array_create()
```

- Creates a **single shared AVL tree pool** for entire document
- Each element will have its own subtree within this pool
- Initial pool size: 2048 nodes

### 2.2 Stylesheet Application

```
lxb_dom_document_stylesheet_attach(document, stylesheet)
│
├─ Parse stylesheet into CSS rules
│
└─ lxb_dom_document_stylesheet_apply(document, stylesheet)
    │
    └─ For each CSS rule in stylesheet:
        │
        └─ lxb_dom_document_style_attach(document, rule)
            │
            └─ lxb_selectors_find(selector, callback)
                │
                └─ [Traverses entire DOM tree from root]
                    │
                    └─ For each element node:
                        │
                        ├─ lxb_selectors_state_run(node, selector)
                        │   └─ Check if selector matches this node
                        │
                        └─ If matches:
                            │
                            └─ lxb_dom_document_style_attach_cb(node, specificity, rule)
                                │
                                └─ lxb_dom_element_style_list_append(element, declarations, specificity)
                                    │
                                    └─ For each declaration in rule:
                                        │
                                        └─ lxb_dom_element_style_append(element, declaration, specificity)
```

**Key Points:**
1. Stylesheets are applied **immediately** when attached
2. Selector matching happens during stylesheet application
3. **Lexbor traverses the ENTIRE DOM tree for EACH CSS rule**
4. Only elements that **match the selector** get declarations added to their AVL subtree
5. Cascade resolution happens **inline** during insertion

### 2.3 Example: `body { font-family: Arial; }` Application

**Question:** Does this rule apply only to `<body>` or to all descendant elements?

**Answer:** Lexbor applies styles **ONLY to elements that match the selector**.

**Process:**
```
1. Parse CSS rule: "body { font-family: Arial; }"
   - Selector: body (element selector)
   - Declaration: font-family: Arial
   - Specificity: 0x000001

2. lxb_selectors_find() traverses entire DOM tree:
   <html>
     <body>               ← Matches "body" selector
       <div>              ← Does NOT match "body" selector
         <span>Text</span> ← Does NOT match "body" selector
       </div>
     </body>
   </html>

3. Callback invoked ONLY for <body> element:
   lxb_dom_document_style_attach_cb(<body>, 0x000001, rule)
   └─ Adds font-family: Arial to <body>'s AVL tree

4. Result:
   - <body>: font-family = Arial (stored in AVL tree)
   - <div>: NO font-family in AVL tree
   - <span>: NO font-family in AVL tree
```

**Key Insight:**
- The rule is applied **ONLY to the `<body>` element itself**
- Descendant elements (`<div>`, `<span>`) do **NOT** get `font-family` in their AVL trees
- Font inheritance happens **later during layout**, not during style application

**How Descendants Get Font Properties:**

During layout phase:
```c
// Layout <body>
body->font->font_family = lookup_from_avl_tree(body, "font-family")
                        = "Arial"  // Found in body's AVL tree

// Layout <div> (child of body)
div->font = alloc_font_prop(lycon)
          = copy of parent (body->font)
          = {font_family: "Arial", ...}  // Inherited via struct copy

// No font-family in div's AVL tree, so keeps inherited value
```

**This is why Lexbor's inheritance is "implicit":**
- CSS rules apply only to matching elements
- Descendants inherit during layout via `*prop = *lycon->font.style`
- No explicit "walk parent chain to find inherited property"

### 2.4 Core Function: `lxb_dom_element_style_append()`

This is the **heart** of Lexbor's style system. It handles:
- AVL tree insertion
- Cascade resolution (specificity comparison)
- Overridden declaration management

```c
lxb_status_t
lxb_dom_element_style_append(lxb_dom_element_t *element,
                             lxb_css_rule_declaration_t *declr,
                             lxb_css_selector_specificity_t spec)
{
    uintptr_t id = declr->type;  // CSS Property ID
    lxb_dom_document_css_t *css = doc->css;

    // STEP 1: Search if property already exists in element's AVL tree
    node = lexbor_avl_search(css->styles, element->style, id);

    if (node != NULL) {
        // Property exists - perform cascade comparison

        if (spec < node->sp) {
            // New declaration has LOWER specificity
            // Add to "weak" list (keep for debugging)
            return lxb_dom_element_style_weak_append(doc, node, declr, spec);
        }

        // New declaration WINS
        // Move old winner to weak list
        lxb_dom_element_style_weak_append(doc, node, node->entry.value, node->sp);

        // Update with new winner
        node->entry.value = declr;
        node->sp = spec;

        return LXB_STATUS_OK;
    }

    // STEP 2: Property doesn't exist - insert new AVL node
    node = lexbor_avl_insert(css->styles, &element->style, id, declr);
    node->sp = spec;

    return LXB_STATUS_OK;
}
```

**Algorithm Complexity:**
- Search: O(log n) where n = properties on element
- Insert: O(log n) with AVL rebalancing
- Cascade comparison: O(1)

---

## 3. AVL Tree Structure Per Element

### 3.1 Tree Organization

Each element's style tree is organized as:

```
element->style (root pointer into shared pool)
    │
    ├─── AVL Node: CSS_PROPERTY_COLOR (0x0050)
    │       ├─ value: red_declaration
    │       ├─ sp: 0x010000 (ID selector)
    │       ├─ weak → blue_declaration (sp=0x000100) → green_declaration (sp=0x000001) → NULL
    │       ├─ left  → Node: CSS_PROPERTY_BACKGROUND_COLOR
    │       └─ right → Node: CSS_PROPERTY_FONT_SIZE
    │
    ├─── AVL Node: CSS_PROPERTY_FONT_WEIGHT (0x0082)
    │       ├─ value: bold_declaration
    │       ├─ sp: 0x000100 (class selector)
    │       ├─ weak → NULL
    │       └─ ...
    │
    └─── AVL Node: CSS_PROPERTY_MARGIN_TOP (0x00A1)
            └─ ...
```

**Balancing:** Tree is balanced by **property ID** (not specificity), ensuring O(log n) lookup performance.

### 3.2 Example: Multiple Rules

**CSS:**
```css
div { color: green; }         /* sp = 0x000001 */
.highlight { color: blue; }   /* sp = 0x000100 */
#special { color: red; }      /* sp = 0x010000 */
```

**Processing for `<div id="special" class="highlight">`:**

1. **Rule 1** applied:
   ```
   COLOR node: value=green, sp=0x000001, weak=NULL
   ```

2. **Rule 2** applied (higher specificity):
   ```
   COLOR node: value=blue, sp=0x000100, weak=[green:0x000001]
   ```

3. **Rule 3** applied (highest specificity):
   ```
   COLOR node: value=red, sp=0x010000, weak=[blue:0x000100, green:0x000001]
   ```

**Final Result:**
- **Winning value:** `red` (used for rendering)
- **Weak list:** `[blue, green]` (available for debugging)

---

## 4. Property Lookup

### 4.1 Retrieval Functions

```c
const lxb_css_rule_declaration_t *
lxb_dom_element_style_by_id(const lxb_dom_element_t *element, uintptr_t id)
{
    const lxb_style_node_t *node;

    // O(log n) search in element's AVL subtree
    node = lexbor_avl_search(doc->css->styles, element->style, id);

    return (node != NULL) ? node->entry.value : NULL;
}
```

### 4.2 Usage in Layout Phase

```c
// Example: Get font-weight property
const lxb_css_rule_declaration_t *declr =
    lxb_dom_element_style_by_id(element, LXB_CSS_PROPERTY_FONT_WEIGHT);

if (declr) {
    lxb_css_property_font_weight_t *font_weight = declr->u.font_weight;
    span->font->font_weight = font_weight->type;
}
```

---

## 5. CSS Inheritance Mechanism

### 5.1 Implicit Inheritance via Layout Context

Lexbor handles CSS inheritance **implicitly** during the layout phase, not during style resolution:

```c
FontProp* alloc_font_prop(LayoutContext* lycon) {
    FontProp* prop = (FontProp*)alloc_prop(lycon, sizeof(FontProp));

    // INHERITANCE HAPPENS HERE: Copy parent's font properties
    *prop = *lycon->font.style;

    return prop;
}
```

**Flow:**
```
<div style="font-size: 16px; color: blue;">
    <span style="font-weight: bold;">Text</span>
</div>

Layout Process:
1. Layout <div>:
   - Process div's AVL tree → font-size: 16px, color: blue
   - Set lycon->font.style = {font_size: 16px, color: blue, ...}

2. Layout <span>:
   - Create span->font = alloc_font_prop(lycon)
     → Copies: {font_size: 16px, color: blue, font_weight: normal}
   - Process span's AVL tree → font-weight: bold
   - Override: span->font->font_weight = bold
   - Result: {font_size: 16px, color: blue, font_weight: bold}
```

### 5.2 How It Works

1. **LayoutContext carries parent state:**
   ```c
   struct LayoutContext {
       FontState font;      // Current font properties (inherited)
       // ... other state
   };
   ```

2. **Property allocation copies parent:**
   - When a child element needs font properties
   - If not in its AVL tree, copies from `lycon->font.style`
   - This achieves inheritance without walking the DOM tree

3. **Property updates override:**
   - If child has property in AVL tree, it overrides inherited value
   - No explicit "inherit" keyword handling needed

---

## 6. Architecture Limitations

### 6.1 Memory Management Issues

**Problem:** Shared global pool can lead to fragmentation
- All elements share one AVL tree pool
- Deleted elements leave holes in the pool
- No easy way to compact memory

**Impact:** Long-lived documents with dynamic content may have poor memory locality

### 6.2 Limited Inheritance Support

**Problem:** Inheritance is implicit and limited to specific property types

**Limitations:**

1. **Only works for properties in specific structs:**
   ```c
   struct FontProp {
       float font_size;
       int font_weight;
       int font_style;
       // ... only these fields inherit automatically
   };
   ```

2. **No metadata system:**
   - Cannot query "is this property inheritable?"
   - No `lxb_css_property_is_inherited(property_id)` function
   - Hard-coded behavior in layout code

3. **Missing CSS properties:**
   - `line-height` - NOT in `FontProp`, doesn't inherit automatically
   - `text-align` - In different struct, different inheritance path
   - `visibility` - No inheritance mechanism
   - Custom properties (`--var`) - Cannot inherit

4. **Wrong timing:**
   - Inheritance happens during **layout phase** (too late)
   - Should happen during **style resolution phase** (before layout)
   - Cannot use inherited values for cascade decisions

### 6.3 Cascade Limitations

**Problem:** Inline cascade resolution during insertion

**Limitations:**

1. **No separation of concerns:**
   - Style application and cascade happen together
   - Hard to debug or modify cascade algorithm
   - Cannot easily implement CSS cascade layers

2. **Weak list overhead:**
   - Keeps all overridden declarations in memory
   - Good for devtools, bad for performance
   - No way to disable for production

3. **Order dependency:**
   - Stylesheet application order matters
   - Cannot easily re-apply stylesheets
   - Dynamic style changes are expensive

### 6.4 Property Value Representation

**Problem:** No normalized/computed value storage

**Limitations:**

1. **Raw CSS values stored:**
   ```c
   // Lexbor stores:
   font_weight->type = CSS_VALUE_BOLD  // Enum value

   // But needs mapping:
   int weight = (font_weight->type == CSS_VALUE_BOLD) ? 700 : 400;
   ```

2. **Conversion happens at use site:**
   - Every time you read a property, must convert
   - No caching of computed values
   - Repeated work during layout

3. **Unit conversions:**
   ```c
   // If CSS has: font-size: 1.5em
   // Must compute at use time:
   float size = parent_size * 1.5;
   ```

### 6.5 Selector Matching Performance

**Problem:** Full document traversal for each stylesheet

**Limitations:**

1. **No selector indexing:**
   - Must check every element against every selector
   - O(N × M) where N=elements, M=selectors
   - No optimization for common patterns

2. **Repeated work:**
   - Same selector matched multiple times
   - No memoization of selector results
   - Dynamic elements force re-matching

### 6.6 Missing CSS Features

**Problem:** Architecture doesn't support modern CSS

**Limitations:**

1. **No CSS Variables:**
   - `var()` function not handled in property values
   - Custom properties (`--name`) cannot be stored in AVL tree
   - No variable resolution mechanism

2. **No Cascade Layers:**
   - `@layer` rules not supported
   - Single specificity value per property
   - Cannot implement layered cascade

3. **No Container Queries:**
   - Style based on parent size requires re-computation
   - AVL tree is per-element, not per-context
   - Would need multiple style trees per element

4. **No CSS Scoping:**
   - `@scope` rules not supported
   - Global namespace only
   - Cannot implement shadow DOM styles

### 6.7 Threading and Concurrency

**Problem:** Single global AVL pool

**Limitations:**

1. **Not thread-safe:**
   - Shared mutable state
   - Would need locking for concurrent access
   - Hard to parallelize layout

2. **No work-stealing:**
   - Cannot split document across threads
   - Layout must be sequential
   - Poor utilization of multi-core CPUs

---

## 7. Performance Characteristics

### 7.1 Time Complexity

| Operation | Complexity | Notes |
|-----------|------------|-------|
| Insert property | O(log n) | n = properties on element |
| Lookup property | O(log n) | AVL tree search |
| Apply stylesheet | O(E × S) | E = elements, S = selectors |
| Cascade resolution | O(1) | Per property, during insert |
| Inheritance | O(1) | Struct copy during layout |

### 7.2 Space Complexity

| Component | Space | Notes |
|-----------|-------|-------|
| AVL node | ~48 bytes | Node + weak list overhead |
| Per element | O(p) | p = number of styled properties |
| Weak declarations | O(r × p) | r = average rules per property |
| Global pool | O(E × p) | E = elements, p = avg properties |

### 7.3 Bottlenecks

1. **Stylesheet application:**
   - Must traverse entire DOM for each stylesheet
   - Selector matching is expensive
   - No incremental updates

2. **Memory fragmentation:**
   - Shared pool with no compaction
   - Deleted nodes leave holes
   - Poor cache locality over time

3. **Property lookup:**
   - Log(n) per property access
   - No caching of frequently-used properties
   - Repeated lookups during layout

---

## 8. Comparison with Lambda CSS

### 8.1 Architecture Differences

| Aspect | Lexbor | Lambda CSS |
|--------|--------|------------|
| **Storage** | Shared global AVL pool | Per-element `StyleTree` |
| **Tree Type** | AVL tree (balanced) | AVL tree (balanced) |
| **Cascade** | Inline during insertion | Separate cascade phase |
| **Inheritance** | Implicit via layout context | Explicit DOM tree walk |
| **Property Lookup** | O(log n) | O(log n) |
| **Metadata** | None | Property inheritance flags |
| **Computed Values** | None | Can store computed values |

### 8.2 Lambda CSS Advantages

1. **Explicit inheritance:**
   ```cpp
   // Can query metadata
   if (css_property_is_inherited(property_id)) {
       // Walk parent chain and inherit
   }
   ```

2. **Separation of concerns:**
   - CSS parsing → StyleTree
   - Cascade resolution → StyleTree
   - Style resolution → Layout properties
   - Layout → View tree

3. **Extensibility:**
   - Easy to add new properties
   - Metadata-driven behavior
   - Can implement new CSS features

4. **Correct CSS timing:**
   - Inheritance during style phase
   - Can use inherited values for cascade
   - Follows CSS specification

### 8.3 Lexbor Advantages

1. **Performance:**
   - Single struct copy for inheritance
   - No DOM tree walk needed
   - Faster for simple cases

2. **Memory efficiency:**
   - Shared pool reduces allocation overhead
   - No duplicate tree structures
   - Compact representation

3. **Simplicity:**
   - Less code
   - Easier to understand
   - Fewer moving parts

---

## 9. Recommendations for Improvement

### 9.1 Short-term Fixes

1. **Add property metadata system:**
   ```c
   bool lxb_css_property_is_inherited(uintptr_t property_id);
   const char* lxb_css_property_name(uintptr_t property_id);
   ```

2. **Implement explicit inheritance:**
   - Walk parent chain during style resolution
   - Copy inherited properties before layout
   - Correct CSS spec compliance

3. **Cache computed values:**
   ```c
   struct lxb_style_computed {
       float font_size_px;      // Already computed
       int font_weight_numeric;  // Mapped from enum
       // ... other computed values
   };
   ```

### 9.2 Long-term Refactoring

1. **Separate style and layout phases:**
   - Style resolution produces computed values
   - Layout uses computed values only
   - Clear phase boundaries

2. **Per-element AVL trees:**
   - Each element owns its style tree
   - Easier memory management
   - Better cache locality

3. **Selector indexing:**
   - Build selector index on stylesheet parse
   - Use Bloom filters for quick rejection
   - Incremental style updates

4. **Thread-safe design:**
   - Per-thread style pools
   - Read-only sharing of stylesheets
   - Parallel layout where possible

---

## 10. Conclusion

Lexbor's AVL tree architecture is **efficient for simple cases** but has **significant limitations**:

### Strengths:
✅ Fast property lookup (O(log n))
✅ Efficient cascade resolution
✅ Low overhead for basic styling
✅ Simple implementation

### Weaknesses:
❌ Limited CSS inheritance support
❌ No property metadata system
❌ Tight coupling between style and layout
❌ Cannot support modern CSS features
❌ Memory fragmentation issues
❌ Not thread-safe

**For Lambda Script:** The explicit inheritance approach in `lambda_css_resolve.cpp` is **more correct** and should be preferred, despite being slightly slower. It provides:
- Full CSS specification compliance
- Extensibility for new CSS features
- Clear separation of concerns
- Better debugging capabilities

The performance cost is acceptable given the flexibility and correctness gains.
