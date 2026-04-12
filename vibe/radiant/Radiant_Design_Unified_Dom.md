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

**Cleanup — ASSESSED (Phase 5 done, but none removable):**

| Item | Verdict | Reason |
|------|---------|--------|
| `DomElement.native_element` | **Cannot remove** | MarkEditor API, event dispatch `dispatch_lambda_handler`, 50+ test usages. In ui_mode it's redundant (could derive via `dom_element_to_element`), but non-ui_mode paths still need it |
| `DomText.text/length/native_string` | **Cannot remove** | Layout engine directly mutates `text/length` during `::first-letter` split and text-transform; JS DOM API reads them; serialization reads them |
| `DomDocument.element_dom_map` | **Could optimize for ui_mode** but not remove | In ui_mode, the `element_dom_map_lookup` calls in `rebuild_lambda_doc_incremental` could use `element_to_dom_element()` instead. But the map is still needed for non-ui_mode HTML docs |
| `DomDocument.html_root` | **Cannot remove** | Used for PDF vs HTML detection, script execution, initial DOM construction, incremental sync |
| `DomNode` base class | **No action needed** | Provides `is_element()`/`is_text()`/`as_element()` helpers. No vtable. Working correctly |

### Phase 5: Unified DOM for `lambda view script.ls` — NOT STARTED

#### Problem Statement

When `lambda view script.ls` runs, the pipeline is:

1. **JIT execution** (`run_script_mir` → `execute_script_and_create_output`): The MIR-compiled Lambda script runs and produces an Element tree. Elements are allocated via `elmt()` → `heap_calloc(sizeof(Element))` on the GC heap. Strings are allocated via `heap_strcpy()` → `heap_alloc(len + 1 + sizeof(String))`. Children are built via `list_push()` → `expand_list()` using `heap_data_alloc()` for `items[]` buffers. **None of these allocations are DOM-aware.**

2. **Result wrapping** (`load_lambda_script_doc`): If the result is an `<html>` element, it's used as-is. Otherwise, a `MarkBuilder` wraps it in `<html><body>result</body></html>`. This MarkBuilder operates on the `script_output` Input which has **`ui_mode = false`**, so it allocates plain Elements.

3. **DOM build** (`build_dom_tree_from_element`): Runs in non-ui_mode — creates **separate** `DomElement`/`DomText` wrappers on the DomDocument arena, building a second parallel tree. The `element_dom_map` is populated to bridge the two trees.

4. **Reactive rebuild** (`render_map_retransform` → `rebuild_lambda_doc`/`rebuild_lambda_doc_incremental`): When a UI event handler mutates state, the template body function is re-executed (still on the GC heap, producing plain Elements), `items[]` in the parent element is patched, and the DOM tree is rebuilt from scratch (full rebuild) or incrementally (via `element_dom_map` lookup).

This means the script path currently maintains **two parallel trees**: Lambda Elements on the GC heap and DomElements on the document arena. Phase 1–4 unified the HTML parsing path but this path remains dual-tree.

#### Approach: Arena-Based Fat Allocation

The core idea: **pre-create a result `Input` with a persistent arena, and route all JIT allocations through it in ui_mode.** Elements allocate fat `DomElement` directly from the result arena. Strings first live on the GC heap (for intermediate computation), then are copied into the arena as fat `DomText` nodes when added to an element via `list_push()`. This completely eliminates GC involvement for the DOM tree — no new type tags, no GC scanner changes, no interior pointer issues.

##### Why Arena Over GC Heap

| Concern | GC heap approach | Arena approach |
|---------|------------------|---------------|
| New GC type tags | Requires `LMD_TYPE_DOM_ELEMENT`, `LMD_TYPE_DOM_TEXT` | None needed |
| GC scanner changes | Must add offset-adjusted scan cases + interior pointer handling | No changes — arena objects are invisible to GC |
| Interior pointer complexity | `gc_mark_item` must detect and back-adjust Element* → DomElement* | Not applicable — arena isn't scanned by GC |
| Allocation speed | Large object pool (DomElement > 256 bytes, slow) | Bump allocator (fast, good cache locality) |
| Memory contiguity | Scattered across GC pools | Contiguous within arena chunks |
| Consistency with input parsers | Different path (GC heap) | Same pattern as HTML/XML input parsers |
| Lifetime management | GC cycle collects unreachable nodes | Arena persists for document lifetime |

##### Result Input: Pre-Created Arena Owner

`load_lambda_script_doc` creates a **result `Input`** before script execution. This Input owns the arena where all DOM nodes will live:

```cpp
DomDocument* load_lambda_script_doc(Url* script_url, ...) {
    Runtime* runtime = ...;
    runtime_init(runtime);

    // Create result Input with fresh arena — this is the DOM's memory owner
    Pool* result_pool = pool_create();
    Input* result_input = Input::create(result_pool, script_url);
    result_input->ui_mode = true;

    // Set up runtime context to route allocations to result arena
    runtime->context.ui_mode = true;
    runtime->context.arena = result_input->arena;  // JIT code uses this arena

    Input* script_output = run_script_mir(runtime, nullptr, script_filepath, false);
    // script_output->root contains the Lambda result tree
    // All elements in this tree are fat DomElements on result_input->arena
    ...
}
```

##### Element Allocation: Fat DomElement on Arena

`elmt()` in ui_mode allocates from `context->arena` instead of `heap_calloc`:

```c
Element* elmt(int64_t type_index) {
    if (context->ui_mode) {
        // allocate fat DomElement on the result arena
        DomElement* dom = (DomElement*)arena_calloc(context->arena, sizeof(DomElement));
        dom->node_type = DOM_NODE_ELEMENT;
        Element* e = dom_element_to_element(dom);
        e->type_id = LMD_TYPE_ELEMENT;
        ArrayList* type_list = (ArrayList*)context->type_list;
        e->type = (TypeElmt*)(type_list->data[type_index]);
        return e;  // Lambda sees Element* at embedded offset
    }
    // non-UI: unchanged — GC heap allocation
    Element *e = (Element *)heap_calloc(sizeof(Element), LMD_TYPE_ELEMENT);
    e->type_id = LMD_TYPE_ELEMENT;
    ArrayList* type_list = (ArrayList*)context->type_list;
    e->type = (TypeElmt*)(type_list->data[type_index]);
    return e;
}
```

This is identical to how MarkBuilder allocates elements in ui_mode (`arena_calloc(input->arena, sizeof(DomElement))`). The returned `Element*` points to the embedded `elmt` field inside the fat `DomElement`.

##### String Handling: GC Heap → Arena Copy on Attachment

Unlike elements, **strings are NOT pre-allocated as DomText on the arena.** The JIT runtime performs many intermediate string operations (concatenation, interpolation, formatting) that produce temporary strings. These must remain on the GC heap for normal GC lifecycle.

The key insight: **a string only becomes a DOM text node when it is added to an element's children via `list_push()`.** At that point, we copy it into the result arena as a fat `[DomText][String][chars]` block:

```c
void list_push(List *list, Item item) {
    // ... existing content-list spreading logic ...

    if (context->ui_mode && list->is_content) {
        TypeId tid = get_type_id(item);
        if (tid == LMD_TYPE_STRING) {
            // Copy GC-heap string into arena as fat DomText
            String* src = it2s(item);
            size_t total = sizeof(DomText) + sizeof(String) + src->len + 1;
            DomText* dt = (DomText*)arena_calloc(context->arena, total);
            dt->node_type = DOM_NODE_TEXT;
            String* dst = dom_text_to_string(dt);
            dst->len = src->len;
            dst->is_ascii = src->is_ascii;
            memcpy(dst->chars, src->chars, src->len + 1);
            item = s2it(dst);  // replace with arena-allocated DomText string
            // Original GC string becomes garbage, collected normally
        }
    }

    // ... normal push logic ...
    expand_list(list);
    list->items[list->length++] = item;
}
```

**String merging** in `list_push()` (adjacent string concatenation): In ui_mode, merged strings should also be allocated as DomText on the arena. The existing merge path checks `input_context->consts` to decide between `context_alloc` and `pool_calloc` — we add a third branch for ui_mode that allocates fat DomText on the arena.

##### items[] Growth: Arena Alloc + Copy

`expand_list()` already supports dual-mode allocation (arena vs heap). Since `context->arena` is set in ui_mode, the arena path is automatically taken:

```cpp
void expand_list(List *list, Arena* arena) {
    list->capacity = list->capacity ? list->capacity * 2 : 8;
    Item* old_items = list->items;

    if (!arena && input_context && input_context->arena) {
        arena = input_context->arena;
    }
    bool use_arena = (arena != nullptr && (old_items == nullptr || arena_owns(arena, old_items)));

    if (use_arena) {
        // arena_realloc: extends in-place if at chunk end, else copies + frees old to free-list
        list->items = arena_realloc(arena, old_items,
                                    list->length * sizeof(Item),
                                    list->capacity * sizeof(Item));
    } else {
        // heap_data_calloc for GC-managed data buffers
        list->items = heap_data_calloc(list->capacity * sizeof(Item));
        if (old_items) memcpy(list->items, old_items, list->length * sizeof(Item));
    }
}
```

When `arena_realloc` can't extend in-place, it allocates a new buffer, copies, and puts the old buffer on the arena's free-list for potential reuse. **The old `items[]` buffer becomes garbage in the arena** — this is the expected fragmentation cost of the persistent arena model.

**Same approach for attribute `data` buffers**: `elmt_fill()` and map/element attr data allocation use similar patterns. In ui_mode, these go through arena allocation. Growth (rare for attributes — typically set once) copies and leaves old buffers as arena garbage.

##### Reactive Updates: Reuse the Same Arena

When `render_map_retransform()` re-executes a template body function:

1. The JIT code runs with `context->ui_mode = true` and `context->arena` pointing to the result Input's arena
2. New DomElements are allocated on the **same arena** as the original DOM tree
3. `items[]` in the parent element is patched to reference the new child Element*
4. Old elements and their `items[]` buffers remain in the arena as **dead garbage**

The arena is **not reset** between reactive updates. Garbage accumulates:
- Old replaced DomElement structs (sizeof(DomElement) ≈ 400+ bytes each)
- Old `items[]` buffers from expand_list (variable size)
- Old attribute `data` buffers (variable size)
- Old DomText blocks for replaced text content

This is acceptable for typical interactive UIs where mutations are localized (one component's template re-renders). The garbage ratio stays low because most of the DOM tree is stable.

##### Arena Compaction

When cumulative garbage exceeds a threshold (e.g., 50% of arena usage, or absolute size > N MB), the arena is **compacted**:

1. **Allocate a new arena** from the same pool
2. **Walk the live DOM tree** (rooted at `dom_doc->root_element`):
   - For each live DomElement: copy to new arena, update `items[]` pointers
   - For each live DomText: copy to new arena (variable-size: `sizeof(DomText) + sizeof(String) + len + 1`)
   - Update parent/child/sibling linked list pointers
3. **Swap** the new arena into the result Input, destroy the old arena

This is essentially a **copying collector scoped to just the DOM arena** — much simpler than a full GC because:
- The root set is a single DOM tree (no stack scanning, no weak refs)
- All objects have known, fixed types (DomElement or DomText)
- The linked list structure provides a natural traversal order
- No finalization, no cycles — pure tree structure

**Trigger heuristic**: Track `arena_total_allocated` vs `arena_live_bytes` (inferred from live DOM node count × avg size). Compact when `total_allocated > 2 * estimated_live_bytes`.

**Pointer fixup during compaction**: Each node's new address is known after copying. A fixup pass updates:
- `DomNode.parent`, `first_child`, `last_child`, `next_sibling`, `prev_sibling`
- `Element.items[]` entries (element/string items point to other arena nodes)
- Parent element's `items[]` entry referencing this element
- `DomDocument.root_element`, `dom_doc->body_element`, etc.

Strategy: use a temporary `HashMap<void*, void*>` mapping old address → new address. Walk the tree, copy each node, record mapping. Second pass fixes up all pointers.

##### GC Interaction: None

Arena-allocated DomElements/DomTexts are **invisible to the GC**:
- No `gc_header_t` — arena objects have no GC metadata
- The GC scanner never encounters them — they're not on any GC slab/pool
- `is_gc_object()` returns false for arena pointers
- The GC's `gc_mark_item()` sees Items in `items[]` — for arena-allocated containers, the pointer won't be in the GC heap, so `is_gc_object()` skips them

**Critical: GC must not collect the `items[]` buffer or `data` buffer** for arena-allocated elements. Since these buffers are also on the arena (via `expand_list` arena path), the GC never sees them. No risk.

**Exception: intermediate strings on GC heap.** During JIT execution, temporary strings (from concatenation, formatting, etc.) live on the GC heap. If such a string is stored in a local variable or JIT register and a GC cycle runs mid-execution, the GC must keep it alive. This is already handled by the normal GC stack/register scanning for JIT code — no change needed.

##### Context: Propagating ui_mode and Arena

The `Context` struct gains a `ui_mode` field:

```c
typedef struct Context {
    Pool* pool;
    Arena* arena;          // already exists — reused for result arena in ui_mode
    void** consts;
    void* type_list;
    Url* cwd;
    void* (*context_alloc)(int size, TypeId type_id);
    bool run_main;
    bool disable_string_merging;
    uintptr_t stack_limit;
    bool ui_mode;  // NEW: allocate fat DomElement on arena, copy strings on attachment
} Context;
```

`Context.arena` **already exists** and is used by `expand_list()` for the arena path. In ui_mode, `load_lambda_script_doc` sets it to the result Input's arena before `run_script_mir()`. The existing `expand_list` arena detection (`input_context->arena`) picks it up automatically.

##### load_lambda_script_doc Changes

```cpp
DomDocument* load_lambda_script_doc(Url* script_url, ...) {
    Runtime* runtime = ...;
    runtime_init(runtime);

    // 1. Create result Input — arena owner for all DOM nodes
    Pool* result_pool = pool_create();
    Input* result_input = Input::create(result_pool, script_url);
    result_input->ui_mode = true;

    // 2. Configure runtime to route element/list allocations to result arena
    runtime->context.ui_mode = true;
    runtime->context.arena = result_input->arena;

    // 3. Execute script — elements are fat DomElements on result arena
    Input* script_output = run_script_mir(runtime, nullptr, script_filepath, false);

    // 4. Result wrapping (if needed) — MarkBuilder also uses result_input arena
    //    since result_input->ui_mode = true and we pass result_input to MarkBuilder
    Element* root_elmt = it2elmt(script_output->root);
    if (!is_html_element(root_elmt)) {
        MarkBuilder mb(result_input);  // uses result_input->arena in ui_mode
        mb.start("html"); mb.start("body");
        mb.add_element(root_elmt);     // attaches existing DomElement
        mb.end(); mb.end();
        root_elmt = it2elmt(result_input->root);
    }

    // 5. build_dom_tree_from_element in ui_mode — init-only (no wrapper allocation)
    //    Just initializes DOM linked list (parent/child/sibling) from items[]
    DomDocument* dom_doc = ...;
    build_dom_tree_from_element(dom_doc, root_elmt);  // ui_mode: only link nodes

    // 6. No element_dom_map needed — use element_to_dom_element() directly
    return dom_doc;
}
```

##### Retransform / Reactive Rebuild

When `render_map_retransform()` re-executes a template body function:

1. Ensure `context->ui_mode = true` and `context->arena` = result Input's arena
2. JIT code calls `elmt()` → fat DomElement on arena
3. JIT code calls `list_push()` → strings copied to arena as DomText
4. Parent's `items[]` is patched with new Element* (pointing into new DomElement on arena)
5. Old DomElement/DomText remain in arena as garbage

`rebuild_lambda_doc` / `rebuild_lambda_doc_incremental` changes:
- No more `element_dom_map` — use `element_to_dom_element()` to recover DomElement from any Element in the tree
- `build_dom_tree_from_element()` in ui_mode: re-links DOM node list from `items[]` without allocating
- Incremental path: `element_dom_map_lookup(doc->element_dom_map, old_elem)` → `element_to_dom_element(old_elem)`

##### elmt_fill and Attribute Data Buffers

`elmt_fill()` (called by JIT-generated code) allocates packed attribute data via `heap_data_calloc()`. In ui_mode, this should use arena allocation instead:

```c
Element* elmt_fill(Element *elmt, ...) {
    TypeElmt* type = (TypeElmt*)elmt->type;
    int data_size = type->data_size;
    if (data_size > 0) {
        if (context->ui_mode) {
            elmt->data = arena_calloc(context->arena, data_size);
        } else {
            elmt->data = heap_data_calloc(data_size);
        }
        elmt->data_cap = data_size;
    }
    // ... fill attribute values ...
}
```

Attribute data buffers are typically set once and never grown, so no garbage concern here.

##### Summary of Changes

| Component | Change |
|-----------|--------|
| `EvalContext` (lambda.h) | Add `bool ui_mode` field |
| `elmt()` / `elmt_with_tl()` (lambda-data-runtime.cpp) | In ui_mode: `arena_calloc(context->arena, sizeof(DomElement))`, return `dom_element_to_element(dom)` |
| `elmt_fill()` (lambda-data-runtime.cpp) | In ui_mode: `arena_calloc` for attr data buffers instead of `heap_data_calloc` |
| `list_push()` (lambda-data.cpp) | In ui_mode + content list: copy GC string to arena as fat DomText; merged strings also arena-allocated |
| `expand_list()` (lambda-data.cpp) | No change — already supports arena path via `context->arena` |
| `heap_strcpy()` (lambda-mem.cpp) | No change — strings stay on GC heap; copied to arena on attachment |
| GC scanner (gc_heap.c) | No change — arena objects are invisible to GC |
| `load_lambda_script_doc` (cmd_layout.cpp) | Create result Input, set `context->ui_mode = true`, set `context->arena = result_input->arena` |
| `build_dom_tree_from_element()` | In ui_mode: init-only path (same as HTML path in Phase 2) |
| `rebuild_lambda_doc` / `rebuild_lambda_doc_incremental` | Replace `element_dom_map` lookups with `element_to_dom_element()` |
| `render_map_retransform()` | Ensure context ui_mode + arena are set before re-execution |
| Arena compaction | New: walk live DOM tree, copy to fresh arena, fixup pointers |

##### Implementation Order

1. **Add `ui_mode` to EvalContext**, propagate through runner/context setup
2. **Update `elmt()` / `elmt_with_tl()`** — arena-allocate fat DomElement in ui_mode
3. **Update `elmt_fill()`** — arena-allocate attr data in ui_mode
4. **Update `list_push()`** — copy GC strings to arena as fat DomText in ui_mode; arena-allocate merged strings
5. **Update `load_lambda_script_doc`** — create result Input, set ui_mode + arena, switch to init-only DOM build
6. **Update reactive rebuild** — use `element_to_dom_element()` instead of `element_dom_map`, ensure context setup before retransform
7. **Test** with IoT dashboard script and reactive UI todo tests
8. **Implement arena compaction** — triggered by fragmentation threshold, copies live DOM tree to fresh arena
9. **Remove `element_dom_map`** once both paths (HTML + script) use unified tree

### Phase 6: Unified DOM for `lambda view *.xml` — DONE

#### Problem

`load_xml_doc()` uses `input_from_source()` which creates its own Input with `ui_mode = false`. The XML parser (`parse_xml`) runs through MarkBuilder on that Input, allocating plain Elements on the pool. Then `build_dom_tree_from_element` creates separate DomElement wrappers — the same dual-tree problem Phase 5 solved for scripts.

#### Approach

Mirror the HTML path (`load_lambda_html_doc`):
1. Create `Input` directly with `ui_mode = true` (bypass `input_from_source`)
2. Call `parse_xml(input, xml_content)` — MarkBuilder sees `ui_mode` and allocates fat DomElements on the Input's arena
3. Pass that Input to `dom_document_create()` — `build_dom_tree_from_element` uses the init-only path

The `<html><body>` wrapper must also use MarkBuilder on the same Input (not `dom_element_create`) so the wrappers are fat DomElements too.

#### Changes

| Component | Change |
|-----------|--------|
| `load_xml_doc()` (cmd_layout.cpp) | Create Input directly with `ui_mode = true`, call `parse_xml()`, use MarkBuilder for html/body wrappers, pass to `dom_document_create` |
