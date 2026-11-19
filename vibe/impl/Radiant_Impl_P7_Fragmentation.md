# P7 qualification and advanced page fragmentation

**Status:** topology-changing remaining-item reflow, split-item width replay and
vertical principal-flow fragmentation are implemented on macOS, 2026-11-12.
All 126 focused cases pass with AddressSanitizer, required cast lint passes, and
all 33 independent preview/PDF/Poppler controls pass on debug and optimized hosts.
Owner aggregate and frozen Lambda integration checks are complete for these
finite cores. Broader P7 remains partial; RI-050 and the known integration
failures remain open.
**Authority:** D9.1.1v4 / D9.1.3v2 (native ownership), D5.5.1 / D5.1.4
(published editions and retained resource lifetimes). Page layout follows
`../../../Lambda-opus/vibe/radiant/Radiant_Layout_Paged_Media.md` §1.3 and §3.2;
`../../../Lambda-opus/vibe/impl/Radiant_Impl_Paged_Integration.md` §7 retains the
historical P7 measurements. No formal language/design ruling changed.

## Active continuation milestone

User continuation, 2026-11-12: topology-changing remaining-item reflow, inline
width changes inside split items, and vertical-root page progression. Native
ownership remains D9.1.1v4 / D9.1.3v2; captured geometry and retained editions
follow D5.5.1 / D5.1.4. Preserve the prior uncommitted paging work, concurrent
native/Lambda paint work and the historical archive waiver. The user subsequently
requested a commit of the pagination paths and RI-050 evidence. The initial
source/control host is in
`temp/paged-continuations-start/`.

Implemented cores:

1. The normal flattened item collector accepts a continuation probe's committed
   prefix. Flex wrapping and grid auto-placement repack the remaining DOM items
   at the destination width. Existing committed rows keep their geometry and
   journal indexes; the remaining rows receive the new formatter grouping,
   positions and heights. Source-ordered generated counters remain intact.
2. The normal formatter measures destination track boxes; finished item paragraphs
   retain their line assignments and feed actual inline measures into bounded
   reference replay. Each continuation line wraps in its destination track, and
   fragment boxes/glyphs publish at that track's physical X position. Concurrent
   items fit independently, and the common row takes the maximum continuation.
   Existing flow capture/apply owns retained text; its scalar geometry query
   shares the same captured result rather than duplicating sizing rules.
3. Vertical principal modes use a logical block-coordinate policy over the
   existing fitter and physical page publisher. Normal layout retains committed
   line allocations before mapping glyph ink to physical rectangles, so widows
   and legal line breaks work in both directions. Right-anchored boxes grow left
   with compensating local child coordinates. Normal glyph runs keep their
   physical orientation; publication translates them into the selected sheet.
   Forced breaks, different physical sheet widths, ordinary orthogonal roots,
   continuous editions and retained-page budget failure have acceptance cases.

Finite scope: ordinary DOM-backed horizontal row items and ordinary vertical
principal-flow blocks/inline content. Nested formatting contexts inside width
remeasurement, anonymous-item topology, active spanning/track-topology changes
inside a split item, richer definite-size/alignment combinations, vertical
auxiliary/native producers, vertical multicolumn body regions and changing
vertical inline capacity remain wider profiles. Physical side/binding choices
continue to use `PagedLayoutOptions::first_side` / `right_binding`. Page and pass
budgets, protected references and previous retained editions are preserved.

Original failures are retained in `temp/paged-continuations-topology-before.xml`,
`temp/paged-continuations-split-before.xml`,
`temp/paged-continuations-vertical-before.xml` and
`temp/paged-continuations-neighbors.xml` (vertical forced boundaries).
`temp/paged-continuations-continuous-before.log` preserves the reproduced null-
physical-page crash. Its cause was a new vertical admission assumption; the
continuous edition correctly has no physical page. The final focused XML is
`temp/paged-continuations-final.xml` (126 passes), and required lint checks 89
casts with zero unmarked. `temp/paged-continuations-asan.xml` also has 126 passes;
`temp/paged-continuations-asan/receipt.json` freezes the instrumented binary,
archives, source hashes and `detect_leaks=0` option.

`temp/paged-continuations-debug-qualified/report.json` has 33 exact exported
controls. Ten new inputs pair with independently authored physical placements
and explicit text lines on forced sheets: flex/grid topology, flex/grid split
widths, vertical LR/RL boxes, vertical LR/RL text with widows, and LR/RL forced
boundaries. All ten reject the previous frozen host, recorded in
`temp/paged-continuations-negative/summary.json`. Existing controls and tolerances
are unchanged. The collector's supporting inventory now includes the shared
flattened item collector as well as the flow capture helper/header.

The optimized collector also passes all 33 controls in
`temp/paged-continuations-release-qualified/report.json`. The first optimized
attempt omitted `LAMBDA_HOME` and failed to load the PDF package; its receipt is
retained in `temp/paged-continuations-qualified/`. The configured rerun passed
without a source change.

`temp/paged-continuations-causal-control/receipt.json` verifies that only the
start versions of `layout_paged.cpp.o` and `layout_containing_block.cpp.o` replace
current release archive members. All other object members, the client and link
inputs are identical. Four alternating pairs, seven samples after two warmups
per invocation, give 28 samples per host/workload. The unchanged 21/300-page
input hashes and geometry/glyph digests agree exactly. Median ms/page before /
after: 0.975667 / 0.940286 (ratio 0.963737) and 0.846538 / 0.793257 (0.937059).
This increment adds no measured ordinary-flow slowdown in that run; it does not
repair or reclassify the older RI-050 regression. Raw samples and commands stay
beside `report.json`. No historical speedup or source-reduction claim is made.

Final owner evidence: `temp/paged-continuations-owner-native-results/` has
52 XML files, 1,062 registered tests, 1,060 executed passes, zero failures/errors
and two disabled paint throughput cases. The disabled cases belong to concurrent
paint work; this continuation changes no test admission. The owner baseline log
`temp/paged-continuations-owner-baseline.log` records 3,851 static plus 630
scripted layout passes, six static skips and zero layout failures. Required lint
checks 89 casts with zero unmarked in `temp/paged-continuations-final-lint.log`.

Standalone native archive closure also passes. The first attempt observed a
96-byte generated debug archive containing only its symbol index; the empty
artifact and failed log are retained. Rebuilding that artifact through the native
Makefile passes in `temp/paged-continuations-native-closure-retry.log`, without a
source change. Its original producer is unconfirmed. The successful archive is
frozen as `temp/paged-continuations-debug-owner.a`.

Lambda-opus `temp/paged-continuations-integration/summary.json` combines all
final receipts, source hashes, private build configuration and frozen binaries.
The paging suite has 504/505 passes; the selected PDF/FO/native subset has 84/90.
Every failure name and exact message equals the previous track checkpoint:
RI-052, RI-051 and Lambda-owned LR12-50 remain open. The original and private
PDF baseline SHA-256 remains
`ced461ca27c7a5455b8b450f8a87b71f02e5d51555d3d2cb94ce5919f6880d64`.
The selected 90-case subset is not a new full 472-case qualification.

Shared generated Lambda Makefiles were concurrently selecting a separate paint
checkout and losing the split host's provider patch. The final integration uses
private generated-Makefile snapshots, the frozen native debug archive and direct
private executable targets. PDF driver normalization changes only the owner path
prefixes, with its original source hash and exact reverse check retained in the
Makefile receipt; flags, test inventory and libraries are unchanged. All final
native source hashes match the focused, ASan and optimized receipts. No Lambda
source, protected reference, concurrent paint source or test tolerance changed.
The subsequent commit request covers pagination paths and RI-050 evidence only.

The normal formatter and the existing common fitter remain authoritative, in
accordance with the paged design's section 1.3. Standards context: Flexbox section
10 and Grid section 12 permit implementation choices in exact fragmented layout;
Writing Modes 3 sections 7.3 and 8 define orthogonal and principal flows.
https://www.w3.org/TR/css-flexbox-1/#pagination ;
https://www.w3.org/TR/css-grid-1/#fragmentation ;
https://www.w3.org/TR/css-writing-modes-3/#principal-flow .
No formal language/design ruling changed.

## Previous track and physical-axis milestone

The user's next continuation requests variable-width track reflow, orthogonal
layouts and wrapped column-flex continuation. These remain native Radiant work
under D9.1.1v4 / D9.1.3v2; retained geometry and edition payloads follow D5.5.1 /
D5.1.4. The source/executable checkpoint in `temp/paged-tracks-start/` pins the
previous qualified reservation implementation. The worktree's HEAD advanced to
`328dc16` during this task; preserve concurrent paint implementation and ledger
changes, and do not stage or commit them with pagination.

Implemented cores:

1. An auto-width horizontal flex/grid row can resolve its tracks again at the
   destination page's width when its DOM item membership stays stable. The normal
   formatter lays out once per distinct destination width in a fit; existing
   `layout_flow_result_capture/apply` owns text, used geometry and item allocation.
   Other rows restore their committed geometry. Selected rows restore the new
   allocation, rewrap real text, grow/shrink their row and refit on the selected
   page. Counters and generated glyphs retain their source-order values. Fixed
   absolute containers keep their measure. Different row membership is diagnosed
   before publication rather than pretending the old tracks still fit.
2. Flex captures physical-Y units from its resolved axes, including vertical
   writing modes. Grid captures whichever logical track family maps to physical
   Y (columns in a vertical grid), preserving spans through the common connected
   range grouping. Ordinary orthogonal flow roots remain indivisible in physical
   Y so pagination never inserts a break inside their vertical inline line boxes.
3. Each wrapped physical-Y flex line has an independent row/strut journal. A
   forced break affects its own lane, reversed directions retain resolved physical
   order, and the common container/following flow grows by the largest lane
   continuation. Page/pass limits and retained edition pixels remain checked.

Acceptance: 104 focused cases pass, including 120 alternating-width sheets per
formatter with complete authored byte coverage and source-ordered generated
counter glyphs. Wrapped first/last source-order item breaks propagate to the
container/following flow, while internal lane breaks remain independent. Required
layout cast lint: 89 casts, zero unmarked. Seven new independent placement controls
all reject the previous frozen host. All 23 current controls pass complete decoded preview pixels, every PDF sheet geometry,
Poppler raster bytes and literal color witnesses. Mixed-size preview witnesses
include the existing centering offset of narrow sheets; controls and full image
comparisons are unchanged by that coordinate correction.

Evidence so far: `temp/paged-tracks-before.xml` retains the four original axis/
lane failures; `temp/paged-tracks-first.xml` also retains the two original width
failures. `temp/paged-tracks-final2.xml` has the first 102 passes;
`temp/paged-tracks-boundaries-before.xml` reproduces the two endpoint propagation
gaps before their repair. `temp/paged-tracks-asan.xml` has all 104 passes with
address instrumentation, `detect_leaks=0`; the frozen executable/archive/source
hashes are in `temp/paged-tracks-asan/receipt.json`. The new collector inventory
includes `layout_pass.cpp`. `temp/paged-tracks-qualified/report.json` has all 23
passes on the optimized host; `temp/paged-tracks-negative/summary.json` retains
seven negative controls. Initial
collector failures are preserved under `temp/paged-tracks-exports/` (non-integer
witness) and `temp/paged-tracks-new-exports/` (preview-coordinate assumptions).

`temp/paged-tracks-causal-control/` replaces only the pagination object with
`temp/paged-tracks-start/layout_paged.cpp`; every other current release archive
object (including the shared flow helper) is byte-identical. Four alternating
pairs give 28 samples per host/workload. Before/after median ms/page is
0.954405/0.947262 for 21 pages, and 0.819257/0.820018 for 300 pages. Published
geometry/glyph digests are identical. This increment is within about 1%; the
separate earlier RI-050 regression is not closed by this result. Historical
archive and platform limits remain unchanged. Source accounting: 9,613 raw lines
across the four historical paths versus 9,403 at current HEAD (+210); the shared
helper/header/hooks are inventoried separately. No reduction or old speedup
claim is made.

Final normal owner evidence: `temp/paged-tracks-final-native-results/` has 52
XML files, 1,035 registered cases, zero failures/errors and one disabled case;
`temp/paged-tracks-final-owner-baseline.log` records 3,851 static and 630 scripted
layout passes with six static skips. `temp/paged-tracks-final-lint.log` records
89 casts and zero unmarked. The normal profile was restored after sanitizer
qualification; shared dependency outputs require sequential profile builds.

Lambda-opus `temp/paged-tracks-integration/summary.json` combines the final
frozen executables, native source/archive hashes, owner/ASan/export/performance
receipts and integration XML. The 505-case paging run has the same RI-052 failure;
the 90-case PDF/FO/native subset has the same six RI-051/LR12-50 failures. Exact
messages match `temp/paged-reservations-integration-final/`, and the original
and private PDF baseline SHA-256 stays
`ced461ca27c7a5455b8b450f8a87b71f02e5d51555d3d2cb94ce5919f6880d64`.
The subset is not a new full 472-case run. No Lambda source changes, staging or
commit were performed for this continuation. Concurrent paint files and RI-055
ledger edits remain untouched.

At that checkpoint, changing auto-placement/wrap topology, inline width changes
inside one already split item and vertical-root page progression were separate
extensions; the active milestone above now supplies their finite cores. Nested
formatters inside remeasured rows and combining lane continuation with unequal
widths or richer balancing remain separate profiles. CSS Writing Modes
3 section 7.3 distinguishes orthogonal sizing and positioning and section 7.3.3
retains line-box indivisibility:
https://www.w3.org/TR/css-writing-modes-3/#orthogonal-flows . Flexbox section 10
and Grid section 12 define break propagation and admit implementation choices
for the exact fragmented layout; their sample algorithms are informative:
https://www.w3.org/TR/css-flexbox-1/#pagination ;
https://www.w3.org/TR/css-grid-1/#fragmentation . No formal ruling changed.

## Previous reservation checkpoint

- Native workspace: `/Users/henryluo/Projects/radiant`, starting from `ead8d2a`
  with a clean worktree. The user authorized the three continuation cores below.
  Native sources, tests, fixtures and documentation stay in this owner checkout.
  No Lambda or input source changes are required; preserve unrelated sheet work.
- Implementation: reservation-aware final-column balancing, bounded backward-float
  reservation replay, oversized/nested row-content fitting, single-column flex
  in either direction, and trimming gaps at exact fragment boundaries.
- Final focused gate: 82 cases pass, including batched discovery of 20 backward
  figures within the default pass budget. Layout integer-cast lint checks 89
  casts with zero unmarked; whitespace checks pass. Original failures are retained.
- Final aggregate: 1,010 native passes and one disabled case; 4,481 layout passes
  and six skips. The explicit profile gate leaves parallel wrapped column-flex
  lines on their previous whole-context path.
- All 16 independent preview/PDF/Poppler controls pass exact pixels, literal
  color witnesses and every physical page's geometry. Preserve the first failed
  control run under `temp/paged-reservations-exports/`; its one-pixel text artifact
  was removed from the new authored input by making the real blue anchor line
  match its blue body box. No engine tolerance or existing reference changed.
- Final Lambda integration: 504/505 pass; selected PDF/FO/native export inventory
  is 84/90. Exact failure messages match earlier controls, and both original and
  private PDF baseline hashes remain unchanged. These are final-source frozen
  builds; the PDF subset does not replace the full 472-case inventory.
- Performance limitation: the held-object optimized comparison attributes about
  6% higher ordinary-flow composition time to the pagination object (RI-050).
  Correctness digests match; no performance no-regression claim is made.
- All new core gates must preserve source intervals, page/pass budgets, exact
  preview/PDF pixels, physical sheet geometry and previously published editions.
  No protected references or tolerances are changed. No staging/commit requested
  for this continuation.
- Historical archive waiver (user, 2026-11-12): assume the original P7 baseline
  archive was deleted/lost and ignore that comparison. RI-053 is closed by waiver,
  not by passing old visual, performance or source-reduction measurements.

## Delivered reservation and nested-formatting cores

1. Final equal body columns can balance with column top/bottom floats and notes.
   The search includes actual reserved extents and per-column indivisible floors;
   monolithic-box retry thresholds include their auxiliary capacity demand.
   Line-less insertion still binds to its next committed box, so concurrent
   reservation acceptance uses an actual line rather than assuming an empty
   box commits the figure's anchor. Text coverage and retained pixels are checked.
2. End-relative page figures can precede their committed body anchor in the
   existing one-column, one-sequence profile. Stable source/sequence identities
   retain reservation plans across bounded reference replay. Fragmentainers
   install those slots before earlier body fitting; later actual anchors bind
   once without double-counting or following moved body boxes. Destination
   sizing, top/bottom placement, source ordering, body text refit and failed
   page/pass admission have native acceptance cases.
3. Oversized flex/grid row items fit their finished ordinary block and inline
   content through the existing line/keep fitter. Nested resolved formatting
   rows use the same machinery. Each concurrent item fits independently; the
   row grows by the largest continuation and advances later rows/following flow.
   Real text remains DOM-backed after formatter scratch retirement. Tests cover
   legal block edges, widows, exact authored intervals, concurrent peers and
   byte-identical retained page pixels after failed admission.
4. Single-column flex items use their resolved physical block order, including
   `column-reverse`. Horizontal flex/grid gaps crossing a fragment boundary are
   discarded before testing the next row, including exact full-row boundaries;
   gap trimming participates in the existing strut undo journal.

These are finite native profiles. They do not assert complete CSS fragmentation
conformance. Supporting standards: CSS Multicol §7.1
https://www.w3.org/TR/css-multicol-1/#cf ; CSS Page Floats §5.1 (a working draft)
https://www.w3.org/TR/css-page-floats-3/#float-defer-property ; CSS Flexbox §10
https://www.w3.org/TR/css-flexbox-1/#pagination ; CSS Grid §12
https://www.w3.org/TR/css-grid-1/#fragmentation . Internal native ownership and
retained-resource decisions remain governed by the formal D# rulings above.

## Previous reservation qualification receipts

- `temp/paged-reservations-before.xml` preserves the four original auxiliary
  rejection cases; `temp/paged-backward-before.xml` preserves late-anchor
  rejection; `temp/paged-nested-before.xml` preserves five complex-row failures.
  `temp/paged-nested-neighbors.xml` caught the exact-boundary gap regression.
  `temp/paged-backward-batch-before.xml` reproduces per-figure pass exhaustion.
- `temp/paged-reservations-final.xml`: 82 focused passes.
  `temp/paged-reservations-lint.log`: required integer-cast check.
- `temp/paged-reservations-native-results/` and
  `temp/paged-reservations-owner-baseline.log`: first owner aggregate run.
  Final post-batching aggregate outputs are
  `temp/paged-reservations-batch-native-results/` (52 XML files, 1,011 registered,
  1,010 executed passes, one disabled) and
  `temp/paged-reservations-batch-owner-baseline.log`.
- `test/fixtures/paged/cases.json` adds six independent controls, total sixteen:
  auxiliary column balance, backward top/bottom figures, nested flex/grid and
  column flex. Controls are separately authored absolute rectangles on forced
  physical sheets; they contain no reservation, balance or row policy under test.
  Existing controls and protected external references remain unchanged.
- `temp/paged-reservations-negative/<case>/report.json`: all six new controls
  reject the previously qualified frozen host from `temp/paged-advanced-final/`.
  Earlier hosts reject auxiliary/backward policy explicitly, differ on nested
  preview/PDF pixels, and omit the third column-flex sheet witness. These are
  negative controls, not six completed successful old-engine exports.
- `temp/paged-reservations-final/report.json`: final frozen host, all sixteen
  complete exported controls, audited/supporting source hashes and working patch.
  Its optimized composition client records seven samples after two warmups per
  unchanged 21/300-page ordinary-flow workload, medians 0.968/0.829 ms/page.
  Published geometry/glyph digests match the previous qualified host. Debug
  clients refuse timing; parsing, export, digest and retirement stay outside the
  timer. `temp/paged-reservations-final-release-build.log` records the build.
  Interleaved archived-host comparison is under `temp/paged-reservations-paired/`;
  no historical P7 speedup or source-reduction target is claimed. The stronger
  `temp/paged-reservations-causal-control/` replaces only pagination source from
  `ead8d2a`, verifies every other archive object is identical, and alternates
  28 samples per host/workload. Median ms/page is 0.901476/0.955524 before/after
  for 21 pages and 0.781150/0.827102 for 300 pages, about 6% slower. Exact published
  digests agree. RI-050 retains this open performance qualification issue.
- The four historical source paths total 9,403 raw lines versus 9,250 at
  `ead8d2a` (+153). Supporting sources remain independently inventoried; retired
  engine selectors/types are absent. This is source accounting, not a reduction.
- Lambda-opus `temp/paged-reservations-integration-final/` retains pinned final
  executables, native source hash, private PDF baseline, XML/logs and combined
  `summary.json`. Its final results remain 504/505 paging and 84/90 selected PDF;
  RI-052/RI-051/LR12-50 remain open. The earlier initial-source run is preserved
  separately in `temp/paged-reservations-integration/`.

## Earlier completed increments

The original increments implemented positive page-float offsets and nested
auto-width named-page changes (`ad33423`), followed by equal-column balancing,
horizontal flex/grid row units, fixed-width named-page continuation and negative/
`last` float deferral (`ead8d2a`). Auto/fixed ancestors retain their existing
width behavior and definite sizing references. Positive out-of-range clamping
retains its earlier partial behavior; negative out-of-range offsets fall back to
the committed anchor. First-page/last-page sizing and budgets are not waived.

`temp/paged-advanced-final/` retains 60 focused and ten exact exported controls;
`temp/paged-advanced-native-results/` retains 988 native passes plus one disabled
case; the layout gate had 4,481 passes plus six skips. Its optimized paired
21/300-page ordinary-flow workloads had exact geometry/glyph digests and observed
changes within about 1%; these are earlier receipts, not current measurements.
The four historical source paths totaled 9,250 raw lines at that point, with
supporting headers/hooks inventoried separately. No size-reduction claim was made.

Lambda-opus `temp/paged-p7-integration/` retains the full 472-case PDF inventory:
466 pass, six fail. The raw run stopped after 468 cases at hidden WebGL context
creation; all four GPU cases passed separately with normal macOS access. Each
exact failure was independently reproduced with pagination source `340ad59` and
otherwise identical linked objects/flags. The original raw run was preserved.
`temp/paged-advanced-integration/` retains the subsequent 504/505 paged run and
84/90 selected export run. RI-052/RI-051 and Lambda-owned LR12-50 stay open.

## Remaining profiles and resume point

1. Unequal explicit column rectangles, page-wide reservations across multiple
   body columns and more sequence/terminal-master combinations. Current balance
   support uses equal native columns and their own auxiliary slots.
2. Nested formatters inside width remeasurement, anonymous-item topology,
   topology/spanning changes inside an active split item, richer definite-size
   and alignment combinations, vertical auxiliary/multicolumn producers,
   changing vertical inline capacity and complete nested forced-break propagation.
   The topology, split-width and vertical-principal cores extend the earlier
   shared fitter; they do not close every combination.
3. Column-attached/multi-sequence deferred-float scheduling and named-page width
   transitions through percentage-containing or independent formatting contexts.
   Backward reservation convergence remains bounded by the caller's pass budget.
4. Owner-side root-cause resolution of RI-052/RI-051, the measured RI-050
   composition slowdown, and Lambda-owned LR12-50, then wider P7 qualification.
   Keep their reference/assertion evidence intact until the owning cause is repaired.
5. Platform qualification beyond this macOS host. Fresh release receipts do not
   replace the lost historical archive or establish its old targets.

The current finite continuation milestone is qualified on macOS. Resume from the
remaining profiles above; do not repeat completed gates unless the source changes.
Keep the earlier RI-050 regression, known integration failures and wider platform
gates open until their own causes and evidence are resolved. Preserve known red
integration profiles. Keep performance, historical and platform claims separate
from focused native delivery.
