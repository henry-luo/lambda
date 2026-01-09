# Lexbor CSS Parsing and Style Tree Analysis

## Overview

Lexbor implements a complete CSS parsing and styling system that processes CSS stylesheets and builds AVL trees to store computed styles for each DOM element. This document analyzes the architecture and flow of CSS rule parsing and style tree construction.

## Architecture Components

### 1. Core Data Structures

#### AVL Tree Node (`lexbor_avl_node_t`)
Located in: `lexbor/source/lexbor/core/avl.h`

```c
struct lexbor_avl_node {
    size_t            type;        // Property ID (e.g., LXB_CSS_PROPERTY_WIDTH)
    short             height;      // AVL tree balance height
    void              *value;      // Pointer to lxb_css_rule_declaration_t

    lexbor_avl_node_t *left;      // Left child
    lexbor_avl_node_t *right;     // Right child
    lexbor_avl_node_t *parent;    // Parent node
};
```

The AVL tree is a self-balancing binary search tree where:
- **Key**: CSS property ID (type field)
- **Value**: CSS declaration (the actual property value and metadata)
- **Purpose**: Fast O(log n) lookup of computed styles by property ID

#### Style Node (`lxb_style_node_t`)
Located in: `lexbor/source/lexbor/style/base.h`

```c
typedef struct {
    lexbor_avl_node_t              entry;      // AVL tree node
    lxb_style_weak_t               *weak;      // Linked list of weaker specificity styles
    lxb_css_selector_specificity_t sp;         // Current specificity
} lxb_style_node_t;
```

This extends the AVL node with CSS cascade support:
- **entry**: Base AVL tree node containing the winning declaration
- **weak**: Linked list of declarations with lower specificity (for cascade fallback)
- **sp**: Specificity of the current winning declaration

#### Weak Style (`lxb_style_weak_t`)
Located in: `lexbor/source/lexbor/style/base.h`

```c
struct lxb_style_weak {
    void                           *value;     // lxb_css_rule_declaration_t pointer
    lxb_css_selector_specificity_t sp;         // Specificity
    lxb_style_weak_t               *next;      // Next in linked list
};
```

Maintains a sorted linked list of declarations that lost the cascade but may be needed for:
- Dynamic style updates
- Property inheritance
- Cascade re-evaluation

### 2. Document CSS Structure

#### Document CSS Context (`lxb_dom_document_css_t`)
Located in: `lexbor/source/lexbor/style/dom/interfaces/document.h`

```c
struct lxb_dom_document_css {
    lxb_css_memory_t    *memory;          // CSS-specific memory allocator
    lxb_css_selectors_t *css_selectors;   // CSS selector parser
    lxb_css_parser_t    *parser;          // CSS parser
    lxb_selectors_t     *selectors;       // Selector matcher

    lexbor_avl_t        *styles;          // Global AVL tree for all styles
    lexbor_array_t      *stylesheets;     // Array of stylesheets
    lexbor_dobject_t    *weak;            // Memory pool for weak styles

    lexbor_hash_t       *customs;         // Custom property name → ID mapping
    uintptr_t           customs_id;       // Next custom property ID
};
```

This is the central styling context attached to each document, managing:
- **styles**: Global AVL tree allocator (all elements share this)
- **stylesheets**: All loaded stylesheets
- **parser**: Reusable CSS parser instance
- **selectors**: Selector matching engine

### 3. CSS Rules

#### CSS Rule Hierarchy
Located in: `lexbor/source/lexbor/css/rule.h`

```c
lxb_css_stylesheet_t
    └── lxb_css_rule_list_t (root)
         └── lxb_css_rule_style_t (for each style rule)
              ├── selector: lxb_css_selector_list_t
              └── declarations: lxb_css_rule_declaration_list_t
                   └── lxb_css_rule_declaration_t (for each property)
                        ├── type: property ID
                        ├── u.user: parsed property value
                        └── important: !important flag
```

**Key types:**
- `LXB_CSS_RULE_STYLESHEET`: Top-level stylesheet
- `LXB_CSS_RULE_LIST`: Container for rules
- `LXB_CSS_RULE_STYLE`: Style rule (selector + declarations)
- `LXB_CSS_RULE_DECLARATION`: Single CSS property declaration

## CSS Parsing Flow

### Phase 1: Stylesheet Parsing

**Entry point**: `lxb_css_stylesheet_parse()`
Located in: `lexbor/source/lexbor/css/stylesheet.c`

```
Input: CSS text
    ↓
1. Tokenization (lexbor/css/syntax/tokenizer.c)
   - Breaks CSS into tokens: identifiers, strings, numbers, etc.
    ↓
2. Syntax Parsing (lexbor/css/parser.c)
   - Builds syntax tree following CSS grammar
   - Creates rule structures
    ↓
3. Selector Parsing (lexbor/css/selectors/)
   - Parses CSS selectors into selector trees
   - Computes selector specificity
    ↓
4. Property Parsing (lexbor/css/property/)
   - Parses property values (colors, lengths, keywords)
   - Validates values against property definitions
    ↓
Output: lxb_css_stylesheet_t with parsed rule tree
```

**Key function**: `lxb_css_parser_init()`
```c
lxb_status_t
lxb_css_parser_init(lxb_css_parser_t *parser, lxb_css_syntax_tokenizer_t *tkz)
{
    // Initialize parsing state stack (1024 entries)
    parser->states_begin = malloc(sizeof(lxb_css_parser_state_t) * 1024);

    // Initialize syntax rules stack (128 entries)
    parser->rules_begin = malloc(sizeof(lxb_css_syntax_rule_t) * 128);

    // Create or reuse tokenizer
    if (tkz == NULL) {
        tkz = lxb_css_syntax_tokenizer_create();
        parser->my_tkz = true;
    }

    parser->tkz = tkz;
    // ... initialize other components
}
```

### Phase 2: Stylesheet Attachment

**Entry point**: `lxb_dom_document_stylesheet_attach()`
Located in: `lexbor/source/lexbor/style/dom/interfaces/document.c`

```c
lxb_status_t
lxb_dom_document_stylesheet_attach(lxb_dom_document_t *document,
                                   lxb_css_stylesheet_t *sst)
{
    // 1. Add stylesheet to document's stylesheet array
    status = lexbor_array_push(document->css->stylesheets, sst);

    // 2. Apply stylesheet to existing DOM
    return lxb_dom_document_stylesheet_apply(document, sst);
}
```

**Apply stylesheet to DOM**: `lxb_dom_document_stylesheet_apply()`
```c
lxb_status_t
lxb_dom_document_stylesheet_apply(lxb_dom_document_t *document,
                                  lxb_css_stylesheet_t *sst)
{
    lxb_css_rule_t *rule = sst->root;
    lxb_css_rule_list_t *list = lxb_css_rule_list(rule);
    rule = list->first;

    // Iterate through all style rules
    while (rule != NULL) {
        switch (rule->type) {
            case LXB_CSS_RULE_STYLE:
                // Attach style rule to matching elements
                status = lxb_dom_document_style_attach(document,
                                                       lxb_css_rule_style(rule));
                break;
            // ... handle other rule types
        }
        rule = rule->next;
    }

    return LXB_STATUS_OK;
}
```

### Phase 3: Selector Matching

**Entry point**: `lxb_dom_document_style_attach()`
Located in: `lexbor/source/lexbor/style/dom/interfaces/document.c`

```c
lxb_status_t
lxb_dom_document_style_attach(lxb_dom_document_t *document,
                              lxb_css_rule_style_t *style)
{
    lxb_dom_document_css_t *css = document->css;

    // Find all elements matching the selector
    return lxb_selectors_find(css->selectors,           // Selector engine
                              lxb_dom_interface_node(document), // Search root
                              style->selector,          // CSS selector
                              lxb_dom_document_style_attach_cb,  // Callback
                              style);                   // Callback context
}
```

The selector matcher (`lxb_selectors_find`) traverses the DOM tree and calls the callback for each matching element:

```c
static lxb_status_t
lxb_dom_document_style_attach_cb(lxb_dom_node_t *node,
                                 lxb_css_selector_specificity_t spec, void *ctx)
{
    lxb_css_rule_style_t *style = ctx;

    // Apply all declarations from this style rule to the element
    return lxb_dom_element_style_list_append(lxb_dom_interface_element(node),
                                             style->declarations, spec);
}
```

## Style AVL Tree Construction

### Building Element Style Trees

**Entry point**: `lxb_dom_element_style_append()`
Located in: `lexbor/source/lexbor/style/dom/interfaces/element.c`

This is where the magic happens - styles are inserted into the per-element AVL tree:

```c
lxb_status_t
lxb_dom_element_style_append(lxb_dom_element_t *element,
                             lxb_css_rule_declaration_t *declr,
                             lxb_css_selector_specificity_t spec)
{
    lxb_dom_document_t *doc = element->owner_document;
    lxb_dom_document_css_t *css = doc->css;

    // 1. Get property ID
    uintptr_t id = declr->type;

    // 2. Handle !important flag in specificity
    lxb_css_selector_sp_set_i(spec, declr->important);

    // 3. Handle custom properties
    if (id == LXB_CSS_PROPERTY__CUSTOM) {
        lexbor_str_t *name = &declr->u.custom->name;
        id = lxb_dom_document_css_customs_id(doc, name->data, name->length);
    }

    // 4. Search for existing node in element's AVL tree
    lxb_style_node_t *node = (void *) lexbor_avl_search(css->styles,
                                                         element->style, id);

    if (node != NULL) {
        // Property already exists in element's tree
        if (spec < node->sp) {
            // New declaration has lower specificity - add to weak list
            return lxb_dom_element_style_weak_append(doc, node, declr, spec);
        }

        // New declaration wins - demote current to weak list
        status = lxb_dom_element_style_weak_append(doc, node,
                                                   node->entry.value, node->sp);

        // Replace with new declaration
        lxb_css_rule_ref_dec(node->entry.value);
        node->entry.value = declr;
        node->sp = spec;

        return LXB_STATUS_OK;
    }

    // 5. Property doesn't exist - insert new node into AVL tree
    node = (void *) lexbor_avl_insert(css->styles, &element->style, id, declr);
    node->sp = spec;

    // 6. Increment reference count (declarations are shared)
    return lxb_css_rule_ref_inc(lxb_css_rule(declr));
}
```

### AVL Tree Insertion Algorithm

**Entry point**: `lexbor_avl_insert()`
Located in: `lexbor/source/lexbor/core/avl.c`

```c
lexbor_avl_node_t *
lexbor_avl_insert(lexbor_avl_t *avl, lexbor_avl_node_t **scope,
                  size_t type, void *value)
{
    lexbor_avl_node_t *node;

    // 1. Create new node
    node = lexbor_avl_node_make(avl, type, value);

    if (*scope == NULL) {
        // Empty tree - node becomes root
        *scope = node;
        return node;
    }

    // 2. Standard BST insertion
    lexbor_avl_node_t *current = *scope;
    lexbor_avl_node_t *parent = NULL;

    while (current != NULL) {
        parent = current;

        if (type < current->type) {
            current = current->left;
        } else if (type > current->type) {
            current = current->right;
        } else {
            // Node already exists
            return current;
        }
    }

    // Attach node to parent
    node->parent = parent;
    if (type < parent->type) {
        parent->left = node;
    } else {
        parent->right = node;
    }

    // 3. Rebalance tree up to root
    *scope = lexbor_avl_node_balance(node, scope);

    return node;
}
```

**Tree balancing**: `lexbor_avl_node_balance()`
```c
static lexbor_avl_node_t *
lexbor_avl_node_balance(lexbor_avl_node_t *node, lexbor_avl_node_t **scope)
{
    // Update heights up the tree
    while (node != NULL) {
        lexbor_avl_node_set_height(node);

        short bf = lexbor_avl_node_balance_factor(node);

        if (bf > 1) {
            // Right-heavy
            if (lexbor_avl_node_balance_factor(node->right) < 0) {
                // Right-Left case
                node->right = lexbor_avl_node_rotate_right(node->right);
            }
            // Right-Right case
            node = lexbor_avl_node_rotate_left(node);
        }
        else if (bf < -1) {
            // Left-heavy
            if (lexbor_avl_node_balance_factor(node->left) > 0) {
                // Left-Right case
                node->left = lexbor_avl_node_rotate_left(node->left);
            }
            // Left-Left case
            node = lexbor_avl_node_rotate_right(node);
        }

        // Update root if necessary
        if (node->parent == NULL) {
            *scope = node;
        }

        node = node->parent;
    }

    return *scope;
}
```

### Weak Style List Management

**Entry point**: `lxb_dom_element_style_weak_append()`

This maintains a sorted linked list of losing declarations:

```c
lxb_status_t
lxb_dom_element_style_weak_append(lxb_dom_document_t *doc,
                                  lxb_style_node_t *node,
                                  lxb_css_rule_declaration_t *declr,
                                  lxb_css_selector_specificity_t spec)
{
    // 1. Allocate new weak node from pool
    lxb_style_weak_t *new_weak = lexbor_dobject_alloc(doc->css->weak);
    new_weak->value = declr;
    new_weak->sp = spec;

    // 2. Insert in specificity-sorted order
    if (node->weak == NULL) {
        // First weak entry
        node->weak = new_weak;
        new_weak->next = NULL;
    }
    else if (node->weak->sp <= spec) {
        // Insert at head
        new_weak->next = node->weak;
        node->weak = new_weak;
    }
    else {
        // Insert in middle/end (sorted by specificity descending)
        lxb_style_weak_t *prev = node->weak;
        lxb_style_weak_t *weak = weak->next;

        while (weak != NULL) {
            if (weak->sp <= spec) {
                prev->next = new_weak;
                new_weak->next = weak;
                goto done;
            }
            prev = weak;
            weak = weak->next;
        }

        // Append at end
        prev->next = new_weak;
        new_weak->next = NULL;
    }

done:
    // 3. Increment reference count
    return lxb_css_rule_ref_inc(lxb_css_rule(declr));
}
```

## Complete Flow Example

### Example: Parsing `<style>div { color: red; }</style>`

```
1. HTML Parser creates <style> element
   ↓
2. Style element triggers stylesheet creation
   └─→ lxb_css_stylesheet_parse("div { color: red; }")
       ↓
       ├─→ Tokenizer: [IDENT(div), LBRACE, IDENT(color), COLON, IDENT(red), SEMICOLON, RBRACE]
       ├─→ Parser: Creates lxb_css_rule_style_t
       │   ├─→ selector: "div" (specificity: 0,0,1)
       │   └─→ declarations:
       │       └─→ type: LXB_CSS_PROPERTY_COLOR
       │           value: parsed red color
       ↓
3. Stylesheet attached to document
   └─→ lxb_dom_document_stylesheet_attach(doc, stylesheet)
       ↓
4. Selector matching
   └─→ lxb_selectors_find(doc, "div", callback, rule)
       ├─→ Finds <div id="a"> element
       └─→ Calls callback for each match
           ↓
5. Style tree construction
   └─→ For each <div> element:
       └─→ lxb_dom_element_style_append(element, color_declaration, spec=0,0,1)
           ↓
           ├─→ Get property ID: LXB_CSS_PROPERTY_COLOR
           ├─→ Search element->style AVL tree: not found
           ├─→ Insert new node:
           │   └─→ lexbor_avl_insert(styles, &element->style,
           │                          LXB_CSS_PROPERTY_COLOR, color_declaration)
           │       ↓
           │       └─→ Creates lxb_style_node_t:
           │           entry.type = LXB_CSS_PROPERTY_COLOR
           │           entry.value = color_declaration
           │           sp = 0,0,1
           │           weak = NULL
           ↓
6. Element now has style tree:
   element->style = AVL tree root
   └─→ [COLOR=red, sp=0,0,1]
```

### Example: CSS Cascade

```css
div { color: red; }           /* specificity: 0,0,1 */
.highlight { color: blue; }   /* specificity: 0,1,0 */
```

Applied to `<div class="highlight">`:

```
1. First rule matches: div
   └─→ Insert color=red, sp=0,0,1
       element->style:
       └─→ [COLOR] → value=red, sp=0,0,1, weak=NULL

2. Second rule matches: .highlight
   └─→ Insert color=blue, sp=0,1,0
       └─→ Search AVL tree: COLOR node exists
       └─→ Compare specificity: 0,1,0 > 0,0,1 (new wins!)
       └─→ Demote current to weak list
       └─→ Update node

       element->style:
       └─→ [COLOR] → value=blue, sp=0,1,0
                     weak=[value=red, sp=0,0,1, next=NULL]
```

## Style Lookup

**Entry point**: `lxb_dom_element_style_by_id()`

```c
const lxb_css_rule_declaration_t *
lxb_dom_element_style_by_id(const lxb_dom_element_t *element, uintptr_t id)
{
    lxb_dom_document_t *doc = lxb_dom_element_document(element);

    // O(log n) search in element's AVL tree
    lxb_style_node_t *node = (lxb_style_node_t *)
        lexbor_avl_search(doc->css->styles, element->style, id);

    return (node != NULL) ? node->entry.value : NULL;
}
```

**Computed value access**:
```c
const void *
lxb_dom_element_css_property_by_id(const lxb_dom_element_t *element,
                                   uintptr_t id)
{
    lxb_style_node_t *node = lxb_dom_element_style_node_by_id(element, id);

    if (node == NULL) {
        // Not set - return initial value
        return lxb_css_property_initial_by_id(id);
    }

    lxb_css_rule_declaration_t *declr = node->entry.value;
    return declr->u.user;  // Parsed property value
}
```

## Event Handling

### DOM Mutation Events

Located in: `lexbor/source/lexbor/style/event.c`

When DOM changes, styles are automatically updated:

```c
// Element inserted
lxb_status_t
lxb_style_event_insert(lxb_dom_node_t *node)
{
    if (node->type == LXB_DOM_NODE_TYPE_ELEMENT) {
        return lxb_style_event_insert_element(node);
    }
    return LXB_STATUS_OK;
}

// Element removed
lxb_status_t
lxb_style_event_remove(lxb_dom_node_t *node)
{
    lxb_dom_element_t *el = lxb_dom_interface_element(node);
    lxb_dom_document_t *doc = node->owner_document;

    // Clean up element's style tree
    return lexbor_avl_foreach(doc->css->styles, &el->style,
                              lxb_style_event_remove_cb, el);
}
```

## Performance Characteristics

### Time Complexity

| Operation | Complexity | Notes |
|-----------|------------|-------|
| Style lookup by property ID | O(log n) | AVL tree search, n = # of properties |
| Style insertion | O(log n) | AVL tree insertion + rebalancing |
| Selector matching | O(m × k) | m = # of elements, k = selector complexity |
| Stylesheet application | O(s × m × k) | s = # of style rules |
| Weak list insertion | O(w) | w = # of weak entries (usually small) |

### Space Complexity

- **Per element**: O(p) where p = number of unique CSS properties set
- **Per document**: O(n × p) where n = number of elements
- **AVL tree overhead**: 32 bytes per node (type, height, value, left, right, parent, next)
- **Weak list**: Additional 24 bytes per cascaded declaration

### Memory Efficiency Features

1. **Shared declarations**: Multiple elements referencing same declaration use reference counting
2. **Memory pools**: `lexbor_dobject_t` for fast allocation without fragmentation
3. **Single global AVL tree**: All elements share the same AVL tree allocator

## Key Design Decisions

### 1. Why AVL Tree Instead of Hash Table?

**Advantages:**
- Ordered traversal for serialization
- Memory efficiency (no bucket overhead)
- Predictable O(log n) worst case (hash tables can degrade)
- Automatic balancing without rehashing

**Trade-offs:**
- Slightly slower than hash tables for average case (O(log n) vs O(1))
- More complex balancing logic

### 2. Why Weak Style Lists?

**Purpose:**
- Enables dynamic style updates without re-matching selectors
- Supports cascade re-evaluation when styles change
- Allows inspection of all applicable styles (not just winner)

**Example use case:**
```javascript
// JavaScript can access all matching styles
element.getMatchedCSSRules(); // Returns both winning and weak styles
```

### 3. Why Reference Counting for Declarations?

- Multiple elements can share the same declaration (e.g., all `<p>` elements)
- Avoids duplicating parsed property values
- Safe cleanup when stylesheets are removed

## Summary

The lexbor CSS system implements a sophisticated styling engine with:

1. **CSS Parsing**: Full CSS3 parser with tokenizer, syntax parser, selector parser, and property parser
2. **Style Storage**: Per-element AVL trees keyed by property ID
3. **Cascade Support**: Weak lists maintain losing declarations sorted by specificity
4. **Selector Matching**: Efficient selector engine that applies styles to matching elements
5. **Memory Management**: Reference counting and memory pools for efficiency
6. **Event System**: Automatic style updates on DOM mutations

The AVL tree structure provides O(log n) style lookup while maintaining memory efficiency through shared declarations and pooled allocations. The weak list mechanism elegantly handles the CSS cascade without redundant selector matching.
