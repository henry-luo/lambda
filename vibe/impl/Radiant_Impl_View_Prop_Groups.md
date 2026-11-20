# Radiant implementation: View property groups

Design: [DV4.1, DV4.2, DV6.1, and DV6.2](../radiant/Radiant_Design_View_Prop_Groups.md).
Allocation and attribution follow D5.1.4 and D5.2.1–D5.2.4. The shared
element and presentation lifetime rules remain D5.5.1–D5.5.3, D9.1.1v4,
and D9.1.3v2.

## Current checkpoint

2026-11-20, primary checkout, Lambda `27d7f65817`, Input `3c41675`, and
Radiant `fc6cfbf`, with local changes. The baseline kept the pre-existing
`DomElement` field reorder. Both rounds of property splits were measured on
53 static fixtures and 171 web templates in a native release host. The
`LayoutCache` split was reverted and is deferred to a separate investigation.
Sources, build logs, per-page snapshots, and comparison scripts are under
`temp/view-prop-groups/`.

The integrated Lambda release build failed before this change. The pinned
Input headers lack DOM and parser declarations that Lambda uses. See
[LR12-64](../Lambda_Issue_Ledger.md#lr12-64--split-host-expects-dom-and-html-declarations-missing-from-its-input-pin).
The native release archives and runtime-independent static HTML harness
provide a matched comparison for the parts that can be validated here.

## Implemented cuts

- `BlockPropExt` holds less common list, counter, and fragmentation fields.
  `BlockContainmentProp` holds containment and content visibility. Absent
  groups have immutable defaults. The core releases the groups through the
  existing reset/teardown path.
- `DomElementExt` allocates pseudo styles, attribute state, and fragment
  geometry in separate typed groups. Pseudo styles and attribute state use
  document ownership. The fragment group uses the existing document pool and
  resets with view geometry. The inline attribute variant saved about 111 kB
  in the paired cache trial. The retained build keeps the original grouping
  from the property-only trial.

## Placement decision

The baseline census found 19,248 live extensions on 19,499 connected elements
(98.7%). The move candidates occur on 3,286 elements, 3,179 of which already
had an extension. Moving all seven slots directly into the 288-byte shell
would save 14,056 bytes on the 251 nodes without an extension before charging
for the 107 new 344-byte extensions. The net would be a 22,752-byte increase
in payload across the census. The refactor keeps those slots in place. A
separate view-owned role directory would need its own cost and release-time
qualification.

## Final memory comparison

The native harness was built twice against the respective Input and Radiant
release archives, using Clang arm64 with `-O3` and full LTO. It loaded the
53 local HTML pages at 1200 × 800 with scripting disabled and captured the
view-memory profiler's schema 1 domains after first layout and before
serializing the view. Measurements are summed across independent processes.

| Metric | Before, bytes | After, bytes | Change |
|---|---:|---:|---:|
| Active DOM storage | 42,994,542 | 40,576,358 | −5.62% |
| Active view-property storage | 57,604,938 | 55,870,650 | −3.01% |
| Combined active DOM and view property | 100,599,480 | 96,447,008 | −4.13% |
| Profiled physical live storage | 105,633,807 | 101,481,335 | −3.93% |
| Profiled physical reserved storage | 200,801,280 | 197,524,480 | −1.63% |

The geometric mean reduction of the paired combined active measurements
was 5.56%, below the proposed 15% memory target. The DOM storage metric
alone fell 10.98% by geometric mean. Canonical style bytes remained
5,503,991 in aggregate.
All 53 normalized layout trees matched and both sides reported zero
memory attribution errors.

| Arm64 record | Before | After |
|---|---:|---:|
| `DomElement` | 376 B | 376 B |
| `DomElementExt` | 568 B | 288 B |
| `BlockProp` | 456 B | 344 B |
| `LayoutCache` | 1,240 B | 1,240 B |

Sizes exclude optional allocations. The samples and `accepted-comparison.csv`
record after-first-layout behavior only. A before-layout, repeated reflow,
and dynamic-host performance comparison remain unqualified.

### Web template coverage

The same release binaries measured all 171 `web-tmpl` entries selected by
the layout suite's discovery rule. Each template contributed its
top-level `index.html`. The viewport was 1200 × 800 with local resources
and scripting disabled. All 171 layout trees matched after removing the
capture timestamp. Both builds reported zero memory attribution errors.

| Corpus | Pages | Active DOM and view bytes before | After | Aggregate reduction | Per-page geometric mean reduction |
|---|---:|---:|---:|---:|---:|
| Original fixtures | 53 | 100,599,480 | 96,447,008 | 4.13% | 5.56% |
| Web templates | 171 | 270,631,396 | 260,735,308 | 3.66% | 4.16% |
| Combined | 224 | 371,230,876 | 357,182,316 | 3.78% | 4.49% |

Every web template used fewer active DOM and view bytes. The template
aggregate fell 4.57% for active DOM, 2.85% for active view properties,
3.14% for profiled physical live bytes, and 2.07% for profiled physical
reserved bytes. Canonical style bytes stayed at 50,552,934. The combined
geometric mean reduction remains below the proposal's 15% target (DV11). The
template inventory and paired results are in `web-tmpl/manifest.json`
and `web-tmpl/comparison.csv` under `temp/view-prop-groups/`.

The template census finds 45,579 extensions on 52,516 connected elements
after layout. Of the 15,670 elements using any relocation candidate,
6,017 would need a new shell. Under the same payload model as the original
census, putting all seven slots on the shell adds 1,681,376 bytes across
the templates.

### Scroll, transform, and form property groups

DV4.1 favors separately allocated groups when most instances can use the
defaults. D5.1.4, D5.5.1, and DV10 govern the distinction between
pass-local state and retained geometry.

| Group | Arm64 core before | Core after | Optional group |
|---|---:|---:|---|
| `ScrollProp` | 216 B | 80 B | `ScrollSnapProp` (144 B) stores scroll margin, scroll padding, and snap values. Scroll-into-view reads spacing with or without an active snap axis. |
| `TransformProp` | 408 B | 56 B | `IndividualTransformProp` (360 B) stores `translate`, `rotate`, and `scale` independently of the `transform` list. Animated bindings still borrow the sample owner's values. |
| `FormControlProp` | 256 B | 208 B | `FormPlaceholderProp` (24 B) and `FormFileButtonProp` (40 B) store resolved pseudo paint. The core keeps the live range, select, and text projection exposed to the Lambda adapters. |

The view pool owns each new group. Retained recascade releases stale
groups and restores defaults. Form pseudo paint follows the current
control type and releases its font handle during reset or retirement.
The memory profiler reports allocation counts for all four new groups.

The additional splits saved 723,608 active bytes across the 224 pages
(0.20% aggregate, 0.21% per-page geometric mean). Profiled physical live
bytes fell by the same amount. The pools reserved 793,919,488 bytes in
aggregate on both sides. The full refactor reduced aggregate active DOM
and view storage from 371,230,876 to 356,458,708 bytes (3.98%).

The 3,249 scroll cores used one optional snap/spacing group. The 699
transform cores used no individual-transform groups in this corpus. The
803 form cores used 246 placeholder groups and four file-button groups.
All 224 normalized layout trees matched the first-round refactor and no
page increased active memory. The per-page data is in
`temp/view-prop-groups/phase2/comparison.csv`.

The paired native release timing run randomized build order and recorded
15 samples per build per page after two warmups. The timed process loaded,
laid out, and released the page. It skipped profile and layout output.

| Page | Median CPU before | After | Change | Median wall change |
|---|---:|---:|---:|---:|
| Bootstrap Kitchen Sink | 427.73 ms | 426.94 ms | −0.18% | +0.06% |
| Underscore.js | 149.03 ms | 145.55 ms | −2.34% | −2.23% |
| Contact form | 24.41 ms | 23.85 ms | −2.28% | −1.81% |
| Avenger template | 58.34 ms | 58.01 ms | −0.56% | −0.85% |
| Victory template | 57.80 ms | 57.02 ms | −1.35% | −1.19% |

These timings cover native static layout. The raw samples are in
`temp/view-prop-groups/phase2/timings.json`.

`BoundaryProp` (224 B) merits a census because normal block layout writes
its 56-byte `flow_margin` snapshot. Measure how often the retained margin
differs before changing that path. `PositionProp` (208 B), `FlexItemProp`,
and `GridItemProp` still have candidates for a DV10 pass-lifetime audit.

## Deferred: layout-cache storage

The retained cache has its nine general measurement entries and four
intrinsic-width entries inline. The width-only trial moved the four entries
to a 768-byte allocation made on first store. It reduced aggregate active
DOM and view storage to 90,072,288 bytes (−10.46% from baseline), but the
median CPU time rose from 382.1 to 417.5 ms on Bootstrap Kitchen Sink and
from 129.3 to 145.0 ms on Underscore.js in the paired native release run.
The measurement-only and combined trials also showed timing regressions
on large pages. These experiments remain in the saved profiles. Their
savings are excluded from the retained comparison above.

## Verification

- The scroll, transform, and form round passed 123 of 124 native dynamic
  tests. Both new regressions passed, with retained recascade,
  scroll-into-view using spacing, input type changes, and animation
  allocation and removal. A combined static fixture exercised all four
  optional groups and matched the first-round layout. The same run
  passed all 39 StateStore tests and 3,857 static layout smoke fixtures.
  The single dynamic failure was the export image case tracked in RI-141.
- The native release layout smoke test loaded and laid out all 3,857
  non-dynamic fixtures. Six targeted layouts also matched the baseline,
  covering containment, ruby and bidi, counters, and multicolumn layout.
- The native cache and dynamic-layout tests passed 119 of 120. The export
  image case missed its preloaded cache entry and tried to resolve its
  fixture hostname. See [RI-141](../Radiant_Issue_Ledger.md#ri-141--native-export-image-fixture-misses-its-preloaded-cache-entry).
- The Radiant integer-cast lint rule passed. The integrated Lambda build
  did not reach its test suite because of the Input pin mismatch.
