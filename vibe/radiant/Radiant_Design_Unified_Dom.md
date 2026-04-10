# Unified DOM Tree Design

## Motivation

Currently Radiant maintains two parallel trees:
1. **Lambda Element tree** — built by MarkBuilder during HTML parsing. Stores tag names, attributes (shape-based packed data), and children in a flat `items[]` array.
2. **DomElement tree** — built by `build_dom_tree_from_element()` in a second pass. Wraps each Lambda Element with CSS/layout state and re-links children as a `first_child`/`next_sibling` doubly-linked list.

This dual-tree architecture introduces:
- **Memory overhead**: every node is allocated twice (Input arena + DomDocument arena).
- **Sync cost**: attribute writes go through `MarkEditor` to update the Lambda Element, then update the DomElement wrapper. The `element_dom_map` hashmap exists solely to bridge the two trees during incremental rebuild.
- **Build cost**: `build_dom_tree_from_element()` walks the entire Lambda tree to create a parallel DomElement tree, copying tag names, extracting attributes, and re-linking children.

The goal is to **unify into a single tree** that serves both Lambda runtime and Radiant layout.

## Design

### Core Idea

In **UI mode** (layout/render/view commands), MarkBuilder allocates `DomElement` instead of `Element`, and `DomText` (with inline `String`) instead of `String`. Since `DomElement` embeds `Element` and `DomText` embeds `String`, the tree is a valid Lambda tree AND a valid DOM tree simultaneously.

In **non-UI mode** (scripting, data processing), MarkBuilder allocates plain `Element` and `String` as before. No DOM overhead.

### Uniform Fat-Pointer Layout

Both DomElement and DomString use the same pattern: **DomNode fields at offset 0, Lambda data (Element/String) in the middle**. The `items[]` array points to the Lambda data part; the DOM linked list points to the DomNode part (allocation start).

This means `DomNode*` is always at offset 0 of the allocation, so linked-list traversal, `is_element()`/`is_text()`, `next_sibling`, `parent` — all work uniformly through `DomNode*` without any offset math.

### DomElement Layout

```
DomElement allocation in memory:
┌──────────────────┬──────────────────┬────────────────────────┐
│  DomNode fields  │  Element fields  │  DomElement-specific   │
│  (node_type,     │  (type_id, flags,│  (tag_id, id, styles,  │
│   parent, sibs,  │   items, length, │   font, bound, blk,    │
│   first/last     │   type, data,    │   display, ...)        │
│   child, x, y,   │   data_cap)      │                        │
│   w, h, ...)     │                  │                        │
└──────────────────┴──────────────────┴────────────────────────┘
^                   ^                                   
DomNode*            Element* ← stored in parent's items[]
(linked list)       (Lambda runtime sees normal Element)
```

**Key properties:**
- `items[]` stores pointers to the **Element part** (at a known offset). Lambda runtime code (`MarkReader`, `get_attr()`, `items[]` iteration) sees a normal `Element*`.
- The DOM linked list (`first_child`/`next_sibling`) uses `DomNode*` which points to offset 0 of the allocation.
- `DomNode*` is the same as `DomElement*` (offset 0). Casting between them is a no-op.
- `native_element` pointer is removed — conversion between the two views is a constant offset.

**Struct layout (C++):**

```cpp
struct DomElement {
    // === DomNode fields (offset 0) ===
    DomNodeType node_type;       // = DOM_NODE_ELEMENT
    DomNode* parent;
    DomNode* next_sibling;
    DomNode* prev_sibling;
    DomNode* first_child;
    DomNode* last_child;
    ViewType view_type;
    float x, y, width, height;
    int source_line;
    bool layout_dirty;
    float layout_height_contribution;
    // pad to 8-byte alignment if needed

    // === Element fields (known offset) ===
    TypeId type_id;              // = LMD_TYPE_ELEMENT
    uint8_t flags;
    Item* items;
    int64_t length;
    int64_t extra;
    int64_t capacity;
    void* type;                  // TypeElmt*
    void* data;                  // packed attr data
    int data_cap;

    // === DomElement-specific fields ===
    const char* tag_name;
    uintptr_t tag_id;
    const char* id;
    const char** class_names;
    int class_count;
    StyleTree* specified_style;
    // ... CSS/layout props ...
    DomDocument* doc;
};
```

**Note:** DomElement does NOT use C++ inheritance from either DomNode or Element. It is a flat struct with fields from both laid out explicitly. This gives full control over field offsets and avoids multiple inheritance pointer arithmetic.

**Conversion macros:**

All offsets are derived from `offsetof()` or `sizeof()` — **never hardcoded byte values**. As DomElement or DomText gains/loses fields (e.g., new layout properties, removed caches), the offsets adjust automatically at compile time.

```cpp
// DomElement* ↔ Element* conversion (offset auto-adapts to DomElement field changes)
#define dom_element_to_element(de)  ((Element*)((char*)(de) + offsetof(DomElement, type_id)))
#define element_to_dom_element(e)   ((DomElement*)((char*)(e) - offsetof(DomElement, type_id)))

// DomElement* ↔ DomNode* — same address (offset 0), just a type cast
#define dom_element_to_node(de)     ((DomNode*)(de))
#define node_to_dom_element(dn)     ((DomElement*)(dn))  // caller must verify is_element()

static_assert(offsetof(DomElement, type_id) % 8 == 0,
              "Element fields must be 8-byte aligned within DomElement");
```

### DomString Layout (Same Pattern)

`String` uses a C99 flexible array member (`char chars[]`), so it cannot be extended via inheritance. DomString uses the same fat-pointer pattern: **DomNode fields at offset 0, String data after**.

```
DomString allocation in memory:
┌──────────────────────────┬──────────────────────────────┐
│  DomNode + DomText fields│  String { len, is_ascii,     │
│  (node_type, parent,     │           chars[] }           │
│   sibs, x, y, w, h,     │                              │
│   rect, font, color)    │                              │
└──────────────────────────┴──────────────────────────────┘
^                           ^                              
DomNode* / DomText*         String* ← stored in parent's items[]
(linked list)               (Lambda runtime sees normal String)
```

**Struct layout:**

```cpp
struct DomText {
    // === DomNode fields (offset 0) ===
    DomNodeType node_type;       // = DOM_NODE_TEXT
    DomNode* parent;
    DomNode* next_sibling;
    DomNode* prev_sibling;
    ViewType view_type;
    float x, y, width, height;
    int source_line;
    bool layout_dirty;
    float layout_height_contribution;

    // === DomText-specific fields ===
    DomTextContentType content_type;
    TextRect* rect;
    FontProp* font;
    Color color;
    // pad to 8-byte alignment if needed

    // === String follows immediately (flexible array member) ===
    // String { len, is_ascii, chars[] } starts here
    // Accessed via dom_text_to_string(dt)
};

// Total allocation: sizeof(DomText) + sizeof(String) + len + 1
```

**Note:** DomText does NOT contain `text`/`length`/`native_string` fields — those are redundant since the String is inline. Access via `String.chars` and `String.len` directly.

**Conversion macros (offset auto-adapts via `sizeof(DomText)`):**

```cpp
// DomText* → String* (offset past DomText header — adapts as DomText fields change)
#define dom_text_to_string(dt)      ((String*)((char*)(dt) + sizeof(DomText)))

// String* → DomText* (back up by DomText header size)
#define string_to_dom_text(s)       ((DomText*)((char*)(s) - sizeof(DomText)))

// DomText* ↔ DomNode* — same address (offset 0)
#define dom_text_to_node(dt)        ((DomNode*)(dt))
#define node_to_dom_text(dn)        ((DomText*)(dn))  // caller must verify is_text()

static_assert(sizeof(DomText) % 8 == 0,
              "DomText size must be 8-byte aligned so String starts aligned");
```

**How it works:**
- The `Item` tagged pointer in `items[]` points to the **String part** (at offset `sizeof(DomText)`). Lambda runtime sees a normal `String*` — `s->len`, `s->chars`, `s2it()` all work unchanged.
- DOM linked list uses `DomNode*` at offset 0 — `next_sibling`, `parent`, `is_text()` all work uniformly with DomElement nodes.
- DOM code recovers the text node from a String: `string_to_dom_text(s)`.

### Child Iteration

**Layout code** uses the linked list — unchanged from today:

```cpp
for (DomNode* child = elem->first_child; child; child = child->next_sibling) {
    if (child->is_element()) {
        DomElement* ce = (DomElement*)child;  // same address, offset 0
    } else if (child->is_text()) {
        DomText* dt = (DomText*)child;        // same address, offset 0
        String* s = dom_text_to_string(dt);
    }
}
```

**Lambda runtime** uses `items[]` — unchanged from today:

```cpp
for (int64_t i = 0; i < elem->length; i++) {
    Item child = elem->items[i];
    TypeId tid = get_type_id(child);
    if (tid == LMD_TYPE_ELEMENT) {
        Element* ce = child.element;  // points into middle of DomElement
    } else if (tid == LMD_TYPE_STRING) {
        String* s = child.get_string();  // points into middle of DomText
    }
}
```

**Conversion helpers** between the two views:

```cpp
// From items[] Item to DomNode* (for either element or text)
inline DomNode* item_to_dom_node(Item child) {
    TypeId tid = get_type_id(child);
    if (tid == LMD_TYPE_ELEMENT) return (DomNode*)element_to_dom_element(child.element);
    if (tid == LMD_TYPE_STRING)  return (DomNode*)string_to_dom_text(child.get_string());
    return nullptr;
}
```

### Linked Child List (Retained)

The `first_child`/`last_child`/`next_sibling`/`prev_sibling` linked list is **kept** for fast layout navigation. Layout code traverses siblings heavily (`prev_placed_view()`, float scanning, inline splitting), and pointer-chasing a linked list is simpler and faster than index arithmetic with type dispatch on every step.

The linked list is populated during `init_dom_tree()` (second pass) by walking `items[]` once and linking DomElement/DomTextFields nodes. The `items[]` array remains the source of truth for Lambda runtime; the linked list is a derived index for layout.

```cpp
// In DomElement (offset 0, so these are DomNode-compatible):
DomNode* first_child;   // first child in linked list
DomNode* last_child;    // last child in linked list
DomNode* next_sibling;
DomNode* prev_sibling;
DomNode* parent;

// In DomText (also at offset 0, DomNode-compatible):
DomNode* next_sibling;
DomNode* prev_sibling;
DomNode* parent;
```

Both representations must be kept in sync during mutations (see MarkEditor section below).

### MarkBuilder: Dual-Mode Allocation

MarkBuilder gains a **UI mode flag** and allocates larger structures when active:

```cpp
class MarkBuilder {
    bool ui_mode;  // false = normal Lambda, true = allocate DomElement/DomText
    // ...
};
```

**Element creation (UI mode):**

```cpp
// In ElementBuilder constructor (UI mode):
DomElement* dom = (DomElement*)arena_calloc(arena_, sizeof(DomElement));
dom->node_type = DOM_NODE_ELEMENT;
Element* elmt = dom_element_to_element(dom);  // pointer to Element part
elmt->type_id = LMD_TYPE_ELEMENT;
// ... set up TypeElmt, tag_name as before on elmt ...
elmt_ = elmt;  // MarkBuilder works with Element* as before
```

`arena_calloc` zeros all bytes, so DOM-specific fields (styles, font, layout props) start as null/zero. The DOM builder second pass initializes them.

**String creation (UI mode):**

```cpp
// In createString / createStringItem (UI mode):
size_t total = sizeof(DomText) + sizeof(String) + len + 1;
DomText* dt = (DomText*)arena_calloc(arena_, total);  // zeros DomText fields
dt->node_type = DOM_NODE_TEXT;
String* s = dom_text_to_string(dt);  // pointer to String part
s->len = len;
s->is_ascii = ...;
memcpy(s->chars, str, len);
s->chars[len] = '\0';
return s2it(s);  // Item points to String part — Lambda sees normal String
```

### DOM Builder Second Pass

After MarkBuilder produces the unified tree, the DOM builder initializes CSS/layout fields:

```cpp
void init_dom_tree(DomElement* root, DomDocument* doc) {
    // Set doc pointer, extract id/class, resolve tag_id, create specified_style
    root->doc = doc;
    root->tag_id = tag_name_to_id(root->tag_name);
    root->id = extract_id(root);
    extract_classes(root);
    root->specified_style = style_tree_create(doc->arena);
    // ... etc.

    // Walk items[] — init DOM fields and build linked list
    DomNode* prev = nullptr;
    root->first_child = nullptr;
    root->last_child = nullptr;

    for (int64_t i = 0; i < root->length; i++) {
        Item child = root->items[i];
        TypeId tid = get_type_id(child);
        DomNode* node = nullptr;

        if (tid == LMD_TYPE_ELEMENT) {
            DomElement* child_elem = element_to_dom_element(child.element);
            child_elem->parent = (DomNode*)root;
            node = (DomNode*)child_elem;
            init_dom_tree(child_elem, doc);  // recurse
        } else if (tid == LMD_TYPE_STRING) {
            DomText* text = string_to_dom_text(child.get_string());
            text->parent = (DomNode*)root;
            node = (DomNode*)text;
        }

        if (node) {
            // Build linked list
            node->prev_sibling = prev;
            node->next_sibling = nullptr;
            if (prev) prev->next_sibling = node;
            else root->first_child = node;
            root->last_child = node;
            prev = node;
        }
    }
}
```

This replaces `build_dom_tree_from_element()`. No new allocations — just initialization of already-allocated (and zeroed) fields, plus linked list construction.

### MarkEditor: DOM-Aware Mutations

MarkEditor currently operates on plain Lambda trees — `elmt_insert_child()`, `elmt_delete_child()`, etc. manipulate `items[]` and shape data. In the unified tree, these operations must also maintain DOM state:

1. **Child insertion** (`elmt_insert_child`, `elmt_append_child`): After updating `items[]`, must also:
   - Set the child's `parent` pointer
   - Link `next_sibling`/`prev_sibling` into the linked list
   - Update parent's `first_child`/`last_child`
   - For text insertions, allocate fat DomText (with String inline) not plain String

2. **Child deletion** (`elmt_delete_child`, `elmt_delete_children`): After updating `items[]`, must also:
   - Unlink from sibling chain
   - Clear child's `parent` pointer
   - Update parent's `first_child`/`last_child` if head/tail removed

3. **Attribute updates** (`elmt_update_attr`, `elmt_delete_attr`): These only touch the shape/data on Element — no DOM-specific work needed since DomElement extends Element directly.

4. **Element creation** (via MarkEditor's copy/clone paths): Must allocate `DomElement` (not plain `Element`) in UI mode.

**Implementation approach**: MarkEditor gains a `ui_mode` flag (or detects it from the Input/DomDocument context). When `ui_mode` is active, child mutation methods call a DOM sync helper after the base `items[]` operation:

```cpp
// After items[] is updated:
void dom_sync_child_insert(DomElement* parent, Item child, int64_t index);
void dom_sync_child_remove(DomElement* parent, Item child);
void dom_sync_children_rebuild(DomElement* parent);  // full relink after batch ops
```

The `dom_sync_children_rebuild()` function re-walks `items[]` and rebuilds the entire linked list. This is appropriate for batch operations (`elmt_delete_children` with a range) where surgical link updates would be error-prone.

### Invariant: UI Mode Strings Are Always Prefixed

In UI mode, **all** Strings created by MarkBuilder have DomTextFields prefixed. This is guaranteed by construction:
- MarkBuilder's `createString()`/`createStringItem()` checks `ui_mode` and allocates the prefix.
- Tree mutations via MarkEditor are routed through a DOM-aware path that also allocates fat DomText nodes.
- Lambda script code in reactive UI handlers cannot directly inject raw Strings into DOM elements — all mutations go through MarkEditor APIs.

No runtime flag-checking or sentinel validation is needed. The invariant is structural: if you're in a UI-mode document, every String in the tree has a DomTextFields prefix.

### Handling Symbols

Currently, symbols (`LMD_TYPE_SYMBOL`) in `items[]` become DomText nodes with `content_type = DOM_TEXT_SYMBOL`. Two options:

1. **Convert to DomText at build time**: When MarkBuilder encounters a symbol child in UI mode, immediately create a fat DomText with the symbol's resolved text and `content_type = DOM_TEXT_SYMBOL`. Symbols no longer appear in `items[]`.
2. **Keep symbols as-is**: Layout code skips `LMD_TYPE_SYMBOL` items and handles them separately (current approach adapted).

Option 1 is cleaner — it means layout only sees `LMD_TYPE_ELEMENT` and `LMD_TYPE_STRING` in `items[]`.

## What Gets Removed

| Component | Status |
|-----------|--------|
| `DomElement.native_element` | **Removed** — it's `this` |
| `DomText.text` / `DomText.length` | **Removed** — use `String.chars` / `String.len` via `dom_text_to_string()` |
| `DomText.native_string` | **Removed** — the String is inline, access via `dom_text_to_string()` |
| `DomDocument.element_dom_map` | **Removed** — no separate mapping needed |
| `build_dom_tree_from_element()` | **Replaced** by `init_dom_tree()` (no allocations, just field init + linked list build) |
| `DomNode` base class | **Removed** as a class — DomNode fields inlined at offset 0 of both DomElement and DomText |

## What Gets Kept

| Component | Reason |
|-----------|--------|
| `DomElement.first_child` / `last_child` | Fast layout iteration without type dispatch per step |
| `next_sibling` / `prev_sibling` | Sibling navigation in layout (float scan, inline splitting, `prev_placed_view()`) |
| `DomElement.parent` / `DomTextFields.parent` | Upward traversal for style inheritance, positioning |

## What Changes

| Component | Change |
|-----------|--------|
| MarkBuilder | Gains `ui_mode` flag; allocates fat `DomElement`/`DomText` when active |
| MarkEditor | DOM-aware mutations: syncs linked list + allocates fat DomText in UI mode |
| Layout iteration | Unchanged — still uses `first_child`/`next_sibling` linked list |
| DomDocument | Removes `html_root` (Lambda tree) — the DOM tree IS the Lambda tree |

## Migration Plan

### Scope Note: Two UI Paths

There are two paths that invoke Radiant layout:

1. **`lambda view *.html`** — HTML file is parsed directly into a DOM tree. This is the **simple path** and the focus of Phase 1–4.
2. **`lambda view script.ls`** — A Lambda script runs first (functional transformation), producing a Lambda Element tree, which is then passed to layout. This is the **transform path** and will be handled in a future phase after the simple HTML path is fully migrated.

Additionally, `input('*.html')` in non-UI scripting mode must continue to work as before — plain `Element`/`String` allocation, no DOM overhead.

### Phase 1: Fat-pointer DomElement — ✅ DONE

Embedded `Element elmt` field inside `DomElement`. Conversion via `element_to_dom_element()` / `dom_element_to_element()` using `offsetof`. `dom_element_init` has self-copy guard for ui_mode path. All tests pass.

### Phase 2: Fat-pointer DomText + MarkBuilder ui_mode — ✅ DONE

- `Input.ui_mode` flag; `MarkBuilder.ui_mode_` drives fat allocation.
- `ElementBuilder` allocates `DomElement` in ui_mode; `createDomTextString()` allocates `[DomText][String][chars]` for text content. `createString()` left unchanged (used for attribute values).
- HTML5 parser's `html5_flush_pending_text` / `html5_flush_foster_text` use `createDomTextStringFromBuf` in ui_mode.
- `build_dom_tree_from_element` ui_mode path: `element_to_dom_element` / `string_to_dom_text` (no allocation, just field init + linked list).
- `cmd_layout.cpp`: `load_lambda_html_doc` sets `input->ui_mode = true`.
- All tests pass (567 Lambda, 4002 layout, 454 WPT CSS, 39 page load, 86 pretext).

### Phase 3: DOM-aware MarkEditor — ✅ DONE

- `MarkEditor.ui_mode_` (from `input->ui_mode`).
- `dom_relink_children(Element*)`: full rebuild of `first_child`/`last_child`/`next_sibling`/`prev_sibling` from `items[]`. Recovers `DomElement*` via `element_to_dom_element()`, `DomText*` via `string_to_dom_text()` (with safety check: `node_type == DOM_NODE_TEXT && native_string == s`). Symbols skipped.
- Hooked into all 5 inline child mutation methods: `elmt_insert_child`, `elmt_insert_children`, `elmt_delete_child`, `elmt_delete_children`, `elmt_replace_child` — each calls `if (ui_mode_) dom_relink_children(elmt)` before inline return.
- DOM functions guarded against double-linking in ui_mode: `dom_element_append_child`, `dom_element_append_text`, `dom_element_append_comment` skip `dom_append_to_sibling_chain`; `dom_text_remove`, `dom_comment_remove` skip manual sibling unlinking.
- All tests pass (same counts as Phase 2; UI automation `todo_*` tests fail — pre-existing, scoped to `lambda view script.ls` path).

**Not yet done**: DOM API functions (`dom_element_append_text`, `dom_text_set_content`) still create non-fat strings via `createStringItem()` — should use `createDomTextString()` in ui_mode for the reactive UI path. Deferred since those callers are only used by the `lambda view script.ls` path.

### Phase 4: Fat DomText in DOM API + Cleanup — PARTIAL

**Fat DomText in DOM API — ✅ DONE:**
- `dom_element_append_text`: in ui_mode, uses `createDomTextString()` and returns the embedded `DomText*` directly (no separate `dom_text_create` allocation). In non-ui_mode, unchanged.
- `dom_text_set_content`: in ui_mode, uses `createDomTextString()`. Copies DOM properties (content_type, rect, font, color) from old to new embedded DomText. Old `text_node` pointer has updated fields but is orphaned from linked list (new DomText takes its place via `dom_relink_children`).
- `dom_comment_set_content`: in ui_mode, uses `createDomTextString()` for comment content strings.
- All tests pass (same counts as Phase 3).

**Cleanup — NOT DONE (blocked by `lambda view script.ls` path):**
- `DomElement.native_element`: still used by MarkEditor callers and `element_dom_map` for incremental rebuild.
- `DomText.text` / `DomText.length` / `DomText.native_string`: layout uses `text_data()` method (not direct fields), but fields still set during init for backward compat.
- `DomDocument.element_dom_map`: used by `cmd_layout.cpp` incremental rebuild for `lambda view script.ls`. In ui_mode, could be replaced with `element_to_dom_element()`, but requires migrating the incremental rebuild path.
- `DomDocument.html_root`: signal field distinguishing HTML (layout) from PDF (no layout). Still in use.
- `DomNode` base class: provides `is_element()`/`is_text()`/`as_element()` helpers. No vtable. Working correctly, no benefit to removing.
