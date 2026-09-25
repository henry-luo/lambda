# Lambda Shape Transitions — Design

**Date:** 2026-09-25
**Status:** RATIFIED 2026-09-25 as **D3.4.3v2** (Formal Design 13.0.0). Maps: implemented. Elements: design settled 2026-09-25 (§7); E1–E7 landed the same day (plan phases P0–P3): elements and the editor's rebuilds share through the tree, and the shape pool is retired. Plan: [Impl_Element_Type_Sharing](impl/Lambda_Impl_Element_Type_Sharing.md). Revised the same day as **D3.4.3v3** (Formal Design 14.0.0): a path shares its field entries, not only its types (§3, *Sharing the prefix*); implemented.
**Scope:** how a map or element obtains its `TypeMap`, when that type is shared, and what may change a shared type. Out of scope: the shape pool's internals ([Lambda_Shape_Pool.md](Lambda_Shape_Pool.md)) and JavaScript property semantics ([JS_06](../doc/dev/js/JS_06_Objects_Properties_Prototypes.md)).
**Spec linkage:** §2–§6 → D3.4.3v3. Within them, type identity (§3) → D3.4.2, D3.4.4v2, D3.4.7; "metadata, not a cache" (§3) → D8.4.1v2; layout-changing writes (§4) → D3.4.5. §7 → D3.4.3v3's element clause, D2.6.6v3 (the declared content pattern; a nominal name is not a tag), S11.1.6v3 (element content is a sequence-pattern slot), SO46 and DO31 (open).
**Relation to prior docs:** distils the transition-tree decisions recorded in [JS Tune10](<jube/JS_Tune10_Fast_Paths (done).md>) §9.3–§9.9 (the add-transition choice, the reverted and re-enabled root, the bounds) and [Transpile_Js_Tune11](jube/Transpile_Js_Tune11_Callsite_Cache.md) (the edge table). Name identity on edges follows [Lambda_Design_Name_Identity.md](Lambda_Design_Name_Identity.md). Supersedes [Lambda_Shape_Pool.md](Lambda_Shape_Pool.md) as the sharing mechanism for maps (Appendix S).

---

## 1. Why share types

A map's values live in its own data buffer; its `TypeMap` and `ShapeEntry` chain only describe the layout. Unshared, every map carries its own type and chain, so record-shaped data — a JSON array of objects, or objects built by one constructor — pays for one layout per object where one per shape would do.

The case is memory, not speed (Tune10 §9.9). The clearest measurement is indirect. When the A1v2 change (2026-09-25) removed 256 bytes from every `TypeMap`, the JSON parse peak stayed at 191 MB, because its maps already share types. The HTML parse peak fell from 315 to 233 MB, because every parsed element still has a private type (§7).

## 2. Where a map's type comes from

- **Literal.** A literal such as `{a: 1, b: 2}` is compiled into one `TypeMap`, and `map(type_index)` attaches it to every map that literal creates. It is shared by construction. Element literals share their `TypeElmt` the same way (`elmt_with_type`).
- **Transition tree.** A map built one field at a time — by a parser through `MarkBuilder`, or a JavaScript object through `map_put_heap` — takes its type from its `Input`'s tree (§3).
- **Published family shape.** A JavaScript runtime family may publish one sealed shape for its instances (the RegExp instance shape), flagged `is_shared_constructor_shape`. The per-call-site constructor shapes that once used this flag were retired with the inline caches ([JS_06](../doc/dev/js/JS_06_Objects_Properties_Prototypes.md) §10); constructor-built objects now share through the tree.
- **Private.** A type owned by one map: a fresh type where the tree declined, or a detached clone (`is_private_clone`). Only a private type may grow in place.

`typemap_is_shared_shape` is true for tree and published shapes. A literal's type carries no flag, but runtime layout changes never edit it in place: they build new types (§4).

## 3. The transition tree

Each `Input` owns one empty root shape, created on first use. A new map starts on the global `EmptyMap`, and its first add moves it into the tree at that root.

Adding a field to a map whose type is in the tree follows the edge keyed by:
- the field's identity: its `name_id` (D3.4.4v2), or, for an id-less `Input` name, its bytes — a pooled key never matches through the bytes;
- its key kind;
- the value's `TypeId`.

On a hit the map's type becomes the child, and the value is stored at the child's new offset: nothing is allocated. On a miss the child is minted once — the parent's entries plus one new entry at offset `parent.byte_size`, with its lookup table and fixed-slot prefix — registered in the `Input`'s type list, and the edge is recorded on the parent. The child takes the parent's entries by extending its chain in place, or copies them when another child already has (*Sharing the prefix* below). A hit re-checks that the child still extends its parent: a child that shares the parent's chain passes at once, and a copied one is walked, O(depth), measured at 1.5% of an add at 8 fields and 3.1% at 256.

```
root ──id:int──► {id} ──name:string──► {id, name}
```

Every map built as `id` then `name`, with those value types, lands on the last node. Consequences:
- **Identity is the path.** Field order is significant (D3.4.2): `{a, b}` and `{b, a}` get different types, as do `{id: 1}` and `{id: "x"}`. JavaScript class metadata is part of identity too (D3.4.7): a root serves one `js_meta`, and a map whose blueprint carries different metadata keeps a private type.
- **No semantic effect.** Map equality is key-unordered (S5.4.1), `is` matches fields by name, and a map prints in its own insertion order (S2.3.1), whichever type it lands on.
- **Metadata, not a cache.** The edge list is shape metadata keyed on immutable inputs and attached to a shape, never to a call site, so D8.4.1v2 holds.

Only plain maps enter the tree, and only with ordinary string keys. Symbol and private keys, which need their own identity, and array-index keys on array property maps keep a private type.

### Where the tree lives (2026-09-25)

Nodes and edges come from the `Input`'s arena: the roots, each child with its entries, lookup table and slot array, and each `TypeMapTransition` (`input_tree_alloc`, `TypeAlloc`). None is ever freed on its own, which is the arena's side of D4.1.4v4 (sequential allocation, batch free), and the arena goes with the `Input`, which the pool it lives in now owns (D4.2.6). Before, they came from the `Input`'s pool, whose per-block free the tree never used.
- **The element-root table is the exception.** Growing it replaces it and frees the old one, so it stays a pool block. A replaced table used to be left in the pool until the pool died.
- **Private types stay in the pool:** clones, fresh types where the tree declined, and editor rebuilds that own their chain.
- **JavaScript descriptor retags** (`js_property_attrs.cpp`) keep `js_input`'s pool for their edges and targets. A retag target can be an existing private clone promoted in place, so moving them would change which object is cloned, not only where it is stored.

Measured before the move on a release build, over two 13 MiB corpora: creating types took 0.36 ms of a 1,260 ms parse (layout tests: 1,831 types, 9,892 allocations) and 0.89 ms of 69 ms (77 real sites: 5,179 types, 33,379 allocations). A zeroed 64-byte block costs 13.6 ns from the pool and 3.7 ns from the arena, so the arena saves about 0.1 and 0.33 ms, plus the pool's 32-byte header on each allocation (0.3 and 1.1 MB). The tree's time is in its edge lookups, which the allocator does not touch: 98.7% and 94.6% of attribute adds follow an existing edge.

### Sharing the prefix (2026-09-25, D3.4.3v3)

**The problem.** Until v3 every node copied its parent's whole chain and built its own lookup table, so a path of *n* fields cost *n*(*n*+1)/2 entries (104 bytes each) and *n* tables sized to their node. Records are cheap — a 20-field shape pays 210 entries once — but one wide object is not: a JSON object with 1,000 keys parsed to a 98.8 MB peak (17.5 MB at 100 keys), and a JavaScript object used as a dictionary (`o[key] = v` for 1,000 keys) to 84.7 MB. Growth stopped at 1,024 keys only because the map budget (§5) ran out. The chain could not be shared as it was: it is linked forward and ends in NULL, so `a->next` cannot lead to both `b` and `c`, and `{a}`'s own walk must end after `a`.

**The design — the owner of the tail extends it (V8's descriptor sharing).**
- **Entries.** A child extends its parent's chain in place while the parent still owns the chain's tail — `parent->last` has no successor yet. The parent keeps its view (`shape`, `last`, `length`); the child's view is one entry longer. Once another child has taken the tail, a branch copies the prefix once and starts a chain of its own, which its children extend in turn. A linear path therefore holds *n* entries; `{a, b}` and `{a, c}` hold four (the branch's copy of `a` is the extra one), where a trie would hold three.
- **Walks stop at `last`.** A type's fields run from `shape` through `last`, and the chain may continue into a descendant's fields, at offsets outside this type's maps' data. `FOR_EACH_MAP_FIELD`, `typemap_next_field` and `shape_chain_next_until` (for a chain still being assembled) are the only steps; the collector reads `last` through the GC ABI (`LAMBDA_GC_OFF_TYPE_MAP_LAST`). A type whose `last` is unset owns an unshared chain and is walked to its end — such a type is never extended in place.
- **The link is renamed.** `ShapeEntry::next` became `chain_next`, so every raw walk failed to compile and was converted — 181 sites in 36 files, tests included — and a new one cannot slip in unnoticed; the lint rule `no-raw-shape-chain-walk` flags any read of it outside the audited `SHAPE_CHAIN_OK` sites, while assigning to it (linking) stays free.
- **Lookup tables are shared along the chain.** An in-place child takes its parent's table while the table has room for it (at least twice the child's length) and holds no entry with the added field's identity; otherwise it builds its own, which its own in-place children then share. Tables thus grow by doubling along a path, and each type sees only its own entries: an entry the tree appended records its position (`chain_index`, in padding — `ShapeEntry` stays 104 bytes), and a type treats a hit at or past its length as a miss (`typemap_hash_entry_is_own`). A hit on another type's entry answers *absent* immediately, never by walking the chain: record #2 of a JSON array looks up each of its keys on a node whose table already holds that key from record #1's path, and a walk there would make every row quadratic. That is sound only because a shared table never takes a second entry for an identity it holds — a repeated name (`{"a": 1, "a": "x"}` adds a second `a`) gets its own table, so the first `a`'s slot, which `{a}` still reads, is never overwritten. Entries the tree did not append carry position 0, which every type of a chain contains, so private and literal types never consult their length.
- **Slot prefixes are shared too.** An in-place child's fixed-slot prefix is the parent's own entries, so it takes the parent's `slot_entries` array.
- **Immutability, restated.** No type gains, loses or reorders a field it has: the only write to a shared type's records is its tail's link, set once, from NULL, and past its `last`. Retags never write a shared entry — a type-changing write on a shared shape detaches first (`map_shared_ctor_shape_should_detach_for_type`) — so sharing entries along a path cannot spread one map's retag to another type.

**Measured.** Release builds, medians of five interleaved runs: the 1,000-key JSON object's parse peak 94.1 → 16.5 MB; two 13 MiB HTML corpora (real-site pages 99.3 → 99.0 MB, layout tests 104.7 → 104.5 MB) and 60K 20-field JSON records (102.7 MB either way) are unchanged in memory and parse time — their paths are short or already shared. Debug builds: 2,000 keys 103.1 → 18.1 MB, a 1,000-key JS object 84.7 → 18.4 MB. The test262 batch peak is unchanged (643.9 vs 639.8 MB; it varies by 150 MB between runs). The path's tree now takes about 0.4 MB of arena (`TransitionTreePrefixSharingTest.WideObjectKeepsOneChain` pins it under 4 MB).

**Rejected.**
- **Reverse-linked chains** (a real trie: one entry per node, pointing back at the parent's) share branches too, but field order is identity (D3.4.2v2), so enumeration would need a reversal, and the GC ABI walks forward. Branch copies are bounded by the 16-edge fan-out and short in practice.
- **Pointer arrays over shared entries** (8 bytes per field per node instead of 104) stay quadratic and change every walk anyway.
- **A depth cap** bounds the copies but sends every wide record — a 100-column CSV row — to a private type per row, which costs far more.

**Pitfalls met.**
- A stale `last` would now truncate a walk. Types whose `last` was never set (the transpiler's map literals) keep unbounded walks; the one builder that joins two literal chains (`build_ast` map merge) now moves `last` with the join when it is set.
- A `ShapeEntry` copied by struct assignment into a private chain must drop its `chain_index` (`map_extend_open_shape`), or a later field removal could make an own entry look foreign.
- Dead tail searches (`while (e->next) e = e->next`, the TOML table header path) would have found a descendant's last field; they had no reader and are gone.

## 4. A shared type's field list is immutable

Nothing edits a shared type's fields in place:
- An add with no usable edge — a bound reached (§5), or a key that may not enter the tree — first detaches the map onto a private clone (`map_clone_typemap_for_mutation`), then appends to the clone.
- At runtime, adding a field (`map_extend_open_shape`, the miss path of `fn_map_set`) and a write that changes a field's type (`map_rebuild_for_type_change`, D3.4.5) build a new private type.
- A type-compatible write (D3.4.5) touches only the map's data buffer.
- The one write a shared type's records ever see is its tail's link, set once by the first child that extends the chain (§3, *Sharing the prefix*). It lies past the type's `last`, so no walk or lookup of that type reaches it.

## 5. Bounds

The tree is bounded four ways. Past any bound a map or element keeps a private type, which is exactly the behaviour before the tree existed.
- **Root fan-out: 256 edges.** The root's out-degree is the number of distinct first fields in everything the `Input` builds. Capped at 16 like other nodes, it saturated after about 835 adds, and every later map fell back to a private type (Tune10 §9.9).
- **Interior fan-out: 16 edges.** A node with more outgoing edges is a dictionary-shaped site (per-record keys), where a linear walk of the edge list on every add would not pay.
- **Graph budget: 1,024 shapes per `Input`.** The fan-out caps bound one node, not the graph. A long-lived `Input` running thousands of unrelated scripts (the test262 batch runner) kept minting shapes and reached 5.4 GB; with the budget its peak went from 5,662 to 891 MB.
- **Element budget: 16,384 element types per `Input`, counted apart (P1, 2026-09-25).** One document's attribute variety runs to thousands of distinct sequences: a 13 MiB corpus of 77 real sites needs 5,093 element types (179 tag roots), a 13 MiB layout-test corpus 1,775. On the map budget either would saturate early and leave most elements private; the map budget stays at 1,024 because it guards the long-lived JS `Input`s it was sized for. An `Input` that reaches the element budget logs `element_tree_budget` once.

## 6. Why maps left the shape pool

The shape pool (D3.4.3 v1, Appendix S) interned a map's chain at `final()`: the builder made a throwaway chain and the pool swapped in a shared one. For maps this had two costs:
- **Two chains per map**, the throwaway and the pooled one, and sharing only once the whole map existed.
- **Shared records that were not immutable.** JavaScript property flags live on `ShapeEntry`, so a pooled record would let one map's descriptor change alter another map.

On 2026-08-08 `map_finalize_shape` became a no-op, and since the shared root landed (2026-09-09) the tree carries map sharing. In between, parsed maps shared nothing. The edge table had existed since 2026-06-25, but `map_put` followed edges only from a type that was already shared, and a new map's first put minted an ordinary private type, so a parsed map never reached the tree. The tree shares from the first field, allocates each shape once, and never edits a shared record.

Since P2 the editor no longer uses the pool either: a rebuilt type comes from the tree, or is a type the container owns with a chain of its own (§8). P3 deleted the pool on 2026-09-25: a container built without an `Input` — a map or element grown at runtime — keeps a type of its own, and nothing deduplicates `ShapeEntry` chains any more.

## 7. Elements — design settled and implemented 2026-09-25

**Before this design.** Element literals shared their compile-time type, but every parsed element got a private `TypeElmt` from `ElementBuilder`, registered in the type list; `elmt_finalize_shape` swapped in a pooled attribute chain, but the `TypeElmt` itself was never shared. A `TypeElmt` is 168 bytes plus a 32-byte pool header; the 13 MiB HTML benchmark built about 320K of them, roughly 64 MB of its 233 MB peak. P1 measured the change on two 13 MiB corpora: a real-site one fell from 109.6 to 86.3 MB and a layout-test one (203K elements sharing 1,775 types) from 184.0 to 120.4 MB, parsing 1.43× faster. Until P0 the one field that differed between two `<p class="x">` elements was `content_length`, a copy of the child count — a field with three meanings: the declared content arity on a type pattern or nominal type (read by the validator and by nominal-kind selection), the number of content expressions on an element literal's type (read by the transpiler), and that per-instance copy on parsed elements (written by every parser, read by the Markdown table parser and one test).

**The design** (D3.4.3v3, D2.6.6v3, S11.1.6v3):
- **E1 — Layout.** `TypeElmt` keeps its `TypeMap` part — the attribute layout and the sharing machinery are required — plus `name`, `name_id` and `ns`. `TypeList* content_list` replaces `content_length` at the same size. It holds a DECLARED type's content pattern: the resolved `<tag attrs; c, d>` section as a typed `TypeList`, its slots filled the way a bracket pattern's are (`resolve_array_type`). It is NULL on every instance and literal type. NULL means content unconstrained; a present but empty list would mean *no children*, whose spelling is open (SO46) — until ruled, an empty content section yields NULL too.
- **E2 — Sharing.** A parsed or runtime-built element takes its type from the transition tree: a root per tag and namespace, attributes added as edges exactly as map fields are, so elements with one tag, namespace and attribute sequence share one `TypeElmt`. `content_list` plays no part in sharing; it is a validation artefact of declared types only.
- **E3 — Attribute changes after construction follow map rebuilds.** The HTML5 tree builder's attribute merge onto `<html>`/`<body>`, runtime `elmt_put`, DOM `setAttribute`/`removeAttribute`, and `MarkEditor` rebuilds and tag renames: an add follows one edge; a remove, retype or rename replays the attribute sequence from the tag root. Nothing writes a shared `TypeElmt` in place, which retires the editor's fresh-`TypeElmt`-per-edit rule along with the pool.
- **E4 — Construction keeps its pre-sizing.** The transpiler reads an element literal's content count from its syntax tree (`AstElementNode::content`), emits `list_fill(el, count, …)` for small counts as today, and a static element's content array is allocated once at that size. Nominal literals keep their per-literal counts on `AstObjectLiteralNode`.
- **E5 — Declared content is validated.** `validate_against_element_type` matches the element's normalized content (S2.6) against `content_list` with the bracket-pattern matcher (`array_pattern_runs_match`): runs with backtracking, literal items by value, the whole content. `is` and the validator both go through it; LR13-10 closes. A nominal type is element-kinded iff its `content_list` is non-NULL (D2.6.6v3, replacing the `content_length > 0` test), and `TypeNominal::content_length`, never read, goes.
- **E6 — Names.** A nominal type's name is never matched against a tag; a tag is enforced by a structural element pattern (`type e = <tag …>`) alone. `type <B> { … }` is deferred (DO31, OB23).
- **E7 — The shape pool retires (done, P3).** `shape_pool.cpp`/`.hpp`, `Input::shape_pool` and its memory-registry node, `elmt_finalize_shape`, `map_finalize_shape` and `shape_builder_finalize` are gone; `ShapeBuilder` remains as the editor's field list, its drafts in the editor's arena; D3.4.2v2 names the tree path, not a pool signature, as shape identity. Nothing compared shapes by pointer, and the transpiler's `shape_pool` field had no reader.

**Consequences worth stating.**
- Element content is normalized before it is matched, so `<p; string, string>` can never match — adjacent strings merge (S2.6.4).
- Open content is spelled with a trailing `any*`; a bare `<div>` is open too, by E1.
- The tree budget (§5) is shared by maps and elements per `Input`. Single pages need a few hundred element nodes (a Wikipedia article 286, a news page 386, counted as distinct tag-plus-attribute-name sequences), but a stitched corpus or custom-tag XML could exhaust 1,024 — measure on the 13 MiB benchmark before settling the bound.

Phases and gates: [Impl_Element_Type_Sharing](impl/Lambda_Impl_Element_Type_Sharing.md).

## 8. Open questions

- **Fixed 2026-09-25 (P2): inline map edits rewrote a shared type.** `container_rebuild_with_new_shape` rewrote a map's registered `TypeMap` in place on an inline edit, exempting only unregistered types and `EmptyMap`; a GTest confirmed that an edit then re-laid the fields under a sibling map built the same way. It now edits in place only a type the container owns (`is_private_clone`).
- **Done 2026-09-25 (P2): editor rebuilds go through the tree.** For maps and elements alike, the new field list replays from the container's root: a kept field replays its record's identity (`TypeTreeStep::like`), a new one joins under a pooled key. Repeated edits to one shape hit existing nodes and allocate nothing. A declined tree leaves a type the container owns, with its own chain, which later inline edits may rewrite in place.
- **Done 2026-09-25 (D3.4.3v3): chain copies per node.** Each node copied its parent's whole chain, so a path of n fields cost n(n+1)/2 entries per `Input`. A path now keeps one chain and one growing table, and a branch copies its prefix once (§3, *Sharing the prefix*).
- **Budget exhaustion is permanent.** A long-lived `Input` that reaches 1,024 shapes stops sharing for the rest of its life. Per-document roots, or eviction?
- **Literal and tree types never meet.** A map literal and a parsed map with the same fields have different types. Harmless, since type identity is not observable.

---

## Appendix A — Implementation pointers

- `lambda/lambda-data.hpp`: `TypeMap`'s sharing fields (`transitions`, `is_transition_shared_shape`, `is_shared_constructor_shape`, `is_private_clone`), `TypeMapTransition`, `typemap_is_shared_shape`, `TypeTreeStep`; `TypeAlloc`, `type_alloc_zeroed`, `typemap_hash_build_in` (storage); `ShapeEntry::chain_next` and `chain_index`, `typemap_first_field`, `typemap_next_field`, `shape_chain_next_until`, `FOR_EACH_MAP_FIELD`, `typemap_hash_entry_is_own`, `typemap_hash_holds_equal` (prefix sharing).
- `lambda/input/input.cpp`: `transition_target_for_key` (`MAX_SHAPE_TRANSITIONS`, `MAX_SHAPE_GRAPH`, `MAX_ELEMENT_SHAPE_GRAPH`), `map_transition_prefix_matches_parent`, `map_transition_target_for_add`, `map_shape_transition_root`, `map_put_via_shape_transition`, `map_put_with_data_growth`, `map_clone_typemap_for_mutation`, `elmt_tree_root`, `elmt_put_tree`, `type_tree_root_like`, `type_tree_follow`, `input_tree_alloc`.
- `lambda/core/lambda-data.cpp`: `alloc_type_in`.
- `lambda/runtime/lambda-data-runtime.cpp`: `map`, `map_alloc_for_type` (literal types), `elmt_with_type`, `map_put_heap`.
- `lambda/runtime/lambda-eval.cpp`: `map_extend_open_shape`, `map_rebuild_for_type_change`.
- `lambda/io/mark_builder.cpp`: `ElementBuilder` (starts on `elmt_tree_root`), `putToElement`.
- `lambda/io/mark_editor.cpp`: `container_rebuild_with_new_shape`, `rebuild_steps`, `rebuild_private_chain`.
- `lambda/core/shape_builder.cpp` (the editor's field list; the shape pool is deleted).
- `lambda/runtime/gc/gc_heap.c`: `gc_trace_shape_fields` stops at `last`; `lambda/lambda.h`: `LambdaGcTypeMapLayout::last`.
- `utils/lint/rules/c-cpp/no-raw-shape-chain-walk.yml`; tests `TransitionTreePrefixSharingTest.*` in `test/test_mark_builder_gtest.cpp`.

## Appendix S — Superseded rulings

- ~~**D3.4.3*** Shapes intern in a **per-Input shape pool** — hierarchical lookup with parent inheritance (a parent hit is returned, not copied down), interning **at finalization, not incrementally** (builders construct a throwaway chain; `final()` swaps in the pooled one), and null-safe opt-in (no pool ⇒ per-map chains, no semantic change). Runtime-constructed maps do not intern today — they rebuild per transition. [Shape_Pool §3–§8]~~ — superseded 2026-09-25 by **D3.4.3v2**: maps share incrementally through the per-`Input` transition tree (§3–§6); the pool remains for element attribute chains and editor rebuilds until elements adopt the tree (§7).
- ~~**D3.4.3v2**~~ as ratified in Formal Design 13.0.0, where **a shared type is immutable** and every tree node copied its parent's chain — superseded the same day by **D3.4.3v3** (Formal Design 14.0.0): a shared type's *field list* is immutable, a path shares one entry chain and one growing lookup table, and every walk stops at `last` (§3, *Sharing the prefix*; §4).
