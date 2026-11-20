# Radiant implementation: View property groups

Design: [DV4.1, DV4.2, DV6.1, and DV6.2](../radiant/Radiant_Design_View_Prop_Groups.md).
Allocation and attribution follow D5.1.4 and D5.2.1–D5.2.4. The shared
element and presentation lifetime rules remain D5.5.1–D5.5.3, D9.1.1v4,
and D9.1.3v2.

## Current checkpoint

2026-11-20, primary checkout, Lambda `27d7f65817`, Input `3c41675`, and
Radiant `fc6cfbf`, with local changes. The baseline kept the pre-existing
`DomElement` field reorder. The property splits and directory were measured on
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

## Lazy table, form, transform, and pseudo directory

DV6.2 moves `tb`, `form`, `transform`, and `pseudo` into a 32-byte
`DomPropExt`. `position` stays directly on `DomElement`. On arm64 the
element shrinks from 376 to 352 bytes. The node's original storage pool
owns the directory, including when an adopted element has a different
owner document. Under D5.5.1, the view pool still owns its payloads and
StateStore retains canonical form state. Script-created form properties
can attach before layout. View teardown releases the directory when its
last slot clears, and retirement also covers form state created before a
view exists. The parent-item and table/form role contracts follow DV5a.

After first layout, 14,272 of 72,015 connected elements (19.8%) needed
the directory across the 224 pages. Every normalized layout tree matched
the immediately preceding build. The allocator profiler reported the
following sums across independent native release runs.

| Metric | Before, bytes | After, bytes | Change |
|---|---:|---:|---:|
| Process tracked live | 1,052,725,719 | 1,050,691,087 | −0.19% |
| Process tracked reserved | 2,243,999,752 | 2,241,861,640 | −0.10% |
| Active DOM and view properties | 356,458,708 | 356,583,276 | +0.035% |

Every page used less tracked process live memory. The process live saving
was 2,034,632 bytes and the reserved saving was 2,138,112 bytes. The
active DOM and view composite charges the new document-pool allocations
but does not include `input.arena`. The HTML builder allocates parser-backed
DOM elements in `input.arena`. Process totals include that storage.
These are tracked allocator counters. The per-page data is in
`temp/view-prop-groups/dom-prop-ext/impl/comparison.csv`.

Two paired release runs recorded 15 samples per build per page in each
run after two warmups. The measured process loaded, laid out, and
released the page. Output serialization was disabled.

| Page | Median CPU change, first run | Median CPU change, repeat |
|---|---:|---:|
| Bootstrap Kitchen Sink | +1.08% | +1.02% |
| Underscore.js | +1.47% | +1.17% |
| Contact form | −0.55% | +0.18% |
| Avenger template | +0.49% | +1.24% |
| Victory template | +1.01% | +0.32% |

The two largest pages exceeded the proposed 1% median timing limit in
both runs. The raw samples are in `timings.json` and `timings-repeat.json`
beside the comparison file.

## Namespace identity

Under D5.5.1 and DV6.2, the node retains its expanded name across view
resets and adoption. An inline flag records the standard HTML namespace.
`DomPropExt::namespace_uri` stores other resolved URIs, including an
explicit empty namespace. The local name uses `tag_name` or the suffix
after its colon when the name is qualified. The initial namespace split
also allowed a separate explicit local-name override.
Parser-supplied Mark metadata still has a read path, and the private
attribute getters expose the resolved fields to existing native callers.
`DomAttributeCache` shrank from 64 to 48 bytes, `DomPropExt` grew from
32 to 48 bytes, and `DomElement` is still 352 bytes on arm64.

In the same 224 paired native release loads, after-first-layout usage
changed as follows.

| Metric | Before | After |
|---|---:|---:|
| Elements with `DomElementExt` | 64,827 (90.0%) | 55,399 (76.9%) |
| Elements with `DomPropExt` | 14,272 (19.8%) | 14,497 (20.1%) |
| Elements with `DomAttributeCache` | 61,483 | 781 |
| Active DOM and view property bytes | 356,583,276 | 341,872,017 |
| Tracked process live bytes | 1,050,691,087 | 1,035,894,680 |
| Tracked process reserved bytes | 2,241,861,640 | 2,178,390,024 |

Active DOM and view storage fell 4.13% in aggregate. All 224 normalized
layout outputs matched. Pseudo-style storage was still present on 45,959
elements. Per-page data is in
`temp/view-prop-groups/namespace-identity/comparison.csv`.

The five-page release benchmark sampled load, first layout, and teardown
15 times per build per page after warmup. Median CPU time fell 7.74% for
Bootstrap Kitchen Sink, 11.49% for Underscore.js, 5.28% for Avenger, and
4.64% for Victory. It increased 0.88% on the contact-form fixture.

The adopted-node check also exposed a teardown ordering defect. A
destination registry used an adopted node's address after destroying the
source document's arena. Manual slot links are now released before the
document retires those resources, while the registry remains available to
the rest of cleanup. [LI-081](<../Lib_Input_Issue_Ledger (fixed).md#li-081>)

### Derive the local name from the tag

Under D10.1.4 and S8.2.2v6, XHTML retains its expanded namespace identity
and case. Parsed Mark elements already carry the local name in the typed
tag. The DOM builder recognizes normalized XHTML, SVG, and MathML names
from the resolved namespace metadata already stored on those elements.
XHTML emits one private URI attribute and derives the local part from the
tag. Literal HTML colons and namespace-qualified colons remain distinct.

`DomPropExt` no longer stores a local-name string. The native identity
setter accepts the tag spelling or its qualified suffix. An authored
`__lambda_ns_local_name` cannot change selector identity. The private
getter projects the same derived name for existing native callers.

On arm64, `DomPropExt` shrank from 48 to 40 bytes. The 14,497
directories in the 224-page corpus used 115,976 fewer active DOM and
view-property bytes in aggregate. Both sizes occupy the same aligned
pool span, and tracked reserved bytes were unchanged. All 224 normalized
layout trees matched. The separate XHTML fixture with prefixed HTML,
SVG, MathML, and a null namespace also matched. All 141 native DOM
integration tests passed. Per-page data is in
`temp/view-prop-groups/derived-local-name/comparison.csv`.

In 15 paired release samples per page, median CPU time fell 8.40% for
Bootstrap Kitchen Sink, 5.58% for Underscore.js, and 1.84% for both
templates. It increased 0.23% on the contact-form page.

## Input-owned namespace URI strings

Each URI spelling is interned in the Input NamePool. DOM element and
attribute records borrow those bytes. XML namespace targets, parsed `xmlns`
values, and XHTML’s private URI metadata share the same storage. Prefixes
retain their lexical spelling, and namespace comparisons use URI content
under S8.2.2v6. The document caches the pooled HTML URI for elements that
use the inline HTML flag, and the pooled empty URI for the null namespace.

Copies into another Input intern the URI in the destination NamePool.
Adopted nodes retain the source Input behind any URI bytes they borrow
under D5.5.1.
The cache uses two pointers per DOM document. On arm64, `DomElement` is
352 bytes and `DomPropExt` is 40 bytes.

The release fixture with 2,000 SVG `use` elements in XHTML used 96,287 fewer
tracked live process bytes (1.06%) and 65,536 fewer reserved bytes (0.44%).
All 224 corpus layouts and the XHTML stress layout matched. Across the 224
separate page loads, active DOM and view storage fell by 6,291 bytes
(0.0018%) and total tracked live process bytes rose by 12,632 bytes
(0.0012%). Per-page profiles are in
`temp/view-prop-groups/interned-namespaces/comparison.csv`.

Over 15 paired release samples, the XHTML stress fixture’s median CPU time
rose from 45.55 to 45.70 ms (0.32%). Median CPU changes on five ordinary
pages ranged from −0.41% to +2.92%. The Avenger template had the largest
increase, from 63.80 to 65.67 ms. Its paired changes varied across runs.

The native release checks passed all 142 CSS/DOM integration tests, all 83
HTML/XHTML parser tests, all 41 namespace tests, and the static layout
smoke suite. The native dynamic suite passed 124 of 125 tests. Its export
image fixture still reproduces RI-141.

## Packed element extension flags

`DomElementExt` packs 14 Boolean flags into the alignment gap after its
`NameId`. The native producer stores image offsets as fractional CSS pixels.
FormData and dialog submission round them with `math_round_to_int_sat`
when serializing decimal text across the DOM seam (D9.5.6v2). The state
belongs to the document under D5.5.1.

The arm64 record layout uses 248 bytes per extension, down from 288 bytes
(40 bytes, 13.9%). The 55,399 extensions in 224 separate release page loads
used 2,215,960 fewer active DOM and view storage bytes (0.65%). All 224
normalized layouts matched the URI-interning baseline. Per-page data is in
`temp/dom-element-ext/comparison.csv`. The integrated host still reproduces
LR12-64, which blocks end-to-end form submission validation.

## Pseudo-style storage and shared resets

The document-owned `DomPseudoStyleGroup` now lives under `DomPropExt`
(D5.5.1, DV6.1). Its private trees use the existing borrow-retirement path
when cascade or node retirement replaces them. `DomPropExt::empty()` keeps
authored styles alive across view teardown. `DomElementExt` uses 240 bytes,
`DomPropExt` uses 48 bytes, and the element is 352 bytes on arm64.

The existing selector match and active stylesheet checks govern universal
`::before` and `::after` resets. When a matched rule sets just `box-sizing`
to a literal `content-box` or `border-box`, the source document pool can
publish one owned copy of its declaration. Two four-bit indexes in an
existing byte of node padding select the immutable trees for each element.
The copy keeps the original cascade priority and survives stylesheet edits
while a retained node can still see it. Adoption resolves the indexes in
the physical source document. A conflicting match or a writable pseudo
slot materializes the selected tree before applying the additional
declaration. Unshareable rules and exhausted indexes use the private path.
The element flags also record private group presence so layout can skip
the property-directory lookup on elements with no private pseudo styles.

Each stage uses the same 224 release pages with normalized layouts compared
against the previous build. Counts are summed across independent processes.

| After first layout | Baseline | Move only | Shared resets |
|---|---:|---:|---:|
| Elements with `DomElementExt` | 55,399 | 16,245 | 16,245 |
| Elements with `DomPropExt` | 14,497 | 52,752 | 18,628 |
| Private pseudo-style groups | 45,959 | 45,959 | 10,856 |
| Elements referencing a shared reset | 0 | 0 | 38,608 |
| Active DOM and view bytes | 339,533,790 | 331,645,854 | 282,136,428 |
| Tracked process live bytes | 1,033,559,976 | 1,025,669,680 | 976,160,254 |

Per-page profiles and layout comparisons are in
`temp/pseudo-style-placement/{move,shared}/comparison.csv`.
All 224 normalized layouts matched. The release build passed all 142
CSS/DOM integration tests and all 3,857 static layout smoke fixtures. The
native CSSOM regression exercises a reset on a non-generated pseudo,
promotion to a private cascade, selector and stylesheet removal, and the
generated box's borrow. The adoption check preserves source-owned shared
styles. The native dynamic suite passed 125 of 126 tests, with the export
image failure tracked in RI-141.

Over 15 paired release samples, the finished build changed median CPU
time by −0.34% on Bootstrap Kitchen Sink, +0.64% on Contact Form, −0.56%
on Avenger, and +0.60% on Victory. Underscore.js rose from 125.25 to
127.17 ms (+1.54%). A second run with 30 pairs measured 123.69 to
126.66 ms (+2.40%) on Underscore.js. That page exceeded the earlier 1%
timing target. Raw results are in `temp/pseudo-style-placement/shared/`.

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

- The namespace identity round passed all 139 CSS/DOM integration tests
  and 3,857 static smoke fixtures. The native dynamic suite passed 124 of
  125 tests, with the existing export image failure (RI-141).
- The `DomPropExt` round passed 124 of 125 native dynamic tests, all 39
  StateStore tests, and all 3,857 static smoke fixtures. The new test
  exercises a script-created form directory before first layout and its
  release and retirement. A separate compatibility fixture exercised all
  four slots and shared three directories across five live slot instances.
  The existing export image case (RI-141) was the sole dynamic failure.
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
