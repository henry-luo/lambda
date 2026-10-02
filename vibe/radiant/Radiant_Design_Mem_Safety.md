# Radiant Memory Safety — Phase 0: Memory Model, Kind Templates and Migration Plan

**Status:** proposal, not ratified. Nothing in this document is implemented.
**Date:** 2026-10-02.
**Source baseline:** `8b40c7754` plus the working tree. Counts were measured on that tree with `grep` and a line-pattern script; they are occurrence counts, not AST counts, and the first migration step replaces them with an AST inventory.
**Scope:** Radiant-specific code in four areas — view management, HTML/CSS/SVG style resolution, layout, rendering — plus the `lib/` memory primitives they stand on. State management is the next phase. Script (JS/Lambda) integration is out of scope and treated as a contract boundary.
**Spec linkage:** **D4.1.4v4** (four mechanisms; arena = batch free with whole-arena and tail-region variants; pool = individual free; single writer / multiple readers), **D4.1.5** (owner-backed builders), **D4.2.1v3 / D4.2.3** (`MemContext` owns allocators in a tree; every arena and pool bound to a context), **D4.2.4** (batch invalidation requires exclusivity), **D4.2.5v3** (tracking is diagnostic, never the mechanism), **D4.5.1v3** (Radiant = arenas-as-regions + type-stable pools + generation handles + RAII; seam contracts pin / gen-check / copy-as-value). No fifth mechanism is introduced. The Radiant Heap of §2–§3 would revise D4.5 when ratified.
**Series:** rules R8–R16 extend R1–R7 of [Radiant_Design_Memory.md](Radiant_Design_Memory.md). The kind templates extend the template series [Memory_Safety_Template.md](../Memory_Safety_Template.md) (`lib/ownership.hpp`, `lib/tagged.hpp`).
**Organisation:** Part I is the design; Part II is the migration plan, kept in this file as requested.

**Decisions already given (2026-10-02):**

- Object lifetime is reviewed, simplified and standardised before any verification work.
- The lifetime classes are named **Arena**, **Stack** and **Heap**.
- Radiant memory forms **one hierarchical ownership tree**, the Radiant Heap. Every object in it is owned, the tree is traceable and centrally managed in the way the script runtime's GC heap is. Only Stack memory and memtracked temporary RAII resources are outside it.
- Arena storage is inside the tree. Tracing is object-level and decidable by static analysis: no `void*` in Heap objects.
- Every pointer field has one declared kind, declared through the memory-safety templates.
- **Counted** and **Handle** are mechanisms inside the Radiant Heap. A Counted object has one owner; when no specific object owns it, the Heap root does.
- `arena_free` is removed.

---

# Part I — Design

## 1. Goal

Make ownership and lifetime in Radiant a declared, checkable structure rather than a set of conventions:

1. every long-lived object has exactly one owner in one tree;
2. every pointer field states, in its type, how it relates to that tree;
3. the tree can be walked from its root and every object reached, with the type of every pointer known statically;
4. the ways a lifetime can end are few and the same everywhere.

This is the base a full static verification can stand on (§9). It also has value alone: a debug-build tracer can check the whole model at run time.

Phase 0 does not prove anything, does not cover state management, and does not touch script integration.

## 2. Memory classes

| Class | Storage (D4.1.4v4 mechanism) | Lifetime ends | Contents |
|---|---|---|---|
| **Radiant Heap** | one ownership tree of nodes; each node stores objects in an **Arena** (batch) or as **Heap objects** (individual, from a pool) | node teardown or reset; individual destroy by the owner | every long-lived object |
| **Stack** | scratch arena, tail-region mark/restore | restore, strictly LIFO | pass-local temporaries |
| **Temp** | memtracked allocation held by an RAII owner | the holder's scope exit | function-scoped buffers |

Pointers go from shorter-lived to longer-lived: Stack and Temp objects may point into the Heap; a Heap object never points at Stack or Temp memory.

Per D4.2.5v3 the tree is the ownership mechanism itself. `memtrack` statistics stay diagnostic.

## 3. The ownership tree

### 3.1 Nodes

A node is an owner with a lifetime. It has one parent, owns its allocators, and owns its child nodes. Nodes are `MemContext` nodes (D4.2.1v3) given an object-level meaning.

| Node | Parent | Allocators today (factory label) | Lifetime |
|---|---|---|---|
| Heap root | — | — | process |
| UI context | root | `ui.font.pool`, `ui.font.arena`, glyph arena; image cache; `rdt.vector.tvg` | window |
| Document | UI context | `dom.document.pool`, `dom.node.arena`, style-epoch pool | navigation |
| View tree | Document | `view_tree.prop_pool`, canonical prop arena, `view_tree.scratch_arena` (backs the Stack) | document; props reset at retained-layout boundaries |
| Retained display-list cache | Document | `retained_dl.arena` | document |
| Tile grid / worker | UI context | `tile.arena` | window |
| Task | whichever node starts it | `render.svg_inline`, `render.svg_layer`, `render.effect.*`, `render.*.backdrop.arena`, `render.document_transform`, `iframe_srcdoc`, `css.active_rule_program` | one operation |
| Document state (next phase) | Document | `state.arena`, `state.dirty`, `state.reflow`, `state.store` | document |

Measured gap: 32 allocator creations in `radiant/` pass a `NULL` parent context, so they hang directly off the root and the context tree is nearly flat today. Only the DOM document's pool and arena, the layout scratch, and the cascade scratch name their owner.

### 3.2 Tree rules

- **One owner edge per object.** An object is owned by a node (it lives in the node's arena or pool) or by another object's owning pointer field.
- **Tracing** starts at the root and follows owner edges with a per-type field map. Nothing is reachable only through a non-owning pointer.
- **Static target types.** No `void*` field in a Heap object; a tagged union names its tag.
- **Teardown at quiescent points** (R15): a node is destroyed or reset only at the top of the event loop or at a layout-pass boundary, never under frames that may hold pointers into it.

## 4. Pointer kinds

Every pointer field is one of:

| Kind | May point to | Typical use |
|---|---|---|
| **own** | an object this field owns; traced and torn down with the owner | element to its props; struct to its arrays and strings |
| **up** | an object in the same node or an ancestor node, or (from a Stack/Temp object) anything in the Heap | parent/sibling links; element to document; `LayoutContext` to the current view |
| **counted** | a Heap object owned elsewhere, kept alive by a count | font handle, vector picture |
| **handle** | anything else: index plus generation into a slot table | display-list item to an image surface; cache entry to a view |
| **foreign** | a vendor resource behind an opaque token | ThorVG, FreeType, platform objects |

Choosing between the last three: use *counted* when the holder needs the target to stay alive; *handle* when the holder can cope with its absence; *foreign* when the target is not Radiant memory.

A counted object still has exactly one owner in the tree (a cache, or the Heap root). The count pins it against recycling; holders are not owners.

### 4.1 Handles

A handle is a checked reference to an object that may die before the holder does.

- A **slot table** is owned by a node that outlives every holder. A slot is `{pointer, generation}`; a handle is `{index, generation}`, one handle type per target type.
- **Lookup** bounds-checks the index and compares generations. It reads only the table, never the target.
- **Invalidate:** when the target dies its owner increments the slot's generation and recycles the slot. A slot whose generation would wrap is retired.
- A pointer obtained from lookup is valid until the next quiescent point. It is held in locals or on the Stack and never stored in the Heap.
- When a whole node resets at once, one generation on the node replaces per-object slots. `MeasurementCacheEntry.generation` against `tree->measurement_cache_generation` already has this form.

Known sites in scope:

| Holder | Target | Today |
|---|---|---|
| `DlDrawImage` in a retained display list | `ImageSurface` | `void* resource_owner` + `resource_generation`; `retained_dl_surface_generation_current` reads `surface->generation` through the pointer it is validating |
| `DlVideoPlaceholder`, webview layer item | `RdtVideo`, webview `ImageSurface` | `void*` + generation, same pattern |
| Glyph display items | bitmap in the font glyph arena | generation against `glyph_cache_generation` (node form) |
| Flex measurement cache, `layout_cache` | per-layout-generation results | node form |
| SVG layer cache entry | DOM subtree | `node_id` + generation |

## 5. Kind templates

The kind is part of the field's type, extending `lib/ownership.hpp`:

```cpp
struct ViewBlock {
    Up<ViewElement>      parent;   // same node or ancestor
    Own<BlockProp>       blk;      // owned, traced
    OwnArr<float>        col_x;    // owned block of scalars
    Counted<FontHandle>  font;     // pinned
    Handle<ImageSurface> image;    // slot + generation, not dereferenceable
    Foreign<RdtPicture>  pic;      // vendor token
};
```

### 5.1 Requirements

- **Pointer-sized and trivial.** One pointer member (`Handle`: two 32-bit integers), defaulted default constructor, no destructor, no copy logic. Radiant structs are zero-allocated from pools and copied with `memcpy`, never constructed. `static_assert`s on size, alignment, triviality and standard layout pin this; it is rule 5 of the template series ("preserve ABI layout").
- **Reads unchanged.** Implicit conversion to `T*` and `operator->`, so existing read sites compile as they are.
- **Writes explicit.** A write needs the kind's constructor (`Own<T>(p)`, `Up<T>(p)`). No implicit raw-to-kind conversion and no up-to-own conversion; `Own<T>` gives an `Up<T>` through `borrow()`. `Handle<T>` has no dereference, only lookup.
- **No `void`.** The wrappers reject `T = void`.
- **Locals and parameters stay raw.** A raw `T*` in a local or parameter is a borrow for the current turn (template rule 1: wrappers at ownership boundaries, not everywhere).

A compile probe of this shape passed with clang++ in C++17 on 2026-10-02: trivial, pointer-sized, `calloc`/`memcpy` safe, implicit write rejected.

### 5.2 The set

| Template | Kind | Notes |
|---|---|---|
| `Own<T>` | own | single object |
| `OwnArr<T>` | own | block of scalar elements (`float*`, `bool*`, `uint32_t*`, `char*` strings); no element tracing needed |
| `OwnSpan<T>` | own | pointer plus length, for arrays whose elements hold pointers; replaces a `(ptr, count)` field pair |
| `Up<T>` | up | |
| `Counted<T>` | counted | assignment only through the retain/release helpers of the one `lib/` count implementation |
| `Handle<T>` | handle | |
| `Foreign<T>` | foreign | `T` is an opaque vendor type |
| `Temp<T>` | Temp class | the existing `OwnedPtr<T, LayoutSessionDomain>` / `SessionPtr<T>`; RAII, for locals only |

Each Heap struct declares its node once through a trait, in the manner of `ViewTagToType`:

```cpp
template<> struct NodeOf<ViewBlock> { typedef DocumentNode type; };
```

### 5.3 Relation to the existing templates

- `OwnedPtr` has a destructor and move semantics. It is right for Temp and cannot be a field of a pool object. It is kept and aliased.
- `PersistentFieldRef` wraps the write site while the field stays a raw pointer, and the caller asserts the source domain with a cast (`lam::GcPtr<char>((char*)"monospace")`). Once fields carry their own kind it is retired, together with the name-pattern lint `retained-field-write`.
- The four domains (`GcHeapDomain`, `PoolDomain`, `LayoutSessionDomain`, `InputScratchDomain`) become node tags plus Stack and Temp; `DomainOutlives` becomes the ancestor relation of §3.1.

### 5.4 What the templates check and what they do not

| Checked by the compiler | Checked by lint | Not checked by either |
|---|---|---|
| a field's kind is declared | no raw pointer field and no `void*` in a Heap struct | that `Up<T>(p)` is given a pointer that really is in the same or an ancestor node |
| kinds are not mixed on assignment | every `Up` field satisfies the outlives relation between holder node and target node (needs the enclosing struct, which the wrapper cannot see) | that a pointer taken from lookup is not kept past a quiescent point |
| a handle is not dereferenced | `Counted` writes go through the helpers | array bounds; tag and layout agreeing in a union |

The right-hand column is the gap the series has always stated: templates encode state at a point, not flow over time (`Memory_Safety_Review.md`). §6 covers it at run time and §9 statically.

## 6. Heap tracer

A debug-build walk from the Heap root over owner edges, using field maps generated from the kind types. At each field it checks:

- **own:** the target lies in storage of the holder's node or a child node (`pool_owns` / `arena_owns`), and is reached exactly once;
- **up:** the target lies in the same or an ancestor node;
- **counted:** the count is positive and the target's owner is live;
- **handle:** the index is in range for its table.

It runs at quiescent points under a flag and in the test suites. The field maps come from the same declarations the compiler sees; `lib/typemeta.h` already defines a descriptor format and has no users.

## 7. Rules (extending R1–R7)

- **R8 — Stack is LIFO only.** `scratch_free` is replaced by `scratch_mark` / `scratch_restore` scopes.
- **R9 — `arena_free` and `arena_realloc` are removed.** Closes the D4.1.4v4 conformance gap. `ScratchArena` moves to the tail-region mark/rewind variant D4.1.4v4 describes.
- **R10 — Long-lived memory is in the Radiant Heap.** `mem_*` is for Temp only and always behind an RAII holder. Heap roots allocated with `mem_calloc` move into their parent node's pool; long-lived container backing stores become pool-backed (D4.1.5 owner-backed case).
- **R11 — One owner per object; one kind per pointer field.** No raw pointer field and no `void*` in a Heap struct.
- **R12 — Pool objects are typed.** A pool that frees individually holds one object type with a free list (`DocState.range_freelist` is the existing example); this is the type-stable pool of D4.5.1v3.
- **R13 — One container family.** One growable array, string builder, hash map, slot table and count implementation, each able to take its storage from a node. `Span` is the only way to pass a pointer with a length.
- **R14 — One float-to-int conversion and one always-on check.** A saturating helper replaces raw `(int)` casts of floats that feed an index or size; `*_require` downcasts use a check that survives `NDEBUG`.
- **R15 — Nodes die only at quiescent points.**
- **R16 — Cross-lifetime references are handles.** Ad-hoc generation counters are replaced by the slot table or the node generation of §4.1.

## 8. Not changed

- The allocators themselves, except R9 and the typed-pool layer of R12.
- Struct layouts, except where a `(ptr, count)` pair becomes `OwnSpan` or a `void*` plus generation becomes a `Handle`.
- `extern "C"` surfaces and C views of structs: a wrapper is layout-identical to the pointer it replaces.
- Vendor code.

## 9. Relation to full verification

The kind templates are the single declaration of ownership in this code base. Some properties cannot be expressed by a kind in a type: relations between a length and an array, a union tag and its layout, loop invariants, the identity of one node instance as against another. Where full static verification needs those, they are stated and proved by an external verification effort maintained outside this repository. No verifier-specific annotation enters Radiant source.

What this code base owes that effort is stability of the declarations in this document: kinds on every pointer field, a node for every Heap struct, no `void*`, the rules R8–R16, and always-on checks at the places a proof would otherwise need arithmetic.

---

# Part II — Migration plan

## 10. Inventory (measured 2026-10-02)

### 10.1 Pointer fields to classify

| Header | Structs with pointer fields | Pointer fields |
|---|---|---|
| `lambda/input/css/dom_node.hpp` + `dom_element.hpp` | — | 99 |
| `radiant/view.hpp` | — | 163 |
| `radiant/layout.hpp` | — | 157 |
| `radiant/render.hpp` | — | 124 |
| **Total** | **184** | **543** |

By target type: `char*` 71, `void*` 42, `float*` 38, `ViewBlock*` 19, `DomElement*` 15, `LayoutContext*` 14, `DomNode*` 13, `RdtPath*` 13, `bool*` 10, `uint32_t*` 10, `ImageSurface*` 9, `FontProp*` 9, `Pool*` 9, `Arena*` 7, others below 8 each.

First-pass classification of the core types, to be confirmed in M0:

| Struct | Fields | Proposed kind |
|---|---|---|
| `DomNode` | `next_sibling` | own (each child owns the next; the tracer's path through a child list) |
| | `parent`, `prev_sibling` | up |
| `DomElement` | `first_child` | own |
| | `last_child` | up |
| | `doc` | up |
| | `tag_name`, `id`, `class_names` | own string / own array |
| | `font`, `bound`, `in_line`, `blk`, `tb`, `td`, `form`, `scroller`, `embed`, `position`, `transform`, `pseudo` | own — but the storage is the view tree's `prop_pool`, a descendant node (open point O1) |
| | `layout_cache` | own; validity by node generation |
| `DomDocument` | `document_pool`, `node_arena` | own (node allocators) |
| | `root`, `view_tree`, `stylesheets` | own |
| | `embedding_document` | up |
| `ViewTree` | `prop_pool`, `canonical_prop_arena`, `scratch_arena`, `measurement_cache` | own |
| | `root` | up (the root element is owned by the document) |
| `PositionProp` | `first_abs_child` … `next_static_inline_position` | up |
| `FontProp` | `font_handle` | counted |
| | `family` | own string |
| `ImageSurface` | `pixels` (`void*`) | own array of pixels, or foreign when borrowed from a decoder (O2) |
| | `pic` | counted |
| `EmbedProp` | `img`, `poster` | counted or handle (O2) |
| | `doc` | own (child document node) |
| `LayoutContext`, `RenderContext` (Stack structs) | `view`, `elmt`, `doc`, `ui_context`, `dl`, … | up |
| `DlDrawImage` and similar | `resource_owner`, `video`, `surface`, `src_surface`, `filter` (`void*`) | handle, or typed up when the list never outlives the frame |

### 10.2 `void*` fields

42 at struct level; 27 of them in `render.hpp`. Groups: display-list resource references (`resource_owner`, `video`, `surface`, `src_surface`, `filter`, `backdrop_filter`, `font`), SVG render context (`svg_root`, `id_scope`, `pool`, `font_context`, `style_rules`), backend contexts (`ctx`, `image_resolver_context`), vendor objects (`tvg_animation`, `tvg_canvas`), pixel buffers (`ImageSurface.pixels`), and `view.hpp:208–209` (`target`, `state`).

### 10.3 Allocation and free sites in the four areas

| Operation | Sites | Largest files |
|---|---|---|
| `scratch_free` | 93 | `layout_table_metadata.cpp` 17, `layout_multicol.cpp` 13, `layout.hpp` 10, `intrinsic_sizing.cpp` 7, `render_filter.cpp` 6, `render_background.cpp` 6, `layout_table.cpp` 6, `grid_positioning.cpp` 6 |
| `scratch_alloc` / `scratch_calloc` | 133 | |
| `scratch_mark` | 17 | |
| `mem_free` | 192 | `render_svg_inline.cpp` 33, `surface.cpp` 29, `rdt_vector_tvg.cpp` 24, `canvas_2d.cpp` 13, `font_face.cpp` 12, `font.cpp` 9, `grid_utils.cpp` 8 |
| `mem_alloc` / `mem_calloc` | 82 | |
| `mem_strdup` | 30 | `render_svg_inline.cpp` 12, `font_face.cpp` 5, `surface.cpp` 4 |
| `pool_free` | 8 | `view_reuse.cpp` 6, `view_pool.cpp` 2 |
| `pool_alloc` / `pool_calloc` / `alloc_prop` | 37 / 45 | |
| `arraylist_new`/`free`, `hashmap_new`/`free`, `strbuf_new`/`free` | 33 / 10 / 45 | backing stores come from the system heap in `lib/arraylist.c`, `lib/hashmap.c`, `lib/strbuf.c` |
| `lam::ArrayList<T>` | 12 | |
| Allocators created with a `NULL` parent context | 32 (all of `radiant/`) | |

## 11. Steps

Every step is independently green. Gates for all: `make build`, `make test-radiant-baseline` at 100%, `make layout suite=baseline`, `make lint`, and the memory dump report clean. A step that changes `lib/` also runs `make test-lambda-baseline`.

### M0 — Review and rulings (no code)

1. Replace §10.1 with an AST-accurate inventory: every struct allocated in the four areas, its node, its storage, how its lifetime ends.
2. Classify all pointer fields; give each `void*` a static type; list every field that fits no kind.
3. Fix the node list of §3.1 and the slot tables of §4.1.
4. Rule on R8–R16 and the open points of §13; draft the D4.5 revision.

*Deliverable:* the classification table as an appendix to this document. *Gate:* review.

### M1 — Platform in `lib/` (no production call sites change)

1. Kind templates in `lib/ownership.hpp`: `Own`, `OwnArr`, `OwnSpan`, `Up`, `Counted`, `Handle`, `Foreign`, `Temp` alias, `NodeOf`, node tags, the outlives relation.
2. Slot table (C, `lib/`) with the generation and retirement rules of §4.1.
3. Typed pool with free list (R12), generalising the `DomRange` free list.
4. Node API over `MemContext`: create a node under a parent, obtain its arena and pool, tear down a subtree.
5. Containers able to take storage from a node's pool (`hashmap_new_with_allocator` exists; `arraylist` and `strbuf` need the equivalent), per D4.1.5.
6. Always-on check macro; saturating float-to-int helper (R14).
7. Tests: extend `test/test_own_tagged.cpp` with positive probes and negative compile probes (implicit write, up-to-own, `void`, handle dereference), plus slot-table and typed-pool unit tests.

*Gate:* `lib` tests; no change in Radiant behaviour or binary size beyond noise.

### M2 — Build the tree

1. Parent each of the 32 `NULL`-context allocators under its owner node.
2. Move heap-root structs allocated with `mem_calloc` (document and view-tree containers, font-face descriptors, media players) into the parent node's pool, turning R2's `OBJ_HEAP_OK` list into moves.
3. Audit every node teardown and reset path against R15; record the quiescent points.

*Gate:* the memory dump shows the node tree of §3.1 with nothing but nodes under the root; a navigation stress run leaves the root's child count unchanged.

### M3 — Stack (R8, then R9)

1. Convert the 93 `scratch_free` sites to mark/restore scopes, file by file in the order of §10.3. `FlexLayoutScope` and `GridLayoutScope` (`layout_flex.cpp`, `layout_grid.cpp`) already use mark/restore and are the pattern to follow.
2. Then remove `arena_free`/`arena_realloc` from `lib/arena.h`, move `ScratchArena` to tail-region mark/rewind, and delete the arena free-list code.
3. Lint: ban `scratch_free`, `arena_free`, `arena_realloc`.

*Gate:* scratch live count is zero at every pass exit in debug builds.

### M4 — Temp versus Heap for `mem_*` (R10)

For each of the 192 `mem_free` / 82 allocation / 30 `mem_strdup` sites, decide:

- function-scoped: wrap in `Temp<T>` so the free is the holder's scope exit;
- pass-scoped: move to the Stack;
- longer-lived: move to the owning node's arena or pool.

Order by file: `render_svg_inline.cpp`, `surface.cpp`, `rdt_vector_tvg.cpp`, `canvas_2d.cpp`, `font_face.cpp`, `font.cpp`, `grid_utils.cpp`, then the remainder. Long-lived `arraylist`/`hashmap`/`strbuf` instances switch to node-backed storage here.

Lint: bare `mem_free` in the four areas becomes an error; the `radiant-alloc-allowlist` and `obj-heap-ok` warnings become errors.

*Gate:* no `mem_free` outside `Temp` and `lib/` in the four areas.

### M5 — Field kinds, header by header (R11)

One sub-step per header, in dependency order:

| Sub-step | Header | Fields | Notes |
|---|---|---|---|
| M5.1 | `dom_node.hpp`, `dom_element.hpp` | 99 | tree links and prop pointers; decides O1 |
| M5.2 | `view.hpp` | 163 | props, `ViewTree`, `ImageSurface`; most `char*` strings |
| M5.3 | `layout.hpp` | 157 | mostly Stack structs, so mostly `Up`; `float*`/`bool*` arrays become `OwnArr` |
| M5.4 | `render.hpp` | 124 | display-list items; most of the `void*` fields |

Per header:

1. add `NodeOf` for each struct;
2. change field types to kind templates; the compiler then reports every write site;
3. fix write sites; a write that cannot be given a kind is a finding, not a cast;
4. extend the lint rule "no raw pointer field, no `void*`" to that header, as an error.

`PersistentFieldRef` call sites and the `retained-field-write` rule are removed for a header when its fields are converted.

*Gate, in addition to the standing gates:* a release build shows no layout-time or render-time change beyond noise (the wrappers are meant to compile away; this confirms it).

### M6 — Counted and Handle (R16)

1. Slot tables for image surfaces, video and webview surfaces; display-list items hold handles. Removes the `void*` + generation fields and the read-through-the-pointer check.
2. `ImageSurface` gets one owner; `cache_owned` / `network_owned` flags are replaced by ownership in the tree plus counts.
3. `RdtPicture`, `FontHandle` and other counted objects use the one `lib/` count implementation through `Counted<T>`.
4. Node-generation checks (glyph cache, measurement cache, layout cache, SVG layer cache) use one shared form.

*Gate:* an eviction stress test (image cache and picture cache evicting under a live retained display list) passes under ASan.

### M7 — Typed pools (R12)

The view tree's `prop_pool` (45 `alloc_prop` sites, 8 `pool_free` sites in `view_reuse.cpp` and `view_pool.cpp`) becomes per-type slabs, or an arena where props are never individually freed. Decided per prop type in M0.

*Gate:* no `pool_free` on an untyped pool in the four areas.

### M8 — Tracer and enforcement

1. Generate field maps from the kind declarations; implement the walk of §6.
2. Run it at quiescent points in the Radiant test suites and the layout baseline.
3. All new lint rules at error severity; rules R8–R16 recorded in the formal design.

*Gate:* the tracer reaches every live object and reports no violation across the suites.

## 12. Order and risk

| Step | Depends on | Size | Risk |
|---|---|---|---|
| M0 | — | review of 543 fields | low |
| M1 | M0 rulings | new `lib/` code and tests | low |
| M2 | M1 | 32 allocators, heap roots | medium: teardown order |
| M3 | — | 93 sites + `lib/arena.c` | medium: nested flex/grid scopes |
| M4 | M1, M2 | ~300 sites | medium |
| M5 | M1, M0 table | 543 fields and their write sites | high volume, mechanical; compiler-driven |
| M6 | M5.2, M5.4 | display-list and cache references | medium: behaviour changes on eviction |
| M7 | M5.1 | 53 sites | medium |
| M8 | M5–M7 | tracer + lint | low |

M3 can start at once; it needs no ruling beyond R8/R9. M5 is the bulk of the work and is safe to do in small commits because each field conversion either compiles or names its write sites.

Risks to watch:

- **Layout or ABI drift.** Guarded by the `static_assert`s of §5.1 and by leaving C views untouched.
- **Read-site ambiguity.** Implicit conversion to `T*` can make an overloaded call or a ternary ambiguous; those sites need `.get()`.
- **Hidden lifetime dependencies.** Parenting allocators (M2) changes teardown order; R15's audit comes first within M2.
- **Concurrent edits.** M5 touches the central headers; each sub-step should land in one sitting.

## 13. Open points

- **O1 — Owner and storage in different nodes.** Element-owned view props live in the view tree's `prop_pool` while the element lives in the document; retained reset and destroy handle the pointers by a walk (`view_teardown_visit_node` with `VIEW_TEARDOWN_RESET_IN_PLACE` or `VIEW_TEARDOWN_CLEAR_POINTERS`). Options: keep `Own` with mandatory clearing at reset, checked by the tracer; or make the props a view-tree-owned side table reached by handle.
- **O2 — Image surfaces.** `ImageSurface` can be owned by the image cache, a network resource or a webview. One owner must be chosen; holders become counted or handle.
- **O3 — Outlives check for `Up`.** The wrapper cannot see its enclosing struct. Options: a type-aware lint over the Clang AST (precise), or a per-struct `static_assert` list (in the language, but repetitive).
- **O4 — Field-map generation for the tracer.** Generated from the AST into `lib/typemeta.h` descriptors, or hand-written per struct and checked for completeness by lint.
- **O5 — Arrays of pointer-bearing elements.** `OwnSpan<T>` changes layout where the count is stored separately today; confirm that is acceptable or keep the count field and name it in the declaration.
