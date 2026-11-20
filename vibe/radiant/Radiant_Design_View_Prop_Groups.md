# Radiant design: View property groups

**Status:** Proposed, 2026-11-20. Storage decisions and budgets below are candidates for measurement and review.

Implementation and measured placement decisions live in the [implementation record](../impl/Radiant_Impl_View_Prop_Groups.md).

**Scope:** Property storage reachable from `DomElement` and `DomElementExt`, from DOM construction through browsing and paged layout, paint, and teardown. Preserve CSS behavior, CSSOM answers, and the shared DOM/view node used by Radiant.

**Spec linkage:** [D5.1.4 and D5.2.1–D5.2.4](../../doc/Lambda_Formal_Design.md#d5-memory) govern allocation, ownership, attribution, and diagnostics. D5.5.1–D5.5.3 govern the Radiant seam and retained presentation. D9.1.1v4 and D9.1.3v2 govern the Input/Radiant split. This proposal extends [DV1–DV4, DV5a, DV6, and DV9–DV11](Radiant_Design_View_Struct.md#3-decisions) with finer property groups and a placement test. It proposes no change to the formal rulings.

## 1. What costs memory today

The largest opportunity is the payload allocated for common elements. Moving pointer fields can shrink every node, but a smaller `sizeof(DomElement)` can still increase the tree's memory if many more elements need an extension. Measure the document pool, node arena, and view storage together.

| Record | Current arm64 payload | What makes it expensive |
|---|---:|---|
| `DomElement` | 376 B | Every element pays for all inline slots, including pointers for roles it may never take. |
| `DomElementExt` | 568 B | The first feature to need it pays for every inline field. Six fragment unions take 144 B and ten pseudo-style pointers take another 80 B before any pointees. |
| `BlockProp` | 456 B | About 120 direct fields, including grouped value components and layout state. Common flow and inheritance paths call `ensure_block()`, paying for unrelated rare features. |
| `ScrollProp` / `TransformProp` | 216 B / 408 B | Scroll includes both snap configuration and live view geometry. Transform embeds three individual transform records even when only the transform list, origin, or perspective is used. |
| `FlexItemProp` / `GridItemProp` / `FormControlProp` | 256 B / 176 B / 256 B | Specializing a slot's location saves a pointer per node. The payloads themselves warrant a separate audit of role-specific fields and layout state. |
| `BoundaryProp` / `LayoutCache` | 224 B / 1,240 B | A field count alone misses nested records and arrays. Cache size matters only in combination with its current allocation rate and hit rate. |

These are compiler record sizes from the current headers. The earlier DV6 implementation recorded 328 extensions across 2,187 elements (15.0%), before the sidecar grew to its current size. Appendix A records aggregate smoke measurements and their limits. [Element and extension fields](../../lambda-input/lambda/input/css/dom_element.hpp) · [View prop definitions](../../radiant/radiant/view.hpp) · [DV6 implementation measurements](<../impl/Radiant_Impl_Dom_View_Struct (done).md#12-p3-measurements-and-decisions>)

## 2. Split properties by use and lifetime

### DV4.1 — Keep property groups small

Aim for **fewer than 15 logical properties per `ViewProp` payload**. Count a value, its unit, and its presence flag together when they are read and written as one property. Record exact size, direct member count, nested storage, and transitive allocations separately so an inline array or bulky record does not disappear from the accounting. Review groups with 15 or more properties or unusually large payloads. Keep jointly used, small records together when splitting would add allocations and dependent loads without skipping much storage.

Use three tests to place a field. Does it have the same lifetime as its neighbors? Do the same elements need it? Do the same hot readers usually read it alongside its neighbors? A group should be independently lazy if most elements that allocate its parent never use it. Reserve pass scratch for values that expire before publication (DV10). Retained geometry belongs to the view and its generation. Durable authored values stay with the cascade (DV9b).

### DV4.2 — Give `BlockProp` a common core and a lazy extension

Target **at most 96 B** for the common payload in the first prototype, including its extension link. Select the final hot fields from read profiles and per-element occupancy. Begin with 14 logical properties: width, height, box sizing, writing mode, direction, bidi mode, zoom, text alignment, white space, line height, text transform, word break, overflow wrap, and text wrap mode. A layout-only probe with those fields, their current basic size kinds, the embedded computed line-height value, and an extension pointer measured **80 B** on arm64. Confirm min/max constraints and baseline usage before deciding their final placement. Preserve the distinction between computed and used forms where both are needed.

Make `BlockPropExt` an optional view-owned group for the uncommon block features. It may own smaller feature groups if they are independently sparse. Start with these cuts and measure their co-occurrence before choosing which groups warrant a separate allocation:

| Candidate group | Initial contents and boundary |
|---|---|
| Advanced sizing and alignment | Min/max bounds and kinds, alternate sizing expressions and percentage bases, fit-content arguments, uncommon alignment. Keep any frequently co-read constraints with the core sizing path if the split increases cost. |
| Text flow options | Indent with its deferred value, last-line alignment and justification, hyphenation options, tabs and spacing. Split by feature when the options have different allocation rates. Resolve inherited values and choose the group once per paragraph or flow context where possible. |
| Generated lists and counters | Marker type, position and image, counter operations. Use separate subgroups if generated counters and lists seldom coincide. |
| Fragmentation and containment | Break rules, orphans/widows, and box decoration behavior in a fragmentation group. Container queries, containment, and content visibility in a separate group. Keep last remembered intrinsic size in storage that survives reflow if its semantics require it. |
| Line trim and clamp | Authored trim and clamp options in small style groups. Keep working accumulators in pass state when their lifetime allows. Keep any committed baseline, clamp, or trim outcome needed by replay, paint, or CSSOM in view-owned geometry. |
| Float and vertical-flow bookkeeping | Move pass-local exclusion and resolution values into the existing layout context or per-box scratch after auditing multipass and replay readers. Preserve any state proven to cross the pass boundary with an explicit lifetime. |

Allocate the extension on the first real write to a cold group, including inherited non-initial values and UA defaults when relevant. A read of an absent group returns its typed canonical defaults or a null-presence result. A CSS-wide keyword, explicit default, NaN percentage sentinel, presence bit, or deferred calc must retain its current meaning. The extent of the split depends on measured writes: a branch that initializes a feature for every block can make a nominally optional group nearly universal.

## 3. Shrink the extension and evaluate each slot move

### DV6.1 — Keep a small extension directory with explicit ownership

Target **128–160 B** for the populated `DomElementExt` shell, including admitted moved slots. Keep the storage owner needed for adoption and a small set of typed slots. Allocate feature payloads only when their feature is used. Keep a direct slot if almost every element that needs its parent also needs that payload, or if an extra hop is costly in a frequent path. Collapse redundant shells when a feature has only one payload.

| Current extension contents | Proposed placement |
|---|---|
| Fragment unions, presence bits, inline edge snapshots, layout fragment list/count, ruby geometry | View-owned geometry groups with explicit generation and teardown. Measure which union kinds are live together before choosing a dense record or a small common record with a rare tail. Keep list/count together and group ruby separately if sparse. |
| Pseudo specified-style trees | A document-side pseudo-style group, allocated at first slot. Evaluate a small representation for the commonly populated kinds before reserving ten pointers. The existing `pseudo` view slot is generated content and layout state, with a separate lifetime. |
| Multicolumn, vector path, filters, backdrop filters, markers | Keep typed view slots, grouped into a feature directory only if occupancy offsets the extra pointer and load. Custom layout paint may borrow a document resource, so its view binding and resource keep separate ownership. |
| Transition state, Web Animations, animated display | Optional document-owned effect state for animation data that intentionally survives relayout. Keep transient samples and view-pool borrows in view-owned slots with explicit reset. |
| Pending scroll coordinates and remembered intrinsic sizes | Small document-owned records with their existing presence and lifetime rules. Coordinate with `DocState` for live interaction. |
| Shadow/slotting, attributes and CSSOM declaration backing, popover/dialog, scripts and form/task state | Document-owned metadata grouped by feature and allocated independently. `Own` and borrow semantics apply here. Keep storage-owner tracking safe through adoption and keep pending task state through view teardown and detach. |

Document-owned groups follow D5.5.1 and D5.5.2. A `ViewProp<T, Slot>` owns a view payload. A `ViewRef<T, Slot>` refers to view storage or storage that outlives it and needs its binding cleared. View children under a view-owned root can use typed `Own<T>` fields, with one teardown path for the root and its children. Only document-to-view roots need `DomViewSlot` registration. Keep opaque declarations and the shared element definition on the Input side, and keep concrete view payload definitions and view teardown in Radiant, per D9.1.1v4 and D9.1.3v2.

### DV6.2 — Move cold slots only after testing the placement

| Slot | Proposed order and an acceptance question |
|---|---|
| `tb`, `td`, `form` | First candidates. Profile tables and dense forms separately. Forms and table cells can coexist, and either can be a flex or grid item. Keep separate slots and test dirty CSSOM and native control access before a clean layout. |
| `pseudo` | First candidate. Compare overlap with the proposed document-side pseudo-style group and account for generated and synthetic nodes. Sample generated-content and counter-heavy pages. |
| `transform` | Trial after profiling absent checks during paint, hit testing, and animation. Alongside the move, measure a separate experiment that allocates the inline-sized individual transforms only where used and preserves both sampled and owned transform-function lifetimes. |
| `fi`, `gi` | Move as **one eight-byte tagged union** only if flex/grid-heavy pages pass. Their readers run in inner loops, and modern pages can have many items. Bind the typed item pointer at algorithm entry or in existing item-layout records so inner loops do not load `ext` repeatedly. |
| `scroller` | Last candidate. Overflow and clipping checks are frequent even when the pointer is absent. First test keeping the fast overflow check on the current path while splitting rarely used snapping configuration from `ScrollProp`. Measure root/body overflow, scroll containers, clip and scroll hit testing, and scroll event delivery before relocating the slot. |

Start with `font`, `in_line`, `bound`, `blk`, and existing placement of unlisted slots such as `position`, `embed`, and `layout_cache`. Compare two placement candidates for the moved slots: direct fields in the small extension shell, and a single view-owned role or feature directory reachable from the shell. A directory can save space for elements that need only DOM metadata, but adds a lookup and an allocation for elements that need its contents. A slot that misses the performance gate stays shallow. Preserve the parent-item tag independently of the table and form roles (DV5a). In-place reflow and view retirement must keep their current scroll and form semantics.

On this ABI, those candidates represent **56 B of inline slots** in total. If all move with no added inline storage or padding, the node could reach **320 B** from 376 B. Keeping `scroller` at the first level gives an illustrative **328 B** with the other moves. These are layout estimates, not expected savings for the whole tree.

Compare against the baseline **after** shrinking the shell, so a slot move does not get credit for savings from a different change. For \(N\) elements, \(E_0\) and \(E_1\) live extensions before and after a placement trial, and actual shell allocation charges \(H_0\) and \(H_1\), an approximate byte delta is:

```text
delta = (node_charge_after - node_charge_before) * N
      + H1 * E1 - H0 * E0
      + delta_payload_and_allocator_bytes
```

Use payload bytes for the logical calculation and allocator charges for the physical calculation. Count allocator overhead once. Count the union of elements requiring any moved slot, and measure how many would already have a shell. For example, moving six slots saves 48 payload bytes per node. If a 96 B shell becomes 144 B and it previously covered 30% of nodes, the move must stay below about **53%** shell coverage to win before allocation overhead, with other payloads held equal.

## 4. Make each group's storage contract explicit

1. **Expose a read accessor, a presence test where needed, and one mutable ensure path.** A read must never allocate. Put default selection and correct pool choice there, and reuse existing typed prop helpers. Call sites in cascade, layout, paint, and CSSOM should work through the same typed accessors even during placement trials. Keep anonymous scratch items in pass storage without allocating a document shell. Secondary resolution continues to use its selected view's storage and leaves browsing-view bindings alone.
2. **Use one registration source per view slot.** Extend the existing slot/teardown catalog where practical so the slot, typed access, ownership, default/reset behavior, payload cleanup, and clear behavior agree. Keep compile-time coverage and debug post-clear checks. Child groups should follow their root's existing teardown entry. Avoid per-element maps, virtual lookup, and extra per-property scans in hot paths.
3. **Keep immutable resolved style separate from used geometry where the split makes sharing possible.** Keep the existing canonical `InlineProp` bindings and write gate (DV9c). Treat interned inherited text deltas as a separate experiment if they pay for themselves. Mutable view results, history, and animation samples must keep the lifetimes their readers expect. Preserve invalidation keys when storage moves, especially inherited style identity, revisions, and the computed/used size distinction.
4. **Audit large payloads by bytes and use after the block split.** Test `BoundaryProp::flow_margin` as an optional fragmentation snapshot. `LayoutCache` already has optional histories. Measure the hit rates of its inline measurement and intrinsic-width arrays before testing a small base with optional arrays. Split `FormControlProp` by subtype and place flex/grid measurement scratch in the pass when it never escapes (DV10). The 15-property guideline opens these reviews. Space saved per document and observed regressions decide which changes ship.

## 5. Measurement and acceptance

- **Capture four states.** Sample a constructed DOM before browsing-view properties are allocated, first committed layout, repeated retained reflow or recascade, and the same settled document after a fixed mutation script. Track peak and settled memory separately. Count synthetic and authored nodes separately and include both in byte totals. Track detached or retained objects separately from connected-tree slot occupancy.
- **Measure group occupancy and overlap.** Extend the existing diagnostic profiler with a single typed walk at the sample points. Report live group counts and allocated bytes, including child payloads, and reconcile estimates against actual storage-owner pool charges. Count shared allocations once in their owning storage and report static defaults separately. Count unique elements with extensions and with each candidate slot, old-extension overlap, the union of candidate users, and non-initial field/group frequency. Sample how often hot paths read each slot, including checks for absent values. Use opt-in aggregate allocation/free counts and check for empty groups after repeated reflow to expose churn. Keep this instrumentation diagnostic under D5.2.4 and run timings with it off.
- **Use paired workloads.** Reuse the 46-page capture harness, plus focused plain text, flex/grid dashboards, tables/forms, scroll/snap, generated content, transformed/animated pages, and ruby/multicolumn/paged layout. Include large sparse-style trees and one dense case per candidate so a corpus median cannot hide a regression in a common workload. Use the same resources, viewport, fixture operations, and equivalent output for both builds.
- **Require a material memory win.** Proposed bar: at least **15% lower typical combined active DOM and view-property bytes** after first layout across the page corpus, using the geometric mean of paired per-page ratios. Report pre-layout savings, reserved and peak bytes, tail pages, and cost per authored element separately. No repeatable material increase on dense role workloads. Compare any savings with canonical style and pool retention growth so bytes moved into another domain still count. Treat the proposed struct-size targets as diagnostic until the measured variant passes (DV11).
- **Require no detectable performance regression.** Measure construction, cascade, first layout, retained reflow, paint, hit testing, and interactive scroll as applicable, in verified release builds on the same host, with paired runs and separate warm/cold measurements. Establish noise with repeated baseline runs. Proposed equivalence limits are **1% for median time** and **3% for p95** on preselected representative workloads, with sufficient repetitions to resolve those limits. A repeatable slowdown fails the gate even when the overall average improves. Report inconclusive comparisons and retest before admitting the placement. Capture size and timing for each split and slot move independently.
- **Require parity at each step.** Compare layout trees, CSSOM results, paint output, and retained interaction behavior. Run focused regressions for the touched features, then the Radiant and layout baselines. Cover rebuild/reset, style removal after a cold-group allocation, reflow replay, detach and adoption, view teardown, and alternate/paged views. Form-in-grid and form-in-table-cell exercise role coexistence. Include a large table and dense flex/grid fixture for performance.

## Appendix A. Baseline evidence and implementation sequence

### Evidence collected for this proposal

The header probe used Clang C++17 targeting arm64 macOS. It included `view.hpp` and `layout.hpp` and measured the records with `sizeof` and a compiler record-layout dump. The workspace had a local `dom_element.hpp` field reorder, which the numbers above include. The Lambda, Input, and Radiant revisions at capture were `27d7f65817`, `3c41675`, and `fc6cfbf` respectively. The element payload has no gaps between its direct fields in this layout, so repacking those fields cannot recover the 56 B projected for moving slots.

Five fixtures were run through the existing `lambda.exe layout ... --view-memory-profile` path to confirm observability. Values are the profiler's logical composites at `post_layout_pre_output`, in bytes. The executable's exact source revision and build profile were not established. These are memory-only smoke samples, with all six domains present and zero attribution errors. They establish neither present slot incidence nor the savings or performance of any proposal.

| Fixture | Active DOM | Active view property storage | Canonical style |
|---|---:|---:|---:|
| cern | 104,718 | 140,802 | 4,096 |
| article-layout | 206,762 | 264,306 | 55,290 |
| flex | 861,754 | 1,474,898 | 193,541 |
| contact-form | 107,801 | 149,068 | 56,458 |
| jqueryui | 829,420 | 974,375 | 304,271 |

The current profiler provides [pool-level attribution](../../radiant/radiant/view_memory_profile.cpp), and [the capture harness](../../utils/profile_view_reuse_memory.py) compares memory domains and normalized layout digests. The historical `DOM_PROP_ALLOCATION_RATE` summary counts allocations at document teardown. A live slot census is needed for placement decisions. For example, [overflow layout](../../radiant/radiant/layout_block.cpp) can allocate a `ScrollProp` even without authored scroll styles, so counting declarations would underpredict the number of relocated slots.

### Order of work

1. Add the opt-in live census and collect a paired release baseline. Record both absolute and overlapping occupancy for `fi/gi`, `tb`, `td`, `form`, `transform`, `scroller`, and `pseudo`.
2. Split `BlockProp` in small feature cuts while keeping its node slot in place. Start with groups with a clear lifetime and sparse usage. Migrate defaults, reset/free, CSSOM, inherited style identities, and percentage resolution together with each cut. Audit scratch copies, self-referenced computed line-height backing, and any replay or reset that copies a whole old record.
3. Split the current extension into document and view groups, then test relocation one group at a time. Preserve allocation affinity for adopted nodes and clear view roots before destroying their pools. Release document-owned feature payloads on node retirement or document teardown according to their owner. Fix the final shell and node size ratchets after measuring the winning layout.
4. Follow up on scroll, transform, boundary, and cache payload reductions only where their measured footprint justifies the change. Keep the version of each change that independently passes the memory, behavior, and timing gates.

The main integration points are [typed defaults](../../radiant/radiant/view_prop_defaults.cpp), [ensure helpers](../../radiant/radiant/view_prop_ensure.cpp), the [teardown catalog](../../radiant/radiant/view_pool.cpp), [retained resolved styles](../../radiant/radiant/css_resolved_style.cpp), [layout capture/replay](../../radiant/radiant/layout_pass.cpp), and the shared [element and slot declarations](../../lambda-input/lambda/input/css/dom_element.hpp). Use [the test map](../../test/README.md) and the existing `make test-radiant-baseline`, `make test-layout-baseline`, and `make test-css-cascade-memory` gates during implementation.
