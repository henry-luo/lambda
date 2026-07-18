# Radiant Design: DOM/View Struct Refactoring

Status: **DV1–DV15 IMPLEMENTED** (campaign complete — see impl plan §10/§16); **DV16 (construction convention) proposed 2026-07-18 as follow-up**, implementation = impl plan P7.
Scope: `dom_node.hpp`, `dom_element.hpp` (lambda/input/css/), `radiant/view.hpp`, `radiant/layout.hpp`.
Follows: header consolidation (`vibe/radiant/Radiant_Imp_Code_Dedup.md`), robustness design (T7 stale-View), memory design (R1–R7).

---

## 1. Goals & Non-Goals

**Goals**
1. Establish an explicit, enforceable convention for what lives where: persistent DOM/view data in `dom_*.hpp` / `view.hpp` (grouped in `*Prop` structs), transient layout state in `layout.hpp` (`*Box` structs and contexts).
2. Substantially shrink `DomElement` (584 B today) and `DomText` (136 B today) by completing the lazy-`*Prop` pattern: grouped fields, role-correct unions, a second-level `ext` group for rare state.
3. Evolve the tree API toward the C+ struct-based class convention: methods on `DomNode`/`DomElement`, per-group inline accessors, a thin C-ABI wrapper set for the JS bridge.
4. Provide efficient computed-style access for any DOM element, **derived from the stored props** — no separate computed-style storage in the tree.

**Non-Goals (explicitly deferred)**
- Browser-style **style sharing** (interning identical prop groups across siblings). Natural follow-up phase; this design must not preclude it (see DV9 immutability).
- Major **DomDocument** refactoring. Only incidental grouping of its accreted fields in this phase (DV13).
- Changing the unified-tree model (DOM tree == view tree) or the Lambda `Element` embedding.

---

## 2. Current State (Findings)

Sizes measured with `temp/size_probe.cpp` (clang, arm64 macOS, 2026-07-18):

| Struct | Size | | Prop group | Size |
|---|---|---|---|---|
| `DomNode` | 80 B | | `FontProp` | 96 B |
| `DomText` | 136 B | | `BoundaryProp` | 160 B |
| `DomComment` | 112 B | | `InlineProp` | 52 B |
| `DomElement` | **584 B** | | `BlockProp` | 256 B |
| embedded `Element` | 64 B | | `FlexItemProp` | 96 B |
| `DomDocument` | 2416 B | | `GridItemProp` | 144 B |

**F1 — The lazy-Prop convention is already half in place.** `DomElement` holds ~18 lazily allocated prop pointers (`font`, `bound`, `in_line`, `blk`, `scroller`, `embed`, `position`, `transform`, `filter`, `backdrop_filter`, `multicol`, `pseudo`, `vpath`, …) and the `fi/gi/tb/td/form` union tagged by `item_prop_type`. The view classes (`ViewElement`, `ViewBlock`, `ViewTable*`) are zero-size overlays over `DomElement`, enforced by `static_assert`s (view.hpp ~line 1623). The refactor *finishes and systematizes* this; it does not invent it.

**F2 — The remaining bloat is inline scalars and rare pointers, not ungrouped props.**
- Four "fragment union" blocks (`has_* + 4 floats` each, ~84 B total) used only by inline elements with special fragmentation (soft hyphen, collapsed whitespace, block-in-inline splits).
- Five pseudo-element `StyleTree*` (`before/after/first_letter/marker/placeholder`, 40 B) — rarely non-null.
- Intrinsic-width cache (2 floats + 2 bools), pending element scroll (2 floats + 2 bools), scattered bools with padding.
- ~10 rare pointers: `vpath`, `multicol`, `backdrop_filter`, `custom_layout_paint`, `layout_fragments`+count, `shadow_host`, `shadow_root`, `transition_state`.

**F3 — The `fi/gi/tb/td/form` union is grouped on the wrong axis (correctness bug class).** It conflates three orthogonal roles: item-role in the parent's formatting context (`fi`/`gi` — truly exclusive), the element's own display role (`tb`/`td`), and element kind (`form`). CSS allows overlap across these axes: a `display:table` element can be a flex item; an `<input>` can be a flex/grid item. Evidence: the `CRITICAL` workaround at `radiant/layout_flex_multipass.cpp:1121` re-allocates `TableProp` mid-layout because flex measurement clobbered `tb` with `fi`. Form-in-flex is the same latent collision.

**F4 — Three lifetime tiers exist but are implicit.** (a) DOM-persistent (survives relayout; doc pool/arena): `specified_style`, `css_variables`, `transition_state`. (b) View-persistent (valid until next relayout; view pool): `bound`, `blk`, `TextRect`, most props. (c) Layout-transient (a single pass; stack/`lycon`): `BlockContext`, `Linebox`, `FloatBox`. The comment on `transition_state` (dom_element.hpp ~line 432) exists precisely because tier (a) vs (b) is undocumented; robustness finding T7 (stale `View*`) is the same disease.

**F5 — Prop structs internally mix tiers.** `FlexItemProp` carries persistent item style (`flex_basis`, `flex_grow`, `align_self`, `order`) alongside per-pass scratch (`hypothetical_cross_size`, `resolved_*`, `intrinsic_*` caches). `GridItemProp` likewise (`measured_*`, `track_*`). `TableCellProp` mixes spanning style with per-layout `col_index`/`intrinsic_width`.

**F6 — `DomText` carries per-node copies of style data.** `color` (legacy direct PDF→DomText mapping; now write-dead — see DV7), a 4-byte `content_type` enum for the rare symbol case, plus `rect` and `font` (both hot, justified).

**F7 — No computed-style read path.** The cascade writes used values directly into prop groups ("we do not store computed_style"); `specified_style` (AVL tree) is the retained source of truth. There is no `CssPropertyId → stored value` mapping, so `getComputedStyle` currently has nothing to walk.

**F8 — Header location.** `dom_node.hpp`/`dom_element.hpp` live in `lambda/input/css/` because input-side parsers/builders allocate DOM nodes. They reference radiant prop types **only through forward-declared opaque pointers** — the input side never dereferences them. There is one definition of the tree structs, not two.

---

## 3. Decisions

### DV1 — Three lifetime tiers, declared per struct
Every persistent/transient struct declares its tier and allocator in a one-line header comment, and the convention is normative:

| Tier | Name | Allocator | Header | Examples |
|---|---|---|---|---|
| 1 | **DOM-persistent** | doc pool / arena | `dom_node.hpp`, `dom_element.hpp` | tree links, `specified_style`, `css_variables`, `transition_state` |
| 2 | **View-persistent** | view pool | `view.hpp` | all `*Prop` groups, `TextRect`, geometry results |
| 3 | **Layout-transient** | stack / `lycon` scratch | `layout.hpp` | all `*Box`, `BlockContext`, `Linebox`, `FloatBox` |

Rule of thumb: a Tier-1 field may point at Tier-1 only; Tier-2 may point at Tier-1/2; Tier-3 anywhere. A pointer stored *against* this direction (e.g. Tier-1 field → view-pool object) must carry an explicit comment justifying its invalidation story (this is the T7 bug class).

Comment tag convention: `// tier-2: view-pool, rebuilt each relayout` at each struct head.

### DV2 — Header roles and naming
- `dom_node.hpp` / `dom_element.hpp`: the persistent tree spine. Structs: `Dom*`. Contains **no** prop definitions — opaque `*Prop` pointers only.
- `view.hpp`: all `*Prop` definitions (resolved/used style property groups, Tier-2) + the zero-size view overlays + `ViewTree`.
- `layout.hpp`: Tier-3 only. Structs named `*Box` (or `*Context`/`*Scope` for the established context/RAII types). Any `*Prop` currently containing per-pass scratch migrates that scratch here (DV10).
- Naming: `*Prop` = persistent property group; `*Box` = transient layout construct. New code must not introduce a persistent struct in `layout.hpp` or a transient one in `view.hpp`.

### DV3 — Single DOM definition; no base/derived fork
The tree structs keep **one definition**, shared by input-side builders and radiant. Layering is achieved by opacity (forward-declared prop types), not by inheritance splits — a base-at-input / full-at-radiant fork would recreate the sync problem and break the zero-size overlay model. The existing `static_assert` overlay checks remain the enforcement mechanism and are extended with size ratchets (DV11). Header location stays `lambda/input/css/` this phase; an optional rename/move (e.g. `lambda/dom/`) is cosmetic and deferred.

### DV4 — Null-default storage + canonical-default read accessors
- **Storage**: absent prop group pointer (`nullptr`) ⇒ the element has the CSS initial values for that whole group.
- **Reads**: each group gets an inline accessor returning a canonical immutable default instance when absent:
  ```cpp
  extern const BlockProp BLOCK_PROP_DEFAULT;   // tier-2 defaults, statically initialized
  inline const BlockProp* DomElement::block() const { return blk ? blk : &BLOCK_PROP_DEFAULT; }
  ```
  Readers never null-check; hot layout loops read through the same pointer dereference either way.
- **Writes**: `ensure_block(el, lycon)` lazy-allocates from the appropriate pool and **initializes by `memcpy` from the canonical default**, then returns a mutable pointer. Nobody writes through `block()`.
- **No 0-encodes-initial mandate.** Fields keep natural encodings (`flex-direction: row`, `opacity: 1.0`, `CssEnum` values). The invariant "absent ⇒ initial" holds at the *group-pointer* level via the memcpy-init, not per-field. (Rejected: shared-default + COW — a single missed check-and-clone in C+ silently mutates every element's defaults; radiant mutates prop fields in place pervasively, making the write-barrier audit unverifiable.)

### DV5 — Split the item union by role
Replace the single 5-way union with two fields:

```cpp
// item role in the PARENT's formatting context — genuinely exclusive (one parent)
union { FlexItemProp* fi; GridItemProp* gi; };      // tag: parent_item_kind (none/flex/grid)
// the element's OWN role — discriminated by display/tag, exclusive within itself
union { TableProp* tb; TableCellProp* td; FormControlProp* form; };  // tag: role_kind
```

This fixes the F3 collision class (table-as-flex-item, form-in-flex) and removes the `layout_flex_multipass.cpp:1121` workaround. Net cost vs today: +8 B vs the 5-way union, repaid many times by DV6/DV7. Both tags pack into a shared flags byte (DV8a).

**DV5a — Access style: tagged-union storage, overlay-subclass accessors.** The unions stay *typed and tagged* in `DomElement` (debugger-transparent, grep-able, zero-cost), but the sanctioned access path is accessor methods on the zero-size overlay subclasses, which centralize the tag check: `DomTable::table()` (debug-asserts `role_kind == ROLE_TABLE`, returns `tb`), `ViewTableCell::cell()`, flex/grid item accessors on the container's item view, `form()` on the form-control overlay. This extends the existing pattern (`ViewTable::acts_as_tbody()`, `lam::view_require<RDT_VIEW_TABLE>`). Direct union-member reads outside the accessors are a convention violation; the repeated manual `item_prop_type == X && el->y` guards (~20 sites in event.cpp alone) collapse into the accessors. *(Rejected: `void*` placeholder slot with typed fields only in subclasses — same discipline benefit but the base struct goes debugger-opaque, and overlay subclasses cannot add real storage without breaking the size invariant anyway.)*

### DV6 — Second-level `ext` group for rare state
Add one `DomElementExt* ext` (lazy, doc-pool or view-pool per field tier split — see impl note) holding fields that are null/zero on the vast majority of elements:
- the four fragment-union blocks (F2) — restructure as a small array `FragmentUnion frags[4]` + presence mask;
- five pseudo-element `StyleTree*`;
- `multicol`, `vpath`, `backdrop_filter`, `custom_layout_paint`, `layout_fragments` + count;
- `shadow_host`, `shadow_root`;
- pending element scroll (2 floats + presence bits).
Access pattern: `el->ext->...` guarded by `if (el->ext)` or accessor helpers. Hot groups (`bound`, `blk`, `font`, `in_line`, `scroller`, `position`, `embed`, `transform`, `filter`) stay first-level — one indirection max on hot paths.
*Impl note:* `ext` contains both Tier-1 (`shadow_*`, pseudo StyleTrees) and Tier-2 fields today; either allocate `ext` from the doc pool (Tier-1, safe superset) with Tier-2 members rebuilt each pass, or split `ext`/`ext_view`. Decide by measurement in P3; default = doc pool.

### DV7 — DomText slimming
- `color` is **dropped outright** (verified dead 2026-07-18 by field-rename compile sweep): it was a legacy channel for direct PDF→DomText color mapping, but the current PDF pipeline (`lambda/package/pdf/pdf_to_html`, PDF.js-style) renders visible text as SVG `<text>` (color = SVG fill) with a *transparent* `<span>` selection layer — no writer ever sets the field. Total users: one clone copy (`dom_element.cpp:2298`) and one dead read (`render_text.cpp:180`, guarded by `color.c != 0` which is never true); both delete with the field.
- `content_type` (symbol vs string) becomes a **bit in a `node_flags` byte on `DomNode`** (fits in existing padding next to `layout_dirty`), preserving W3C `DomNodeType` numbering. `is_symbol()` reads the flag.
- `rect` and `font` stay inline (hot; every text node in flow uses both).
- Candidate (verify in impl): `text`/`length` duplicate `native_string->chars`/`len` — replace with inline accessors over `native_string`, saving 16 B. Gate: confirm no site stores a `text` that diverges from the backing String (symbols store the name in both per current comment).
- Target: 136 B → ~112 B (→ ~96 B if the text/length candidate lands).

### DV8 — Flags consolidation
`DomElement`'s scattered bools (`needs_style_recompute`, `styles_resolved`, `float_prelaid`, `has_cached_intrinsic_widths`, `measuring_intrinsic_width`, `has_pending_element_scroll_*`, …) consolidate into one `uint32_t elmt_flags` with named bit constants, alongside the two union tags from DV5 (3 bits each). Kills ~8–12 B of bool+padding and makes state grep-able.

### DV9 — Computed style is derived, never stored (+ write discipline)
No computed-style storage in the tree. `getComputedStyle(el, prop_id)` derives answers from stored prop groups via a single **property table**:

```cpp
struct CssPropAccessor {
    CssPropertyId id;
    PropGroupKind group;        // which prop group (or SPECIAL for derived)
    uint16_t offset;            // field offset within the group
    CssPropValueKind kind;      // float px / CssEnum / Color / length-pair / ...
    SerializeFn serialize;      // value → canonical CSSOM string
    DeriveFn derive;            // nullptr = direct field read; else re-derive (e.g. line-height: normal)
};
```

One table, three consumers: the cascade writer (resolve), `getComputedStyle` (read), and the Jube DOM3 table dispatch (replacing strcmp chains — see `vibe/Lambda_Jube_DOM3.md`); keep the table layout compatible with the D0a–D0d constraints there.

Constraints that make this correct:
- **DV9a — Flush before query.** Props are only valid post-resolution. One entry point (`dom_ensure_computed(el)`) forces style resolution (and layout, for used-value properties like width/height) before reading. Mirrors browser `getComputedStyle` flush semantics.
- **DV9b — `specified_style` is the durable truth.** Used-value overwrites (DV10) are safe *only because* props are regenerated from `specified_style` each pass. This invariant is normative: no code may treat a prop group as surviving authority across relayout.
- **DV9c — Shared groups are immutable post-cascade.** Any prop group aliased between elements (inherited `FontProp` today; style sharing later) must not receive used-value writes. Used-value writes target only element-owned groups. This is the door left open for the style-sharing follow-up (Non-Goal).
- **DV9d — Per-property fidelity is declared, not implied.** The table records for each property: direct read / derived / unsupported. Fidelity target = match browser used-value answers for the properties the test corpus (WPT-style, jQuery) actually queries; exotic properties may be marked unsupported initially.

### DV10 — Used values overwrite in place; scratch moves to Tier 3
- Default: layout writes used values **into the same prop field** (compression goal, DV9b makes it sound). A twin field is added only where both values are needed *simultaneously* (e.g. percentage bases for incremental relayout, transition endpoints — `transition_state` already covers the latter).
- Per-pass scratch currently inside `*Prop` structs (F5: `FlexItemProp` hypothetical/resolved/intrinsic caches, `GridItemProp` measured/track fields, `TableCellProp` per-layout indices) migrates to Tier-3 `*Box` structs in `layout.hpp` where the pass structure allows; where the multipass algorithms genuinely need cross-pass persistence, the fields stay but get explicit `// tier-3 scratch, valid within layout pass` tags and are excluded from the computed-style table. Migrate opportunistically per phase, not big-bang.

### DV11 — Size ratchets
`static_assert(sizeof(DomElement) <= N)` (and `DomText`, `DomNode`) added once the refactor lands; N fixed from the measured result during implementation, then only lowered, never raised without a design-doc amendment. `temp/size_probe.cpp` is the measurement tool. No target value is dictated at design time; the estimate below suggests ~368 B is reachable for `DomElement` (~37% cut) without touching hot groups.

### DV12 — Function grouping (C+ struct-based convention)
- Tree/attribute/class operations become `DomNode`/`DomElement` **methods** (continuing `append_child`, `as_element`): navigation, attr get/set, class ops, structural queries. The ~40 `dom_element_*` free functions in the header shrink accordingly.
- A thin **C-ABI wrapper** set survives only where the JS bridge / Jube requires C linkage; wrappers are one-line calls into methods (no logic).
- Style application moves behind the CSS engine surface (`StyleContext`/cascade entry points), not on the element.
- Layout stays **free functions** around `LayoutContext`/`*Box` — hot paths, no benefit from methodization.
- Each prop group gets its accessor pair (`block()`/`ensure_block()`) declared next to the field.

### DV13 — DomDocument: incidental cleanup only
Group the accreted `js_*` runtime fields (`js_mir_ctx`, `js_preamble_state`, `js_runtime_*`, mutation records) into one `DomJsRuntime` sub-struct, and viewport/scale fields into `ViewportMeta`, purely for coherence (2416 B × 1 per doc — size is irrelevant). The doc-level major refactor is a separate future phase.

### DV14 — Style sharing: out of scope, kept possible
Interning identical prop groups across elements is the natural next phase. This design keeps it possible via DV4 (canonical default instances are already "shared groups"), DV9c (immutability discipline for shared groups). No further provisions in this phase.
### DV15 — Uniform node size; subclass-with-storage rejected
`DomElement` remains the single, fixed-size node type for all elements; per-display specialization is *data* (lazy prop groups, DV5) and *methods* (zero-size overlays, DV5a), never *storage*. The alternative — `DomTable : DomElement` with real extra fields, parser builds the small base, layout reallocates+replaces the node when it discovers the display role — is **rejected**:
- **Node identity is load-bearing through the embedded Lambda `Element`**: the parent Element's Lambda items array points at `&child->elmt`, i.e. into the middle of the `DomElement` allocation. Relocating a node means rewriting the parent's Lambda children array plus `element_dom_map`, `view_state_ref`/DocState, JS wrapper `DomNode*`s, event/focus references, `transition_state` back-pointers — the T7 stale-pointer class as a routine operation. (Blink/WebKit can subclass layout objects by display only because their layout tree is separate from the DOM tree; Radiant's unified tree forecloses that.)
- `display` is not discovered once: JS mutation, `:hover`, media queries flip roles across passes → realloc churn + arena garbage per flip.
- The size win is marginal post-DV5/DV6 (role data is out-of-line either way; the base carries only two union slots, 16 B), and inlining prop structs into subclasses conflicts with DV10 tiering (they carry per-pass scratch).
- Uniform size keeps pool allocation/recycling trivial and fragmentation-free.
The existing `static_assert(sizeof(View*) == sizeof(DomElement))` wall is this decision's enforcement; it stays.

### DV16 — Construction convention: no C++ constructors; static `create()` member functions  *(follow-up; impl plan P7)*
All C++ constructors on `DomNode`, `DomText`, `DomComment`, `DomElement` are **removed**. Node creation is one static member function per type that combines allocation and initialization in one place:

```cpp
// contract: storage comes zeroed (arena_calloc); create() stores ONLY the
// non-zero fields — null/0 fields are never written again.
static DomElement* DomElement::create(DomDocument* doc, const char* tag_name, Element* backing);
static DomText*    DomText::create(String* str, DomElement* parent);
static DomComment* DomComment::create(Element* backing, DomElement* parent);
```

Rationale (evidence 2026-07-18, still true post-campaign):
- **The constructors never run in production.** All real allocation paths (`dom_element_create`, `mark_builder.cpp` UI mode, `lambda-data-runtime.cpp`) do `arena_calloc` + sparse manual stores; the lengthy init lists execute only for stack/test objects.
- **They have already drifted from the real paths**: the `DomElement` ctor sets `display{CSS_VALUE_NONE}` (non-zero) while `dom_element_init` (dom_element.cpp:307) sets `{CSS_VALUE__UNDEF}` (=0, "critical for table elements"); `DomNode`'s ctor sets `inline_line_number = −1` vs the arena paths' 0. Parallel init lists are a standing divergence hazard; `create()` makes there be exactly one.
- **Zero + sparse stores is also the fastest form**: no ~500-B template read (vs memcpy-from-default), no redundant zero writes (vs constructor); arena pages are often pre-zeroed by the OS.

Rules:
- `create()` assumes zeroed storage as a stated contract (allocator must be a calloc/zeroing arena).
- Guarded by `static_assert(std::is_trivially_copyable_v<T>)` per node type; a member that would break triviality is a design change requiring amendment here.
- Existing `dom_*_create` free functions become one-line C-ABI shims over the static members (JS bridge/Jube linkage only), or are absorbed where nothing external links them.
- Stack-allocated/test uses migrate to `create()` on an arena, or `T x = {};` + the same sparse-defaults helper — never a hand-written field list.
- The `display` (NONE vs `__UNDEF`) and `inline_line_number` (−1 vs 0) divergences are resolved to one canonical value each; the arena paths are the de facto behavior and win unless a consumer proves otherwise.
- **Net LOC must decrease**: this is pure consolidation (four init lists + per-site duplicated stores → one `create()` per type); a LOC-positive outcome means an old init path was not fully retired.

**This is the normative C+ struct-based construction method** for DOM/view node types, complementing DV4's memcpy-from-canonical-default for prop groups — the same "canonical defaults" principle with the implementation chosen by default density (nodes: ~all-zero → zero + sparse stores; prop groups: dense CSS initials → template copy). `doc/dev/C_Plus_Convention.md` gets a section recording the pattern once P7 lands.

---

## 4. Target DomElement Sketch (estimate)

```
DomNode base                    80    (unchanged + node_flags in padding)
Element elmt (embedded)         64    (fixed by unified-DOM design)
tag_name/tag_id/id/class_names  32
class_count + display + flags   16    (DV8 packed)
first_child/last_child          16
specified_style                  8    (pseudo StyleTrees → ext)
css_variables / doc              16
style_version                    4
font / bound / in_line          24
item union + role union         16    (DV5)
content_width/height             8
blk/scroller/embed/position/
  transform/transition/filter   56
layout_cache                     8    (intrinsic-width cache folds in — impl item)
ext                              8    (DV6)
                              ~360 B  (vs 584 B today, ~-38%)
```
Further candidates during tuning: remove `native_element` (its own TODO; `dom_element_to_element()` replaces it, −8), fold `filter` into ext if profiling allows (−8).

---

## 5. Implementation Plan

**Detailed execution plan: `vibe/radiant/Radiant_Impl_Dom_View_Struct.md`** (task-level breakdown, call-site counts, size tracking, tier-audit ledger). Summary below.

Gate for every phase: `make test-radiant-baseline` 100%, `make layout suite=baseline` clean; release-build perf spot-check (`make release`) on the layout benchmark set for P3+.

- **P0 — Conventions + probes.** Land tier comment tags (DV1/DV2), `temp/size_probe.cpp` as a checked-in tool, and the flags field (DV8) without moving any prop. Mechanical.
- **P1 — Accessor layer.** Introduce `group()`/`ensure_group()` accessors against the *current* layout (DV4, incl. canonical defaults) and mechanically port call sites. Big diff, semantically null. This is the seam that makes P3 small.
- **P2 — Union split.** DV5: introduce the two role unions + tags, port the tag checks, delete the `layout_flex_multipass.cpp:1121` workaround (its scenario becomes a regression test), audit form-in-flex/grid.
- **P3 — Field regrouping.** DV6 `ext`, DV7 DomText, intrinsic-cache fold, pseudo StyleTrees move. Small commits touching only structs + accessors; measure after each; set ratchet values (DV11).
- **P4 — Computed-style table.** DV9: property table + `dom_ensure_computed` + serializers for the supported set; wire to JS `getComputedStyle`; coordinate table shape with Jube DOM3 dispatch.
- **P5 — Scratch migration + method grouping.** DV10 opportunistic scratch moves; DV12 methodization + C-ABI wrapper trim. Can trail incrementally.
- **P6 — DomDocument grouping (DV13).** Independent; any time after P0.

---

## 6. Risks

- **Churn breadth**: P1 touches most of `radiant/` + `js_dom`. Mitigation: accessor-first (P1 is semantically null and mechanically verifiable), then P3 is small.
- **Hot-path regression**: extra indirection is confined to rare (`ext`) groups; hot groups keep one deref. Gate: release-build layout benchmarks per phase.
- **Shared-group writes (DV9c)**: today's code may already write through aliased `FontProp`s. P1 audit item: find aliasing sites before enabling used-value overwrite assumptions.
- **`ext` tier mixing (DV6 impl note)**: wrong pool choice reintroduces T7-style staleness. Default doc-pool; measure.
- **Fidelity expectations (DV9d)**: `getComputedStyle` consumers (tests, JS libs) may query properties marked unsupported; the table makes gaps explicit and cheap to fill.

## 7. Open Items

- OQ1: `ext` allocation pool — doc pool (default) vs split `ext`/`ext_view` (decide in P3 by measurement).
- OQ2: `DomText.text/length` derivation from `native_string` — verify no divergent site, then take the −16 B.
- OQ3: Which properties are in the initial supported set of the computed-style table (drive from test corpus queries).
- OQ4: Whether `filter` (8 B) is hot enough to stay first-level (profile in P3).
- OQ5: Optional header relocation (`lambda/dom/`) — cosmetic, decide at end of phase.
