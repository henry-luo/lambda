# Radiant Issue Ledger

Working issue record for Radiant's native layout, rendering, event-host and
state-store code under D9.1.1v4 and D9.1.3v2. This ledger records issues and
evidence; design rulings remain in the formal specifications.

Use stable IDs `RI-001`, `RI-002`, and so on. For each new issue record its
status, affected code or test, reproduction, observed and expected behavior,
and whether it blocks work in Lambda. Cite the applicable `S#` or `D#` ruling
when one covers the issue. Record uncertain causes as unconfirmed, link any
owner-side fix and verification. Move resolved entries to the
[fixed issue ledger](<Radiant_Issue_Ledger (fixed).md>), preserving their IDs
and leaving an anchor link here for existing references.

The [historical Lambda-hosted Radiant ledger](radiant/Radiant_Issue_Ledger.md)
retains existing `RAD` IDs and mixed issue history.
Native Radiant entries have been moved below with their original IDs; mixed,
Lambda-owned, input-owned and resolved records remain there. The imported
observations still require re-verification. Add new Radiant issues here.

## Open issues

### RI-141 — Native export image fixture misses its preloaded cache entry

Open, 2026-11-20. In the standalone native release suite,
`DynamicLayoutTest.ExportQueuesOrdinaryNetworkImagesBeforeInitialLayout`
stores an SVG for `https://example.test/radiant-export-ready.svg`. Export
still attempts DNS and produces fallback image geometry. The same failure
occurs when the test runs alone with an isolated cache. The cache-read
cause is unconfirmed. The other 119 tests in the cache and dynamic-layout
run passed. This blocks a clean dynamic-layout gate for the property group
refactor; it did not block static parity or memory measurement.
Evidence is in `temp/view-prop-groups/layout-dynamic-isolated.log`.
Resource acquisition follows D9.1.2v2.

### RI-140 — Image button paint and dialog pointer selection

Partial. The image-button layout loads `src` through the shared replaced
image path, and raster form dispatch now paints it through the same content
renderer used by images. Flat SVG and PDF also paint the loaded source. The
native replay checks the painted pixel, click-local result, focus after close
and cleared pixel. Natural-size, broken-image fallback and other image input
geometry remain unqualified (**D9.1.1v4 / D9.1.3v2**).

### RI-139 — Dynamic body zoom loses the first list's collapsed block-start margin

Open. `wpt-css-lists/list-style-image-zoom-dynamic.html` doubles the body zoom
after a geometry read. Radiant scales the list and marker content, but places
the body and list at 16 px instead of 32 px. Root height comes out 32 px short.
The case reproduces in isolation at `6f38618` and after the named-slot layout
repair. The fixture has no shadow tree. It remains a failure in the CSS Lists
baseline (**D9.1.1v4 / D9.1.3v2**).

<a id="ri-138"></a>

### RI-138 — FIXED

Trial layout no longer commits iframe viewport sizing or clamps retained
scroll offsets (**D5.5.1**). See the
[EPUB page-turn reproduction](<Radiant_Issue_Ledger (fixed).md#ri-138>).

<a id="ri-137"></a>

### RI-137 — FIXED

Inline flex containers export baselines after their child content is laid
out (**D5.5.1**). See the
[baseline-source reproduction](<Radiant_Issue_Ledger (fixed).md#ri-137>).

<a id="ri-136"></a>

### RI-136 — FIXED

Flex items flattened through boxless DOM wrappers use the real flex box's
padding/border offsets. Affinity's navigation is readable and aligned; its
separate artwork startup omission remains. See the
[boxless flex positioning repair](<Radiant_Issue_Ledger (fixed).md#ri-136>).

<a id="ri-135"></a>

### RI-135 — FIXED

Retained descendants refresh inherited text shadows from the parent's computed
list; explicit inherit/unset share the same owned-copy path. Boldare's complete
outlined headline returns. See the
[computed-shadow inheritance repair](<Radiant_Issue_Ledger (fixed).md#ri-135>).

<a id="ri-134"></a>

### RI-134 — FIXED

Video snapshots now ask the existing decoder for an available first frame while
the asynchronous player is opening, retaining poster precedence and clocks.
Mymind's large halo is restored. See the
[first-frame readiness repair](<Radiant_Issue_Ledger (fixed).md#ri-134>).

<a id="ri-133"></a>

### RI-133 — FIXED

Image signature detection skips bounded leading XML comments before inspecting
SVG markup. Ordinary and XML-declaration controls retain their behavior.
See the [SVG prefix repair](<Radiant_Issue_Ledger (fixed).md#ri-133>).

<a id="ri-132"></a>

### RI-132 — FIXED

Balance and intrinsic-axis probes retain their already selected content width
instead of resolving authored width/min/max constraints again. Three milestone
regressions return to their unchanged 100% references. See the
[assigned-width contract repair](<Radiant_Issue_Ledger (fixed).md#ri-132>).

<a id="ri-131"></a>

### RI-131 — FIXED

Text-balancing line-count probes no longer start balancing the same paragraph
again through the atomic inline-flow height fallback. Other paragraph owners
retain independent balancing. See the [measurement recursion repair](<Radiant_Issue_Ledger (fixed).md#ri-131>).

<a id="ri-130"></a>

### RI-130 — FIXED

Text-clipped backgrounds reach nested block/inline descendants through the
shared painter, retaining owner coordinates and ancestor paint order. Wiza's
saved heading and copy return; its separate live startup omission stays high.
See the [ancestor text-background repair](<Radiant_Issue_Ledger (fixed).md#ri-130>).

<a id="ri-129"></a>

### RI-129 — FIXED

Intrinsic inline-flow trials retain the measured box's resolved width and
ratio-derived percentage-height basis. Percentage images no longer inflate
Clay's 95px logo strip to 1494.8px. See the
[containing-size repair](<Radiant_Issue_Ledger (fixed).md#ri-129>).

<a id="ri-128"></a>

### RI-128 — FIXED

Animation background shorthands now feed the existing background-color track,
retaining the shorthand grammar through variable substitution. Other background
components remain outside this repair. See the
[shorthand repair](<Radiant_Issue_Ledger (fixed).md#ri-128>).

<a id="ri-127"></a>

### RI-127 — FIXED

Exports honor the existing host-selected post-load window, including script-free
CSS frame preparation. Unspecified exports retain their initial frame. See the
[selected-frame repair](<Radiant_Issue_Ledger (fixed).md#ri-127>).

<a id="ri-126"></a>

### RI-126 — FIXED

Intrinsic inline height now formats atomic boxes during its trial and preserves
live frame scrolling. See the [trial-flow repair](<Radiant_Issue_Ledger (fixed).md#ri-126>).

<a id="ri-125"></a>

### RI-125 — FIXED

The positioned-child pass allocates its flattened list through its
own scratch scope (**D5.1.4 / D5.5.1**). See the
[scratch ownership repair](<Radiant_Issue_Ledger (fixed).md#ri-125>).

<a id="ri-124"></a>

### RI-124 — FIXED

Atomic inline fit tests now reuse the float edge comparator's scale-relative
roundoff handling. Python's header no longer adds a line for a 0.00003px edge
difference. Real overflowing boxes still wrap. See the
[fractional edge repair](<Radiant_Issue_Ledger (fixed).md#ri-124>).

<a id="ri-123"></a>

### RI-123 — FIXED

Intrinsic inline height now retains the parent's line strut when atomic inline
boxes contain block descendants or positioned decorations. The existing inline
formatter supplies baseline and line-box geometry instead of a child-height
maximum. See the [line-height repair](<Radiant_Issue_Ledger (fixed).md#ri-123>).

<a id="ri-122"></a>

### RI-122 — Named view-progress animation timelines are unsupported

**Status:** OPEN major feature gap, 2026-11-17. Blocks Nolla's visual parity;
independent native repairs can continue. Ownership follows **D9.1.1v4 /
D9.1.3v2**; initial capture clocks remain unchanged (**D10.1.7**).

Nolla's script-free hero and following panels declare a named view timeline,
animation-timeline and a containment/exit animation range. The style registry
and native animation subsystem do not implement those properties. The ordinary
animation path samples the parallax end values: the hero wrapper is at opacity
zero and translated 156.244px while its heading and other geometry exist.
Chromium's range-bound animation leaves the hero visible at initial scroll.
This explains the major blank main region on the same HTTP document
(29.85% mean error). See
[Scroll-driven Animations §3–§4](https://www.w3.org/TR/scroll-animations-1/#view-timelines).

The earlier comparison was unqualified because its temporary browser driver
assigned an absolute numeric currentTime to a progress-based timeline. The
driver now pauses progress timelines at their natural initial scroll position
and assigns numeric zero only to numeric times; the comparison completes.
No canonical capture clock, scroll position, style or reference is changed.
Named timeline lookup, progress/range calculation and sampling require a new
subsystem, outside this round's bug-fix scope. No fade suppression or native
timing exception is added.

Evidence in Lambda: temp/online-visual-round77/nolla-comparison.json,
the exact-source native/browser PNGs and nolla-probe-lambda-output.log.
The page remains high severity.

<a id="ri-121"></a>

### RI-121 — FIXED

Snapshot resource discovery now prepares eligible media before the existing
wait and first layout. Local HTML can initialize its manager from a remote
media source. The existing preload/autoplay/audio policy remains shared.
See the [resource-ordering repair](<Radiant_Issue_Ledger (fixed).md#ri-121>).

<a id="ri-120"></a>

### RI-120 — FIXED

SVG natural dimensions now require a successfully parsed length. An auto or
nonnumeric height no longer invents a definite 150px natural axis and compresses
Wispr's percentage-width SVGs.
See the [SVG metadata repair](<Radiant_Issue_Ledger (fixed).md#ri-120>).

<a id="ri-119"></a>

### RI-119 — FIXED

Intrinsic height classification now excludes positioned decorations and
recognizes inline content in independent flow roots. Overpass's saved hero
no longer stacks its letters' height contributions.
See the [inline-height repair](<Radiant_Issue_Ledger (fixed).md#ri-119>).

<a id="ri-118"></a>

### RI-118 — FIXED

Content-only intrinsic measurements now make descendant percentage widths
indefinite even when the flex item has a preferred width. Orum's saved hero
columns recover; its independent live blank capture remains unresolved.
See the [intrinsic-width repair](<Radiant_Issue_Ledger (fixed).md#ri-118>).

<a id="ri-117"></a>

### RI-117 — Chromium ignores Readymag's missing SVG mask reference

**Status:** OPEN interoperability discrepancy, 2026-11-17; no native defect
confirmed. **Lambda blocker:** no. Preserve the existing native rule rather
than introducing a browser-matching exception. Ownership follows
**D9.1.1v4 / D9.1.3v2**; effects ownership **D5.5.1**.

Readymag's saved white card has a path mask URL naming an ID absent from the
entire document (zero mask elements). Four isolated controls retain the real
SVG and vary only that attribute and the CSS drop-shadow. Native output is
transparent with the missing mask, and white without it, independently of
the drop-shadow. Chromium paints white even with the missing reference.

[CSS Masking Level 1 §7.1](https://www.w3.org/TR/css-masking-1/#the-mask-image)
specifies a transparent-black layer for a nonexistent mask reference and
applies to SVG graphics elements. The existing native effect path implements
that rule intentionally. This is not evidence to suppress the mask or render
a replacement white shape. No renderer change or reference adjustment is
made. It explains this saved-source card discrepancy only; Readymag's blank
live startup remains independently unresolved.

Evidence: Lambda `temp/online-visual-round69/card-results.json`, the four
minimal documents and native PNGs, and `operate-static-browser.png`.
The existing qualified round68 renderer is unchanged. No additional baseline
is needed for this read-only diagnosis.

<a id="ri-116"></a>

### RI-116 — FIXED

Lifted layers now replay intermediate positioned ancestors in their containing
block's coordinate basis. Readymag's live startup remains unresolved.
See the [paint-coordinate repair](<Radiant_Issue_Ledger (fixed).md#ri-116>).

<a id="ri-115"></a>

### RI-115 — FIXED

Flex height restoration now visits the same flattened items that supplied
the snapshot. Superlist's app image returns and leaves high severity.
See the [height-restoration repair](<Radiant_Issue_Ledger (fixed).md#ri-115>).

<a id="ri-114"></a>

### RI-114 — FIXED

The shared positioned-child layout pass now flattens contents wrappers just
like flex/grid item collection. Superlist's separate app-image ratio-box loss
remains unresolved. See the [positioned-box repair](<Radiant_Issue_Ledger (fixed).md#ri-114>).

<a id="ri-113"></a>

### RI-113 — FIXED

Preferred aspect ratios now evaluate dimensionless CSS math rather than
collapsing calculated-ratio boxes. Remote's independent live startup remains
unresolved. See the [ratio repair](<Radiant_Issue_Ledger (fixed).md#ri-113>).

<a id="ri-112"></a>

### RI-112 — FIXED

Positioned document roots now enter raster painting directly rather than
being deferred to a nonexistent parent stacking pass. Dayos's independent
live startup overlay remains unresolved.
See the [root-paint repair](<Radiant_Issue_Ledger (fixed).md#ri-112>).

<a id="ri-111"></a>

### RI-111 — FIXED

Canonical inline-property sharing no longer erases isolation, ruby position or explicit line-strut state.
See the [identity repair](<Radiant_Issue_Ledger (fixed).md#ri-111>).

<a id="ri-109"></a>

### RI-109 — FIXED

See the [qualified repair](<Radiant_Issue_Ledger (fixed).md#ri-109>).

<a id="ri-110"></a>

### RI-110 — FIXED

See the [qualified repair](<Radiant_Issue_Ledger (fixed).md#ri-110>).


<a id="ri-108"></a>

### RI-108 — FIXED

Final grid percentage heights lost definiteness; intrinsic image ratios reused old percentage constraints.
See the [grid and retained image-sizing repair](<Radiant_Issue_Ledger (fixed).md#ri-108>).

<a id="ri-107"></a>

### RI-107 — FIXED

Reuse initially parsed stylesheets when delivering prefetched link completion.
See the [completion repair](<Radiant_Issue_Ledger (fixed).md#ri-107>).

<a id="ri-106"></a>

### RI-106 — FIXED

Lifted positioned children painted over their ancestor's exported outline.
See the [shared walker repair](<Radiant_Issue_Ledger (fixed).md#ri-106>).

<a id="ri-105"></a>

### RI-105 — FIXED

CSS Shapes WPT coverage reached 294 passing cases. See the
[repair and scoped reference approvals](<Radiant_Issue_Ledger (fixed).md#ri-105>).

<a id="ri-104"></a>

### RI-104 — FIXED

Intrinsic margins resolved custom properties and font units on the measuring parent, collapsing a hero grid after NaN propagation.
See the [owner-context repair](<Radiant_Issue_Ledger (fixed).md#ri-104>).

<a id="ri-103"></a>

### RI-103 — FIXED

Final flex content shrank a grid-assigned height before positioned percentages resolved.
See the [assigned-height repair](<Radiant_Issue_Ledger (fixed).md#ri-103>).

<a id="ri-102"></a>

### RI-102 — FIXED

Downloaded stylesheets never completed their DOM load/error events.
See the [stylesheet completion repair](<Radiant_Issue_Ledger (fixed).md#ri-102>).

<a id="ri-101"></a>

### RI-101 — FIXED

Unregistered custom-layout names disabled ordered generated content globally.
See the [registered-callback preparation repair](<Radiant_Issue_Ledger (fixed).md#ri-101>).

<a id="ri-100"></a>

### RI-100 — FIXED

Inline buttons exposed internal label spaces to outer line-fill lookahead.
See the [atomic-context repair](<Radiant_Issue_Ledger (fixed).md#ri-100>).

<a id="ri-099"></a>

### RI-099 — FIXED

Oversized inline background radii generated inverted paths outside the fragment.
See the [shared constrained-path repair](<Radiant_Issue_Ledger (fixed).md#ri-099>).

<a id="ri-098"></a>

### RI-098 — FIXED

Oversized rounded inset radii discarded glyphs and native hits.
See the [shared used-radius repair](<Radiant_Issue_Ledger (fixed).md#ri-098>).

<a id="ri-097"></a>

### RI-097 — FIXED

Nested zero-level foreground painted below a lifted absolute background.
See the [context-owned layer repair](<Radiant_Issue_Ledger (fixed).md#ri-097>).

<a id="ri-096"></a>

### RI-096 — FIXED

Static flex/grid items ignored authored stack levels.
See the [stacking eligibility repair](<Radiant_Issue_Ledger (fixed).md#ri-096>).

<a id="ri-095"></a>

### RI-095 — FIXED

Flex-assigned grids lost their allocated height during content layout.
See the [assigned-context repair](<Radiant_Issue_Ledger (fixed).md#ri-095>).

<a id="ri-094"></a>

### RI-094 — FIXED

Individual translation discarded percentage calculations.
See the [affine-math repair](<Radiant_Issue_Ledger (fixed).md#ri-094>).

<a id="ri-093"></a>

### RI-093 — FIXED

Assigned grid/flex float contexts misplaced or omitted float exclusions.
See the [repair and independent browser evidence](<Radiant_Issue_Ledger (fixed).md#ri-093>).

<a id="ri-092"></a>

### RI-092 — FIXED

Intrinsic column measurement omitted definite flex bases, with two later size handoff errors.
See the [basis-contribution repair](<Radiant_Issue_Ledger (fixed).md#ri-092>).

<a id="ri-091"></a>

### RI-091 — FIXED

The isolated custom-layout and retained display-list test hosts now resolve
their service boundaries without importing conflicting production providers.
See the [link trace and aggregate qualification](<Radiant_Issue_Ledger (fixed).md#ri-091>).

<a id="ri-090"></a>

### RI-090 — FIXED

See the [fixed record](<Radiant_Issue_Ledger (fixed).md#ri-090>).


<a id="ri-089"></a>

### RI-089 — Min-content minimum widens floated intrinsic contribution boxes

**Status:** FIXED, 2026-11-14. The sizing baseline passes 776/776 and the full
Lambda Radiant baseline has zero failures. See the
[contribution repair](<Radiant_Issue_Ledger (fixed).md#ri-089>).

<a id="ri-088"></a>

### RI-088 — FIXED for selected-flow restoration

The three saved Mymind restoration failures are traced to derived pseudo
cascade refreshes invalidating authored ancestor inputs (Input LI-069).
Unregistered custom-layout markup also disabled ordered preparation (RI-101).
Both repairs are qualified; see the [follow-up record](<Radiant_Issue_Ledger (fixed).md#ri-088>).
The live page remains blank and is not cleared. Malformed CoreText-name
messages are a separate observation; an independently decoded Stellar font
contains those control bytes in its original name table, so those messages
alone do not establish an ownership defect.

<a id="ri-087"></a>

### RI-087 — Propagated body overflow clips its own zero-height box

**Status:** FIXED for shared viewport-source selection and body-local paint,
hit and coordinate scopes, 2026-11-14. See the
[viewport-source repair](<Radiant_Issue_Ledger (fixed).md#ri-087>).

<a id="ri-086"></a>

### RI-086 — Empty-child root collapse erases resolved minimum height

**Status:** FIXED for the final root sizing step, 2026-11-14. See the
[root sizing repair](<Radiant_Issue_Ledger (fixed).md#ri-086>).


<a id="ri-085"></a>

### RI-085 — Wide SVG Gaussian kernels exhaust the work budget

**Status:** FIXED for bounded linear window evaluation, 2026-11-14. See the
[wide Gaussian repair](<Radiant_Issue_Ledger (fixed).md#ri-085>).


<a id="ri-084"></a>

### RI-084 — Static effect contexts lose positioned descendants and empty-box backdrops

**Status:** FIXED for block paint ownership and deferred descendant compositing,
2026-11-14. See the [positioned opacity repair](<Radiant_Issue_Ledger (fixed).md#ri-084>).
No online page clearance is claimed from this repair.

<a id="ri-083"></a>

### RI-083 — Early flex intrinsic widths omit deferred scalar gaps

**Status:** FIXED for variable, calculated and font-relative scalar gaps,
2026-11-14. See the [intrinsic gap repair](<Radiant_Issue_Ledger (fixed).md#ri-083>).


<a id="ri-082"></a>

### RI-082 — Gaussian work accounting charges skipped flat-run convolutions

**Status:** FIXED for executed-work accounting and exact transparent runs,
2026-11-14. See the [Gaussian repair](<Radiant_Issue_Ledger (fixed).md#ri-082>).
Overflow's larger filter graph remains unresolved.


<a id="ri-081"></a>

### RI-081 — Wrapped flex height queries omit item cross margins

**Status:** FIXED for outer cross-size contributions, 2026-11-14. See the
[wrapped flex margin repair](<Radiant_Issue_Ledger (fixed).md#ri-081>).


<a id="ri-080"></a>

### RI-080 — Nested ratio height queries consume parent free space and stale SVG heights

**Status:** FIXED for preferred-width and definite SVG percentage-height queries,
2026-11-14. See the [focused intrinsic repair](<Radiant_Issue_Ledger (fixed).md#ri-080>).


<a id="ri-079"></a>

### RI-079 — Unmatched stylesheet icons exhaust resource discovery

**Status:** FIXED for selected-cascade discovery, 2026-11-14. See the
[bounded discovery repair](<Radiant_Issue_Ledger (fixed).md#ri-079>).


<a id="ri-078"></a>

### RI-078 — Absolute percentage math keeps a provisional containing height

**Status:** FIXED for late preferred-height resolution, 2026-11-14. See the
[focused sizing repair](<Radiant_Issue_Ledger (fixed).md#ri-078>).


<a id="ri-077"></a>

### RI-077 — SVG inherited reads repeat shared ancestor declaration queries

**Status:** FIXED for ancestor snapshot reuse, 2026-11-14. See the
[bounded repair evidence](<Radiant_Issue_Ledger (fixed).md#ri-077>).
Dock's independent whole-document capture deadline remains open as RI-031.



<a id="ri-076"></a>

### RI-076 — SVG percentage viewport attributes inflate intrinsic height

**Status:** FIXED, 2026-11-14. See the [focused repair and qualification](<Radiant_Issue_Ledger (fixed).md#ri-076>).



<a id="ri-075"></a>

### RI-075 — Canvas general matrix operations are unavailable to the DOM host

**Status:** FIXED, 2026-11-14. See the [bounded matrix-operation repair](<Radiant_Issue_Ledger (fixed).md#ri-075>).


<a id="ri-074"></a>

### RI-074 — A min-content minimum incorrectly caps the preferred width

**Status:** FIXED, 2026-11-14. See the [focused repair evidence](<Radiant_Issue_Ledger (fixed).md#ri-074>).


<a id="ri-073"></a>

### RI-073 — Cyclic percentage invalidation discards replaced media height

**Status:** FIXED, 2026-11-13. See the [repair and qualification](<Radiant_Issue_Ledger (fixed).md#ri-073>).


<a id="ri-072"></a>

### RI-072 — Atomic inline alignment loses enclosing relative Y offsets

**Status:** FIXED, 2026-11-13. See the [repair and qualification](<Radiant_Issue_Ledger (fixed).md#ri-072>).


<a id="ri-071"></a>

### RI-071 — Absolute and relative zero-level siblings paint in separate passes

**Status:** FIXED, 2026-11-13. See the [repair and qualification](<Radiant_Issue_Ledger (fixed).md#ri-071>).


<a id="ri-070"></a>

### RI-070 — Inherited percentage width retains the parent's used width

**Status:** FIXED, 2026-11-14. See the [computed-size repair](<Radiant_Issue_Ledger (fixed).md#ri-070>).


<a id="ri-069"></a>

### RI-069 — Height queries omit width expressions and constraints

**Status:** FIXED, 2026-11-13. See the [repair and qualification](<Radiant_Issue_Ledger (fixed).md#ri-069>).


<a id="ri-068"></a>

### RI-068 — Grid row contributions reuse provisional whole-grid widths

**Status:** FIXED, 2026-11-13. See the [repair and qualification](<Radiant_Issue_Ledger (fixed).md#ri-068>).


<a id="ri-067"></a>

### RI-067 — Row-flex cross heights reuse the preferred width after flexing

**Status:** FIXED, 2026-11-13. See the [repair and qualification](<Radiant_Issue_Ledger (fixed).md#ri-067>).


<a id="ri-066"></a>

### RI-066 — Auto flex columns measure empty wrappers and unlaid-out child boxes

**Status:** FIXED, 2026-11-13. See the [repair and qualification](<Radiant_Issue_Ledger (fixed).md#ri-066>).


<a id="ri-065"></a>

### RI-065 — Structured inline breaks understate nested flex height

**Status:** FIXED, 2026-11-13. See the [repair and qualification](<Radiant_Issue_Ledger (fixed).md#ri-065>).


<a id="ri-064"></a>

### RI-064 — Inline CSS image URLs never enter snapshot resource discovery

**Status:** FIXED, 2026-11-13. See the [repair and qualification](<Radiant_Issue_Ledger (fixed).md#ri-064>).


<a id="ri-063"></a>

### RI-063 — Decoded video overwrites foreground page content

**Status:** FIXED, 2026-11-13. See the [repair and qualification](<Radiant_Issue_Ledger (fixed).md#ri-063>).


<a id="ri-062"></a>

### RI-062 — Cyclic percentage flex children inflate the content minimum

**Status:** FIXED, 2026-11-13. See the [repair and qualification](<Radiant_Issue_Ledger (fixed).md#ri-062>).


<a id="ri-061"></a>

### RI-061 — Bounded snapshot resource grace can leave major media unavailable

**Status:** OPEN capture qualification limitation, 2026-11-13. **Lambda
blocker:** no for the selected grid repairs; limits interpretation of some
fresh media differences. Native ownership **D9.1.1v4 / D9.1.3v2**, finite
capture frame contract **D10.1.7**. The resource scheduler/priority contribution
beyond the observed grace expiry remains **unconfirmed**.

The first round14 captures omit large images on Cofounder505, Banky121,
Amperos565 and Pelago234. Their resource summaries respectively report71/85,
33/37,63/81 and37/39 completed at about1000ms, with no resource failures.
`snapshot_resource_wait_timeout_ms` in `network_integration.cpp` defaults to
1000ms; snapshot preparation then flushes updates and proceeds even with
outstanding optional resources. This observation does not identify every pending
request or establish one missing-image cause for all pages.

Alternating current-head before/repaired hosts under identical capture settings
later report85/85 for Cofounder,37/37 for Banky and81/81 for Amperos. Both hosts
restore those images. Pelago remains38/39 at the same grace. Thus a shared
regression from RI-058/RI-059 is not established by the initial score movement;
cache/network readiness remains a plausible contributor. These diagnostics do
not replace the canonical captures or claim stable initial readiness.

No grace, parent deadline, admission rule or virtual time is changed, and no
later media frame is substituted. Further work must isolate the required
dependency/priority or capture-readiness policy before changing behavior;
raising a per-site timeout is not a root-cause repair. Evidence: Lambda
`temp/online-visual-round14/media-readiness-controls.json/log`, alternating
before/final PNGs, and canonical `test/online/snapshots/` logs for these labels.

<a id="ri-060"></a>

### RI-060 — Native style replacement retains removed padding in a grid child

**Status:** OPEN, cause unconfirmed, 2026-11-13. **Lambda blocker:** no for
Stripe initial rendering or the selected grid-area repair. Ownership
**D9.1.1v4 / D9.1.3v2**; this is a native mutation observation, not proof of a
general script/CSSOM failure.

In the first native grid-area investigation, replace the subject's inline style
`grid-area:photo;padding:5px;border:2px solid` with
`grid-area:photo;width:calc(100% - 20px);height:50%`, using
`DomElement::set_attribute`, `radiant_recascade_document` and forced layout.
Its old5px padding remains after the border resets. Later variants also retain
that padding. Chromium's fresh equivalent input has zero padding; the current
fresh native cases agree with it. Style removal/reset ownership and the exact
cache/projection cause remain unisolated. No reset is added merely to hide it.

The selected area tests now load separate fresh inputs, matching the real PNG
and independent Chromium contracts, while each input still repeats forced
recascades. Live tests separately change tracks, area, alignment and explicit
size, without claiming padding-removal coverage. The mutation discrepancy is
retained here rather than assigned to grid placement. Evidence: Lambda
`temp/online-visual-round14/native-after.log` and
`padding-removal-reproducer.cppfrag`; see the checkpoint's distinction between
the initial mutation probe and final fresh-input acceptance cases.

<a id="ri-059"></a>

- [RI-059 — Nested grid publication replaces formatted height with a flow cursor](<Radiant_Issue_Ledger (fixed).md#ri-059>).

<a id="ri-058"></a>

- [RI-058 — Absolute grid placement sizes contents against the whole container](<Radiant_Issue_Ledger (fixed).md#ri-058>).

<a id="ri-057"></a>

- [RI-057 — Sizing and noscript snapshot regressions in the Lambda layout gate](<Radiant_Issue_Ledger (fixed).md#ri-057>).

<a id="ri-056"></a>

### RI-056 — Native input-value test target does not link its CSS resource call

**Status:** OPEN, 2026-11-12. **Lambda task blocker:** no for the sync demo's
selected native UI and Lambda baseline gates; blocks the aggregate `make
build-test` target. Source ownership: **D9.1.1v4 / D9.1.3v2**.

From Lambda `3ffe3477a` plus the uncommitted sync demo, run `make build-test`.
The arm64 linker fails building `test_radiant_input_value_gtest.exe` with undefined
`_process_css_resource`, referenced twice by
`RadiantInputValueTest.DownloadedStylesheetsUseOwnerOrderAcrossCompletionOrder`
(`radiant/test/test_radiant_input_value_gtest.cpp:102,105`). The function is
declared in `radiant/resource_loaders.h:23` and implemented in
`radiant/resource_loaders.cpp:99`. Lambda's selected
`build_lambda_config.json` target at the `test_radiant_input_value_gtest.cpp`
entry lists stylesheet/form/host helpers, but does not include that resource
implementation. The intended independent-test dependency closure or replacement
waist is **unconfirmed**; no sibling source or test workaround was applied.
Expected: the owning test target links its legitimate native dependencies.

Evidence in Lambda: `temp/sync-demo/build-test.log`, lines 380–388. The sync
runtime host builds successfully. Focused `test_radiant_view_gtest` and Lambda
baseline work can continue without this unrelated target.

<a id="ri-055"></a>

- [RI-055 — Paint engine lacks the M3 wet-media kernels](<Radiant_Issue_Ledger (fixed).md#ri-055>).

<a id="ri-054"></a>

- [RI-054 — Owner-held font fixture lookup under Lambda integration](<Radiant_Issue_Ledger (fixed).md#ri-054>).

<a id="ri-053"></a>

- [RI-053 — Historical P7 comparison artifacts are unavailable in the current checkouts](<Radiant_Issue_Ledger (fixed).md#ri-053>).

<a id="ri-052"></a>

### RI-052 — Existing empty-cell table baseline assertion differs by 10px

**Status:** open, qualification limitation, 2026-11-12. Native ownership:
**D9.1.1v4 / D9.1.3v2**. Cause is **unconfirmed**.

`SecondaryViewTest.TableBaselineUsesFirstNestedLineOrTheEmptyCellContentEdge`
reports glyph baseline 33 versus empty-cell y 10 + expected 33 = 43, for both fixed
and auto table layout. It reproduces identically with pagination source from
`340ad59` and the current source, with all remaining linked objects and flags
held constant. The full current Lambda paged inventory passes 504/505 after the
new nested-width acceptance and owner font lookup correction.

Evidence in Lambda-opus: `temp/paged-p7-integration/paged-final.xml`,
`control/table.xml`, `control/receipt.json` and `updated-focused.xml`.
This blocks an all-green legacy paged integration claim; it does not block the
19 focused native fragmentation cases or the 947-pass owner baseline. Leave the
assertion visible until its layout or coordinate-oracle cause is established.

Continuation evidence: Lambda-opus `temp/paged-advanced-integration/paged.xml`
still passes 504/505, with the same failure messages as `paged-final.xml`.
The expanded Radiant core now passes 60 focused cases, 988 owner-native cases
(one disabled) and 4,481 layout cases (six skips). RI-052 stays open.

Reservation/nested-row continuation: Lambda-opus
`temp/paged-reservations-integration-final/paged.xml` remains 504/505 with the
same assertion messages. The final owner increment passes 82 focused cases,
1,010 native cases (one disabled) and 4,481 layout cases (six skips).

<a id="ri-051"></a>

### RI-051 — Current full PDF/export qualification retains six unchanged failures

**Status:** open, qualification limitation, 2026-11-12. Ownership of native
qualification: **D9.1.1v4 / D9.1.3v2**; underlying causes are **unconfirmed**
except the Lambda-owned archive sentinel recorded as LR12-50.

The current 472-case PDF/export inventory completes with 466 passes and six
failures: inside image marker raster parity, background shorthand SVG resource
text, grid font-unit SVG geometry, RTL two-line clamp raster parity, four Ghent
ReadMes import baseline rows, and the archive sentinel rejected by the independent
reference reader. Each exact failure message reproduces with pagination source
from `340ad59` and otherwise identical current objects, headers and flags. The
four WebGL cases pass with normal macOS graphics access after their first sandbox
run stalled creating its hidden context. All 472 case names are accounted for;
the interrupted raw run is preserved rather than rewritten as passing XML.

Reproduction and evidence in Lambda-opus: `temp/paged-p7-integration/pdf.log`,
`pdf-gpu.xml`, `pdf-control.xml`, `pdf-summary.json`, `receipt.json` and
`control/receipt.json`. Both original and isolated-copy PDF baseline hashes are
unchanged. Current native core controls under Radiant's `temp/paged-p7-final/`
pass separately. This blocks full P7 PDF/export closure, not the delivered native
float/width increments. Do not reset references or change tolerances to close it.

Continuation evidence: Lambda-opus `temp/paged-advanced-integration/pdf.xml`
reruns 90 affected PDF/FO/native cases including these six failure controls:
84 pass and the six exact failure-message profiles still match `pdf-control.xml`.
Both baseline copies remain unchanged. Radiant
`temp/paged-advanced-final/report.json` passes ten independent export controls.
This subset does not replace the earlier 472-case inventory.

Reservation/nested-row continuation: Lambda-opus
`temp/paged-reservations-integration-final/pdf.xml` remains 84/90 with exactly the
previous six failure messages and unchanged baseline copies. Radiant
`temp/paged-reservations-final/report.json` passes sixteen independent exported
controls. RI-051 remains open; no protected references were changed.

<a id="ri-050"></a>

### RI-050 — Reservation/nested-row pagination increment increases composition cost

**Status:** open, performance qualification limitation, 2026-11-12. Native
ownership: **D9.1.1v4 / D9.1.3v2**. The cause within `layout_paged.cpp` is
**unconfirmed**; no timing workaround or workload change was made.

On unchanged optimized 21/300-page ordinary-flow workloads, four alternating
before/after pairs (28 samples per host/workload after two warmups per run) show
about 6% more composition time. A control rebuild replaces only
`layout_paged.cpp` with source from `ead8d2a`; every other current archive object,
benchmark client and link input is held constant. The archive symbol index
necessarily changes with its replaced object. Exact published geometry/glyph
FNV digests agree in every sample.

Median ms/page: 21 pages 0.901476 before / 0.955524 after (1.05995 ratio);
300 pages 0.781150 / 0.827102 (1.05883). Parsing, export, hashing and edition
retirement are excluded. A separate archived-host comparison also observed
about 5%; the held-object control is the stronger attribution evidence.

Reproduce from Radiant: run each of
`temp/paged-reservations-causal-control/{before,after}` with
`temp/paged-reservations-final/composition-{21,300}.html` and sample count `7`,
alternating order. `temp/paged-reservations-causal-control/` holds the original
source, compiler/link arguments, object/archive checks, executables, raw samples,
`receipt.json` and `report.json`. Current correctness is separately qualified by
82 focused cases, 1,010 native passes, 4,481 layout passes and sixteen exact
export controls. This blocks a current performance no-regression claim, not the
verified reservation/row behavior. The lost historical P7 archive waiver does
not waive this new measured difference.

<a id="ri-048"></a>

- [RI-048 — Grid-item percentage sizes use the whole grid instead of their area](<Radiant_Issue_Ledger (fixed).md#ri-048>).

<a id="ri-049"></a>

- [RI-049 — Root font-size evaluation leaves descendants on the initial rem basis](<Radiant_Issue_Ledger (fixed).md#ri-049>).

<a id="ri-047"></a>

- [RI-047 — Raw basic-shape parsing drops computed clip geometry](<Radiant_Issue_Ledger (fixed).md#ri-047>).

<a id="ri-046"></a>

- [RI-046 — Pending style text reparses discard later CSSOM edits](<Radiant_Issue_Ledger (fixed).md#ri-046>).

<a id="ri-045"></a>

- [RI-045 — Headless document hosts omit the browsing profile](<Radiant_Issue_Ledger (fixed).md#ri-045>).

<a id="ri-043"></a>

- [RI-043 — Unsubstituted variables incorrectly trigger cyclic intrinsic sizing](<Radiant_Issue_Ledger (fixed).md#ri-043>).

<a id="ri-044"></a>

- [RI-044 — Intrinsic images restore HTML width hints overridden by CSS](<Radiant_Issue_Ledger (fixed).md#ri-044>).

<a id="ri-038"></a>

- [RI-038 — Intrinsic ratio heights ignore inline min/max constraints](<Radiant_Issue_Ledger (fixed).md#ri-038>).

<a id="ri-037"></a>

- [RI-037 — Authored display bypasses scripting-enabled noscript suppression](<Radiant_Issue_Ledger (fixed).md#ri-037>).

<a id="ri-036"></a>

- [RI-036 — Nested column flex intrinsic heights omit main-axis gaps](<Radiant_Issue_Ledger (fixed).md#ri-036>).

<a id="ri-039"></a>

- [RI-039 — Intrinsic height recursion borrows an outer percentage-height basis](<Radiant_Issue_Ledger (fixed).md#ri-039>).

<a id="ri-040"></a>

- [RI-040 — Maximum constraints erase definite flex main sizes](<Radiant_Issue_Ledger (fixed).md#ri-040>).

<a id="ri-042"></a>

- [RI-042 — CSS math gaps are dropped before layout](<Radiant_Issue_Ledger (fixed).md#ri-042>).

<a id="ri-041"></a>

### RI-041 — URL-based CSS image masks have no paint consumer

**Status:** OPEN feature gap, 2026-11-12. **Lambda blocker:** no; captures
complete, but Craft's decorative paper shapes remain substantially different.
Ownership **D9.1.1v4 / D9.1.3v2**, effects contract **D5.5.1 / CI-18**.
The existing mask path is limited to radial gradients: MaskProp retains only
a RadialGradient and resolve_css_mask_image calls resolve_radial_gradient_value;
URL image masks are not retained or consumed. The support matrix already
marks URL/linear/multilayer mask consumers open.

Craft's authored CSS uses mask-image:url(/images/decorations/paperSheetMask.svg)
and paperShredTriangleFrontMask/paperShredBlobMask variants. These masks cut
the torn edges in Chromium; Radiant paints the unmasked solid paper geometry.
The inline logo-outline URL mask has the same unsupported image consumer.
RI-040 restores the heading and product placement, isolating this remaining
large decoration difference from the repaired flex sizing.

Implementing resource-backed mask images, sizing/positioning and SVG mask
rasterization/composition is outside this round's existing-bug milestone.
No shape is substituted or hidden. Evidence: Lambda
temp/online-visual-round5/craft-static.html, round-six live capture
temp/online-visual-round6/craft-live.png, and the unchanged browser reference
test/online/snapshots/saas_landing_436-browser.png. This observation does not
assign other pages' blank content to masking without their own evidence.

**Additional evidence, 2026-11-16:** Draftbit's border overlay uses two linear
mask layers, `content-box` and `mask-composite:exclude` (prefixed `xor`). The
radial-only consumer leaves its full gradient rectangle painted over the hero.
After RI-094/RI-095, navigation, heading and phone preview render, while this
large tint remains. Saved unchanged source and before/after PNGs are in Lambda
`temp/online-visual-round39/`; no mask layer was hidden or replaced. Linear,
multilayer and composite mask implementation remains outside this bug milestone.


<a id="ri-035"></a>

### RI-035 — Unavailable-voice native provider test stays pending on this macOS session

**Status:** OPEN, cause unconfirmed, 2026-11-12. **Lambda blocker:** no;
this limits the native aggregate qualification, not the online page captures.
Ownership **D9.1.1v4 / D9.1.3v2**. Running
`./build/test/test_radiant_media_backend_gtest --gtest_filter=RadiantNativeMedia.SpeechProviderReportsAnUnavailableVoice`
in the owner checkout returns a non-null speech job for
`invalid.lambda.test.voice`, but `rdt_speech_job_state` remains pending (0),
not failed (2), and the error is null. It fails in the native gate and three
isolated repeats. No speech implementation or test was changed by the online
rendering repairs. The cause and any dependency on the macOS provider/service
state remain unisolated; no assertion or skip was added. Evidence: Lambda
`temp/online-visual-round4/{native-gate-final.log,speech-1.log,speech-2.log,speech-3.log}`.

<a id="ri-034"></a>

- [RI-034 — Positioned intrinsic sizing leaks its query mode to following blocks](<Radiant_Issue_Ledger (fixed).md#ri-034>).

<a id="ri-033"></a>

- [RI-033 — Propagated body backgrounds cover negative stacking layers](<Radiant_Issue_Ledger (fixed).md#ri-033>).

<a id="ri-032"></a>

- [RI-032 — Flex SVG automatic cross sizes ignore the flexed main size](<Radiant_Issue_Ledger (fixed).md#ri-032>).

<a id="ri-031"></a>

### RI-031 — Current Dock document exceeds the online layout deadline

Latest release qualification (2026-11-14): a saved script-free control completes
in 11.58s wall / 9.83s CPU. The scripted page instead reaches 150.04s with 149.13s
CPU and 23 completed full layout passes. A short CPU sample places most sampled
work in post-script-turn full rebuilds; repeated SVG ancestor declaration reads
are one cost contributor. RI-077 repairs that reuse path with identical static
pixels, but the final matched live profile still times out at150.04s /149.91CPU,
with25 completed layouts. The repeated full-rebuild triggers remain incompletely
isolated. This confirms a native workload/cascade cost, not a transport wait;
no deadline, admission or virtual frame is changed. These profiles are diagnostic
and do not replace the canonical failed capture. Evidence: Lambda
`temp/online-visual-round29/`; the release build and source pins are recorded
there. RI-031 remains OPEN; other online work can continue.


**Status:** OPEN, root cause unconfirmed, 2026-11-12. **Lambda blocker:** yes
for a new canonical Dock snapshot; other online cases continue. Native layout
ownership **D9.1.1v4 / D9.1.3v2**. Two fresh repaired-host attempts at
`https://www.dock.us/` were killed by the existing 150s parent deadline during
deep nested grid/flex content layout. A control using the prior qualified
renderer (`8f3326ea42be5041d44bc776888dad4b4c1f2a7a008eb42611aa2666a88044a2`)
also times out at 150s. The failure therefore also exists without these changes; its cost change is unmeasured;
current document complexity, layout repetition and origin-dependent input
still need isolation. This is a capture correctness observation, not a release
performance benchmark. No deadline was raised or prior PNG accepted as fresh.
The round-fourteen current-main host with RI-058/RI-059 also times out at
the unchanged150s deadline (2026-11-13T07:00:39.276Z); see Lambda
`temp/online-visual-round14/dock-result.json`. No timing or layout-cost repair
is inferred from the other pages' visual improvements.
The round-thirteen root-font repair retry also times out at the unchanged
150s deadline (2026-11-12T13:34:03.960Z); see Lambda
`temp/online-visual-round13/dock-result.json`. The round-twelve result is
retained in its corresponding evidence directory.
The round-eleven DOM interface/initial-observer retry also times out at the
unchanged 150s deadline (2026-11-12T12:11:34.567Z); see Lambda
`temp/online-visual-round11/dock-result.json`. No partial PNG is promoted.
The round-ten computed-clip repair retry also times out at the unchanged
150s deadline; see Lambda temp/online-visual-round10/dock-result.json.
The round-nine CSSOM synchronization repair retry also times out at the
unchanged150s deadline; see Lambda temp/online-visual-round9/capture.log.
The round-eight session/cookie repair retry also times out at the unchanged
150s deadline; see Lambda temp/online-visual-round8/capture.log.
The round-seven intrinsic-sizing repair retry also times out at150s; see
Lambda temp/online-visual-round7/capture.log.
The round-six three-fix retry also times out at the unchanged 150s deadline;
see Lambda temp/online-visual-round6/capture-threefix.log.
The round-five five-fix retry also times out at the same 150s deadline; see
Lambda `temp/online-visual-round5/capture-fivefix.log`.
The round-four repaired-host retry also times out at the same 150s deadline;
see Lambda `temp/online-visual-round4/capture.log`.
Evidence: Lambda `temp/online-visual-round3/{capture-all.log,capture-grid.log,
dock-control/result.json,dock-control/dock-control-lambda.log}`, plus canonical
`test/online/snapshots/saas_landing_310-lambda.log` and manifest failure status.

<a id="ri-030"></a>

- [RI-030 — Grid row growth leaves stale scroll clips that hide item contents](<Radiant_Issue_Ledger (fixed).md#ri-030>).

<a id="ri-029"></a>

### RI-029 — Modal dialog top-layer and backdrop painting remain incomplete

**Status:** OPEN feature gap, 2026-11-12. **Lambda blocker:** no; major new
paint ordering is outside the current online bug-fix milestone. Ownership
**D9.1.1v4 / D9.1.3v2**. Leaflet's reference page calls showModal: native DOM
modal state and fixed centering exist, but the renderer has no top-layer
painting pass and the parsed `::backdrop` has no layout/paint consumer.
Ordinary page text can paint over the dialog and no dimming backdrop appears.
These limits are already recorded in Lambda's HTML/CSS support matrix;
this record ties them to `https://leafletjs.com/reference.html` and its online
snapshot. The cause is not assigned to image loading or CSS opacity.
Evidence: Lambda round2/browser/leaflet_docs.json, canonical paired PNGs,
`render_block.cpp`, dialog DOM state and the support-matrix dialog/backdrop rows.

<a id="ri-028"></a>

- [RI-028 — Auto-height flex columns shrink natural content to their minimum](<Radiant_Issue_Ledger (fixed).md#ri-028>).

<a id="ri-027"></a>

- [RI-027 — Mobile viewport metadata shrinks desktop layout independently of its host](<Radiant_Issue_Ledger (fixed).md#ri-027>).

<a id="ri-026"></a>

- [RI-026 — Background color resets paint opaque black](<Radiant_Issue_Ledger (fixed).md#ri-026>).

<a id="ri-025"></a>

- [RI-025 — Headless exports never queue ordinary network image dependencies](<Radiant_Issue_Ledger (fixed).md#ri-025>).

<a id="ri-024"></a>

- [RI-024 — Multi-keyword link relations omit entire stylesheets](<Radiant_Issue_Ledger (fixed).md#ri-024>).

<a id="ri-023"></a>

### RI-023 — Online snapshots omit major image/video regions

**Status:** OPEN, 2026-11-12. **Lambda task blocker:** no; further asset and
snapshot-lifecycle diagnosis is needed. Native ownership:
**D9.1.1v4 / D9.1.3v2**; finite frame selection follows **D10.1.7**.
The original observations did not establish one shared defect. RI-025 now
repairs ordinary image/poster request discovery and poster intrinsic sizing;
RI-026 repairs the black reset-color region. Final Curl, NOAA and OCaml captures
restore their missing media. Python's active carousel has authored opacity0 at
the selected frame; its timing follows D10.1.7. Other remaining lazy-media,
video-frame, CSS and application-state differences still require individual
evidence and are not all assigned to one loader bug.

The major online visual review finds missing hero regions on Chrome docs
(`chrome_developers`), CMake (`cmake`), Python (`python`), NOAA (`noaa`) and
multiple marketing pages. After RI-020/RI-021 the Chrome text/theme is repaired,
but its authored `img` for `images/docs/docs-hero.webp` is still absent. Other
cases may use CSS backgrounds, lazy content, animations or video. Do not force
asset sizes, replace images or remove differences from scoring. Reproduce with
the catalog URLs and 1200×800 render options.

Evidence in Lambda: `temp/online-visual/review-{0..28}.png` covers all 226
pre-fix review candidates; the final comparison report retains every catalog
row. `probes/chrome_developers.png` and `pristine-probes/` distinguish the fixed
layout/theme issues from remaining media. Per-page capture logs are under
`test/online/snapshots/`. Other remaining classes include hidden application
content (Lambda JS13-L1), differing consent state and origin/application errors.
Each needs a reduced reproducer before a source repair.

Second-review residual: CMake's video geometry now matches the browser at
1232×528 after RI-025, but its selected snapshot still has a blank media region.
The poster URL returns HTTP200 (144,119 bytes) in a separate direct request;
that alone does not establish that the native manager delivered it before the
selected frame. Native readiness logs retain one pending resource. The remaining
resource/paint/playback cause is unconfirmed; the page is retained for visual
review even though its coarse error falls below the automated threshold.

<a id="ri-022"></a>

- [RI-022 — Downloaded dynamic stylesheets lose their link media condition](<Radiant_Issue_Ledger (fixed).md#ri-022>).

<a id="ri-021"></a>

- [RI-021 — Grid placement declarations suppress actual flex items](<Radiant_Issue_Ledger (fixed).md#ri-021>).

<a id="ri-020"></a>

- [RI-020 — An initially disabled linked theme participates in the cascade](<Radiant_Issue_Ledger (fixed).md#ri-020>).

<a id="ri-019"></a>

### RI-019 — Reflow aborts when the focused target becomes non-focusable

**Status:** OPEN, 2026-11-11. **Lambda task blocker:** no for the selected finite
snapshot milestone; the native focus invariant remains unresolved. Native state ownership:
**D9.1.1v4 / D9.1.3v2**. The invalidation cause is **unconfirmed**.

From Lambda `ff7085cf9`, `./lambda.exe render https://designmodo.com/postcards/faq/ -o ./temp/online-recovery/focus-046.png -vw 1200 -vh 800`
and the equivalent command for `https://pulsetic.com/` both terminate with
SIGABRT in about 38 seconds. Both report:
`state_machine: assertion failed after doc_state_request_reflow: focused target is not focusable`
and `radiant_state_assert_valid`, `state_machine.cpp:1193`.
Expected: handle focus invalidation as page state changes and finish export;
do not disable the assertion or suppress the crash in the launcher.

Evidence in Lambda: `test/online/snapshots/saas_landing_046-lambda-output.log`,
`saas_landing_046-lambda.log`, and corresponding `saas_landing_138` logs;
`temp/online-recovery/probe_results.json` preserves exit signals and timings.
After RI-017, both selected finite snapshots load/export successfully. This
does not repair focus invalidation during later interactive reflow. The assertion
remains enabled; no focus-state workaround was added.


**Eden follow-up, 2026-11-15:** the staged Eden compatibility repair at
`Lambda/temp/eden-load/eden` reaches the `ecosystem` native replay with Lambda
`c2b705a98` (`lambda.exe` SHA-256
`1603697a7eac9ab6a75c8491ae3e5ed8da358ebc36b982bc230f76407d0e3483`).
With macOS graphics access, run `EDEN_LAMBDA_EXE=../../../lambda.exe ./eden smoke
--suite ecosystem` from that staged Eden directory (or select the equivalent
absolute executable). The process aborts after initial rendering with
`state_machine: assertion failed after hover_transition: focused target is not
focusable`, at the same `radiant_state_assert_valid` invariant. The action that
invalidates focus and whether it shares RI-019's cause are unconfirmed. This
does not block Eden's initial terrain startup, but blocks the ecosystem replay.
Evidence: `Lambda/temp/eden-load/smoke-all-final.log` near the first
`hover_transition` failure. No assertion or fixture was changed.

<a id="ri-018"></a>

- [RI-018 — Online exports exhaust memory before viewport PNG encoding](<Radiant_Issue_Ledger (fixed).md#ri-018>).

<a id="ri-017"></a>

- [RI-017 — One-shot exports drain recurring animation before capture](<Radiant_Issue_Ledger (fixed).md#ri-017>).

<a id="ri-016"></a>

### RI-016 — Native macOS speech rejects an invalid voice too late

**Status:** OPEN, 2026-11-11. **Lambda task blocker:** no; speech is outside
the Kian Hup rendering path. Native ownership: **D9.1.1v4 / D9.1.3v2**.
The root cause is **unconfirmed**.

`make test-native INPUT_ROOT=/Users/henry/Projects/lambda-input -j6` completes
902 passing tests but fails
`RadiantNativeMedia.SpeechProviderReportsAnUnavailableVoice`. A focused repeat
also fails: a request for `invalid.lambda.test.voice` returns a job in state 0
with a null error instead of immediate `RDT_SPEECH_FAILED` and a `no voice`
error. No audio playback is requested by this check.

Both `radiant/rdt_speech_avf.mm` and
`test/test_radiant_media_backend_gtest.cpp` are unchanged from HEAD. The
provider uses `AVSpeechSynthesisVoice voiceWithIdentifier:` and rejects the
request immediately only if that lookup returns nil. Whether this platform
returns a fallback voice or another provider state prevents rejection has
not been isolated. No source or test changes were made for this separate
failure. Evidence in the sibling Lambda checkout:
`temp/kianhup-style/baseline-owner-native.log` and `test-owner-speech.log`.

<a id="ri-012"></a>

### RI-012 — Forced window capture repaint omits login fragment foreground

**Status:** OPEN, 2026-11-11. **Task blocker:** no; Lambda's blank-login
startup failure is fixed, and the normally presented window surface contains
the complete login form. This report concerns native capture/repaint behavior
owned by Radiant under D9.1.1v4 / D9.1.3v2. The cause is **unconfirmed**.

With the current Lambda debug host and its pending `navigator.cookieEnabled`
fix, `lambda.exe view https://kianhup.com/login` initializes the public Acro
template and Angular controller successfully. DOM assertions confirm visible
username/password fields and Login text. A `capture_surface` event with the
default `render:true` produces only the background and panel, omitting the
logo, heading, labels, inputs and button. A capture with `render:false` after
the same load reads the complete presented surface. Waiting 1500 ms before
the forced capture does not change its result. A local static/late-revealed
`fragment` control did not reproduce the omission.

Reproduce from the sibling Lambda checkout after building its pending fix:

```sh
./lambda.exe view https://kianhup.com/login --event-file temp/kianhup-blank/login-window-delayed-events.json
./lambda.exe view https://kianhup.com/login --event-file temp/kianhup-blank/login-window-readback-events.json
```

Evidence under `../../lambda/temp/kianhup-blank/`: `login-window.png` (forced
repaint, foreground absent), `login-window-readback.png` (presented surface,
foreground present), corresponding event JSON and stdout files. The delayed
stdout records both fragments as `display:inline`, `visibility:visible`,
`opacity:1` with nonzero bounds. `login.png` and `login-result.json` record the
passing headless replay. Direct OS window capture was unavailable; the
comparison uses Radiant's own surface readback.

Relevant entry points: `radiant/event_sim_render.cpp` `read_surface()` calls
`event_sim_render_surface()` by default, whereas `render:false` calls
`ui_context_materialize_surface()`; `radiant/event_sim.cpp`
`event_sim_render_surface()` suspends the GPU compositor around
`render_html_doc()`. Whether compositor state, retained paint, or another
native invalidation path causes the omission remains unconfirmed. No native
source workaround or repair was attempted for this capture issue. Recheck
after the RI-013/RI-014/RI-015 login repairs: both presented-surface and forced
repaint captures now retain all foreground at 793x620. The exact cause of the
earlier omission was not isolated, so this observation does not attribute it
to a particular fix. Evidence: `../../lambda/temp/kianhup-style/native-final.png`,
`native-forced-final.png`, and corresponding stdout/event files. Lambda verification:
`make test-radiant-baseline` passes, with 8,160 passed, 299 partial and 20
skipped of 8,479 tests; this observation is outside that gate's scored cases.


<a id="ri-010"></a>

RI-010 is [resolved in the fixed issue ledger](<Radiant_Issue_Ledger (fixed).md#ri-010>):
all eight remaining layout comparisons pass after the approved reference repair.
The normal 5,936-case integration gate has zero failures on macOS.

<a id="ri-011"></a>

### RI-011 — Cloned multicol text trim has a pre-existing capacity mismatch

**Status:** OPEN, 2026-11-11. **Task blocker:** no; this is an additional control,
outside the ten reported failing comparisons (D9.1.1v4 / D9.1.3v2).

`wpt-css-inline/text-box-trim-multicol-003` uses `box-decoration-break:clone`.
A fresh Chrome capture places four 80px lines, with their edge leading trimmed,
in each 290px column; native layout places three and creates an extra row.
Element agreement is 100%, text agreement is 30% against that fresh capture.
The existing stored baseline still passes and is unchanged. Insufficient
trim-aware fragment capacity is a suspected cause, not yet confirmed.

Reproduce with the shared layout tester and `-c wpt-css-inline -t
text-box-trim-multicol-003.html`, then compare with a temporary fresh capture.
`temp/radiant-layout-20261111/clone-control.cjs` compares the pinned pre-repair
host and final host: their complete native layout trees are identical.
`multicol-control.json` and `fresh-references/` retain the fresh mismatch.
No clone repair or baseline/reference change is claimed in RI-010.

Continued owner-local repairs (2026-11-11): RAD02-3 now clips positioned HTML
paint, vector output and native hits; RAD03-3 / RAD04-3 share replaced-size
defaults; RAD16-5 no longer has an unreachable frame-clock tail. See the
[implementation and qualification](impl/Radiant_Impl_Ledger_Clip_Defaults.md).
The original reports are retained in the fixed ledger with scoped limits.
Final gate: **893 native tests plus static smoke, all 4,481 geometry comparisons,
7 runner contracts and native archive closure pass**. Existing hardware/static
skips remain; layout lint checks 89 casts with zero unmarked and its five
contracts pass. See the [pinned receipt](../radiant/temp/ledger-next-verification-summary.json).

Latest bounded implementation (2026-11-11): horizontal flex baseline groups,
video posters, a pass-local whitespace cache, and alignment/batch module
extractions are implemented under D9.1.1v4 / D9.1.3v2. RAD22-6 is archived;
broader baseline, whitespace and monolith entries retain explicit PARTIAL
scope. See [the implementation record](impl/Radiant_Impl_Ledger_Repairs.md) and the
final qualification receipt in the fixed ledger.

Final gate: **891 native tests plus static smoke, 7 runner contracts and all
4,481 geometry comparisons pass**, with the existing hardware/static skips.
Layout-cast lint and its five contracts pass. See the
[pinned verification receipt](../radiant/temp/ledger-impl-verification-summary.json).

Continued review on 2026-11-11 repairs RAD04-7, RAD07-1 and RAD15-5 and removes
the redundant float prescan estimates reported as RAD03-2 / RAD04-2. Six more
historical reports are already resolved in current source and have scoped
re-verification records in the fixed ledger. RAD03-1 / RAD04-1 / RAD08-5 remain
PARTIAL: native grid first/last baseline groups are repaired, while broader
baseline behavior remains open. The completed verification receipt is linked
in the continued owner qualification entry in the fixed ledger.

Reviewed 2026-11-11 under D9.1.1v4 / D9.1.3v2: RI-001, RI-002, RI-007, RI-009
and RAD22-7 have owner-local repairs and verification. Eleven imported
reports were already resolved in current source; their original observations
and the precise scope of re-verification are retained in the fixed ledger.
The full native and static/dynamic layout aggregate passes; layout-cast lint
and its five contract checks also pass.
The remaining imported feature/refactoring reports are not claimed fixed.

<a id="ri-004"></a>

### RI-004 — Lambda pointer-event adapter expects an absent native event protocol

**Status:** RESOLVED — the merged native pointer protocol clears the original
release adapter compile failure (D9.1.1v4, D9.1.3v2). See the
[cause, regression coverage and verification limits](<Radiant_Issue_Ledger (fixed).md#ri-004-pointer-event-protocol>).
The descriptive anchor distinguishes this report from the
[older resolved layout issue](<Radiant_Issue_Ledger (fixed).md#ri-004>) that was
independently assigned RI-004; both original IDs are preserved.

The native static-layout qualification and unified-archive provider entries
were independently recorded as `RI-003`. They are now `RI-005` and `RI-006`,
respectively; `RI-003` remains the fixed startup-fragment navigation issue.

## Imported native Radiant backlog

These entries retain their original `RAD` IDs, evidence and historical wording
from the Lambda-hosted ledger. Ownership was reviewed against the split on
2026-11-09 (D9.1.1v4, D9.1.3v2). The 2026-11-11 resolved-entry links below
identify the reports rechecked in current source. Remaining claims and old line
numbers still need re-verification; duplicate or related open entries retain
their original IDs.

<a id="rad13-l1"></a>

### RAD13-L1 — Scene3D gaming profiles remain incomplete

**Updated:** 2026-10-28. The [gaming design](radiant/Radiant_Design_Scene3D.md)
stages native gameplay support while preserving pure authored values and
document-owned live services (**S12.1.1v3 / S12.1.5v2, D9.4.4–D9.4.5**).
Source relocation and the shared scene coordinator are implemented. The first
collision extensions add discrete capsule/convex contacts, mass/inertia,
point/ray tests and named-pair closest-point queries through the shared C
provider. The convex delivery also preserves owned live worlds on compatible
geometry rejection; full M2 remains partial.

Outstanding native work is the design's remaining collision/CCD profiles and
M3–M12: articulated/character movement, navigation, animation graphs, emitters,
game audio, genuine 2D, depth effects, baked/dynamic indirect lighting, fog and
soft bodies. The four Lambda Script gameplay-algorithm scopes remain KIV.
Acceptance and receipts live in the
[implementation record](impl/Radiant_Impl_Scene3D_Gaming.md); no future
profile is advertised as implemented. Its M1 core baseline is green; Radiant
qualification retains six text/inline/sizing layout failures, the existing
header cap failure and the module-boundary import debt described there.
The M2 aggregate also records an unreproduced render-adapter idle timeout;
direct and shared-harness component reruns pass with unchanged checks. The
failed aggregate is retained in the implementation receipts.
Convex qualification exposed the live async sanitizer limit
[LR12-43](<Lambda_Issue_Ledger (fixed).md#lr12-43>), fixed 2026-10-29 under
**D6.1.1 / D6.3.3 / D6.3.6** by reusable-stack lifetime and ASan fiber
bookkeeping. The original runtime and mesh event fixtures now pass **22/22**
and **17/17** assertions with forced/poisoned GC and stack-use-after-return
checking. The standing layout/header/boundary debt above is unchanged.

### RAD01 — View & DOM Model

<a id="rad01-3"></a>

- [RAD01-3](<Radiant_Issue_Ledger (fixed).md#rad01-3>).

<a id="rad01-5"></a>

- [RAD01-5](<Radiant_Issue_Ledger (fixed).md#rad01-5>).

### RAD02 — CSS Style Resolution & Computed Style

<a id="rad02-1"></a>

#### RAD02-1

**Status (2026-11-11): PARTIAL — first bounded extraction delivered.**
`css_alignment.cpp` / `css_alignment.hpp` own container/self alignment resolution
and the stores shared with the dispatcher. Native alignment behavior is covered
by existing and added geometry controls; the remaining property groups still
need decomposition. See [implementation record](impl/Radiant_Impl_Ledger_Repairs.md).
Authority: **D9.1.1v4 / D9.1.3v2**.

**The 726k monolith.** `resolve_css_style.cpp` (~13.9k lines) is the largest file in the repo, and the ~248-case `resolve_css_property` switch (`resolve_css_style.cpp:4632`) is navigable only through comment GROUP banners. *Improvement:* split by property group into separate translation units keyed off a shared dispatch table.

<a id="rad02-2"></a>

#### RAD02-2

**Unimplemented `content:` functions.** `counter()`/`counters()`/`attr()`/`url()`/concatenation for the `content` property are all TODO stubs (`resolve_css_style.cpp:12738`,`:12743`,`:12748`,`:12753`,`:12759`), and pseudo-element content is guessed from selector context ("TODO: improve this", `:12694`).

<a id="rad02-3"></a>

- [RAD02-3](<Radiant_Issue_Ledger (fixed).md#rad02-3>).

<a id="rad02-4"></a>

- [RAD02-4](<Radiant_Issue_Ledger (fixed).md#rad02-4>).

<a id="rad02-6"></a>

- [RAD02-6](<Radiant_Issue_Ledger (fixed).md#rad02-6>).

<a id="rad02-7"></a>

#### RAD02-7

**Silent `NAN` on var()/calc overflow.** The depth-64 guard in `resolve_length_value` (`resolve_css_style.cpp:2457`) returns `NAN` and logs a warning, but deep-but-legal chains simply drop out with no user-visible diagnostic; downstream layout must tolerate `NAN`.

<a id="rad02-8"></a>

#### RAD02-8

**Presence-flag fragility.** The many per-struct `has_*` flags (e.g. `InlineProp::has_color`, `has_svg_*`) must be set by hand in each relevant case; adding a new property case and forgetting its flag silently mis-reports "not set". *Improvement:* generate the flag/set pair from a property table.

### RAD03 — Layout Driver, Block Layout & Block Formatting Context

<a id="rad03-1"></a>

#### RAD03-1

**Status (2026-11-11): PARTIAL — horizontal core delivered.** Grid first/last
sharing groups and horizontal flex container/self alignment now preserve the
compound value and select the correct line baseline. Flex keeps both groups
independent, refreshes natural cross sizes after content layout and redistributes
wrapped-line space before final alignment. Controls cover auto/fixed heights,
mixed groups, row reversal, retained changes, wrapped stretch, auto margins
and explicit start overrides. Local Chrome confirms the wrapped geometry.

Orthogonal baselines, spanning grid items and exhaustive nested-container
baseline export remain unqualified. The original failing flex controls and
passing-after evidence are retained under `temp/ledger-impl-*`; see the
[bounded implementation record](impl/Radiant_Impl_Ledger_Repairs.md). Related:
RAD04-1 and RAD08-5. Authority: **D9.1.1v4 / D9.1.3v2**.

The historical stub description below predates the current implementation.

**Last-baseline alignment is unimplemented.** `compute_element_last_baseline` (`layout_alignment.cpp:242`) is a `// TODO: Implement proper last baseline calculation` returning `-1`. `LayoutOutput::last_baseline` (`layout.hpp`) therefore never carries a real value, so `align-items: last baseline` in flex/grid degrades to first-baseline/edge alignment.

<a id="rad03-2"></a>

- [RAD03-2](<Radiant_Issue_Ledger (fixed).md#rad03-2>).

<a id="rad03-3"></a>

- [RAD03-3](<Radiant_Issue_Ledger (fixed).md#rad03-3>).

<a id="rad03-4"></a>

- [RAD03-4](<Radiant_Issue_Ledger (fixed).md#rad03-4>).

<a id="rad03-5"></a>

#### RAD03-5

**`layout_block` is ~1600 lines mixing many concerns.** The single function (`layout_block.cpp:7486`) handles float detection, context save/restore, view tagging, style resolution, cache/measurement bailout, counters, list-items, and the deep inline-atomic float/wrap flow-into-parent logic (`layout_block.cpp:7849+`, `8600+`). It is hard to test in isolation and predates the RAII scopes it could use. *Improvement:* extract the flow-into-parent margin/collapse tail and the inline-atomic handling into named helpers.

<a id="rad03-6"></a>

#### RAD03-6

**`LayoutOutput` return-struct abstraction is inconsistently threaded**: block layout writes `block->width`/`height` directly, so the measure-vs-layout separation is not carried by a uniform return type.

### RAD04 — Box Model, Containing Blocks & the Layout Cache

<a id="rad04-4r"></a>

#### RAD04-4R — Native layout-cache invalidation

**Status:** UNVERIFIED — native portion of historical RAD04-4; ownership
reviewed 2026-11-09. The original report describes coarse cache clearing in
Radiant's `layout.cpp` and `view_pool.cpp` and raises a possible stale
descendant measurement if invalidation does not propagate. Recheck those
claims against current native invalidation before deciding on a fix. The
Lambda event-dispatch portion remains in the [mixed historical record](Radiant_Issue_Ledger.md#rad04-4).

<a id="rad04-1"></a>

#### RAD04-1

**Status (2026-11-11): PARTIAL.** See [RAD03-1](#rad03-1) for the fresh native
flex/grid repairs, failing-before/passing-after controls and remaining scope. The
historical stub description below predates the current helper implementation.

**`compute_element_last_baseline` is a stub.** `layout_alignment.cpp:237` has `// TODO: Implement proper last baseline calculation` and returns `-1`, so *last*-baseline alignment (`align-items: last baseline`) silently degrades to no-baseline for flex and grid items. *Improvement:* mirror the recursive first-baseline logic over the last in-flow child.

<a id="rad04-2"></a>

- [RAD04-2](<Radiant_Issue_Ledger (fixed).md#rad04-2>).

<a id="rad04-3"></a>

- [RAD04-3](<Radiant_Issue_Ledger (fixed).md#rad04-3>).

<a id="rad04-5"></a>

- [RAD04-5](<Radiant_Issue_Ledger (fixed).md#rad04-5>).

<a id="rad04-6"></a>

#### RAD04-6

**`LayoutOutput` is under-threaded.** `layout.hpp` defines a clean width/height + baseline return struct, but block layout mostly writes `block->width`/`height` in place rather than returning `LayoutOutput`, so the abstraction is not consistently plumbed and the baseline fields are largely unused by the block path.

<a id="rad04-7"></a>

- [RAD04-7](<Radiant_Issue_Ledger (fixed).md#rad04-7>).

### RAD05 — Intrinsic Sizing

<a id="rad05-1"></a>

- [RAD05-1](<Radiant_Issue_Ledger (fixed).md#rad05-1>).

<a id="rad05-2"></a>

#### RAD05-2

**Height is an admitted estimation, and never cached.** `calculate_max_content_height` does not run a real layout for elements (`intrinsic_sizing.cpp:5264-5265`, "for now, use a simplified estimation"), so nested flex/grid/table height can mismatch committed layout; and because there is no height cache, the expensive width-dependent walk repeats on every call.

<a id="rad05-3"></a>

#### RAD05-3

**The monolith is ~3100 lines with heavy duplication.** `measure_element_intrinsic_widths` (`:1939-5060`) inlines table, grid, flex, block/inline, replaced, and pseudo logic. Box-extra/border/padding resolution is duplicated across the table block (`:3433-3465`), the generic tail (`:4900-4948`), the pseudo path (`:4751-4840`), and the grid early-return (`:3703-3714`); font setup is mirrored again in `calculate_max_content_height` (`:5290`). *Improvement:* extract a shared box-extras helper and a per-mode dispatch table.

<a id="rad05-4"></a>

#### RAD05-4

**Grid intrinsic width is a fixed-track approximation with an early return.** The grid branch (`:3606-3744`) sums only fixed-length tracks and gaps, ignoring percentage/`fr` tracks, and early-returns past the generic constraint tail (duplicating the max-width clamp at `:3719`). Real track sizing is in RAD09; the two can disagree.

<a id="rad05-5"></a>

#### RAD05-5

**Per-call timing and the `SLOW MEASURE` beacon signal a known hotspot.** Every element call runs a `std::chrono` timer and warns above 100ms (`:5053-5057`); text measurement loads a glyph per codepoint. The absence of height memoization (issue 2) makes repeated measurement passes (flex, grid multipass) quadratic-ish on deep subtrees.

<a id="rad05-6"></a>

#### RAD05-6

**Debt is expressed as `CRITICAL FIX` / `fallback` / "for now", not `TODO`.** There are no literal TODO/FIXME/HACK tokens; instead accreted special-cases carry `CRITICAL FIX` comments (button UA styling `:1973`, monospace font-before-children `:1973`), form-control border-box flags, and anonymous-table repair — harder to grep and easy to overlook when refactoring.

<a id="rad05-7"></a>

#### RAD05-7

**The re-entrancy guard returns `{0,0}` silently on a cycle.** Breaking a percentage-height ↔ aspect-ratio ↔ width cycle by returning zero (`:1951`) is safe against infinite recursion but can under-measure a legitimately complex box with no diagnostic beyond a `log_debug`.

### RAD06 — Inline & Text Layout

<a id="rad06-1"></a>

#### RAD06-1

**Bidi and complex-script paint remain separate.** `layout_bidi.cpp` resolves visual order for established line fragments, using optional FriBidi or its local fallback, and publishes advance-based ranges. That geometry does not provide Arabic joining, Indic shaping or full bidi glyph substitution; the existing paint and fallback limitations still apply. This scoped refresh does not qualify the complete UAX #9 surface.

<a id="rad06-2"></a>

#### RAD06-2

**No general complex-script shaping.** Only the ASCII-Latin fast path calls `font_measure_text` (`layout_text.cpp:976`); every other script is measured per-codepoint with no GSUB. Arabic joining, Indic reordering, and ligatures beyond the font's simple advance are not handled. The comment at `layout_text.cpp:884` notes the ZWJ-emoji handling is a heuristic "without HarfBuzz text shaping". *Improvement:* integrate a shaper (HarfBuzz) and persist a shaped buffer (see item 5).

<a id="rad06-3"></a>

#### RAD06-3

**Emoji ZWJ sequences are heuristic.** `is_emoji_for_zwj` (`layout_text.cpp:877`) and `is_zwj_composition_base` use hardcoded codepoint ranges to zero post-ZWJ advances, rather than proper UAX #29 grapheme clustering; non-standard emoji sequences will mis-measure.

<a id="rad06-4"></a>

#### RAD06-4

**`<br>` reported height is an approximation.** `layout_inline.cpp:1180` admits the rule "is not fully understood" and uses the font cell height, whereas Chrome's reported `<br>` height varies by context.

<a id="rad06-5"></a>

#### RAD06-5

**Layout and render re-measure independently from byte ranges.** `TextRect` stores only `start_index`/`length`/`width`, so `render_text.cpp` re-decodes and re-loads every glyph. This duplicates work and risks layout/paint divergence. *Improvement:* a shared shaped-glyph buffer keyed by the rect's byte range.

<a id="rad06-6"></a>

#### RAD06-6

**Status (2026-11-11): PARTIAL — layout hot-path cache delivered.** Text
layout, wrapping and lookahead share a pass-local result by parent, invalidated
by style/mutation epochs, layout/measurement generations and owner/view changes.
Inline overrides, UA defaults and retained style changes have native controls.
Context-free paint/query helpers retain their resolver, and the related
collapsed-whitespace bounds observation remains unqualified. No performance
claim is made. See [implementation and receipts](impl/Radiant_Impl_Ledger_Repairs.md).
Authority: **D5.5 / D9.1.1v4 / D9.1.3v2**.

**`white-space` is re-walked per text node.** `layout_text.cpp:2642` carries `todo: white-space should be put in BlockContext`; `get_white_space_value` climbs the ancestor chain on every text node. Related: `:2700` `todo: probably should still set it bounds` for fully-collapsed whitespace nodes.

<a id="rad06-7"></a>

#### RAD06-7

**Hardcoded fallbacks.** tab-size default `8` (`layout_text.cpp:2983`), small-caps lowercase scale `0.7f` (multiple sites, `:3094`/`:3133`/`:3235`), and C1-control line-height synthesis ratios. These are correct for common cases but not configurable.

<a id="rad06-8"></a>

- [RAD06-8](<Radiant_Issue_Ledger (fixed).md#rad06-8>).

<a id="rad06-9"></a>

#### RAD06-9

**Infinite-`goto` guard.** `layout_text` caps its `goto LAYOUT_TEXT` re-entries at `layout_text_iterations > 500` (`layout_text.cpp:2733`) — an explicit admission that the goto-driven control flow can loop under degenerate layouts (extreme negative margins), aborting text layout rather than hanging.

<a id="rad06-10"></a>

#### RAD06-10

**`layout_text` is a single ~1370-line function.** The overflow/wrap cascade and break-recording tail are long and deeply nested; correctness depends on many interacting `Linebox` flags. There is no unit-level test of individual branches — regressions surface only through the layout suite.

### RAD07 — Fonts

<a id="rad07-1"></a>

- [RAD07-1](<Radiant_Issue_Ledger (fixed).md#rad07-1>).

<a id="rad07-2"></a>

- [RAD07-2](<Radiant_Issue_Ledger (fixed).md#rad07-2>).

<a id="rad07-3"></a>

#### RAD07-3

**Platform coupling leaks into the bridge.** The macOS `"System Font"` reuse quirk (`font.cpp:64-73`, guarded `__APPLE__`) is a bridge-level compensation for how CoreText reports `system-ui`. Any second platform whose backend renames the resolved family would need the same treatment, ad hoc. *Improvement:* have the engine normalize the reported family for generic system families so the bridge's cache-match test can stay platform-agnostic.

<a id="rad07-4"></a>

#### RAD07-4

**Cache-match test relies on exact float equality.** `font_handle_matches_prop` compares `handle_size == fprop->font_size` (`font.cpp:75`) with `==` on floats. This is currently sound because both come from the same resolution path, but it is a fragile invariant — any sub-pixel rounding difference silently forces a re-resolve.

<a id="rad07-5"></a>

#### RAD07-5

**Hard-coded caps.** `@font-face` array initial capacity `10` (`font_face.cpp:399`); `max_cached_faces = 64` at context creation (`ui_context.cpp:161`). These are reasonable defaults but are not configurable from CSS or document scale.

<a id="rad07-6"></a>

#### RAD07-6

**No variable-font or feature-control surface.** Radiant's `FontProp`/`FontStyleDesc` expose only family/size/weight/slant plus `font-kerning`. There is no bridge for `font-variation-settings` (variable-font axes) or `font-feature-settings` (arbitrary OpenType features) — only the specific `halt` adjustment (`font_get_halt_adjustment`) is surfaced. *Improvement:* extend `FontStyleDesc` and the bridge to carry axis/feature maps if variable-font support is desired.

<a id="rad07-7"></a>

#### RAD07-7

**Fallback handling is fragile at the seam.** Radiant delegates per-codepoint fallback entirely to `font_load_glyph`, but special-cases only emoji (`font_load_glyph_emoji`); the fallback-metric blend for line height depends on `LoadedGlyph::font_normal_*` being populated correctly by whichever fallback face the engine picks — a dependency that is implicit and untested at the bridge level.

### RAD08 — Flexbox Layout

<a id="rad08-1"></a>

- [RAD08-1](<Radiant_Issue_Ledger (fixed).md#rad08-1>).

<a id="rad08-2"></a>

#### RAD08-2

**Hard-coded measurement fallbacks.** The default container width `366` in `measure_flex_child_content` (`:661`), the `line_init(..., 10000)` sentinel width (`:641`), the scattered `10000.0f` "unconstrained" widths (`layout_flex_multipass.cpp:2123` and elsewhere), and the **tag-based height estimates** (e.g. h4 = 20px at `:756`, and the "p=36px, h1=32px" heuristics noted at `layout_flex_multipass.cpp:2121`) are magic numbers that can mis-measure content in edge cases. *Improvement:* derive these from font metrics / actual available space.

<a id="rad08-3"></a>

#### RAD08-3

**Correctness caps.** `MAX_ITERATIONS = 10` (`layout_flex.cpp:4194`), `MAX_FLEX_DEPTH = 16` (`layout.hpp`), and `MAX_MEASURE_DEPTH = 32` (`layout_flex_measurement.cpp:436`) are hard caps added for fuzzer-found blowups. They protect against pathological trees but silently truncate genuinely deep or genuinely slow-converging layouts rather than reporting a limit hit.

<a id="rad08-4"></a>

- [RAD08-4](<Radiant_Issue_Ledger (fixed).md#rad08-4>).

<a id="rad08-5"></a>

#### RAD08-5

**Status (2026-11-11): PARTIAL.** See [RAD03-1](#rad03-1) for the fresh native
flex/grid repairs, failing-before/passing-after controls and remaining scope. The
historical stub description below predates the current helper implementation.

**Incomplete last-baseline alignment.** `layout_alignment.cpp:242` carries `// TODO: Implement proper last baseline calculation`; `align-items:last baseline` is not fully spec-correct.

<a id="rad08-6"></a>

- [RAD08-6](<Radiant_Issue_Ledger (fixed).md#rad08-6>).

<a id="rad08-7"></a>

#### RAD08-7

**Heavy per-phase logging in the hot path.** Every phase and many per-item steps emit `log_info`/`log_debug` (e.g. the "Phase N" and "ITERATIVE_FLEX" lines throughout `layout_flex.cpp`). On large trees this is a real log-volume and throughput concern; the logging is not compiled out in release. *Improvement:* gate the per-item lines behind a debug-only channel.

<a id="rad08-8"></a>

#### RAD08-8

**Monolithic functions mixing spec logic with fixups.** `layout_flex_container` (~1400 lines) and `layout_flex_container_with_nested_content` (~375 lines) interleave the §9 algorithm with the 4b/5b/5c/7b/7c/8b/9a/9.5 fixup branches, making the spec logic hard to test in isolation.

### RAD09 — CSS Grid Layout

<a id="rad09-1"></a>

#### RAD09-1

**Broad flow, ordering and logical-axis qualification remains open.** The focused dense/sparse and both-flow capacity controls do not establish every placement combination.

<a id="rad09-3"></a>

#### RAD09-3

**Pass-3 row-height reconciliation remains heuristic.** It repairs underestimated intrinsic heights from laid-out content and applies only to non-fixed tracks.

<a id="rad09-4"></a>

#### RAD09-4

**Placement counts still have a preservation guard.** Post-placement item endpoints cannot reconstruct the entire negative-implicit extent, so recomputation must preserve placement-established counts.

<a id="rad09-5"></a>

#### RAD09-5

**Independent paged grid remains unqualified.** Browsing and PNG/SVG/PDF controls do not establish fragmented-grid behavior.

Focused evidence: `css_grid_auto_fit`, `css_grid_named_lines`, `css_grid_capacity`, `css_grid_extent_policy`, `GridStorage.*`, retained-clone retirement and `RenderOutputParity.Grid*` cases. The active [CSS support plan](impl/Radiant_Impl_CSS_Support.md) records dated qualification and remaining P0–P2 work.

### RAD10 — Table Layout

<a id="rad10-1"></a>

#### RAD10-1

**The 501k single-file mega-function.** `layout_table.cpp` is ~9945 LOC and dominated by `table_auto_layout` (`layout_table.cpp:6142`), a single ~2900-line function whose phases are delimited only by "Step N" comments and share a large flat set of locals. This is the strongest extraction candidate in Radiant: caption handling, structure analysis (already partly factored), border-collapse resolution, auto width distribution, fixed distribution, and cell positioning could each become their own function/file. Cognitive load and merge-conflict surface are both high. *Improvement:* split each Step into a named function taking the `TableMetadata` explicitly.

<a id="rad10-2"></a>

#### RAD10-2

**Debt is structural, not marked.** A grep for `TODO`/`FIXME`/`HACK`/`XXX` across `layout_table.cpp`, `layout_table_metadata.cpp`, and `layout_table_caption.cpp` finds **none** — the tech-debt here is size and coupling, not tagged intent, so it will not surface in a marker sweep. The implementation comments themselves flag "simplified" building (`build_table_tree` at `layout_table.cpp:4463`) and a "Phase 3 optimization" without a corresponding removal plan.

<a id="rad10-3"></a>

#### RAD10-3

**Caption logic is duplicated across placement and inline handling.** Caption sizing/placement appears both in the dedicated `layout_table_caption.cpp` helpers (declared in `layout.hpp`) and inline within `table_auto_layout` (top at `layout_table.cpp:7964`/`8021`, bottom at `9175`), plus the caption-only fast path at `6203`. Keeping these in sync is manual.

<a id="rad10-4"></a>

#### RAD10-4

**Wide parallel-array `TableMetadata`.** The grid carries ~15 parallel arrays (`layout.hpp`) indexed by bare column/row integers; a bug that indexes one array with the wrong axis or an off-by-one against the `cols+1` edge array (`col_edge_max_border`) is silent. *Improvement:* a small accessor layer or a per-column struct-of-arrays would make misuse harder.

### RAD11 — Positioned, Floats, Multi-column, Lists & Counters

<a id="rad11-1"></a>

#### RAD11-1

**Multicol is explicitly simplified.** The header comment at `layout_multicol.cpp:2292` states it uses block-level distribution and does not fully fragment within block elements. Spanner top-margin collapsing is `max(prev_margin_bottom, spanner_margin_top)` and marked "simplified" in-code (`:2716`). *Improvement:* a real fragmentation cursor that can break inside a block, not only project inline lines after the fact.

<a id="rad11-2"></a>

#### RAD11-2

**Broader fixed-position contexts remain partial.** Initial viewport and transformed/contained ancestor selection have bounded sizing coverage, including percentage math and retained changes. General fixed positioning across scrolling, independent views and fragmented contexts is not fully qualified by those controls.

<a id="rad11-3"></a>

#### RAD11-3

**Fragile float coordinate math guarded by `#ifndef NDEBUG`.** `layout_float_element` keeps several debug-only coordinate variables (`layout_positioned.cpp:2162`+, and `cb_x`/`cb_y` in `calculate_absolute_position` at `:704`), indicating the BFC↔local coordinate conversion has been bug-prone. The 100-iteration cap (`:2268`) and the `0.001f` fit epsilon (`:2337`) are defensive constants, not principled bounds.

<a id="rad11-4"></a>

#### RAD11-4

**`layout_abs_block` is a 700-line mega-function with heavy re-correction.** The abspos path in `layout_positioned.cpp:1242`+ re-solves margins, x, and y multiple times (after image load, after shrink-to-fit, after auto height) with many mode guards (flex/grid/form/replaced). Correctness depends on these fix-ups running in exactly the right order; there is no assertion tying the final geometry to the constraint equation.

<a id="rad11-5"></a>

#### RAD11-5

**Image-load failure uses a placeholder box.** Failed abspos `<img>` loads fall back to a hard-coded 40×30 placeholder (`layout_positioned.cpp:1464`); marker image-load failure silently falls through to the numeric/bullet render (`render_list.cpp:70`).

<a id="rad11-6"></a>

#### RAD11-6

**Very few explicit TODO/FIXME markers.** The structural debt in this area shows up as `simplified`/`placeholder`/"Phase N" narration rather than tagged TODOs, so grep-based debt tracking under-counts it (notably the mega-functions and multicol).

### RAD12 — Paint IR, Display List & Backend Abstraction

<a id="rad12-1"></a>

#### RAD12-1

**Eager per-command lowering yields no batching benefit.** The gateway records one `PaintCmd`, lowers it, then clears the `PaintList` on every op (`render.hpp`, and every `paint_record_*` inline). PaintIR is a "thin layer above DisplayList" (`render.hpp`) that today adds a copy-and-switch per op with no cross-op optimisation. The overhead is unmeasured. *Improvement:* record whole subtrees into the `PaintList` and lower in bulk, enabling coalescing/culling before raster.

<a id="rad12-2"></a>

#### RAD12-2

**Triple-mirrored op and payload definitions.** `PaintOp` ↔ `DisplayOp` ↔ the `rdt_*` calls, and `Paint*` ↔ `Dl*` payload structs, mirror each other by hand (`render.hpp` vs `render.hpp`). Adding one primitive means edits in ~3 struct sites plus the lowering switch (`paint_ir.cpp:944`) and the replay switch (`display_list_replay.cpp:85`) — roughly 3 edit sites per primitive before it renders. *Improvement:* generate the mirrored trio from one op table.

<a id="rad12-3"></a>

#### RAD12-3

**~18 fragmented `display_list_*` files.** Recording, replay, storage, bounds, and the per-effect replay families are split across many small files (`display_list_replay_state.cpp` ~60 lines, `..._raster.cpp` ~50, `..._shadow.cpp` ~74, several 5–24-line headers). The split is by effect family, not by a clear layering rule, so navigation cost is high. *Improvement:* consolidate the tiny replay/record units by pipeline stage.

<a id="rad12-4"></a>

#### RAD12-4

**Semantic ops are not fully lowered.** Raster lowering ignores `PAINT_GLYPH_RUN` semantic runs and richer effects (`render.hpp`); SVG handles only text runs and opacity-only groups; `PAINT_SVG_SUBSCENE` is a Phase-F stub (`render.hpp`, `330-331`). Most effect groups round-trip through raster fallback rather than native vector constructs, so the "one paint algorithm for all backends" promise is only partly realised.

<a id="rad12-5"></a>

#### RAD12-5

**Tiling disabled whenever glyphs are present.** `render_output_replay_display_list` forces the serial path if `dl_contains_glyphs` is true (`render_output.cpp:308-309`). Because most real pages have text, the parallel tiled path rarely engages even though tile-local glyph rendering already exists (`tile_pool.cpp:385`). *Improvement:* let glyph ops render tile-locally in the parallel path and drop the page-level gate.

<a id="rad12-6"></a>

#### RAD12-6

**No `TODO/FIXME/HACK/XXX` markers, but the debt is structural.** The core files grep clean of inline debt markers; the migration is incremental-by-design (Phase C/D/F), so the incompleteness above is not flagged in-line and is easy to mistake for finished work.

### RAD13 — Render Walk, Painters & Export

<a id="rad13-1"></a>

#### RAD13-1

**CSS stacking contexts are still partial.** Signed positioned block layers belong to their nearest context, including `isolation:isolate`, with shared paint/hit order. Zero-level descendant lifting, floats, negative inline spans, blend-group isolation and complete flex/grid context rules still need the full CSS stacking-context algorithm.

<a id="rad13-2"></a>

- [RAD13-2](<Radiant_Issue_Ledger (fixed).md#rad13-2>).

<a id="rad13-3"></a>

#### RAD13-3

**Duplicated body-background propagation.** `render_output_canvas_background` (`render_output.cpp:205`) and the iframe branch of `render_embed_doc` (`render.cpp:122-156`) reimplement the same `<html>`-then-`<body>` background walk. *Improvement:* extract one shared helper.

<a id="rad13-4"></a>

#### RAD13-4

**File sprawl (~50 `render_*` files, ~26.8k LOC).** Painter logic, low-level pixel ops, and IO decode are interleaved; the largest are `render_svg_inline.cpp` (5633), `render_pdf.cpp` (2384), `render_background.cpp` (2107), `render_svg.cpp` (1978). No TODO/FIXME/HACK markers exist — the debt is structural (sprawl + duplication), not annotated.

<a id="rad13-5"></a>

#### RAD13-5

**The raster path is only partway onto the shared abstraction.** The header comments admit it: `render.hpp` describes richer raster block state being "migrated in staged slices," and `render.hpp:15-37` labels the PaintIR routing "Phase C." The raster backend deliberately leaves `render_bound`/effect/transform callbacks null and does that work inside `RenderContext` instead of the generic walker, so the two paths are not yet fully unified.

<a id="rad13-6"></a>

#### RAD13-6

**PDF/SVG re-do layout headlessly.** `render_html_to_pdf` (`render_pdf.cpp:2262`) reloads and re-lays-out the HTML rather than reusing an existing view tree, and cannot be driven from the in-memory `render_output_render_view_tree_to_target` (`render_output.cpp:497`). Two parallel entry conventions (`render_html_to_*` vs `render_output_*`) coexist. *Improvement:* allow vector export off a pre-laid view tree.

<a id="rad13-7"></a>

#### RAD13-7

**Effect-group need vs. capability is decided in two places.** The walker's `render_walk_block_effect_group` (`render_walk.cpp:67`) decides an effect group is *needed*; the backend caps decide it is *supported*. A backend that opens a group it cannot honor relies on flatten/fallback correctness elsewhere — there is no single assertion tying the two together.

### RAD14 — SVG, Vector Graphics & Diagram Layout

<a id="rad14-1"></a>

#### RAD14-1

**`render_svg_inline.cpp` is a 5633-line monolith.** It mixes SVG parsing, CSS style resolution, path/arc geometry, gradients/patterns, filters, clips/masks, and text glyph rendering in one file. *Improvement:* split along the natural seams — path parsing, filters/masks, gradients/patterns, and text — into separate TUs sharing `SvgInlineRenderContext`.

<a id="rad14-3"></a>

#### RAD14-3

**Backend caps parity gaps** (`rdt_vector_tvg.cpp:774` vs `rdt_vector_cg.mm:273`). Both leave `opacity_group`, `blend_modes`, `color_matrix_filters`, and `native_text_runs` = false, so those effects silently no-op or fall back to raster. The ThorVG native `gaussian_blur` cap is `__APPLE__`-only (`rdt_vector_tvg.cpp:787-791`) — the CSS-filter blur path degrades on Linux/Windows ThorVG builds (but inline-SVG `<feGaussianBlur>` does not).

<a id="rad14-4"></a>

- [RAD14-4](<Radiant_Issue_Ledger (fixed).md#rad14-4>).

<a id="rad14-5"></a>

#### RAD14-5

**Two directions named "SVG" are easy to confuse.** `render_svg_inline.cpp` is SVG *input* (element tree → pixels); `render_svg.cpp` is SVG *output* (view tree → SVG text). They share only the `PaintSvgSubscene` builder. *Improvement:* rename `render_svg.cpp` to something like `render_svg_output.cpp` to make the direction unmistakable.

<a id="rad14-6"></a>

#### RAD14-6

**Sparse explicit debt markers.** The only "unsupported" log in the inline renderer is the SVG path parser's unknown-command branch (`render_svg_inline.cpp:2263`); `rdt_picture_load` logs unsupported formats rather than falling back. Most debt here is structural (file size, algorithm scope) rather than tagged with TODO/FIXME.

<a id="rad14-7"></a>

#### RAD14-7

**Layer cache has no eviction.** `svg_layer_registry` keeps a captured layer until the document is torn down or the same element changes; an `<svg>` removed from the tree keeps its raster (bounded by the 256 MB per-document cap). A rotated, skewed, flipped or non-uniformly scaled `<svg>` never caches. *Improvement:* evict entries not painted for N frames; draw transformed layers through `DL_DRAW_IMAGE` with a premultiplied upload.

<a id="rad14-8"></a>

#### RAD14-8

**Manual lifetime bookkeeping around `RdtPicture`.** The path-keyed picture cache plus its mutex (`rdt_vector_tvg.cpp:101,286`) pair with hand-managed `Pool`/`Element` ownership on `KIND_SVG_DOM` pictures — a place to watch for leaks or races under concurrent load.

### RAD15 — Events & Input Dispatch

<a id="rad15-4r"></a>

#### RAD15-4R — Native event-simulation complexity

**Status:** UNVERIFIED — native portion of historical RAD15-4; ownership
reviewed 2026-11-09. The historical report calls out the size and complexity
of `radiant/event_sim.cpp`. Recheck the current implementation and whether
a native decomposition is justified. Lambda's `event.cpp` dispatcher remains
Lambda-owned and is retained in the [mixed historical record](Radiant_Issue_Ledger.md#rad15-4).

<a id="rad15-2"></a>

#### RAD15-2

**Zero-level stacking remains partial.** Signed positioned block descendants use their enclosing real stacking context and replay intervening ancestor clips. Absolute/fixed layers translate into the containing-block coordinate frame before hit testing, including their own border box. Zero-level descendant lifting and interleaving absolute/fixed children with in-flow step-8 boxes by tree order remain incomplete (RAD13). Ordinary block background/foreground and float phases share paint and reverse hit order. View-backed DOM `elementFromPoint` first delegates to the native engine target picker, with shadow and geometry fallbacks in `dom_document_element_from_point_native` for cases without an engine hit.

<a id="rad15-5"></a>

- [RAD15-5](<Radiant_Issue_Ledger (fixed).md#rad15-5>).

### RAD16 — Animation & Frame Scheduling

<a id="rad16-1"></a>

#### RAD16-1

**Transition support requires checking the supported property subset.** Rechecked
   2026-10-06: `css_transition_capture_before_change`,
   `css_transition_resolve_config` and `css_transition_resolve` now capture changes,
   read transition longhands and create scheduler instances. The earlier claim
   that transitions were unwired is obsolete. This does not establish support
   for every CSS property or PowerPoint effect; consult the animation value
   parser and the used-value consumer for the requested property.

<a id="rad16-2"></a>

- [RAD16-2](<Radiant_Issue_Ledger (fixed).md#rad16-2>).

<a id="rad16-3"></a>

#### RAD16-3

**`property_name_to_id` is a fixed strcmp table.** Only ~28 properties are animatable (`css_animation.cpp:122`); anything else in a keyframe is silently ignored. New animatable properties require editing three coupled tables (`property_name_to_id`, `property_value_type`, `apply_animated_value`).

<a id="rad16-4"></a>

#### RAD16-4

**Heavy platform `#ifdef` in `frame_clock.cpp`.** Every function branches on `__APPLE__`/`_WIN32`/`__linux__` (`frame_clock.cpp:27`, `226`, `316`, `384`) rather than dispatching through a small vtable; each new clock operation must re-branch in several places. *Improvement:* a `RadiantFrameClockOps` function-pointer table per platform.

<a id="rad16-5"></a>

- [RAD16-5](<Radiant_Issue_Ledger (fixed).md#rad16-5>).

<a id="rad16-6"></a>

#### RAD16-6

**Registry lookup and dedup are linear.** `keyframe_registry_find` (`css_animation.cpp:561`) and the "already running?" scan in `css_animation_resolve` (`css_animation.cpp:1022`) are O(n) over all keyframes / all active animations, run per element during layout. Fine at current scale, a hazard for animation-heavy pages.

<a id="rad16-7"></a>

#### RAD16-7

**Transform-dirty bounds are approximate.** `css_animation_tick` inflates bounds by a generous `max(w,h)` margin for scale/rotate (`css_animation.cpp:859`) rather than computing the true transformed AABB, over-painting to stay correct. Acceptable, but wasteful for large elements.

### RAD17 — Interaction State: DocState, State Machine & Schema

<a id="rad17-1"></a>

#### RAD17-1

**Status (2026-11-11): PARTIAL — batch module extracted.** `state_batch.cpp`
owns begin/end batching and presentation/dirty synchronization. The existing
internal header exposes shared synchronization helpers instead of copying them.
The state suite covers interleaved document batches and native state invariants.
Other state-store families and deferred callbacks remain open. See the
[implementation record](impl/Radiant_Impl_Ledger_Repairs.md). Authority:
**D5.5 / D9.1.1v4 / D9.1.3v2**.

**`state_store.cpp` remains a large monolith** spanning interned names, dump, lifecycle, generic state, constraints, dropdowns, immutable mode, batching, dirty/reflow, caret/selection presentation, focus, and clipboard. It is the prime split candidate in this subsystem — a natural decomposition follows the function-prefix families (`doc_state_*`, `form_*`, `view_state_*`, `selection_*`, `caret_*`, `state_dump_*`).

<a id="rad17-2"></a>

#### RAD17-2

**Immutable / COW mode is an unimplemented stub.** `state_set_immutable` shallow-copies and re-hashes rather than structurally sharing — `// TODO: implement proper HAMT for structural sharing` (`state_store.cpp:5142`). Related dangling TODOs: `state_end_batch` never fires deferred callbacks (`// TODO: trigger deferred callbacks`, `:5284`), and the dump element builder has a `// TODO: add attributes if needed` (`:8338`). Undo/time-travel cannot rely on this mode today.

<a id="rad17-3"></a>

- [RAD17-3](<Radiant_Issue_Ledger (fixed).md#rad17-3>).

<a id="rad17-5"></a>

#### RAD17-5

**Cached `View*`/`DomElement*` pointers require prune-after-reflow.** Numerous `DocState` fields (`hover_target`, `active_target`, `cursor->view`, drag/drop views, editing surfaces) hold raw pointers that go stale on relayout and depend on `state_store_prune_after_reflow` being called at the right time.

### RAD18 — Editing, Selection & DOM Ranges

<a id="rad18-1"></a>

#### RAD18-1

**`dom_range.cpp` is a 4551-line monolith.** It mixes lifecycle, boundary comparison, mutation envelopes, extract/clone/surround, selectAll/triple-click, `Selection.modify`, and stringification. It is the single largest file in the area and the prime split candidate — a natural fission is (a) range/selection core + comparison, (b) mutation envelopes + mutators, (c) modify/selectAll/word-breaking, (d) stringification.

<a id="rad18-2"></a>

#### RAD18-2

**Text-control target range is a StaticRange hack.** `compute_text_control_target_ranges` synthesizes a boundary over the control *element* carrying UTF-16 `selectionStart`/`End`, until form values can be promoted to concrete DOM text nodes. The document-vs-text-control distinction remains intentionally unified by the `EditingSelection` facade.

<a id="rad18-3"></a>

#### RAD18-3

**Incomplete OS clipboard backends.** macOS uses NSPasteboard for text, HTML, images and single image-file copies. Win32/X11 rich-MIME adapters remain unimplemented; the fallback GLFW backend is plain-text-only, and `clipboard_store_sanitize` is a near-no-op.

<a id="rad18-4"></a>

#### RAD18-4

**Pattern-regex validation TODO (F5).** Form constraint validation `te_validate` cannot enforce `pattern="..."` without a lazy-compiled regex. (Detail belongs to RAD19, noted here because it is a spec-coverage gap in the editing surface.)

<a id="rad18-5"></a>

#### RAD18-5

**`Selection.modify` fidelity.** All accepted argument families are implemented, including bidi-aware visual left/right character and word movement. Word segmentation remains a simple alphanumeric classifier, while sentence and some layout-free line fallbacks are structural approximations; locale-sensitive and complex-script results can diverge from a browser. This is a bounded Selection-API conformance limit, not a general contenteditable-support gap.

### RAD19 — Form Controls & Text-Control Editing

<a id="rad19-1"></a>

#### RAD19-1

**Text-control target range is a StaticRange hack.** `editing_compute_target_ranges` builds a synthetic boundary over the control element with UTF-16 offsets instead of real DOM text nodes (`event.hpp`). It stays until form values become concrete DOM text — the same "E0 promotion" blocker referenced in RAD18. *Improvement:* promote `<input>`/`<textarea>` values to a shadow text node so DOM Range and form offsets unify.

<a id="rad19-2"></a>

#### RAD19-2

**`TODO(F5)`: `pattern="…"` validation is unimplemented.** `te_validate` (`text_edit.cpp:641`) checks number/email/url but has an explicit TODO for `pattern` because it "needs lazy-compiled regex". Until then a `pattern` attribute is silently ignored — a control with only a `pattern` constraint always reports `:valid`.

<a id="rad19-3"></a>

#### RAD19-3

**Projection-cache consumers remain.** Canonical `EditingSelection` (StateStore), the HTML-facing `form->selection_start/end` mirror, and caret/anchor projection cache are reconciled by hand in `tc_sync_selection_to_form` (`text_control.cpp:404`). The remaining cleanup is to port projection consumers directly to `EditingSelection` / DomRange geometry.

<a id="rad19-4"></a>

#### RAD19-4

**IME edge cases.** `te_ime_commit_prepare` (`text_edit.cpp:822`) accepts a composition *session* on readonly/disabled controls but rejects the commit, and orphaned compositions are silently dropped in `te_ime_begin` (`text_edit.cpp:775`). There is no reconversion / candidate-window geometry reported back to the OS, and the preedit caret is a single codepoint offset with no segment/clause underlining.

<a id="rad19-5"></a>

#### RAD19-5

**Paste sanitization is minimal.** `te_prepare_paste_replacement` handles newline normalization and `maxlength` truncation only (`text_edit.cpp:649`); it does not strip control characters, apply `inputmode`/type-specific filtering (e.g. non-numeric into `type=number`), or coordinate with the rich clipboard MIME store.

<a id="rad19-6"></a>

#### RAD19-6

**Constraint validation is a v1 subset.** Beyond the missing `pattern` (item 2), `te_validate` (`text_edit.cpp:602`) does not implement `min`/`max`/`step` for number/range, `minlength`, or the full email/url grammar — the `te_value_is_*` helpers are approximate.

<a id="rad19-7"></a>

#### RAD19-7

**Chrome-matched magic numbers.** `FormDefaults` (`view.hpp`) hard-codes dozens of pixel constants (153×21 text input, 57px select, per-type date/time widths in `layout_form.cpp:24-`) reverse-engineered from Chrome. These drift as Chrome's UA stylesheet changes and are unverified across platforms/DPI beyond the tuned cases.

<a id="rad19-8"></a>

#### RAD19-8

**Legacy dual dispatch surface.** `te_replace_byte_range` vs `te_replace_byte_range_no_events`, and `te_ime_commit` vs the `_prepare`/`_finish` split, exist purely to straddle "legacy caller without EventContext" and "unified dispatcher". The legacy variants (`text_edit.cpp:278`, `806`) are latent duplication once all callers own an `EventContext`.

<a id="rad19-9"></a>

#### RAD19-9

**Flat SVG and PDF omit textarea values.** Open, 2026-11-18. PNG paint reads
initial or current control text, including a script-assigned empty value.
The flat vector walk has no form-content callback. In the focused probe,
SVG's textarea group contained no text paint and the rasterized PDF was
identical for empty and populated textareas. Shared paint ownership follows
**D5.5.1** and control policy follows **D10.2.1**. The missing vector consumer
does not block independent HTML work.

### RAD20 — Application Shell, Browsing & Document Loading

<a id="rad20-1"></a>

#### RAD20-1

**Networking is borrowed, not owned, by the session — a lifetime coupling.** `BrowsingSession` holds `thread_pool`/`file_cache` pointers it explicitly does not own (`browsing_session.cpp:98`), and per-navigation `radiant_cleanup_network_support(old_doc)` is called from `session_navigate`/`go_back`/`go_forward` (`browsing_session.cpp:156`, `207`, `254`). The window creates network support only for the *initial* document if it is HTTP (`window.cpp:1100`); a later same-window navigation to an HTTP page relies on `show_html_doc`→`load_html_doc` re-initializing it, so ownership of network lifetime is split across three files. *Improvement:* give the session an explicit owned network context, or document the ownership contract in one place.

<a id="rad20-2"></a>

#### RAD20-2

**History cap is a hard-coded 100 with silent eviction.** `BROWSE_HISTORY_MAX` (`radiant.hpp`) drops the oldest entry by `memmove` (`browsing_session.cpp:131`) with no user signal. `history_ensure_capacity` also silently returns on `mem_realloc` failure (`browsing_session.cpp:39`), after which the push can fail and `session_navigate` returns `NULL` (`browsing_session.cpp:141`). *Improvement:* make the cap configurable and surface allocation failure distinctly from a load failure.

<a id="rad20-3"></a>

#### RAD20-3

**Windowless-by-default is a platform workaround, not a clean abstraction.** The headless branch avoids creating a window because GLFW's hidden-window path can deadlock in macOS LaunchServices (`ui_context.cpp:76-108`), reachable only via `LAMBDA_HEADLESS_GLFW_WINDOW`. Downstream code must therefore tolerate `uicon->window == NULL` everywhere (e.g. `update_window_title` early-returns, `ui_context_cleanup` guards `glfwTerminate`). A missed guard crashes only on the windowless path. *Improvement:* a null-object window wrapper would remove the scattered `if (window)` checks.

<a id="rad20-4"></a>

#### RAD20-4

**Single global `ui_context` blocks multi-document/tabbed browsing.** GLFW callbacks reference the global directly, so there is no per-window state; the shell cannot host more than one document at a time.

<a id="rad20-5"></a>

#### RAD20-5

**Dispatch responsibility is split and the old doc was stale.** Extension dispatch is in `load_html_doc_no_redirect` (`cmd_layout.cpp:3522`) while `load_html_doc` (`cmd_layout.cpp:3597`) only handles redirects — the former `Radiant_View_Design.md` claimed dispatch happened in `load_html_doc()`. `detect_doc_format` in `window.cpp` keeps a second extension list, now used only to name the format and to refuse unsupported extensions; every supported format loads through `load_html_doc`. *Improvement:* one extension→loader table shared by both.

<a id="rad20-6"></a>

#### RAD20-6

**`render()` does network draining and title management inline.** `render` (`window.cpp:685`) both flushes network completions and owns the loading-progress/partial-load window title (`window.cpp:704-757`), overlapping with the main loop's own `resource_manager_flush_layout_updates`. The double-flush is harmless but the responsibility split (loop vs. render) makes the load-completion path hard to follow.

### RAD22 — Media & Embedded Webview

<a id="rad22-2"></a>

#### RAD22-2

**No Linux or Windows video.** `rdt_video_stub.cpp:3-4` documents that FFmpeg and Media Foundation backends are "not yet implemented, using stubs"; every getter returns 0/false and playback never leaves `RDT_VIDEO_STATE_IDLE`. Video works on macOS only; elsewhere the page shows a permanent play button.

<a id="rad22-3"></a>

#### RAD22-3

**Remote media requires a complete downloaded file; progressive/range playback is unavailable.** Reverified 2026-11-17: `radiant_media_prepare` requests HTTP media through the input-owned resource manager, then copies ready cached bytes to an immutable local decoder file. Fast local-server MP4 controls load successfully, so HTTP media is no longer categorically unsupported. `rdt_video_open_file` still receives a complete local file; it does not consume a partially available HTTP stream. RI-121 fixes source-request ordering before the unchanged snapshot wait, while larger transfers may remain unavailable at capture. Going's 7 MB source decodes locally but its normal snapshot still lacks the video. Progressive/range playback requires a larger input/media integration and is not implemented here (**D9.1.2v2 / D10.1.7**).

<a id="rad22-4"></a>

#### RAD22-4

**Layer-mode snapshot is forced synchronous by spinning the run loop.** `webview_layer_mac.mm:379-385` and `webview_layer_linux.cpp:453` block up to 500 ms pumping the platform event loop to turn an async snapshot into a synchronous call for the render pipeline. This is fragile (nested run-loop reentrancy, and a hard 500 ms stall on a slow snapshot) and is the main reason layer mode is still "Stage 2".

<a id="rad22-5"></a>

#### RAD22-5

**AVFoundation frame path is per-frame `AVAssetImageGenerator`, not a video output.** The workaround for error -11821 (`rdt_video_avf.mm:265-271`) copies a `CGImage` every frame via `CGBitmapContextCreate` — noticeably less efficient than a zero-copy `AVPlayerItemVideoOutput`/`IOSurface` path.

<a id="rad22-6"></a>

- [RAD22-6](<Radiant_Issue_Ledger (fixed).md#rad22-6>).

<a id="rad22-7"></a>

- [RAD22-7](<Radiant_Issue_Ledger (fixed).md#rad22-7>).

<a id="rad22-8"></a>

#### RAD22-8

**No macOS child-mode compositing / z-order.** Child-window webviews are always-on-top overlays with no relationship to document z-order; only layer mode composites correctly. Mixing a child webview under other content is impossible.

<a id="rad22-9"></a>

- [RAD22-9](<Radiant_Issue_Ledger (fixed).md#rad22-9>).

## Fixed issues

<a id="ri-013"></a>

- [RI-013 — Downloaded CSS overrides later document styles](<Radiant_Issue_Ledger (fixed).md#ri-013>).

<a id="ri-014"></a>

- [RI-014 — A zero-height body loses its canvas background image](<Radiant_Issue_Ledger (fixed).md#ri-014>).

<a id="ri-015"></a>

- [RI-015 — Authored inline buttons bypass atomic button layout](<Radiant_Issue_Ledger (fixed).md#ri-015>).

<a id="ri-003"></a>

- [RI-003 — Startup fragment navigation is followed as a document redirect](<Radiant_Issue_Ledger (fixed).md#ri-003>).

<a id="ri-004-layout"></a>

- [RI-004 — Dynamic baseline exposes native anonymous-table and match-parent mismatches](<Radiant_Issue_Ledger (fixed).md#ri-004>).

<a id="ri-001"></a>

- [RI-001 — HTML/CSS integration cases skip missing fixtures](<Radiant_Issue_Ledger (fixed).md#ri-001>).

<a id="ri-002"></a>

- [RI-002 — DOM integration cases skip unsupported standalone node creation](<Radiant_Issue_Ledger (fixed).md#ri-002>).

<a id="ri-005"></a>

- [RI-005 — Native static layout baseline needs execution qualification](<Radiant_Issue_Ledger (fixed).md#ri-005>).

<a id="ri-006"></a>

- [RI-006 — Property providers still live outside the unified Radiant archive](<Radiant_Issue_Ledger (fixed).md#ri-006>).

<a id="ri-007"></a>

- [RI-007 — Column flex measures a nested wrapping row too short](<Radiant_Issue_Ledger (fixed).md#ri-007>).

<a id="ri-008"></a>

- [RI-008 — font face source includes a missing alloca header](<Radiant_Issue_Ledger (fixed).md#ri-008>).

<a id="ri-009"></a>

- [RI-009 — Required standalone layout lint target is absent](<Radiant_Issue_Ledger (fixed).md#ri-009>).
