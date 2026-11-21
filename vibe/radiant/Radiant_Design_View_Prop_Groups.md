# Radiant design: View property groups

**Status** · Retained design as of 2026-11-21. The [implementation record](../impl/Radiant_Impl_View_Prop_Groups.md) tracks the original refactor. Appendix A records the release measurements and their build scope.

**Scope** · Property storage reachable from `DomElement` and its optional groups, from DOM construction through browsing and paged layout, paint, and teardown. Preserve CSS behavior, CSSOM answers, and the shared DOM/view node used by Radiant.

**Spec linkage** · D5.1.4 and D5.2.1–D5.2.4 govern allocation, ownership, attribution, and diagnostics. D5.5.1–D5.5.3 govern the DOM and retained presentation. D9.1.1v4 and D9.1.3v2 govern the Input/Radiant split. This record extends DV4 and DV6 and applies DV5a, DV8, DV9, and DV10 from the shared view design. [Formal design](../../doc/Lambda_Formal_Design.md) · [View structure decisions](Radiant_Design_View_Struct.md#3-decisions)

## 1. Retained record sizes

The largest savings come from the payload allocated for common elements. Moving pointer fields can shrink every node, but a smaller `sizeof(DomElement)` can still increase the tree's memory if many more elements need an extension. Measure the document pool, node arena, and view storage together.

| Record | Before, arm64 | Retained, arm64 |
|---|---:|---:|
| `DomElement` | 376 B | 328 B |
| `DomStyleRareExt` | 568 B | 256 B |
| `DomStyleExt` | — | 48 B |
| `BlockProp` | 456 B | 344 B |
| `ScrollProp` | 216 B | 80 B |
| `TransformProp` | 408 B | 56 B |
| `FormControlProp` | 256 B | 208 B |
| `BoundaryProp` | 224 B | 224 B |
| `LayoutCache` | 1,240 B | 1,240 B |

These are direct compiler record sizes. Earlier captures use the name `DomElementExt` for `DomStyleRareExt`. Optional allocations and shared storage contribute to the measured totals in Appendix A. [Element records](../../lambda-input/lambda/input/css/dom_element.hpp) · [View payloads](../../radiant/radiant/view.hpp) · [Layout cache](../../radiant/radiant/layout.hpp)

## 2. Split properties by use and lifetime

### DV4.1 — Keep property groups small

Aim for **fewer than 15 logical properties per `ViewProp` payload**. Count a value, its unit, and its presence flag together when they are read and written as one property. Record exact size, direct member count, nested storage, and transitive allocations separately so an inline array or bulky record does not disappear from the accounting. Review groups with 15 or more properties or unusually large payloads. Keep jointly used, small records together when splitting would add allocations and dependent loads without skipping much storage.

Use three tests to place a field. Does it have the same lifetime as its neighbors? Do the same elements need it? Do the same hot readers usually read it alongside its neighbors? A group should be independently lazy if most elements that allocate its parent never use it. Reserve pass scratch for values that expire before publication (DV10). Retained geometry belongs to the view and its generation. Durable authored values stay with the cascade (DV9b).

### DV4.2 — Keep block sizing together and allocate uncommon groups lazily

`BlockProp` keeps width, height, min/max constraints, and the resolved `box-sizing` mode together. `BoundaryProp` supplies the padding and border metrics used for size conversion. Core text and flow values use the same `BlockProp` defaults.

| Optional group | Size | Contents |
|---|---:|---|
| `BlockPropExt` | 104 B | List style, counters, box decoration breaks, fragmentation breaks, widows, and orphans |
| `BlockContainmentProp` | 32 B | Containment, container-query axes and names, intrinsic size hints, and content visibility |

Both groups belong to the view pool. Allocate them on a write requiring the group and use immutable defaults on a read of an absent group. A CSS-wide keyword, explicit default, NaN percentage sentinel, presence bit, or deferred calc must retain its current meaning. Keep simultaneous computed and used forms where their consumers need both (DV9b, DV10). [Block definitions](../../radiant/radiant/view.hpp) · [Typed defaults](../../radiant/radiant/view_prop_defaults.cpp) · [Size conversion](../../radiant/radiant/layout_box.cpp)

### DV4.3 — Split scroll, transform, and form payloads by feature

| Core | Optional groups | Ownership and use |
|---|---|---|
| `ScrollProp` | `ScrollSnapProp` (144 B) | Holds snap configuration, scroll margins, and scroll padding. Scroll-into-view reads spacing even when snapping is inactive. |
| `TransformProp` | `IndividualTransformProp` (360 B) | Holds individual `translate`, `rotate`, and `scale` records. Animated bindings borrow their sample owner's values. |
| `FormControlProp` | `FormPlaceholderProp` (24 B), `FormFileButtonProp` (40 B) | Hold resolved pseudo paint. The core carries the range, select, and text projection, and StateStore owns canonical control values. |

The view pool owns the optional payloads. Retained style resolution releases stale groups and restores defaults. Form pseudo paint releases its font handle when the group resets or retires. Script-created form state can attach before the first layout and has a node retirement path. [Ensure helpers](../../radiant/radiant/view_prop_ensure.cpp) · [View teardown](../../radiant/radiant/view_pool.cpp)

## 3. Place each field at its required lifetime

### DV6.1 — Keep document extensions independently lazy

`DomElement::rare_ext` lazily owns a `DomStyleRareExt` in the document pool. The extension tracks the physical storage owner for adoption (D5.5.1). It holds uncommon DOM state, typed slots for uncommon view features, and pointers to the separately allocated attribute and fragment groups. The document pool owns `DomAttributeCache` (48 B) and `DomFragmentGeometry` (164 B). Layout resets the geometry group as needed. The attributes group holds expanded-name, synthetic attribute, and name-cache state. The extension packs its 12 Boolean flags into the gap before its pointer fields. It stores form-image offsets as fractional CSS pixels and rounds them when FormData or dialog submission serializes them (D9.5.6v2).

The custom-property list and its lookup index live in `DomStyleRareExt`. The first write that needs custom-property storage allocates the extension. Mutation invalidates the index, and the list and index follow the physical storage owner through adoption (D5.5.1). [Custom-property storage](../../lambda-input/lambda/input/css/dom_element.cpp)

`ShadowStyle` holds the shadow host and root links, root options, declaration position, and borrowed scoped inline stylesheet. The physical document pool allocates this `ViewProp` only for elements that need one of those fields. It survives view teardown and is freed when its node retires (D5.5.1). The declaration position uses the backing element's signed 64-bit child index width (D3.1.1, D5.5.3). On arm64, the group is a 40-byte record. The original split reduced `DomStyleRareExt` from 280 to 256 bytes and its minimum pool block by 32 bytes under the allocator then in use (D5.1.4, D5.2.4).

`PopoverStyle` holds the popover modes, visibility, toggle task, and opening order. It also holds the top-layer membership and implicit anchor shared with modal dialogs. The physical document pool allocates this `ViewProp` on demand. Relayout preserves it, and node retirement releases it (D5.5.1). The opening order is an unsigned 64-bit sequence and reserves zero for entries outside the popover stacks. On arm64, the group is a 40-byte record. The original split reduced `DomStyleRareExt` from 304 to 280 bytes and its minimum pool block by 16 bytes under the allocator then in use (D5.1.4, D5.2.4).

`DomElement::ext` lazily owns a `DomStyleExt` directory in the node's physical document pool. It owns private pseudo specified styles through the 80-byte `PseudoStyle` group and borrows namespace URI bytes. A directory stays live while any of its bindings are present. Node retirement clears document-owned state and borrows under D5.5.1 and D5.5.2. [Element extensions](../../lambda-input/lambda/input/css/dom_element.hpp) · [Release paths](../../lambda-input/lambda/input/css/dom_element.cpp)

The document pool uses 8-byte alignment. On arm64 its block header takes 24 bytes, with 32-bit spans and an extent-size cap (D5.1.4, D5.5.1). `ShadowStyle` and `PopoverStyle` each need a minimum 64-byte pool block, down from 80 bytes. `DomStyleExt` needs 72 bytes, down from 80. The default pool alignment is 16 bytes. [Document pool creation](../../lambda-input/lambda/input/css/dom_element.cpp) · [Pool sizing](../../lambda-input/lib/mempool.c)

The element owns `EmbedStyle` (104 B on arm64) through `lam::Own<EmbedStyle>`. The physical document pool allocates it on demand, including when image decoding finishes before the first view exists (D5.5.1). Flex and grid props and the selected image URL use the view pool. Retained layout resets those payloads and preserves an iframe's loaded document (D5.5.3). View teardown and node retirement release the document allocation. [Allocation](../../radiant/radiant/view_prop_ensure.cpp) · [Teardown](../../radiant/radiant/view_pool.cpp)

### DV6.2 — Place the measured role slots in `DomStyleExt`

The lazy directory holds `tb`, `form`, `transform`, and `pseudo`. Each is a `ViewProp` with teardown registered in `DomViewSlot`. `pseudo` carries generated-content view state. The `pseudo_styles` pointer owns private cascaded pseudo declarations and follows document lifetime. View teardown can release the directory when its last binding clears. Adoption preserves its physical owner under D5.5.1.

The element holds `position`, `scroller`, `td`, the tagged flex/grid item union, `font`, `in_line`, `bound`, `blk`, `embed`, and `layout_cache` directly. Table/form roles and flex/grid parent roles are independent (DV5a). Compare live union and overlap counts before moving another slot. [Slot declarations](../../lambda-input/lambda/input/css/dom_element.hpp) · [Teardown catalog](../../radiant/radiant/view_pool.cpp)

For \(N\) connected elements, \(E_0\) and \(E_1\) live extensions, and shell allocation charges \(H_0\) and \(H_1\), an approximate placement delta is:

```text
delta = (node_charge_after - node_charge_before) * N
      + H1 * E1 - H0 * E0
      + delta_payload_and_allocator_bytes
```

Use payload bytes for logical totals and allocator charges for physical totals. Account for each allocation in its owning domain. Evaluate a slot move against the build immediately before that move.

### DV6.3 — Intern namespace URIs and derive local names

Elements, attributes, XML namespace targets, and parsed `xmlns` and XHTML metadata share URI spellings from the Input NamePool. The document caches the HTML URI and empty URI. Ordinary HTML identity uses an inline flag. Other explicitly resolved namespaces can use the borrowed pointer in `DomStyleExt`. Copies into another Input intern in its pool. Adopted nodes keep access to the bytes owned by their retained source Input (D5.5.1, D5.6.1–D5.6.2).

The local name comes from `tag_name()` or its qualified suffix. Identity flags distinguish a literal colon from the separator in a qualified name. Namespace matching compares URI content and preserves the XHTML expanded name and case under S8.2.2v6 and D10.1.4. [URI allocation and namespace targets](../../lambda-input/lambda/io/target.cpp) · [Element namespace identity](../../lambda-input/lambda/input/css/dom_element.cpp)

### DV6.4 — Share universal pseudo resets at document scope

The normal selector matcher and active stylesheet checks determine eligibility for each element. A universal `::before` or `::after` rule can share a tree when it sets only `box-sizing` to a literal `content-box` or `border-box`. The owning document caches an independent declaration snapshot at its original cascade priority. Two four-bit indexes in existing node padding select the shared trees. Elements use zero for an absent shared tree. Each document can cache 15 distinct snapshots. Rules that need another snapshot when the cache is full use the private path.

An element reads its private pseudo tree when present and otherwise reads the selected shared tree. Adding a conflicting declaration or requesting a writable slot materializes the shared tree in the node's pool before applying the override. Layout and pseudo CSSOM use this same lookup, including when a pseudo generates no box. The physical source document keeps a shared snapshot available after adoption. Recascade clears the node's shared indexes, and retained generated boxes follow the existing borrow-retirement path when a private tree is replaced (D5.5.1, D5.5.3, DV9c). [Matching path](../../radiant/radiant/css_cascade.cpp) · [Storage and promotion](../../lambda-input/lambda/input/css/dom_element.cpp) · [Pseudo CSSOM](../../radiant/radiant/css_prop_table.cpp)

### DV6.5 — Retire WebKit box display in Radiant

Radiant rejects `display: -webkit-box` and `display: -webkit-inline-box` before cascade, so a valid fallback declaration can win. Use `flex` or `inline-flex` with `flex-direction` for flex layout, and the unprefixed `line-clamp` for clamped text. The shared CSS property metadata still accepts `-webkit-box-orient` and `-webkit-line-clamp` for declaration and computed style inspection. Neither prefixed property affects Radiant layout (D9.1.1v4, D9.1.3v2).

The four-byte `DisplayValue` holds the outer role and the inner mode. `DisplayInner` packs the standard `list-item` modifier into its high bit. The same representation works for published and animated display. The CSS-wide values `initial` and `unset` resolve to inline flow. [Display storage](../../lambda-input/lambda/input/css/dom_element.hpp) · [Display resolution](../../radiant/radiant/css_display.cpp)

### DV6.6 — Share element tag spellings

`tag_name()` looks up exact catalog matches by `tag_id`. For a backed custom tag such as `<my-card>`, it reads the spelling from the embedded element's `TypeElmt`. This reuses the name shared through the Input type tree (D3.4.3, D5.5.3, D5.6.2). If the spelling cannot be read from either source, the physical document keeps it in a sparse lookup table. This covers names whose XML prefix and local name are stored separately, as well as explicit constructor aliases. The table borrows interned strings from the physical owner's Input, which adoption keeps alive under D5.5.1. Tag dispatch and exact spelling follow S8.2.2v6 and D10.1.4. Generated boxes and document wrappers use catalog entries, including the anonymous boxes that live only for a layout pass. [Tag access](../../lambda-input/lambda/input/css/dom_element.hpp) · [Generated spellings](../../utils/well_known_names_data.py)

The fallback table costs one pointer per document and allocates entries only when used. Replacement and retirement clear the entry. The earlier tag-spelling allocation trial is recorded in Appendix S.

### DV6.7 — Keep the class cache in one lazy allocation

`DomElement::classes` points to a document-owned `DomClassList`. Its eight-byte arm64 header holds the class count, followed by a trailing array of class-name pointers. Classless elements use a null pointer. Selector matching, cascade indexing, and mutations use the same cache. Allocation and release follow the node's physical owner (D5.5.1). [Class cache declaration](../../lambda-input/lambda/input/css/dom_element.hpp) · [Construction and release](../../lambda-input/lambda/input/css/dom_element.cpp)

## 4. Make each group's storage contract explicit

1. Expose a read accessor, a presence test where needed, and one mutable ensure path. Group reads are allocation-free and use typed defaults or null. Reuse the typed ensure helpers to select the correct pool. Keep anonymous scratch items in pass storage. Secondary resolution uses its selected view's storage.
2. Use one registration source per view slot. Keep the typed access, ownership, default/reset behavior, teardown, and clear behavior in agreement. Child groups follow their root's existing teardown entry. The catalog marks `shadow` and `popover` as document-owned slots. DOM retirement releases them (D5.5.1). Keep compile-time coverage and debug post-clear checks. The Input headers expose the shared element definition, and Radiant defines the concrete view payloads and tears them down (D9.1.1v4, D9.1.3v2). [Slot ownership dispatch](../../radiant/radiant/view_pool.cpp) · [Node retirement](../../lambda-input/lambda/input/css/dom_element.cpp)
3. Separate immutable resolved style from used geometry where sharing pays for itself. Keep canonical `InlineProp` bindings behind the existing write gate (DV9c). Treat interned inherited text deltas as a separate experiment. Preserve style identities, inherited revisions, layout invalidation, and the lifetime of retained animation or view state.
4. Keep the nine general measurement entries and four intrinsic-width entries inline in `LayoutCache`. The cache trials moved those arrays and increased CPU time on large pages. Cache storage, `BoundaryProp::flow_margin`, and pass-lived flex/grid measurement state remain separate investigations (D5.1.4, DV10). [Cache trial results](../impl/Radiant_Impl_View_Prop_Groups.md#deferred-layout-cache-storage)

## 5. Measurement and acceptance

- Capture constructed DOM, first committed layout, repeated retained reflow or recascade, and a settled document after a fixed mutation script. Track peak and settled memory separately. Count authored, synthetic, and detached objects in their appropriate populations.
- Measure slot occupancy and overlap. Reconcile logical storage with actual pool charges, include child payloads, and count each shared allocation once. Run the diagnostic census outside the timing path (D5.2.4).
- Compare paired workloads with equivalent resources and output. The release corpus contains 53 original static fixtures and 171 web templates. Use focused cases for generated content, forms, flex/grid, scroll and snap, transformed views, ruby, multicolumn, and paged layout.
- The memory target is a 15% geometric mean reduction in combined active DOM and view bytes at first layout (DV11). Report the aggregate total, per-page geometric mean, tail pages, and process tracking separately. Check dense role workloads for repeatable memory increases.
- Compare release timings on the same host and establish noise with repeated baselines. The timing targets are 1% for median time and 3% for p95. Check construction, cascade, first layout, retained reflow, paint, and interactions as applicable. A repeatable slowdown beyond those bounds fails the timing target even when memory improves.
- Compare normalized layout trees and check CSSOM, paint, and retained interaction behavior. Cover recascade, style removal, view teardown, detach, adoption, and generated-style borrowing. [Memory profiler](../../radiant/radiant/view_memory_profile.cpp) · [Test map](../../test/README.md)

## Appendix A. End-to-end release measurements

The comparisons use native Clang arm64 release builds at `-O3` with full LTO. The original baseline includes the pre-existing `DomElement` field reorder. Each page uses local resources, a 1200 × 800 viewport, and disabled scripting. Samples come from `post_layout_pre_output` after first layout. Totals sum independent page processes. Tracked process memory includes the Input arena that can own parser-backed elements. The profiler accounts for logical storage and allocator-tracked memory in their respective scopes (D5.2.1–D5.2.4).

In profile output, `slots.ext` counts `rare_ext` allocations and `ext_size` gives their record size. `prop_extensions` and `prop_ext_size` describe the `ext` directory.

### Current checkout and archived refactor endpoint

The November 21 endpoint uses Lambda `017c686351`, Radiant `4871cce`, and Input `3b18322`. The replay paired all 224 profiles and reported zero attribution errors. The current column covers the whole checkout, including renderer changes between the two measured endpoints.

| Metric across 224 pages, bytes | Original | After shared resets | November 21 | Reduction from original |
|---|---:|---:|---:|---:|
| Active DOM storage | 169,426,412 | 86,918,684 | 87,225,452 | 48.52% |
| Active view-property storage | 201,804,464 | 195,217,744 | 195,538,127 | 3.11% |
| **Combined active DOM and view storage** | **371,230,876** | **282,136,428** | **282,763,579** | **23.83%** |
| Profiled physical live bytes | 421,282,825 | 332,505,657 | 333,287,304 | 20.89% |
| Profiled physical reserved bytes | 809,779,200 | 620,052,480 | 608,518,144 | 24.85% |
| Tracked process live bytes | 1,067,632,515 | 976,160,254 | 976,227,673 | 8.56% |
| Tracked process reserved bytes | 2,259,859,464 | 2,067,699,720 | 2,054,708,104 | 9.08% |

Every page uses fewer combined active bytes than the original. The aggregate saving is **84.37 MiB** and the per-page geometric mean reduction is **23.76%**. The 53 original fixtures use 12.52% fewer active bytes in aggregate. The 171 web templates use 28.04% fewer.

The shared-reset endpoint reduced combined active storage by 24.00% (84.97 MiB) and matched all 224 original normalized dumps. Compared with that endpoint, the November 21 build uses 627,151 more active bytes (+0.22%) and reserves 12,991,616 fewer process bytes (−0.63%). The replay matches 178 normalized dumps and differs on 46. In the matched subset, active bytes rise by 0.16% and process reserves fall by 0.72% relative to the shared-reset endpoint. The allocator-only trial below provides a layout-matched comparison of the pool change.

Of 72,015 connected elements, 64,827 originally had `DomStyleRareExt` (90.0%), 16,245 had it after shared resets (22.6%), and 16,372 have it in the latest replay (22.7%). The November 21 replay records 18,628 `DomStyleExt` instances, 10,856 private pseudo-style groups, and 38,608 nodes with a shared reset. A node can use a private group and a shared reset for different pseudos.

The pseudo-style stage passed 142 native CSS/DOM integration tests and 3,857 static layout smoke fixtures. The dynamic suite passed 125 of 126 tests, with the export-image case tracked in RI-141. Against the immediately preceding baseline, the pseudo-style stage exceeded the 1% median CPU target on Underscore.js (+1.54%, +2.40% on repeat). The Input pin mismatch blocked integrated Lambda verification of that build (LR12-64). [Stage measurements and timing](../impl/Radiant_Impl_View_Prop_Groups.md#pseudo-style-storage-and-shared-resets) · [RI-141](../Radiant_Issue_Ledger.md#ri-141--native-export-image-fixture-misses-its-preloaded-cache-entry) · [LR12-64](../Lambda_Issue_Ledger.md#lr12-64--split-host-expects-dom-and-html-declarations-missing-from-its-input-pin)

The November 21 profiles and comparison with the shared-reset endpoint are under `temp/view-prop-doc-refresh/`. The join with the original baseline is in `temp/view-prop-doc-refresh/end-to-end/comparison.csv` and `summary.json`. The archived shared-reset comparison is under `temp/view-prop-overall/`.

### Document-pool alignment trial

The allocator-only trial paired the same 224 pages. Live document-pool block spans, including headers and padding, fell from 105.17 to 97.01 MiB (7.76%). The document pool and active node arena together fell from 110.07 to 101.90 MiB (7.42%). Committed document-pool memory fell from 137.98 to 127.41 MiB (7.67%). All 224 normalized layout trees matched. The figures are in `temp/doc-pool-alignment/summary.json` (D5.1.4, D5.2.4).

## Appendix B. Original proposal evidence

### Initial size probe and smoke samples

The header probe used Clang C++17 targeting arm64 macOS. It included `view.hpp` and `layout.hpp` and measured the records with `sizeof` and a compiler record-layout dump. The workspace had a local `dom_element.hpp` field reorder, which the pre-refactor sizes include. The Lambda, Input, and Radiant revisions at capture were `27d7f65817`, `3c41675`, and `fc6cfbf` respectively. The element payload has no gaps between its direct fields in this layout, so repacking those fields cannot recover the 56 B projected for moving slots.

Five fixtures were run through the existing `lambda.exe layout ... --view-memory-profile` path to confirm observability. Values are the profiler's logical composites at `post_layout_pre_output`, in bytes. The executable's exact source revision and build profile were not established. These are memory-only smoke samples, with all six domains present and zero attribution errors. They establish neither present slot incidence nor the savings or performance of any proposal.

| Fixture | Active DOM | Active view property storage | Canonical style |
|---|---:|---:|---:|
| cern | 104,718 | 140,802 | 4,096 |
| article-layout | 206,762 | 264,306 | 55,290 |
| flex | 861,754 | 1,474,898 | 193,541 |
| contact-form | 107,801 | 149,068 | 56,458 |
| jqueryui | 829,420 | 974,375 | 304,271 |

The current profiler provides [pool-level attribution](../../radiant/radiant/view_memory_profile.cpp), and [the capture harness](../../utils/profile_view_reuse_memory.py) compares memory domains and normalized layout digests. The historical `DOM_PROP_ALLOCATION_RATE` summary counts allocations at document teardown. A live slot census is needed for placement decisions. For example, [overflow layout](../../radiant/radiant/layout_block.cpp) can allocate a `ScrollProp` even without authored scroll styles, so counting declarations would underpredict the number of relocated slots.

### Original implementation sequence

1. Add the opt-in live census and collect a paired release baseline. Record both absolute and overlapping occupancy for `fi/gi`, `tb`, `td`, `form`, `transform`, `scroller`, and `pseudo`.
2. Split `BlockProp` in small feature cuts while keeping its node slot in place. Start with groups with a clear lifetime and sparse usage. Migrate defaults, reset/free, CSSOM, inherited style identities, and percentage resolution together with each cut. Audit scratch copies, self-referenced computed line-height backing, and any replay or reset that copies a whole old record.
3. Split the current extension into document and view groups, then test relocation one group at a time. Preserve allocation affinity for adopted nodes and clear view roots before destroying their pools. Release document-owned feature payloads on node retirement or document teardown according to their owner. Fix the final shell and node size ratchets after measuring the winning layout.
4. Follow up on scroll, transform, boundary, and cache payload reductions only where their measured footprint justifies the change. Keep the version of each change that independently passes the memory, behavior, and timing gates.

The main integration points are [typed defaults](../../radiant/radiant/view_prop_defaults.cpp), [ensure helpers](../../radiant/radiant/view_prop_ensure.cpp), the [teardown catalog](../../radiant/radiant/view_pool.cpp), [retained resolved styles](../../radiant/radiant/css_resolved_style.cpp), [layout capture/replay](../../radiant/radiant/layout_pass.cpp), and the shared [element and slot declarations](../../lambda-input/lambda/input/css/dom_element.hpp). Use [the test map](../../test/README.md) and the existing `make test-radiant-baseline`, `make test-layout-baseline`, and `make test-css-cascade-memory` gates during implementation.

## Appendix S. Superseded placement proposals

- ~~Target **at most 96 B** for the common payload in the first prototype, including its extension link.~~ The 80 B probe held 14 logical properties. The retained block core keeps the co-read sizing and flow fields in DV4.2.
- ~~Target **128–160 B** for the populated `DomStyleRareExt` shell, including admitted moved slots.~~ DV6.1 and DV6.2 describe the 256 B rare extension and 48 B directory.
- ~~On this ABI, those candidates represent **56 B of inline slots** in total. If all move with no added inline storage or padding, the node could reach **320 B** from 376 B.~~ The first slot moves produced a 352 B node. The tag and class changes in DV6.6 and DV6.7 contributed to the current 328 B layout.
- ~~Store fallback tag spellings in the optional element directory.~~ That trial found 102 fallback nodes and brought the directory from 48 to 56 B. DV6.6 uses a sparse document table for fallback spellings, so the directory uses 48 B.
- ~~Move the class-cache pointer into `DomStyleExt`.~~ The 224-page census projected 19,323 additional directory allocations and 1,000,816 more bytes of allocation spans with that placement. DV6.7 keeps one pointer on the element.

The original WebKit display census measured zero populated legacy flags across 72,015 connected elements. A focused fixture exercised all three flags before DV6.5 retired the compatibility path (D5.2.4).
