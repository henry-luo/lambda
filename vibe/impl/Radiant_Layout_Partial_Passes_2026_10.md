# Radiant layout partial passes — October 2026

Status: round 11 table-cell inline measurement passes native and paged-media
checks; the corrected leading calculation is undergoing aggregate qualification,
2026-10-19. Round 10 remains fully qualified below.
The input audit remains in round 8 below.

## Scope and initial gate

Improve recorded partial layout matches one root cause at a time. Keep browser
references, tolerances and scored baselines unchanged. The initial
`make test-layout-baseline` result is 2,945 full passes, 349 partial passes,
zero failures across 3,294 tests.

## Hanging whitespace and justification

`pre-wrap-014` leaves a 10px red strip: the green `X X` span measures 70px in
Radiant and 80px in Chromium. Both engines agree on the 80px container, the
soft wrap and the subsequent line. Browser and Radiant PNGs are under
`temp/layout-analysis/`.

Authority: [CSS Text 3 §4.1.2](https://www.w3.org/TR/css-text-3/#white-space-phase-2)
excludes hanging whitespace from text alignment. This is CSS conformance work;
no Lambda semantic or design ruling changes.

Root cause: the layout opportunity counter counts the preserved space after
the green span even though line advance already excludes it. The 20px of
available width is divided between two spaces, rather than the one internal
space. A whitespace-only trailing rectangle also loses its `hanging_trim`
marker because `line_break` only records it on rectangles containing visible
content.

Design: retain the existing hanging marker for every affected rectangle and
give wholly hanging rectangles zero shared justification opportunities. Keep
source byte ranges and CSSOM geometry intact. The same counter serves layout
and raster/SVG paint, so the decision belongs there. Preserved spaces that do
not hang, including `break-spaces`, continue to participate.

Verification: focused WPT comparisons, browser/Radiant PNGs, a native contract
test covering whitespace-only fragments and non-hanging controls,
the layout baseline, the Radiant baseline and the Radiant cast lint.

Scope boundary: mixed visible/hanging rectangles retain the existing counter
behavior. Excluding their suffix exposed a separate preserved-tab justification
defect in `text-align-justify-tabs-003` (text 88.9% to 77.8%). Fresh Chromium
rectangles confirm the recorded reference, including expanded preserved tabs.
A complete treatment must reconcile tab-stop alignment and per-fragment paint
spacing under CSS Text 3 §6.1; it is left for a separate root-cause investigation.

## Preserved tabs and earlier wrap opportunities

`break-spaces-tab-005` is one 20px line shorter in Radiant. At an overflowing
tab, Radiant keeps `X` on the previous line despite an earlier `<wbr>`;
Chromium wraps at that opportunity first. `break-spaces-tab-006` has the same
defect with a mixture of tabs and ordinary spaces. PNGs show red at the start
and bottom of the rectangle.

Authority: [CSS Text 3 §5](https://www.w3.org/TR/css-text-3/#line-breaking)
requires wrapping at an available soft opportunity to minimize overflow;
§3 permits breaks after preserved spaces, without treating those spaces as
discardable or hanging.

Root cause: the overflowing preserved-space branch only uses a saved break
inside the current text node. A prior-node break reaches the unbreakable-run
fallback, which commits the overflowing tab on the current line. The ordinary
text overflow branch already supports moving an uncommitted text fragment to
the next line; tab widths must be remeasured there from the new line origin.

Design: share the existing discard/rewind/new-line operation and use it when
an overflowing `break-spaces` fragment has an earlier cross-node opportunity.
Retain the existing fallback when no earlier break exists. Verify both WPT
fixtures plus the full recorded baseline to exercise adjacent break modes.

## Verification and remaining diagnoses

`make test-layout-baseline`: **2,949 full passes, 345 partial passes, zero
failures**, with the original 3,294-test inventory and unchanged baselines.
`pre-wrap-014`, `break-spaces-tab-005` and `break-spaces-tab-006` now match at
100% for every compared metric. The Markdown suite gains one full pass as well.
The three native text-query tests and `no-int-cast-radiant` lint pass.

The full Radiant gate also exits successfully: 5,427 full passes, 349 partial
passes, one skip and zero failures across 5,777 tests. Run:
`LAMBDA_TEST_MAX_CONCURRENT=4 make test-radiant-baseline`. This run used normal
macOS service access outside the execution sandbox. The initial sandboxed run
was stopped after AppKit service denials stalled Scene3D/view cases; the two
isolated Scene3D cases passed immediately with service access restored, and the
photo-editor JIT case passed in isolation. The successful full run retains all
tests, assertions and timeout settings.

The original tab PNG still shows the overlaid reference obscuring foreground
ink. A diagnostic copy hiding that absolute reference leaves identical
foreground geometry in Chromium and Radiant. The checked-in WPT fixture is
unchanged. Painting the overlay requires its own investigation; the layout
fix does not establish a cause in the paint implementation.

Two further partials were measured and left unchanged:

- `pre-line-with-space-and-newline`: four newline-owning inline boxes move
  200px left and 25px down and lose their 25px height; four preserved newline
  text fragments are absent. Surrounding visible text agrees. Investigate
  collapsed-whitespace classification and forced-break fragment ownership.
- `balance-orphans-widows-000`: four `br` boxes retain a 100px vertical offset
  after column fragmentation, while text comparison remains 100%. Investigate
  fragment projection of forced breaks before changing column balancing.

`column-height-015`, although recorded with a partial score, already matches
fully in the current checkout and needs no change.

Logs, browser rectangles, PNGs and diagnostic scripts remain under
`temp/layout-analysis/`. The native HTML fixture covers hanging justification,
cross-node tab and ordinary-space breaks, and an unbreakable control.

## Round 2: preserved segment-break classification

Initial gate: 2,949 full passes, 345 partial passes, zero failures. The target
is `pre-line-with-space-and-newline`: images agree on the visible 200px square,
but four inline boxes owning preserved segment breaks lose their height and
move from the preceding line end to the next line start. Their newline range
fragments are missing as well.

Authority: [CSS Text 3 §4.1](https://www.w3.org/TR/css-text-3/#white-space-processing)
collapses spaces and tabs in `pre-line` while preserving segment breaks as
forced line breaks. A zero horizontal advance does not make that break
collapsible. No Lambda formal ruling changes.

Root cause: `text_is_all_collapsible_space` and the geometry whitespace
classifiers treat every HTML-space character as collapsible under `pre-line`.
The inline classifier also reads a wrapper's cached whitespace property rather
than the text's resolved inheritance and specified style. The existing
`inline_edge_whitespace_can_collapse` already distinguishes preserved breaks.

Design: promote that existing classifier to the layout module header and
extend it to bounded source ranges. Reuse it for inline participation and
text-fragment geometry so both preserve segment breaks and use the same style
resolution. Retain existing zero-font and fragmentation geometry policies.
Verify the real inline boxes with the native fixture, the focused WPT case,
the recorded layout inventory, Radiant baseline and layout lint.

### Escaped carriage returns

`control-chars-00D` has a 510px blue block in Radiant and a 90px block in
Chromium; false forced breaks displace later content by as much as 450px.
Its source encodes U+000D with character references, so HTML newline
normalization does not turn those characters into LF.

Authority: CSS Text 3 §4 explicitly treats DOM carriage returns as U+0020 in
every respect. The root cause is that the text loop uses CR and LF together
as segment breaks in preserved modes, in leading-space removal and in
segment-break transformation. Geometry and intrinsic forced-break detection
repeat that assumption.

Probe design: use one LF segment-break predicate and normalize CR to a space at
codepoint interpretation boundaries, retaining the original DOM bytes and
source offsets. Apply the same distinction to the shared whitespace
classifier, justification, intrinsic sizing and segment-break geometry.
Add native `pre` and `pre-line` controls with an escaped CR between glyphs;
they must retain a space advance and occupy one line.

Chromium's own blue/orange control arrangement differs in the freshly captured
PNG. The fix follows the explicit CSS rule; residual differences with the
browser reference remain unresolved.

The probe passed native `pre`/`pre-line` controls and reduced the page's maximum
vertical error from 450px to 120px, but the reference text score fell from 38.1%
to 33.3%. It was reverted in full so it does not regress the recorded baseline.
The patches and before/after measurements are retained under
`temp/layout-analysis/round2/`; carriage-return interpretation remains an open
investigation. The retained classifier uses CSS whitespace rather than HTML
syntax whitespace: LF remains a preserved break in `pre-line`, CR classifies
as a space, and form feed is a non-collapsible control. The runtime's CR-to-space
interpretation probe is not retained.

### Round 2 validation

`pre-line-with-space-and-newline` improves from elements 77.8%, spans 100% and
text 73.3% to **100% on all three metrics**. Native boxes owning LF now have
their 20px height in both combined and separated-whitespace cases; the test
failed with zero height before the fix. The CSS classifier's native contract
also covers preserved and collapsed modes, CR, form feed and bounded ranges.

`make test-layout-baseline` finishes with **2,950 full passes, 344 partial
passes, zero failures**, across the unchanged 3,294-test inventory. All four
native text-query tests and the Radiant cast lint pass. The CR probe is deferred
and the measured multicol `br` projection issue remains unchanged. Browser and
Radiant PNGs, rectangle data and comparison logs are in
`temp/layout-analysis/round2/` (browser captures also use the parent directory).

Full Radiant baseline passes with four workers and normal macOS service access:
**5,446 full passes, 348 partial passes, one skip and zero failures** across
5,795 tests. Command: `LAMBDA_TEST_MAX_CONCURRENT=4 make test-radiant-baseline`.

## Round 3: table intrinsic widths and preserved lines

Initial gate: 2,950 full passes, 344 partial passes, zero failures. Seven
`ws-break-spaces-applies-to-*` cases (006–011 and 014) share the same defect:
the reference table is about 158px wide instead of 81px, while its two rendered
text lines already match. Browser/Radiant PNGs confirm the excess empty width.

Authority: [CSS 2.2 §17.5.2.2](https://www.w3.org/TR/CSS22/tables.html#auto-table-layout)
measures cells without optional wrapping, and
[CSS Sizing 3 §2](https://www.w3.org/TR/css-sizing-3/#max-content)
retains forced breaks at max-content. `pre` suppresses soft opportunities but
preserves LF under [CSS Text 3 §3](https://www.w3.org/TR/css-text-3/#white-space-property).
No Lambda formal ruling changes.

Root cause: `measure_cell_widths` always passes `normal` to its direct-text
measurement, so preserved LF adds a space advance and concatenates the lines.
The cell-level `pre` policy then promotes this incorrect max-content to its
minimum column contribution. Inline children also lose their existing
first/last forced-line width metadata when added to the cell's inline run.

Design: extract the existing preserved-line measurement from general intrinsic
sizing into one shared helper. Preserve its tab origins, per-line vertical
measurement and first/last line metadata. Pass the cell's inherited whitespace
and wrap policy to it, and flush cell inline runs at preserved breaks, retaining
the last line for subsequent siblings. Cover direct text, split inline text,
soft-wrap controls and preserved tabs in a native end-to-end fixture before
running all recorded layout and Radiant gates.

The reversed-counter candidates are deferred: fresh Chromium captures reject
`reversed(foo)` and produce negative counters, whereas Radiant's dynamic initial
value follows CSS Lists 3 §4.4.2. Changing that behavior to match unsupported
browser syntax would violate the requested CSS conformance.

### Round 3 validation

Seven additional partials now fully match, with no remaining comparison
differences in the focused reports:

| Test suffix (`ws-break-spaces-applies-to-`) | Elements before | Elements after |
| --- | ---: | ---: |
| 006 | 60.0% | 100.0% |
| 007 | 60.0% | 100.0% |
| 008 | 63.6% | 100.0% |
| 009 | 63.6% | 100.0% |
| 010 | 63.6% | 100.0% |
| 011 | 60.0% | 100.0% |
| 014 | 60.0% | 100.0% |

Text and span metrics remain at 100% for all seven cases. The representative
reference table shrinks from 158.1px to 81.1px, matching Chromium's 81.06px;
its rendered text remains in the same two lines.

The native table fixture checks nine cases with Ahem: direct preserved LF,
inline children spanning LF, a break between inline siblings, preserved tabs,
pre-wrap and break-spaces controls, an atomic inline, first-line indentation,
and a wider later line that must not inherit that indentation. Chromium's
fresh dimensions agree with every expectation. All five native text-query
tests and `no-int-cast-radiant` lint pass.

`make test-layout-baseline`: **2,957 full passes, 337 partial passes, zero
failures** across the unchanged 3,294-test inventory. References, baseline
scores, thresholds and skips remain unchanged.

`LAMBDA_TEST_MAX_CONCURRENT=4 make test-radiant-baseline`: **5,454 full passes,
341 partial passes, one skip and zero regressions**, across 5,796 tests. The
inventory adds the native table regression test. The prior newline and hanging
whitespace controls also pass.

Browser rectangles and PNGs are under `temp/layout-analysis/`; before/after
reports, the seven per-test results, native output and both gate logs are under
`temp/layout-analysis/round3/`. No fix is applied to the deferred reversed-counter
or carriage-return cases.

## Round 4: contextual shaping across inline boundaries

Initial gate: 2,957 full passes, 337 partials, zero failures. The goal is at
least twenty additional full matches, without changing references or scores.

`shaping-000` has three isolated 72.5px Arabic glyph advances in Radiant; the
browser retains joined advances of about 60.59px, 49.64px and 57.19px across the
plain span. Radiant already measures the unsplit reference run correctly.

Authority: [CSS Text 3 §7.3](https://www.w3.org/TR/css-text-3/#boundary-shaping)
requires shaping to continue when inline formatting does not change glyphs.
Nonzero inline-axis margin/border/padding, non-initial vertical alignment and
bidi isolation break shaping. No Lambda formal ruling changes.

Root cause: `measure_shaped_bidi_run` rejects inline text and neighboring RTL
text. Each remaining character is measured without its shaping context.
The Latin fast path similarly shapes one DOM text node at a time, so an
unformatted span can suppress a ligature.

Design: add a font range-measurement API whose input retains surrounding UTF-8
source text and whose range uses original byte offsets. The CoreText backend
measures glyph clusters belonging to the requested source range in the fully
shaped run; unsupported backends retain
their existing independent-run behavior. Gather adjacent word text through
eligible inline boundaries, retaining CSS-required shaping breaks. Use the
same context collector for contextual run measurement and its regression
coverage; do not insert synthetic joining characters or mutate DOM source.
Verify real range advances, inline wrapper geometry, boundary controls and
all recorded layout/Radiant gates. Raster glyph substitution is a separate
existing issue: the unsplit reference also paints unshaped glyphs despite its
correct measured width, so layout measurement alone does not establish a
raster fix.


### Additional causes and boundaries

The shared `is_space(char)` predicate narrowed decoded Unicode codepoints:
U+200D became ASCII CR and prematurely ended a shaped run. Its codepoint API
now retains all 32 bits; the byte-only string scan adapts its predicate without
changing the whitespace set. ZWJ/ZWNJ remain in the shaped source and do not
become kerning endpoints.

A glyph cluster crossing inline boundaries belongs to its first source
character. The range API uses CoreText run string indices and glyph advances,
so a later one-character node can have zero advance without losing its source
or font box. This allocation is permitted by
[CSS Text 3 §1.4](https://www.w3.org/TR/css-text-3/#characters).

For vertical multicol breaks, a zero inline coordinate in an RTL paragraph
already denotes the line's inline-end edge. Adding the parent column pitch
again displaces that break. Use paragraph direction for this correction,
independent of the nested inline's bidi direction. Opposing-direction browser
controls expose edges at 0px for the RTL paragraph and 80px for the LTR one.
Caret subdivision was unsuitable for these source-owned inline fragments.

Only the separating physical edge can stop shaping. One-sided padding leaves
the other edge eligible; an empty decorated inline still separates its
neighbors at both edges. CSS-wide keywords and HTML isolation defaults are
resolved before eligibility is decided. The existing intrinsic inline-edge
helper is promoted and reused. The context collector lives in
`layout_text_context.cpp`; the small `text_metrics.cpp` utility retains its
independent linkage for isolated rendering unit tests.

Shaped runs use the glyph loader's selected fallback face and retain its
normal-line contribution through the shared scalar/shaped metrics helper.
Authority: [CSS 2.2 §10.8.1](https://www.w3.org/TR/CSS22/visudet.html#leading).
This restores the existing full match of `boundary-shaping-010`; it is not
counted among the twenty additional passes.

`word-spacing-002` exposed a separate resolver defect: function values were
accepted for letter-spacing but ignored for word-spacing. Both properties now
use the existing length resolver for `calc()`. Authority:
[CSS Values 4 §10.1](https://www.w3.org/TR/css-values-4/#calc-func) and
[CSS Text 3 §7.1](https://www.w3.org/TR/css-text-3/#word-spacing-property).
The 40px sum and 20px product controls now match Chromium's space advances.

### Round 4 validation

Twenty **additional** partials now have 100% elements, spans and text, with
zero differences in isolated reports:

- `shaping-000` through `shaping-008` (nine cases).
- `shaping-014`, `shaping-016`, `shaping-017`, `shaping-018` (four cases).
- `shaping-join-001`, `shaping-join-002` (two cases).
- `boundary-shaping-001`, `boundary-shaping-003`, `boundary-shaping-004`,
  `boundary-shaping-005` (four cases).
- `word-spacing-002`.

Native coverage adds Unicode/ASCII alias controls, real Arabic presentation-form
advances, explicit joining controls, color/size changes, required CSS boundaries,
UTF-8 range validation, Latin cluster ownership, one-sided and empty decorations,
and computed spacing expressions. All eight text-query tests pass. The Radiant
integer-cast lint and `git diff --check` pass.

`make test-layout-baseline`: **2,977 full passes, 317 partial passes, zero
failures**, across the unchanged 3,294 tests. The original references, scores,
tolerances and skipped entries are unchanged.

`LAMBDA_TEST_MAX_CONCURRENT=4 make test-radiant-baseline`: **5,477 full passes,
321 partial passes, one skip and zero regressions**, across 5,799 tests. Three
new native regression cases account for the inventory increase.

Logs, per-test before/after transitions, isolated final reports and representative
Radiant PNGs are under `temp/layout-analysis/round4/`; browser captures are in
its parent directory. Raster substitution of joined/ligature glyphs remains
unfixed: the layout geometry now matches, but the existing per-codepoint painter
still needs a shaped-glyph path. No pixel-perfect shaping claim is made.

## Round 5: thirty additional full matches

Starting gate: **2,977 full passes, 317 partial passes, zero failures** across
3,294 cases. These thirty improvements exclude all earlier rounds.

### Fixed row sizing

`column-height-012` has an 80px child split into two 40px rows with a 20px
gutter. Its stored fragments and painted union correctly end at 100px, but
the container reports 120px. Fixed balanced-row finalization divides the
already-gapped physical extent by the row height, counting the gutter as
content for a third row. A different formula is selected only when no
`BlockProp` exists; adding an unrelated block property changes row geometry.

Use the row pitch (height plus gap) for all fixed balanced rows, counting no
gutter after the final row. Reuse `multicol_fragment_grid_union_height` to
compute the resulting extent. Verify both default and authored column counts,
balance and auto-fill, and unrelated block properties with native fixtures.
Authority: [CSS Multicol 2 §4.2](https://drafts.csswg.org/css-multicol-2/#ch)
and [§5.2](https://drafts.csswg.org/css-multicol-2/#row-gap). This CSS
implementation repair changes no Lambda formal S/D ruling.

### First formatted line below floats

`below-float2` places its first glyph below a full-width float but loses the
50px indent. `line_reset` consumes `is_first_line` before any line is
formatted; moving the still-empty line below a float resets it again without
the indent. Consume first-line eligibility only when `line_break` publishes
a formatted line. Update the prescan's first-line check to retain the same
indent origin after newly placed floats. Test a plain text node, an inline
wrapper and a `::first-line` font, with a later line as a control.
Authority: [CSS Text 3 §8.1](https://www.w3.org/TR/css-text-3/#text-indent-property)
and [CSS 2.2 §9.5](https://www.w3.org/TR/CSS22/visuren.html#floats).

Tracing also proves the text path searches the full ancestor BFC width:
it finds space beside the float outside the 100px line container and then
publishes an empty line. Promote and reuse the existing atomic-inline search,
which intersects BFC float space with the actual line bounds. Moving this
unformatted line must retain both indentation and `::first-line` styling.
Fixed-row rounding excludes escaping-spanner flows: those groups include
separate spanner fragments and do not form homogeneous row tracks.

The inline-wrapper variant encounters its float after the wrapper's initial
line setup. Refresh float bounds before text on an empty line, retaining the
already-selected LTR indent in the line state (RTL already stores its indent).
The shared width search includes that indent when intersecting the shortened
line; a half-width float can still exclude an indented glyph that would fit
an unindented line. No synthetic line may consume first-line styles.

Float refresh also needs the line's existing initial-letter exclusion. Its
calculation is shared with line setup; the count-selected eligibility is
retained on the line, and refresh does not decrement the exclusion count.
The first-line font control uses an explicit 50px line height, as the WPT
case does. Re-resolving a unitless line-height for `::first-line` is a
separate existing issue.

### Disabled conditional hyphens

The soft-hyphen branch unconditionally records a break, while the predictive
word scans also stop at U+00AD regardless of `hyphens`. Resolve one shared
hyphenation mode for these paths: `none` retains U+00AD as zero-advance source
text without introducing a break. Literal hyphens retain their ordinary
Unicode break behavior. Cover explicit and inherited `none`, a second-line
word, and a manual-hyphenation control.
Authority: [CSS Text 3 §5.3](https://www.w3.org/TR/css-text-3/#hyphens-property).

### Choosing a fitting conditional break

`hyphenate-character-003` records the later soft hyphen after `imple`, even
though that prefix plus the authored bullet exceeds the 5.5ch line. It
overwrites the fitting break after `im`. Retain an earlier conditional break
when a later candidate cannot fit its generated mark; preserve the existing
fallback for the first conditional point when the prefix itself overflows.
Automatic candidates must advance their iterator even when a candidate does
not fit, so a continuation can reach later valid points. Verify authored
hyphen strings of different widths and multiple breaks in a single word.

When wrapping rewinds to an earlier automatic point, restart the iterator
after that chosen point. A rejected point can fit on the continuation, and
must not disappear merely because measurement already scanned beyond it.

### Language resources for automatic hyphenation

The layout gate recognizes only the bundled English resource. Consequently
German `hyphens: auto` cannot produce any automatic break, including the
mixed-case `De-dE` language test. English browser captures also use different
dictionary points from the bundled Liang data. Use the native language
resource when available, with the bundled English resource as the fallback.
On Apple platforms, query Core Foundation availability for the declared
locale and convert its UTF-16 break indices back to source UTF-8 byte offsets.
Unknown languages remain unhyphenated. The default minimum word/prefix/suffix
lengths remain 5/2/2, as suggested by CSS Text 4; source words and references
remain unchanged.

The inherited-`none` regression also finds a style-boundary defect: an inline's
block record, allocated for another property, supplies the default `manual`
value and stops the ancestor walk. The shared inherited text-keyword helper
now distinguishes a default field from an authored declaration, including
`initial` and explicit inheritance. Authority:
[CSS Cascade 5 §7.2](https://www.w3.org/TR/css-cascade-5/#inheritance).

Authority: [CSS Text 3 §5.3](https://www.w3.org/TR/css-text-3/#hyphens-property),
[CSS Text 4 hyphenate-limit-chars](https://drafts.csswg.org/css-text-4/#hyphenate-limit-chars),
and [Apple's hyphenation API](https://developer.apple.com/documentation/corefoundation/cfstringgethyphenationlocationbeforeindex(_:_:_:_:_:_:)).
The browser's [native implementation](https://raw.githubusercontent.com/chromium/chromium/main/third_party/blink/renderer/platform/text/apple/hyphenation_apple.cc)
confirms use of that platform resource; no browser or vendor code is copied.

The German subset face includes U+2010 but omits hyphen-minus. Selecting an
uncovered default mark can use a missing-glyph advance and reject otherwise
valid points. Prefer the selected font's U+2010 when covered, retaining
hyphen-minus when it is not; authored strings, including the empty string,
take precedence. Authority:
[CSS Text 4 hyphenate-character](https://drafts.csswg.org/css-text-4/#hyphenate-character).

The continuation query evaluates the remaining source word, including the
first glyph whose advance is carried into that line. Native resource probes
show that a suffix may have different available points from the complete
word; keeping the old word origin creates extra lines for an empty mark.
If its first syllable itself exceeds an unobstructed line, keep that first
point as an overflow fallback. The browser's German subset-font fixture
exposes a 46px `dampf` prefix in a 40px box: the prefix can overflow, but
the following syllables must still break. Float-obstructed lines retain
their clearance search instead of overlapping the float.

### Oversized floats beside earlier floats

The five `white-space-intrinsic-size` partials have correct glyph sizes and
intrinsic widths. Their second oversized float is incorrectly placed beside
the first within a zero-width parent. The float solver substitutes the whole
BFC width whenever the incoming float is wider than its containing block,
even when an earlier same-side float prevents placement at that block's edge.
Apply the oversized-box exception only at the same-side containing-block
edge; otherwise retain the containing-block bound and advance past the
earlier float. Cover left/right floats, zero/narrow parents, and ordinary
side-by-side floats. Authority:
[CSS 2.2 §9.5.1 rule 7](https://www.w3.org/TR/CSS22/visuren.html#float-position).

A separate zero-width right-float probe overlaps both boxes in Chromium and
Radiant, even with an RTL ancestor. Its rule-2/rule-7 behavior needs a separate
investigation; it is not part of this fix or its native regression fixture.
The fixture covers the proven zero-width left case, both sides in a narrow
parent, and ordinary placements. The probe capture remains in `temp/`.

### Preserved controls at zero font size

Both `white-space-zero-fontsize` cases return from `layout_text` before
processing source when the text node's font size is zero. Glyphs have zero
advance, but preserved LF still separates lines containing nonzero-font
spans, and a length-valued tab stop remains independent of font size.
Process whitespace normally while retaining zero glyph/cell metrics; do not
substitute the default 16px cell for a zero-size font. Cover LF, a length tab,
a numeric tab and zero-advance ordinary characters between visible spans.
Authority: [CSS Text 3 §3](https://www.w3.org/TR/css-text-3/#white-space-property)
and [§4.2](https://www.w3.org/TR/css-text-3/#tab-size-property).

The ordinary-character and numeric-tab controls expose the companion font
root: native font APIs interpret size zero as their default size. Font setup
now releases any prior face and retains zero derived metrics without asking
the platform to create that default face. Length tabs still process normally.

The full gate's five zero-font sizing controls expose stale parent strut
metrics: `setup_inline` updates the strut only when a face exists. Clearing
the zero-size face therefore leaves the parent's 14.5px ascent in its block
context. Reset that zero-size strut to zero as well; a zero-height atomic
child must keep the zero baseline even when an intervening whitespace node
now runs through text processing. Add adjacent zero-height atomic controls.

### Segment-break context across inline boundaries

`seg-break-transformation-018.tentative` inserts a full space after a ZWSP
owned by an inline span. The local preceding-character state starts at zero
for each text node, although whitespace transformation uses adjacent source
text across ordinary inline boundaries. Generalize the existing lookahead
traversal to both directions and reuse the inline module's out-of-flow
classifier. Ignore decorations, hidden boxes and out-of-flow descendants;
atomic boxes and forced breaks stop the context. The same traversal supplies
following context when a whitespace sequence ends at a text-node boundary.
Cover both directions, decorations, positioned/floated/hidden boxes, and
ordinary/atomic/preserved-newline controls. Authority:
[CSS Text 3 §4.1](https://www.w3.org/TR/css-text-3/#white-space-rules)
and [§4.1.3](https://www.w3.org/TR/css-text-3/#line-break-transform).

The August 2026 CSS Text 3 wording makes transformation UA-defined and
recommends conservative heuristics. Radiant's East Asian width-only deletion
implements a retired draft rule, discarding separators without a writing
system policy. Preserve non-ZWSP breaks as spaces consistently, including
across inline boundaries; retain ZWSP's explicit zero-width separation. The
four `segment-break-transformation-removable` fixtures still assert the old
rule, so their current browser-reference matches demonstrate this policy,
not conformance to that obsolete assertion. Both tagged and untagged CJK
controls preserve spaces. This also avoids the cross-node traversal causing
new kana-spacing regressions.

The out-of-flow control reveals a second root in `layout_block`: a block
flushes an unfinished parent line unless its positioned parent is itself
inline. Absolute/fixed boxes are outside normal flow regardless of the
parent's display. Exclude all such boxes from the line flush; retaining the
parent line also retains their static-position context. Authority:
[CSS 2.2 §9.6.1](https://www.w3.org/TR/CSS22/visuren.html#absolute-positioning).

Its block-level hypothetical static position follows the unfinished line,
even when its parent is a block. Extend the existing static-line deferred
list to retain that position until the line's final extent is known; following
larger-font text can enlarge it. Cover a block abspos between text nodes,
later line growth, first-child placement, explicit top, and inline abspos.
Authority: [CSS 2.2 §10.6.4](https://www.w3.org/TR/CSS22/visudet.html#abs-non-replaced-height)
and [CSS Position 3 §3.5.2](https://drafts.csswg.org/css-position-3/#staticpos-rect).

### Conditional hanging before forced breaks

`pre-wrap-019` and `white-space-pre-wrap-trailing-spaces-001` align fitting
trailing spaces as if they were absent. `line_break` subtracts the entire
hanging sequence for both soft and forced breaks. For a forced break in
`pre-wrap`, hang only the overflowing portion; retain fitting spaces when
aligning the line. A long trailing sequence fills the available line before
its excess hangs, so centering does not shift the fitting prefix. Last-line
and explicit LF/BR state already identify forced breaks; pass that distinction
also at block boundaries and shared content-finalization entry points.
Cover fitting and excessive spaces, soft wrapping, LF/BR/block boundaries,
normal collapsing and `break-spaces` as controls. Authority:
[CSS Text 3 §4.1.2](https://www.w3.org/TR/css-text-3/#white-space-phase-2).

### Equal-height descendants with shifted baselines

`boundary-shaping-006` has correctly shaped and positioned text, but its two
outer ordinary spans follow a descendant superscript 13px above their own
baseline. Vertical alignment restores an inline's own font box only when
its child union is shorter or taller. Equal-height shifted descendants skip
both branches and overwrite the ancestor's y-origin. Include equal heights
in the existing normalization branch, retaining its fragmentation, table
and atomic-fragment exclusions. Cover nested superscript/subscript and an
ordinary same-font chain. Authority:
[CSS 2.2 §10.6.1](https://www.w3.org/TR/CSS22/visudet.html#inline-non-replaced)
and [§10.8.1](https://www.w3.org/TR/CSS22/visudet.html#leading).

`text-align-last-015` was also inspected with both renderers. Its individual
fragments already match Chromium; the remaining mismatch is the old stored
reference's grouping of disconnected columns with equal y coordinates.
The current capture code keeps those fragments separate. Leave the case
partial and preserve its reference rather than altering correct layout.

### Full matches and validation

All thirty cases have 100% elements, spans and text against unchanged stored
browser references:

| Proven root | Newly full fixtures |
|---|---|
| Fixed multicol row pitch | `column-height-012` |
| Float clearance and first-line state | `below-float`, `below-float2`, `below-float3` |
| Hyphenation inheritance and `none` | `hyphens-none-011`, `hyphens-none-shy-on-2nd-line-001` |
| Hyphen marks, native resources, fitting points and continuations | `hyphens-auto-005`, `hyphens-auto-inline-010`, `hyphens-character`, `hyphens-span-002`, `lang-tag-case-insensitive`, `hyphenate-character-002`, `hyphenate-character-003`, `hyphenate-character-004` |
| Oversized floats inside their containing block | `white-space-intrinsic-size-001`, `white-space-intrinsic-size-003`, `white-space-intrinsic-size-014`, `white-space-intrinsic-size-016`, `white-space-intrinsic-size-020` |
| Zero-size glyphs retain whitespace controls | `white-space-zero-fontsize-001`, `white-space-zero-fontsize-002` |
| Source context and out-of-flow static placement | `seg-break-transformation-018.tentative`, `seg-break-transformation-019.tentative` |
| Conservative segment-break policy under the current spec | `segment-break-transformation-removable-1`, `segment-break-transformation-removable-2`, `segment-break-transformation-removable-3`, `segment-break-transformation-removable-4` |
| Conditional hanging before forced breaks | `pre-wrap-019`, `white-space-pre-wrap-trailing-spaces-001` |
| Equal-height descendant baseline boxes | `boundary-shaping-006` |

The exact `make test-layout-baseline` gate passes: **3,007 full passes, 287
partial passes, zero failures**, across the same 3,294 tests. The independent
native text-query runner passes all **17 tests**, including nine added root
regressions. Radiant integer-cast lint and `git diff --check` pass. No fixture
references, baseline scores, tolerances or skip lists were changed.

`LAMBDA_TEST_MAX_CONCURRENT=4 make test-radiant-baseline` passes: **5,516 full
passes, 291 partial passes, one skip and zero regressions**, across 5,808
tests. The native inventory adds nine tests, from eight to seventeen; the
layout inventory is unchanged. The full gate also passes UI/DOM, page
snapshot, rendering and CSS cascade checks. Logs are `baseline-final.log`
and `radiant-final.log` in the round-five artifact directory.

Captured browser/Radiant PNGs, initial/final geometry inventories, the explicit
thirty-case before/after manifest and gate logs are in
`temp/layout-analysis/round5/`; browser PNGs also use the shared
`temp/layout-analysis/` capture directory. Native superscript/subscript
captures agree visually; their explicit box assertions verify ancestor
baselines, which transparent wrappers do not reveal in screenshots.

Existing glyph-substitution painting and negative-z reference-overlay
painting limitations remain outside these geometry fixes. Cases with an
unproven root, unsupported browser features or a stale reference grouping
remain partial. Automatic language resources use CoreFoundation on macOS
and retain the bundled English fallback on other platforms.

## Round 6: fifty additional full matches

Starting gate after the merge: **3,007 full passes, 287 partial passes, zero
failures**, across 3,294 cases. Preserve references, tolerances and scored
baselines. Earlier rounds do not count toward these fifty fixes.

### Dictionary-backed line boundaries

`word-break-normal-th-000` leaves its entire unspaced sentence on one line;
Chromium wraps before the final Thai word. CoreFoundation resource probes
also find the expected Lao, Khmer and Myanmar boundaries. Existing character
classification only creates opportunities for spaces, CJK and authored
controls. A pass-local UTF-8 boundary map supplies native Unicode line-break
opportunities; convert the UTF-16 offsets once while scanning source bytes.
Reuse it in final line layout and intrinsic measurement. CSS tailoring keeps
`nowrap`/`break-all` independent and `keep-all` suppresses CJK opportunities
without suppressing other scripts' customary breaks. Terminal text-node
boundaries are not automatically soft breaks. Without a native resource,
retain the existing explicit-break behavior.

The first inventory exposes two integration roots: native boundaries involving
Hangul jamo or mixed Latin/CJK must receive `keep-all` tailoring on both sides,
and shaped Latin runs must flush their min-content tail at a boundary before
following CJK text. Share that predicate across final and intrinsic paths.
Also keep an earlier fitting candidate when a later resource boundary
overflows. Unicode decoding moves into the shared text-metrics module; the small
Unicode letter predicate is inline in the common header. Independent DOM
geometry and paint runners retain their existing small link dependencies.

The full gate finds a Markdown regression: a Unicode character elsewhere in
a long node activates the native tokenizer's ASCII slash/punctuation
tailoring. Restrict this supplemental resource to boundaries involving
Unicode; keep the existing ASCII CSS rules independent of unrelated node
content. A mixed Unicode/URL control covers that integration boundary.

Authority: [CSS Text 3 §5](https://www.w3.org/TR/css-text-3/#line-breaking)
and [Apple's line-break tokenizer](https://developer.apple.com/documentation/corefoundation/1588024-tokenization_modifiers/kcfstringtokenizerunitlinebreak).
No Lambda formal S/D ruling changes.

The empty-line collector exposes another root: inline-only multicol layout
collects text rectangles and treats four BR-only lines as an unbreakable
200px block. Reuse the existing line-fragment projector for those forced
lines and let consecutive BRs advance its logical line index. A fresh
Chromium capture produces the expected two 100px columns. Independent
fractional/three-column and nested controls also check line-box advances.
Authority: [CSS 2.2 §9.4.2](https://www.w3.org/TR/CSS22/visuren.html#inline-formatting).

Cluster substitution currently overwrites the advance after authored spacing
has been added. Retain that spacing once per cluster and use the same cluster
measurement in intrinsic sizing even when tracking is nonzero. Khmer's two
clusters then retain their 40px total tracking increment.

An absolute parallel flow must also fragment when its containing block fits
in one column. Pass the known column-group height into the existing absolute
fragmenter, retain static continuation offsets and unequal first-fragment
capacity, and normalize the resulting union. Scrolling contexts keep their
monolithic contents. Authority: [CSS Break §2.1](https://www.w3.org/TR/css-break-3/#parallel-flows)
and [§4.1](https://www.w3.org/TR/css-break-3/#possible-breaks).

Japanese `word-space-transform` fixtures were also examined. Their stored
references use different primary-font metrics from both the current browser
and Radiant: for `word-space-transform-024`, the saved width is 636.72px,
while a new Times capture and Radiant both report 486px. The current browser
also rejects the experimental property. Do not change correct geometry to
match that font-policy discrepancy; leave these cases partial.

### Shaping and row-size integration

The glyph spacing helper already suppresses cursive tracking, but the run
shaper rejects any nonzero authored `letter-spacing`. Gate it on the effective
spacing of each character instead, and reuse the same helper for intrinsic
glyph advances. This preserves mandatory Arabic joining under suppressed
tracking (CSS Text 3 §7.2.1).

Khmer needs substitutions across grapheme boundaries: per-grapheme advances
measure one word at about 168px, while Chromium's Khmer Sangam MN shapes it
at 129.75px. Extend the existing script-run shaper to CSS's clustered writing
systems and stop at the shared dictionary boundaries. Both intrinsic and
final layout then retain the same word widths and wrap points. Rename the
shared run API to reflect its support for both RTL and clustered scripts.

The multicol row track is distinct from the balanced column content extent.
Cap the balanced extent at the fixed row limit, allow a shorter single set,
and retain fixed tracks for forced breaks. An explicit `column-wrap:wrap`
with a definite `height` also has a fixed row limit (CSS Multicol 2 §§4.2,
4.4, 8.1). Flex-assigned definite sizes must use the existing percentage-basis
definiteness helper rather than inferring height solely from the authored
property (CSS Flexbox §9.8).

For a continuous block, a uniform fragment capacity also leaves the final row
unbalanced: `column-height-014` stores seven fragments ending at 100px, while
Chromium produces eight fragments with a 5px final pair ending at 80px.
Represent the first-row capacity, fixed row track, and balanced final-row
capacity in the transient `ColumnGroup`. Share those capacities and row
origins between the flow cursor, stored fragment boxes, and union geometry.
A spanner may use the remaining space in that row; subsequent flow starts
with the remaining first-row capacity. Track reservation owns the final row
height, so finalization must not add its gutter again. The initial scope is
one continuous, undecorated-by-cloning block without discrete child/line
constraints; other groups keep their existing constraint-aware balance path.

Browser captures verify the independent three-column fractional control
(22.5px tracks, 7.5px gutters, 146.25px flow): the container is 82.5px tall,
the fragment union is 63.75px, and all three final pieces are 3.75px tall.
Native coverage checks that fragment metadata agrees with the union and
also checks finite-height wrapping and flow on both sides of a spanner.

For ordinary definite heights, balance search must retain the used height
limit after min/max constraints (CSS 2.1 §10.7). After a spanner, the next
column set is further bounded by the remaining fragmentainer space;
overflow creates columns rather than increasing that set's height.

`letter-spacing-211` exposes an emphasis root: the existing mark extent is
conditional on clipping, and explicit line-height finalization discards it.
Apply the existing extent regardless of overflow and reuse ruby's annotation
minimum in line finalization (CSS Text Decoration 3 §3.4). Browser controls
show that preserved spaces and punctuation retain that line reservation even
when their marks are not painted; keep layout reservation separate from the
paint-time character filter.

The existing emphasis UI paint test sampled over-marks 10px above the block.
The corrected annotation reservation moves those marks inside the block;
a fresh browser capture confirms that placement. Move its two over-mark
sample points into the reserved line area, retaining every color/state/density
assertion. Scored layout references, tolerances and baseline files are unchanged.

Emoji sequences expose two linked roots: the stateless grapheme test cannot
retain GB11's preceding pictograph context, and the old composition-base
list omits juggling and basketball sequences. Use the existing Unicode
property resource with stateful extended-grapheme segmentation, then measure
the entire cluster through the selected emoji font. Actual shaping keeps
supported ligatures at one glyph advance and unsupported compositions at
their multiple-glyph advances. Intrinsic sizing uses the same cluster helper;
no sequence-specific width or membership list is added.

Authority: [UAX #29 GB11](https://www.unicode.org/reports/tr29/#GB11) and
[CSS Text 3 §1.4](https://www.w3.org/TR/css-text-3/#characters).

Authority: [CSS Text 3 §7](https://www.w3.org/TR/css-text-3/#spacing),
[CSS Multicol 2](https://drafts.csswg.org/css-multicol-2/), and
[CSS Flexbox §9.8](https://www.w3.org/TR/css-flexbox-1/#definite-sizes).
No Lambda formal S/D ruling changes.


### Final boundary and nested-context roots

Clustered-script justification uses a shared stateful grapheme cursor in layout
and all paint paths. Count gaps between typographic units rather than inside
combining sequences; retain the same Unicode resource used for text clusters.
Authority: [CSS Text 3 §6.4.5](https://www.w3.org/TR/css-text-3/#justify-minimum).

Emergency wrapping disables word-run shaping, but must not disable whole
cluster measurement. Retain that measurement and flush min-content only at
cluster boundaries for `overflow-wrap:anywhere` and legacy
`word-break:break-word`; ordinary `overflow-wrap:break-word` retains its
unbroken min-content word. Tamil controls compare the two emergency modes
with a whole-cluster reference and ordinary overflow wrapping.

Narrow no-break space is absent from the Ahem cmap. Chromium synthesizes it
as half the selected face's regular-space advance; Radiant instead selects a
system fallback's 0.2em advance. Resolve available space glyph metrics first,
then synthesize a missing narrow space from the primary face. Reuse the same
advance in line layout, word lookahead and intrinsic sizing. This is a font
fallback policy, not a font-name exception.
Authority: [CSS Fonts character handling](https://www.w3.org/TR/css-fonts-4/#char-handling)
and the [HarfBuzz space synthesizer](https://github.com/harfbuzz/harfbuzz/blob/main/src/hb-ot-shape-fallback.cc).

Column spanning applies only to in-flow block-level boxes. Promote the
existing classifier and share it with intrinsic sizing and formatting-context
selection. An isolated atomic inline must retain its own unfragmented flow;
mixed line/block flows keep their existing collector. A common single-block
collector replaces the repeated traversal shape.
Authority: [CSS Multicol §6.1](https://www.w3.org/TR/css-multicol-1/#column-span)
and [CSS Break §4.1](https://www.w3.org/TR/css-break-3/#possible-breaks).

For `break-all`, adjacent whole unbreakable inline runs retain the existing
punctuation constraints across their DOM boundary. Share the existing inline
edge lookup rather than inventing an independent traversal. Broader partial
word-tail propagation remains outside this bounded change.
Authority: [CSS Text §5.2](https://www.w3.org/TR/css-text-3/#word-break-property).

The RTL inline containing-block end is already resolved relative to its
fragment. Re-adding the first-fragment origin moves a nested auto-blockified
inline by one glyph. Apply that origin only to the padding-box fallback.
Authority: [CSS Position §2.1](https://www.w3.org/TR/css-position-3/#def-cb).

Nested fixed rows expose two roots: the size helper discards column-height
when there is no block-style record inside a definite ancestor, and descendant
projection replaces inner piece heights with the outer capacity. Retain the
own row size independently of the ancestor. For a continuous leaf beginning
at the parent fragment edge, map its stored row tracks into complete parent
slots and preserve each piece's measured height. Independent fractional
controls use 7.5px inner tracks, 2.5px gutters and 20px parent columns; the
leaf union is 17.5px tall and all eight pieces remain 7.5px tall. Discrete
contents and nonzero leading fragment offsets keep their existing paths.
Authority: [CSS Multicol 2 §4.2](https://drafts.csswg.org/css-multicol-2/#ch)
and [CSS Break §2.2](https://www.w3.org/TR/css-break-3/#nested-flows).
No Lambda formal S/D ruling changes.


### Verified full-match manifest

These fifty cases were partial in the fresh Round 6 starting inventory and
now match all compared elements, spans and text at 100%. Earlier rounds and
new native controls are excluded.

**wpt-css-text — 28 cases**

- `letter-spacing-211`
- `letter-spacing-cursive-001`
- `letter-spacing-ligatures-003`
- `letter-spacing-ligatures-004`
- `line-break-anywhere-overrides-uax-behavior-008`
- `line-breaking-013`
- `line-breaking-014`
- `line-breaking-024`
- `line-breaking-025`
- `line-breaking-026`
- `line-breaking-027`
- `line-breaking-atomic-003`
- `line-breaking-atomic-004`
- `overflow-wrap-min-content-size-002`
- `text-autospace-break-001`
- `text-transform-fullwidth-009`
- `word-break-keep-all-003`
- `word-break-keep-all-011`
- `word-break-manual-001`
- `word-break-min-content-003`
- `word-break-min-content-006`
- `word-break-normal-002`
- `word-break-normal-003`
- `word-break-normal-km-000`
- `word-break-normal-lo-000`
- `word-break-normal-my-000`
- `word-break-normal-th-000`
- `word-break-normal-th-001`

**wpt-css-multicol — 18 cases**

- `column-balancing-with-span-and-oof-001`
- `column-balancing-with-span-and-oof-002`
- `column-count-used-001`
- `column-height-003`
- `column-height-007`
- `column-height-013`
- `column-height-014`
- `column-height-017`
- `column-height-018`
- `column-height-020`
- `column-height-028`
- `inline-block-and-column-span-all`
- `multicol-fill-balance-029`
- `multicol-fill-balance-041`
- `multicol-span-all-012`
- `multicol-span-all-children-height-002`
- `multicol-span-all-children-height-002-ref`
- `multicol-span-all-children-height-003-ref`

**wpt-css-position — 4 cases**

- `vlr-rtl-ltr-in-multicols.tentative`
- `vlr-rtl-rtl-in-multicols`
- `vrl-rtl-ltr-in-multicols.tentative`
- `vrl-rtl-rtl-in-multicols`


### Validation

The exact final `make test-layout-baseline` gate passes: **3,057 full matches,
237 partial matches, zero failures**, across 3,294 tests. This is exactly
50 additional full matches over the fresh starting run's 3,007. Browser
references, thresholds, scored baselines and skip lists are unchanged.

All **33 native text/layout controls pass**, including independently captured
fractional row tracks, dictionary boundaries, whole clusters, native tracking,
parallel flows, scrolling, punctuation and opposite-direction paragraphs.
`make lint ARGS='--rule ^no-int-cast-radiant$'` and `git diff --check` pass.
The final nested-row Radiant PNG and Chromium screenshot both show the filled
100px green square with no red.

The full test build exposed a standalone paint-link dependency on the shared
Unicode letter predicate. Inline that small pure predicate in the common
header; this keeps paint clients independent of the full layout object and
restores the exact gate's build without changing its test selection.

The supplemental dictionary resource is currently CoreFoundation on macOS;
other platforms retain their previous explicit/CJK line-break fallback.
Unproven roots, unsupported browser features and stale-font references remain
partial; they are not changed to meet the fifty-case count.


The final `make test-radiant-baseline` gate also passes: **5,582 passed,
241 partial matches, one existing skip, zero failures**, across 5,824 tests.
Its native text/layout runner passes all 33 controls, and the corrected
emphasis UI sampling retains all sixteen color/state/density assertions.

## Round 7 — one hundred additional full matches (2026-10-14, in progress)

The fresh exact baseline starts at **3,057 full / 237 partial / zero failures**
across 3,294 recorded tests. Count additional full matches against that run;
native controls do not count toward the requested hundred. References, scored
baselines, tolerances and skips stay unchanged. These are CSS layout rulings;
no Lambda `S#` or `D#` semantics/design ruling changes.

### Resolved zero spacing

`letter-spacing: 0` reaches the style resolver as a number, while the resolver
accepted only lengths/functions/percentages. Font completion also treated a
zero spacing value as an unresolved slot. Accept valid unitless zero and retain
resolved-state bits for letter and word spacing across copies and inheritance.
Intrinsic measurement uses the same distinction. CSS Values permits unitless
zero lengths; CSS Text 3 §7.1/§7.2 computes `normal` to zero.

The independent control checks zero, normal, descendant inheritance and
shrink-to-fit sizing against Chromium. `letter-spacing-203` becomes fully
matched; `letter-spacing-206` improves but remains partial. Current CSS Text 3
§7.2 requires suppressing tracking at line edges, so conflicting Chromium line
fitting in `letter-spacing-200`/`201` is left unchanged.

### Selected-face normal leading

Normal line-height already collects the actual selected glyph faces' metrics.
A second floor unconditionally inserted system CJK font metrics whenever a line
contained a CJK codepoint, including glyphs supplied by its primary web font.
Remove that unrelated face from line and table sizing. CSS 2.1 §10.8.1 bases
normal inline metrics on the fonts actually used. Remove the obsolete CJK
presence bit; preserve fallback metric collection.

An Ahem control supplies both Latin and U+6C34 in its cmap and confirms 40px
normal leading in paragraphs and table cells. `text-autospace-ideogram-alpha-001`
becomes fully matched. The full text and Markdown inventories show no losses;
all 35 independent native controls pass. Missing-glyph font-environment
differences in other autospace cases remain under investigation.

### Absolute parallel flow and fixed static anchors

An absolute box under a fragmented containing block published its fragment
union while retaining continuous geometry on its child. Its child coordinates
are local to that box, unlike the phase-one normal-flow coordinates. Project
the local flow through the generated column count and retain a stored piece
width across repeated publication. Preserve the absolute box's continuous
origin in pass-owned positioning state, cleared by each new positioned layout;
the published union must not become the source for the next finalization.

The general anchor walk stopped at an absolute ancestor and looked up fixed
children with the absolute containing-block rule. Visit the descendant and use
the existing fixed containing-block and parent-offset helpers. Commit the
current parent offset with the new anchor so the later positioning finalizer
does not add the same movement twice. The nested sequence's uniform-fragment
return path must also project the local child flow. Partial first fragments
need further work and are not routed through that uniform-capacity path.

Empty in-flow multicolumn content returned before finalizing positioned flows.
Run that finalization even for a zero-height column set; the existing definite
ancestor helper supplies the capacity absent from an empty inner fragment list.
Consolidate the horizontal, vertical and nested continuous-extent query so an
auto absolute box retains its content height after publication. Authority:
[CSS Break 3 §2.1/§2.2](https://www.w3.org/TR/css-break-3/#parallel-flows),
[CSS Position 3 §2.1](https://www.w3.org/TR/css-position-3/#def-cb).

Independent Chromium controls confirm a 400px source becoming four 100px
pieces, the fixed anchor at the final piece's end, a two-piece aligned nested
flow, and a zero-height inner set whose absolute flow still fragments. The
corrected Radiant `fixedpos-static-pos-with-viewport-cb-001` image shows the
filled green square at the same location as Chromium. Four additional
multicolumn cases fully match: `fixedpos-static-pos-with-viewport-cb-001`,
`002`, `003`, and `fixed-in-nested-multicol-with-viewport-container`.
All 139 positioning baseline cases remain full matches.

### Required shaping under ineffective styles

A case transform that leaves every character unchanged must retain required
shaping. Likewise, merely enabling autospace does not split a word with no
actual inter-script gap. The shared script-run probe tests the transformed
characters and stops at actual autospace boundaries. Emergency `break-word`
wrapping preserves whole-word shaping when that word fits a fresh line;
min-content sizing retains its ordinary whole-word contribution. Authority:
[CSS Text 3 §5.4/§7.3](https://www.w3.org/TR/css-text-3/#boundary-shaping).
The pinned Noto controls exercise all three case transforms, autospace and
non-binding emergency wrapping. `overflow-wrap-shaping-001` and `002` fully
match; Markdown Unicode improves but remains partial.

### Inline owner restoration and emergency boundary scope

Inline layout restored the font and direction but leaked its current DOM style
owner into subsequent sibling text. Restore that owner on every exit through
the existing parent-state restoration. A preceding `overflow-wrap:anywhere`
inline supplies a boundary before the following ordinary word; preflight that
word against the remaining measure without splitting it. An independent Ahem
control compares the emergency boundary with explicit forced lines and an
unstyled overflow control. `overflow-wrap-anywhere-inline-004.tentative` fully
matches. Authority: [CSS Text 3 §5.4](https://www.w3.org/TR/css-text-3/#overflow-wrap-property).

### Small mixed-face normal leading

The line-height union discarded mixed-font growth of up to 2px even for
`normal`. Times New Roman's 16px normal split and the selected Apple Symbols
Ogham fallback grow their union by 1px per line. Use the layout's 1/64px
quantization tolerance for normal leading; retain explicit line-height
behavior. Independent primary, fallback and explicit-leading controls confirm
the 36px/38px/36px two-line extents. `trailing-ogham-001`, `002` and `003` fully
match. Authority: [CSS 2.1 §10.8.1](https://www.w3.org/TR/CSS21/visudet.html#line-height).

### Mongolian shaping and automatic vertical alignment

The shared shaping probe and inline context collector used RTL direction as
their cursive-script gate, excluding left-to-right Mongolian. Use the existing
Unicode cursive classification, while a zero-advance formatting control alone
cannot create a shaped glyph run. The horizontal pinned-font control verifies
the medial form; the formerly full MVS atomic-inline control remains full.

The joined vertical word then exposed an independent placement defect:
automatic physical height held only padding/border during line alignment,
making its provisional inline content size zero. Reuse deferred line alignment
against the measured automatic inline extent before publishing physical
vertical geometry. Pinned Mongolian and Ahem controls verify the final size and
origin. `shaping-023`, `024` and `025` fully match. Authority:
[CSS Text 3 §7.3](https://www.w3.org/TR/css-text-3/#boundary-shaping),
[CSS Writing Modes 4 §7.3](https://www.w3.org/TR/css-writing-modes-4/#orthogonal-flows).

The exact `make test-layout-baseline` progress gate reports **3,072 full /
222 partial / zero failures**, giving **15 additional full matches**. All
39 independent native controls pass at that checkpoint.

### Context inside a text node

The context collector previously accepted only ranges touching a DOM text edge.
Extend its existing word-slice and piece collector to include the same node's
prefix/suffix, then cross eligible inline edges only if that word reaches them.
Intrinsic sizing supplies its normalized source bounds to the same collector.
Use Unicode contextual-shaping classification for Indic and related units
without misclassifying them as CSS cursive or dictionary-breaking scripts.
An emergency-wrapped character or tracked grapheme retains its unbroken word's
required forms; do not add a second kerning adjustment to its contextual
advance. Authority: [CSS Text 3 §5.3/§5.4/§7.2](https://www.w3.org/TR/css-text-3/#hyphenation).

Pinned Arabic emergency-wrap controls check initial, medial and final forms.
Whole-word Bengali controls check two selected faces and the tracked Bangla MN
case against Chromium. `hyphens-shaping-001`, `002`, `line-break-shaping-001`
and `line-breaking-023` become full matches. Tibetan retains its customary
breaks: this change does not broaden the whole-word optimization's eligibility.
Bengali yaphala remains partial. Neither a positive dependent-mark advance nor
a CoreText source index alone reliably identifies its font-dependent tracking
unit, so no joining-sequence exception is introduced.

### Empty image alternatives

Failed-image layout reserved the optional broken-image icon for an explicitly
empty `alt` attribute. HTML defines an image representing nothing as having
zero natural dimensions. Select that zero fallback for its unresolved axes;
retain explicit dimensions and the existing absent-alternative icon behavior.
The intrinsic path already supplies zero for the empty alternative. Controls
cover empty/absent alternatives, two explicit dimensions and a shrink-to-fit
wrapper whose ordinary line strut remains. Authority:
[HTML §15.4.2](https://html.spec.whatwg.org/multipage/rendering.html#images).
`md_chalk-readme` and `md_mermaid-readme` become full matches.

The next exact `make test-layout-baseline` gate reports **3,078 full / 216
partial / zero failures**, giving **21 additional full matches**. All 41 native
controls and the Radiant integer-cast lint pass. The complete
`make test-radiant-baseline` gate passes **5,611 full / 220 expected partial /
one existing skip** across 5,832 recorded checks, with no failures.

### Final nested column sets and line-count limits

Nested inline projection treated the first outer fragment as a full-capacity
set even when it held all actual content. A specified 300px child height may
publish three outer fragments, but empty trailing pieces do not defer balancing
of the first, final content set. Partition its actual lines into the inner
columns. Reuse one count-based partitioner for both that set and a later final
continuation; select fitting column counts and distributions while retaining
orphans/widows, relaxing line-count restrictions only when no partition fits.
Authority: [CSS Multicol 1 §7](https://www.w3.org/TR/css-multicol-1/#cf),
[CSS Break 3 §3.3/§4.4](https://www.w3.org/TR/css-break-3/#unforced-breaks).

Chromium controls verify first/final and full/continuation sets, auto filling,
three-column distribution and asymmetric line-count limits. A two-line final
continuation stays in one column for `widows: 2`. The Radiant and Chromium PNGs
agree on these placements. `multicol-breaking-000` and
`multicol-breaking-nobackground-000` fully match, bringing the round to
**23 additional full matches**. All 42 native controls pass; all 139 positioning
cases remain full matches, and the multicolumn inventory has no full-match
losses or failures. The artifact manifest records exactly these baseline gains.

### Alignment and bounds of split inlines

Line alignment walked block descendants of a split inline and translated them
with the trailing text. Promote the formatter's existing split-child predicate
to the module header and reuse it in alignment: those blocks participate as
anonymous block siblings. After aligning the inline continuations, recompute
their enclosing split union through the existing bounds helper.

An inline with direct text must still include nonempty block fragments in that
union. Empty splits supply only inline fragments. Traverse transparent inlines
to distinguish these cases; positioned and atomic subtrees do not supply split
block fragments. The split ledger records the principal inline's font boxes;
an atomic descendant's overflow does not enlarge that box vertically.
Authority: [CSS 2.1 §9.2.1.1](https://www.w3.org/TR/CSS21/visuren.html#anonymous-block-level),
[CSSOM View §6](https://drafts.csswg.org/cssom-view/#dom-element-getclientrects).

Chromium controls check leading/trailing/direct text, block-only inlines,
centered atomic inlines, and an empty block beside an absolute descendant.
`md_execa-readme` fully matches. The corrected multicolumn and positioning
inventories retain every full match. All 43 native controls and cast lint pass;
the round now has **24 additional full matches**.

The full gate exposed two inline-list regressions: later atomic siblings raise
the trailing line's baseline after the split ledger was recorded. Merge the
finalized direct text font boxes into the vertical union, and refresh ordinary
split inlines after vertical alignment as well as inline list items. A new
control checks this baseline displacement without including atomic overflow.
The exact `make test-layout-baseline` gate now reports **3,081 full / 213 partial /
zero failures**. All 43 native controls pass.

### Used image percentage bases and winning font families

A loaded image resolved its width percentage only when its containing block
had a non-auto specified width, or used shrink-to-fit sizing. Ordinary auto
block widths are already used values when their children are laid out. Admit
that content width as the percentage basis before intrinsic ratio transfer.
Chromium controls cover ordinary and picture wrappers, a specified parent and
a definite zero parent. All 832 image/sizing baselines remain full matches.
The Zod page improves but retains independent parsing and table differences.
Authority: [CSS 2.2 §6.1.3](https://www.w3.org/TR/CSS22/cascade.html#used-value).

HTML monospace defaults applied the existing UA medium-size adjustment before
author font-family resolution. A winning named family retained that adjusted
size; explicitly repeating the generic family applied the adjustment twice.
Retain the default family state during the font cascade and apply the family
transition once. Keep native control font sizes separate from family-only HTML
defaults. Share the existing HTML role set between cascade and intrinsic
sizing rather than adding another tag list. Controls cover default/generic/
named families, explicit/inherited sizes, shorthand and textarea sizing.
Authority: [CSS 2.2 §6.4](https://www.w3.org/TR/CSS22/cascade.html#cascade),
[CSS Fonts 3 §3.5](https://www.w3.org/TR/css-fonts-3/#font-size-prop).
`hyphens-auto-control` fully matches, giving **25 additional full matches**.

### Explicit zero preferred column widths

The multicolumn property stored both zero lengths and `auto` as zero and tested
only positive widths. Retain a specified-width flag alongside the computed
length, matching the existing column-height model. Reuse a shared presence
predicate in context establishment, intrinsic sizing and dimension selection.
Clamp the preferred used width to the spec's 1px minimum without changing its
computed zero value. Controls cover longhand, shorthand, unitless zero,
subpixel widths, count limits and auto reset, including actual fragments.
Authority: [CSS Multicol 1 §3.1](https://www.w3.org/TR/css-multicol-1/#cw).
All 46 native controls and cast lint pass; all 776 sizing fixtures remain full.
The root-flex zero-width cases still fail: enabling their missing context
exposes independent balancing and descendant-fragment geometry defects.

### Forced-break spaces and independent balance constraints

Intrinsic normalization retained a collapsed separator before a direct or
indirect `<br>`, treating the later text as an inline continuation. Extend the
existing forced-edge predicate to both ends, including `<br>`, and stop the
boundary scan there. The resulting shrink-to-fit width exposed a related line
bug: an overflowing terminal separator forced a new empty line before the
following `<br>`. Defer that terminal collapsed separator so the following
inline item or forced break owns the line decision. Retain preserved spaces
and actual separators between words. Chromium controls check direct/nested
breaks, decorations, preserved spaces, widths, line counts and break positions;
the rendered PNGs agree. `baseline-000` becomes full. Authority:
[CSS Text 3 §4.1](https://www.w3.org/TR/css-text-3/#white-space-processing).
The next exact gate records **3,083 full / 211 partial / zero failures**.

A balance search restarted its view scan at the container's first child for
each spanner group. Pass the actual group items to line/track scanning and
single-item floors. A single unbreakable line supplies a minimum even when
column count comes from preferred width. Preserve size-contained monolithic
items before a spanner; empty ordinary blocks and their unoccupied height can
still fragment through transparent inlines. The existing mixed-flow predicate
distinguishes a following real inline run from such a wrapper. Its content
query also needed a bounded view range: a whitespace node previously inherited
the visibility of text inside later block siblings. Reuse the existing walk
for both whole sibling runs and single-view queries. A contained 40px item,
10px spanner and contained 60px item now total 110px, matching Chromium; the
old engine split the first item and totaled 90px. Controls also retain ordinary
200/300px continuous flows and independent empty groups. Authority:
[CSS Multicol 1 §6/§7](https://www.w3.org/TR/css-multicol-1/#column-span),
[CSS Break 3 §4](https://www.w3.org/TR/css-break-3/#breaking-rules).

### Static positions through an inline containing block

The ancestor offset walk skipped an inline containing block and accumulated
outer block origins. Stop at that inline, and cache its current bounding origin
with static parent offsets so later fragment finalization updates the children.
The formatter's RTL extent and vertical inline expressions already produce
local coordinates; their offset conversion uses that existing coordinate basis
and the block formatting context's direction. Hypothetical custom HTML elements
also retain the initial inline display before absolute-position blockification.

Chromium controls compare custom/known inline and explicit block boxes through
an atomic inline, with an outer offset and preceding text that changes the
inline's union origin. Use the public visual-bounds API for those physical
coordinates. All 139 positioning fixtures retain full matches.
`multicol-oof-inline-cb-001`, its reference, and `multicol-oof-inline-cb-002`
become full matches, bringing the round to **29 additional full matches**.
All 49 native controls pass. Authority:
[CSS 2.1 §9.7](https://www.w3.org/TR/CSS21/visuren.html#dis-pos-flo),
[CSS Position 3 §4.1](https://www.w3.org/TR/css-position-3/#static-position).

The complete gate exposed one anonymous-table regression: generated table
boxes share the absent tag ID with custom HTML elements, but their retained
table roles override HTML defaults. Honor the existing table-fixup role in
intrinsic inline classification and hypothetical display queries. Chromium
controls check cells/rows inside atomic list items, the corresponding ordinary
inline list item and a custom block wrapper. All 50 native controls pass; the
focused lists, multicolumn and positioning inventories have zero regressions.
The exact `make test-layout-baseline` gate now passes **3,086 full / 208 partial /
zero failures**. Cast lint and `git diff --check` also pass.

Two remaining multicolumn text comparisons combine source fragments but retain
only the first Radiant fragment's bounds (`multicol-list-item-002` and
`multicol-margin-003`). This is a comparison-protocol diagnosis; no reference,
tolerance, skip or comparator changes are included in this round.

### Nested forced-column continuations

The outer projection reused the inner layout's applied column offsets and
balanced subslots after forced breaks. Normalize the existing inline offset
before reprojecting, preserve each descendant's continuous source extent,
and carry consumed forced-break flow back through the inner/outer conversion.
Store those extents even when an existing continuation becomes fuller without
adding a new fragment. Chromium controls cover break-before, break-after,
uninterrupted flow, leaf pieces and wrapper fragments.
Authority: [CSS Break 3 §2.2/§4.3](https://www.w3.org/TR/css-break-3/#nested-flows),
[CSS Multicol 1 §7.1](https://www.w3.org/TR/css-multicol-1/#cf).
`multicol-nested-column-rule-003` becomes full, bringing this round to **30
additional full matches**. The exact gate passes **3,087 full / 207 partial /
zero failures**; all 51 native controls and cast lint pass.

The PNG comparison also exposes decoration painting across the outer gutter.
Factor the repeated raster/SVG/PDF rule geometry into a shared iterator over
the multicolumn box's fragments, keeping rules within each inner column set.
Use its center coordinate consistently for stroked rules and place double
rules' thirds within their used width. Native controls check centers and
heights; separate Chromium captures check solid/double rules with padding.
Flat background painting must likewise use individual fragments rather than
their DOMRect union. Reuse one shared PaintIR fast path for those flat
boundaries in all three outputs; complex decorated boundaries retain their
existing fallback pending a complete slice/clone paint model.
Authority: [CSS Multicol 1 §4](https://www.w3.org/TR/css-multicol-1/#column-gaps-and-rules),
[CSS Break 3 §5.4](https://www.w3.org/TR/css-break-3/#fragmented-borders).

The preceding full Radiant checkpoint passes **5,628 full / 212 expected
partials / one existing skip / zero failures**.

### Intrinsic indentation and first segments

Text measurement returned only the longest unbreakable width, and intrinsic
indentation added the offset to that entire text-node minimum. Record the
first segment and the independent maximum of later segments in the shared
segment commit used by every existing break path. Carry those measurements
through preserved lines and transparent inline measurements. Apply inherited
text-indent at its block formatting context rather than again on ordinary
inlines. Chromium controls cover a later wider word, positive/negative offsets,
nowrap, preserved breaks and nested inline wrappers. All 52 native controls
pass. The focused text inventory gains `text-indent-min-max-content-001` with
no losses, bringing this round to **31 additional full matches**; all 776
sizing cases and the multicolumn/Markdown inventories retain their matches.
Authority: [CSS Text 3 §8.1](https://www.w3.org/TR/css-text-3/#text-indent-property),
[CSS Sizing 3 §4](https://www.w3.org/TR/css-sizing-3/#intrinsic).

The fragment-boundary fast path also needed to accept retained standalone
blocks. Query the inherited fragment accessor directly rather than requiring
a DOM node-type tag. The unchanged display-list suite now passes all 83
controls; the 52 native text/layout controls also pass. The complete Radiant
rerun remains outstanding after that correction.

The exact first-segment-indent gate passes **3,088 full / 206 partial / zero
failures**. The fresh full Radiant rerun also passes: **5,632 full / 210
expected partials / one existing skip / 5,843 total / zero failures**.

### Intrinsic punctuation across ordinary inline boundaries

The existing unbreakable-run boundary accounting was restricted to break-all.
Remove that restriction: normal, keep-all and break-word retain punctuation
constraints too, while anywhere explicitly adds emergency breaks. Reuse the
same edge query and boundary predicate for every wholly unbreakable child.
Chromium controls cover all five policies, and the existing native forced-break
fixture checks both dimensions. `overflow-wrap-min-content-size-008` becomes
full, bringing the round to **32 additional full matches** with no focused
text/sizing/multicolumn/Markdown losses. All 52 native controls pass.
Authority: [CSS Text 3 §5.2/§5.5](https://www.w3.org/TR/css-text-3/#line-breaking).

### Independent anonymous flex-item indentation

The direct text pass initialized selected line fields without starting the
anonymous item's block context, so an earlier element item could consume its
first-line state. Extract the existing indent setup and reuse normal line
initialization. Keep each anonymous text item's line origin separately from
its content union: indentation and alignment can move that union, while a
later item-position/baseline translation must preserve the content offset and
consume its own translation once. Reuse a single anonymous-item lookup.

Anonymous text's intrinsic widths also need its fixed indent once, on the first
run. Reuse the same first-segment helper as ordinary blocks; intrinsic
percentage terms use a zero basis, and collapsible whitespace creates no
formatted first line. Chromium controls check multi/single-line text,
percentage/no indent and a following flex element that must shrink beside the
anonymous item's minimum. All 52 native controls pass. The focused text,
sizing, multicolumn and Markdown inventories have no losses;
`anonymous-flex-item-001` becomes full, bringing the round to **33 additional
full matches**. Authority: [CSS Flexbox 1 §4](https://www.w3.org/TR/css-flexbox-1/#flex-items),
[CSS Text 3 §8.1](https://www.w3.org/TR/css-text-3/#text-indent-property),
[CSS Sizing 3 §5.1](https://www.w3.org/TR/css-sizing-3/#cyclic-percentage-size).
Pass-local anonymous item state retains the existing ownership of D4.5.1v4.

### Selected terminal soft-hyphen sources and completed inline struts

The chosen-break path removed a terminal soft hyphen's source bytes in LTR
output. A source node containing only that control then had zero source length,
so its generated mark lost the styled text box. Preserve the LTR source range
that owns the mark; retain the existing separate RTL fragment representation.
The restored mark exposed a second cause: an inline ending on a completed line
added its font strut to the next sibling's new line. Do not contribute that
closed fragment's strut again. Chromium controls compare mark-only and
prefixed sources, unselected marks, explicit breaks and following-line metrics.
The rendered control pages agree, and all 53 native controls pass.
Authority: [CSS Text 3 §5.4](https://www.w3.org/TR/css-text-3/#hyphenation),
[CSS 2.1 §10.8.1](https://www.w3.org/TR/CSS21/visudet.html#line-height).
`shy-styling-001` and `hyphens-span-001` become full, bringing this round to
**35 additional full matches**. The text, inline and multicolumn inventories
have no losses; all 169 inline fixtures remain full.

The preceding anonymous-flex checkpoint passes the exact gate at **3,090 full
/ 204 partial / zero failures**. Two other multicolumn cases expose zero-width
fragment fallback, but Chromium's fresh bounding rectangle selects its last
zero-area fragment while CSSOM View §6 specifies its first. Do not implement
that browser discrepancy to obtain a match.

The soft-hyphen checkpoint passes the exact layout gate at **3,092 full / 202
partial / zero failures**, and the complete Radiant gate at **5,637 passed /
206 expected partial / one existing skip / zero failures**.

### Isolated bidi sequences and transparent empty boundaries

The parent bidi pass correctly treats an isolate as one item, but its contents
were never reordered across separate text nodes. Reuse the fragment pass for
each isolate's own sequence before placing it in its parent. Represent an
isolate by U+FFFC in that parent, as CSS Writing Modes requires, and place a
normal empty inline at the next run's resolved inline-start edge without
adding a character to UAX #9. Fresh Chromium controls cover numeric/neutral
boundaries, LTR contents and nested isolates. All 54 native controls pass.
`empty-span-001` becomes full, bringing the round to **36 additional full
matches**. Text and inline inventories have no losses. The exact layout gate
passes at **3,093 full / 201 partial / zero failures**. Authority:
[CSS Writing Modes 3 §2.2](https://www.w3.org/TR/css-writing-modes-3/#unicode-bidi),
[UAX #9](https://www.unicode.org/reports/tr9/).

### Mandatory letter wrapping in inline lookahead

The ASCII lookahead classified a following span as an unbreakable word despite
its inherited `word-break: break-all`. This moved the whole span to the next
line before ordinary text layout could use its letter boundaries. Make the
ASCII fast path policy-aware and let its per-character probe retain legal
letter opportunities. Existing emergency wrapping is unchanged. Chromium
controls compare a split styled span under break-all and line-break:anywhere
with a nowrap control. All 54 native controls pass; text, inline and Markdown
inventories have no losses. `word-break-break-all-030` becomes full, bringing
the round to **37 additional full matches**. Authority:
[CSS Text 3 §5.2](https://www.w3.org/TR/css-text-3/#word-break-property).

The mandatory-wrapping checkpoint passes the exact layout gate at **3,094 full
/ 200 partial / zero failures**.

### CSS escaped scalar values

CSS string/identifier token decoding disabled invalid-scalar replacement. A
hex escape evaluating to U+0000 encoded a literal NUL, terminating generated
content before layout. Enable the existing decoder's replacement mode and
classify zero, surrogate and out-of-range scalar values before UTF-8 encoding.
Reuse the tokenizer's escape-consumption helper for string hex escapes so
optional terminator whitespace is not mistaken for an unescaped newline.
Complete CSS string continuations by dropping their LF/CRLF/FF source bytes.
The tokenizer controls check invalid escapes with following content, hex
terminators and line continuations. The full text inventory has no losses;
`control-chars-000` becomes full, bringing this round to **38 additional full
matches**. Authority: [CSS Syntax 3 §4.3.7](https://www.w3.org/TR/css-syntax-3/#consume-escaped-code-point),
[§4.3.5](https://www.w3.org/TR/css-syntax-3/#consume-string-token).

The escaped-scalar checkpoint passes the exact layout gate at **3,095 full /
199 partial / zero failures**, and all 28 tokenizer controls pass. The broader
input gate exposes existing CSS parser and formatter failures: two parser
controls, seven stylesheet round trips and one aggregate formatter case fail
identically when relinked against the pre-change tokenizer and escape decoder.
The isolated pre-change executables and logs are retained under the round's
temporary directory; no production source or comparison test is reverted.

A fresh carriage-return diagnostic confirms CSS Text 3 §4 requires U+000D to
behave as U+0020. Chromium instead gives preserved CR zero width in the tested
Ahem font while spaces retain their width. The existing layout also treats CR
as a forced break. This needs a separate source-preserving normalization
design, not a browser-specific zero-width workaround; no CR layout change is
part of this checkpoint. Authority:
[CSS Text 3 §4](https://www.w3.org/TR/css-text-3/#white-space-processing).

### Letter boundaries before Unicode separators

The shared letter-boundary predicate explicitly allowed a break before U+202F
and U+3000. This split nonbreaking groups and moved a break-after separator
away from its preceding letter. Remove those exceptions and preserve the
existing line-break:anywhere override. Fresh Chromium controls verify the
source fragments, overflow and line counts for narrow/nonbreaking spaces and
ideographic spaces. All 55 native controls pass. Text, inline and multicolumn
inventories have no losses. `trailing-other-space-separators-break-spaces-013`
and `-015` become full, bringing this round to **40 additional full matches**.
Authority: [UAX #14 LB12a](https://www.unicode.org/reports/tr14/#LB12a),
[LB21](https://www.unicode.org/reports/tr14/#LB21),
[CSS Text 3 §5.2](https://www.w3.org/TR/css-text-3/#line-break-property).

The separator checkpoint passes the exact layout gate at **3,097 full / 197
partial / zero failures** and the complete Radiant gate at **5,644 passed /
201 expected partial / one existing skip / zero failures**. The broader input
gate remains failed outside this gate: existing CSS parser/formatter failures
are verified against the pre-change decoder. The PDF opacity mismatch also
reproduces with the executable saved before this entire round, at the same
pixel (100,140), expected green 239 versus actual 225. Other input/render
failures are recorded in `input-css-escaped-scalar.log`; do not call that
broader gate passing.

### Space-only inline fragments at a full line

The leading-space preflight forced a break before a whitespace-only text node
when its preceding line was full. Its inline wrapper then lost the accepted
line's zero-width font box and moved to the new line. Let the ordinary
separator/end-of-line path process that source so the existing collapsed
fragment ledger retains its original position. Chromium controls cover normal
and full-width transforms, fitting spaces, leading/terminal spaces and
preserved whitespace. All 56 native controls pass; text, inline and
multicolumn inventories have no losses. `text-transform-fullwidth-008` becomes
full, bringing this round to **41 additional full matches**. Authority:
[CSS Text 3 §4.1.2](https://www.w3.org/TR/css-text-3/#white-space-phase-2),
[CSS Inline 3 §2.1](https://www.w3.org/TR/css-inline-3/#model).

The space-only-inline checkpoint passes the exact gate at **3,098 full / 196
partial / zero failures**.

### Signed indentation and neutral inline static anchors

A negative LTR indent was mistaken for an empty cursor and replaced by the line
edge. Preserve signed cursor positions. Horizontal RTL inline static positions
also omitted indentation and preceding content. Resolving only a paragraph's
logical advance is insufficient: a neutral static anchor between two LTR runs
inside an RTL paragraph belongs to that LTR run. Feed a zero-width U+FFFC
anchor through the existing bidi pass and translate its resolved margin edge
into the actual containing block. Reuse the current line's deferred alignment
list; unplaced empty-line anchors keep their static fallback. Both counting
and filling visit the anchor without descending into its independent flow.
Inline containing-block extents and explicit self-alignment retain their own
existing resolution paths. Metadata stays with the current positioned view and
line under D4.5.1v4; no DOM text or line width is changed.

Fresh Chromium controls compare signed indents, a preceding word, both nested
span directions in an LTR run, the child's own indentation, explicit insets,
block static positions and float exclusions. All 57 native controls pass.
Text, inline, sizing, positioning and multicolumn inventories have no losses;
all 139 positioning and 776 sizing fixtures remain full.
`text-indent-with-absolute-pos-child` becomes full, bringing this round to
**42 additional full matches**. Authority:
[CSS Position 3 §3.5.2](https://www.w3.org/TR/css-position-3/#static-position-rectangle),
[CSS Writing Modes 3 §2.4](https://www.w3.org/TR/css-writing-modes-3/#bidi-algo),
[CSS Text 3 §8.1](https://www.w3.org/TR/css-text-3/#text-indent-property).

The static-anchor checkpoint passes the exact layout gate at **3,099 full / 195
partial / zero failures**.

### Unbounded sequential column heights and forced runs

The auto-fill height path returned the total continuous-flow height even when
forced column breaks separated that flow. Measure the longest forced run in
each column group using the existing tree traversal and ancestor-coordinate
walk. Nested multicolumn contexts retain ownership of their own breaks; an
unbroken auto-filled sequence retains its entire height. This measurement does
not balance ordinary content or borrow an unrelated ancestor's height.

Fresh Chromium captures and native controls cover direct and wrapped breaks,
break-before/after, consecutive ordinary blocks, a definite height and an
independent nested multicolumn context. All 58 native controls pass. Text,
inline, sizing and multicolumn inventories have no losses;
`multicol-fill-auto-004` becomes full, bringing this round to **43 additional
full matches**. The combined diagnostic also exposes separate existing
problems in deeply nested fragment placement and columns-shorthand precedence;
its height checks do not claim those geometries now match. Authority:
[CSS Multicol 1 §7.1](https://www.w3.org/TR/css-multicol-1/#cf),
[CSS Break 3 §4.3](https://www.w3.org/TR/css-break-3/#forced-breaks).

The forced-run checkpoint passes the exact layout gate at **3,100 full / 194
partial / zero failures**, and the complete Radiant gate at **5,650 passed /
198 expected partial / one existing skip / zero failures**.

### Wrapping policies before inline style installation

Line-fit lookahead read the inherited block record before the following inline's
own declarations were installed. Resolve its specified keyword, including
variable substitution, before reading cached fields. Share the existing bounded
CSS substitution owner rather than retaining temporary parsed values. Wrapping
mode also needs the winning white-space/text-wrap component: use the same value
parser in preflight, inline atomic fitting and common-ancestor boundary checks.

Chromium controls distinguish authored and inherited break-all, line-break:
anywhere, emergency wrapping, variables, resets, normal/nowrap and explicit
text-wrap overrides. All ten span rectangles match, and all 59 native controls
pass. Text, inline, multicolumn and sizing inventories have no losses.
`word-break-break-all-inline-002` and `-005` become full, bringing this round to
**45 additional full matches**. Authority:
[CSS Text 3 §5.2](https://www.w3.org/TR/css-text-3/#word-break-property),
[CSS Text 4 §5.1](https://www.w3.org/TR/css-text-4/#text-wrap),
[CSS Cascade 5 §3](https://www.w3.org/TR/css-cascade-5/#shorthand).

The owner-policy checkpoint passes the exact gate at **3,102 full / 192 partial /
zero failures**.

### Inline font boxes after descendant bounds refresh

The small-font tab rectangles already had the correct height and baseline.
Later span recomputation unioned them with a taller descendant's font box,
undoing the enclosing inline's own height. For a single-line inline with direct
text, derive its vertical content box from that text's font rectangles while
retaining the complete horizontal flow extent. Existing bounds helpers supply
these edges; child overflow does not enlarge the ancestor's font box.

Six small-font tab/text/nested controls match fresh Chromium rectangles. The
explicit-break diagnostic retains correct separate text rectangles but still
exposes a separate multiline ancestor-box issue; this change does not claim
that union is fixed. All 60 native controls pass. The four affected inventories
have no losses; `tab-size-block-ancestor` becomes full, bringing this round to
**46 additional full matches**. Authority:
[CSS 2.1 §10.6.1](https://www.w3.org/TR/CSS2/visudet.html#inline-non-replaced),
[CSS 2.1 §10.8.1](https://www.w3.org/TR/CSS2/visudet.html#line-height).

The first full gate exposed eight vertical-positioning regressions: physical Y
is the inline advance after vertical mapping, not the font-content block axis.
Keep the new Y measurement in horizontal formatting contexts; the vertical
mapping path owns its X font box. A native vertical span retains all 112px of
direct and nested inline advance. All 139 positioning fixtures are restored;
the exact gate now passes at **3,103 full / 191 partial / zero failures**.

The complete Radiant gate also passes at **5,655 passed / 195 expected partial /
one existing skip / zero failures**. Fresh browser captures confirm the 30ch
word-space fixtures changed reference fonts (636.72px reference versus 486px
current browser and Radiant); do not force those old font metrics. The SVG/image
list-marker diagnostic also has a loaded raster marker in Radiant versus a
failed file-URL resource in the current browser capture. Neither discrepancy
authorizes reference-specific layout changes.

### Hyphenation lexical boundaries and overflowing syllables

Opening punctuation cleared the capitalization word-start flag, which also
prevented the following lexical word from reaching the hyphenation resource.
Track the lexical source range independently. An ordinary-space break can
rewind into an already probed word, so retain the explicit replay restart as
well. A separate strict pointer comparison rejected a generated break directly
after the current glyph: overflowing first syllables advanced empty lines
instead of committing their text. Accept that in-run automatic boundary through
the existing generated-mark/source-range path.

Fresh browser controls cover opening/closing punctuation, a narrow fragmentainer,
manual/disabled hyphenation and replay after a rejected earlier point. All 61
native controls pass. Text, inline, positioning, multicolumn and sizing inventories
have no losses; `hyphens-punctuation-001` becomes full, bringing this round to
**47 additional full matches**. Narrow automatic fragments now retain nonempty
source ranges instead of publishing the entire word after two empty lines.
`hyphenate-limit-chars-001` remains partial because that property's limits are
not implemented. Authority:
[CSS Text 3 §5.4](https://www.w3.org/TR/css-text-3/#hyphenation),
[CSS Text 3 §5.5](https://www.w3.org/TR/css-text-3/#overflow-wrap-property).

The lexical-boundary checkpoint passes the exact layout gate at **3,104 full /
190 partial / zero failures**.

### Hyphenation character limits: implementation plan

Register `hyphenate-limit-chars` as an inherited text property and share one
parser for its one-to-three nonnegative-integer/auto components. Preserve auto
as a computed sentinel; omitted suffix copies the prefix, and omitted prefix
is auto. Resolve CSS-wide values and variable substitution through the ordinary
cascade owner. Layout filters language-resource candidates by counted source
characters, excluding Mn marks and punctuation, with the specification's
suggested auto limits (five total, two before, two after). Keep authored soft
hyphens on their existing path. Cover grammar, inheritance/reset/variables,
zero limits, omitted components and narrow overflowing words. Authority:
[CSS Text 4 §6.3.4](https://drafts.csswg.org/css-text-4/#hyphenate-char-limits).

The implementation shares component validation, integer rounding/range handling
and language-resource filtering. Source character totals survive continuation
fragments, while prefix/suffix limits apply at each candidate. CSSOM uses the
existing numeric computation owner for font-relative integer math and follows
the canonical component omission checks in the WPT computed-value controls.
The generated property name is appended through the sole name catalog, retaining
existing IDs under D4.6.1v4; no generated file is maintained by hand.

All 21 layout controls match current Chromium. Computed-value checks additionally
follow the current specification and pinned WPT: the current browser rejects
literal zero limits, clamps negative integer math to one, and retains a repeated
suffix in one case. Those browser deviations do not justify changing the new
zero lower bound or canonical omission rules. All 62 native controls and the CSS
parser suite pass. The five focused inventories have no losses;
`hyphenate-limit-chars-001` becomes full, bringing this round to **48 additional
full matches**. Generated-file consistency, integer-cast lint and whitespace
checks pass. Animation of limit triples is outside this static-layout change.

The character-limit checkpoint passes the exact layout gate at **3,105 full /
189 partial / zero failures**.

The character-limit checkpoint also passes the full Radiant gate at **5,659
passed / 193 expected partial / one existing skip / zero failures**, and the
Lambda baseline at **6,842 passed / zero failures**.

### Hyphenated line clearance beside floats

Both automatic and authored hyphenation considered the text prefix separately
from its generated mark when a float shortened the line. A following syllable
could therefore lose its automatic break or overlap the float with an authored
mark. Before retaining the first hyphenation opportunity on an uncommitted line,
fit its prefix plus computed mark through the existing container-clipped float
space search. Move the pending rectangle and line cursor without resetting the
continuation's source or saved metrics. Earlier legal breaks and already
committed content retain their ordinary wrap path.

All 14 browser controls agree, covering both float sides, four float heights,
indentation, empty/long generated marks, an earlier space and nowrap. All 63
native controls pass. Five focused inventories have no losses; both
`hyphens-vs-float-clearance-001` and `hyphens-vs-float-clearance-002` become full,
bringing this round to **50 additional full matches**. Authority:
[CSS 2.1 §9.5](https://www.w3.org/TR/CSS2/visuren.html#floats),
[CSS Text 3 §5.4](https://www.w3.org/TR/css-text-3/#hyphenation).

The paired images additionally expose a paint-source defect: the first
continuation character is associated with the preceding generated-mark range
for queries, but raster/vector rendering consumes that association as glyph
ownership. Preserve query geometry and carry explicit byte ownership into the
common paint iterator. The generated mark stays on the prefix line and the
source character paints on the continuation line. Exercise terminal characters
and overflow projections as well as the ordinary multi-line case.

The float-clearance checkpoint passes the exact layout gate at **3,107 full /
187 partial / zero failures**.

The common source-range projection now serves raster, SVG, PDF and overflow
clipping. Terminal carried glyphs create their real paint fragment and mark the
line nonempty, so the final line contributes its height even when its query
range is empty. The old width-only terminal-inline fragment is removed. All 64
native controls and all 151 vector controls pass, including multibyte terminal
characters, empty/long marks and ellipsis. The 18-case paired images preserve
glyph ownership and line clearance; five inventories retain all full matches.

The completed paint-source checkpoint passes the exact layout gate at **3,107
full / 187 partial / zero failures** and the full Radiant gate at **5,663 passed /
191 expected partial / one existing skip / zero failures**.

### CJK hyphen boundaries in line layout and intrinsic sizing

The letter-unit path allowed a break immediately before U+2010/U+2013 in normal
and strict modes. Reject that boundary outside loose/anywhere modes through the
shared punctuation rule. Intrinsic sizing independently flushed every ideograph;
route its byte, grapheme and shaped-run paths through one boundary helper and
carry the owner's inherited `line-break` value into line measurement. Promote
the existing declaration-aware enum resolver rather than duplicate cascade or
variable handling. Max-content probes disable wrapping to retain whole-run
shaping, including when min-content uses `line-break:anywhere`.

All 65 native controls pass. The five focused inventories have no losses, and
`line-break-normal-hyphens-001` / `line-break-strict-hyphens-001` become full,
bringing this round to **52 additional full matches**. Current Chromium and the
pinned loose-mode oracle forbid the English-tagged ID-to-hyphen boundary that
CSS permits; Japanese-tagged controls exercise the permitted behavior. Preserve
the specification's loose behavior rather than force that remaining oracle to
pass. Ethiopic controls likewise expose changed fallback fonts and a changed
browser punctuation policy; their current geometry agrees with native layout.
Authority: [CSS Text 3 §5.2](https://www.w3.org/TR/css-text-3/#line-break-property),
[CSS Sizing 3 §2](https://www.w3.org/TR/css-sizing-3/#min-content).

The CJK checkpoint passes the exact layout gate at **3,109 full / 185 partial /
zero failures**.

### Hyphenation lookahead across out-of-flow siblings: implementation plan

Lookahead stops at the blockified display of an absolutely positioned sibling,
so an accepted conditional break in the earlier text cannot account for a later
in-flow suffix. Ignore out-of-flow boxes in the shared inline preflight and
retain zero-width empty inline boxes as part of the unbroken sequence. A terminal
generated mark must defer advancing the line until the next in-flow content:
intervening static-position anchors and empty inline boxes belong at the prefix
line's final cursor. Reuse the existing DOM content prescan for that decision,
keeping ordinary spaces, forced BRs, nowrap and unrelated block flow unchanged.
Authority: [CSS Text 3 §5.3](https://www.w3.org/TR/css-text-3/#hyphenation),
[CSS Position 3 §3.5.2](https://www.w3.org/TR/css-position-3/#static-position).

The shared preflight now ignores out-of-flow boxes and treats empty ordinary
inlines as zero-content members of the sequence. BR and WBR retain their
mandatory/optional boundary semantics even without text children; native forced
break controls and both affected word-space controls caught that distinction.
Terminal generated text marks retain a pending line break until the existing
DOM content prescan finds the next in-flow content. Empty inline boxes and
static anchors consequently remain at the prefix line's final cursor. The
separate RTL closed-inline mark representation retains its existing finalization
path.

All 66 native controls pass, including the expanded CJK variable/initial cases
and twelve out-of-flow controls with seven anchor checks. Both
`hyphens-out-of-flow-001` and `hyphens-out-of-flow-002` become full; the five
focused inventories have no losses. This round reaches **54 additional full
matches**. The exact layout gate passes at **3,111 full / 183 partial / zero
failures**. Automatic lexical scanning still uses each text node's resource
range; this preflight change repairs accepted candidates across intervening
out-of-flow nodes, without introducing cross-node dictionary ownership.

Fresh Arabic transform/shaping captures agree with native 74px normal leading,
where the old oracle has 87px. A direct platform probe also returns a 16px
synthesized vertical advance for Menlo's zero, whereas Chromium uses 19px for
upright ch lengths. Neither observation justifies replacing current font
metrics with reference-specific dimensions.

The out-of-flow checkpoint passes the full Radiant gate at **5,669 passed / 187
expected partial / one existing skip / zero failures**.

### Selected-face punctuation-space synthesis

U+2008 was absent from the selected document face, so layout and paint used a
replacement font's narrow space instead of the selected period's advance.
Classify that space through the existing explicit-space measurement path and
share one font-based metric across intrinsic sizing, line fitting and bidi
redistribution. Keep an authored space glyph when the selected face covers it.
For a synthesized blank, raster, overflow clipping, shadows, decorations and
vector glyph traversal advance without drawing the replacement glyph. The
original source bytes remain unchanged.

All eight browser controls match native geometry, including an overflowing
unbreakable prefix, an inline boundary, RTL, intrinsic sizing and ellipsis. The
paired raster, SVG and PDF images retain the blank and following glyph positions.
All 67 native text controls and 151 vector controls pass. Five inventories have
no losses; `trailing-other-space-separators-break-spaces-010` becomes full,
bringing this round to **55 additional full matches**. Authority:
[Unicode 17 §6.2.3](https://www.unicode.org/versions/Unicode17.0.0/core-spec/chapter-6/),
[CSS Text 3 §4.3](https://www.w3.org/TR/css-text-3/#white-space-phase-2).

The remaining autospace probes separate several issues: the current
specification defines normal as alpha/numeric spacing, while the Chinese
punctuation oracle inserts four extra gaps; upright character classification and
vertical advances need a shared layout/paint design; bidi autospace must be
inserted after visual reordering. The supplementary-ideograph control currently
differs only on the lines containing U+3002 because the browser uses Ahem's
missing glyph while native chooses a CJK fallback. No reference-specific font
selection is introduced.

The shared font-layer implementation passes the exact gate at **3,112 full /
182 partial / zero failures** and the full Radiant gate at **5,672 passed / 186
expected partial / one existing skip / zero failures**. All 83 display-list, 67
native text and 152 vector controls pass. Keeping synthesis in the common font
layer lets dependency-light glyph-path consumers use the same metric; the new
vector control verifies its CSS advance at pixel densities one and two.

### Terminal whitespace across empty inline decorations

The end-of-line trim removes the text advance after following empty inline
decorations already acquired their positions. Reuse the source-context edge
walker with an explicit line-content mode: ignore collapsed whitespace and
out-of-flow boxes, retain preserved spaces and atomic content, traverse
`display:contents`, and stop at forced line boundaries. A terminal text run can
then consume its existing collapsed-space ledger before later decorations are
positioned. Source bytes and ordinary separator behavior stay in the shared
text paths. Authority: [CSS Text 3 §4.1.2](https://www.w3.org/TR/css-text-3/#white-space-phase-2).

Early trimming exposed a second defect: the atomic-only fallback for inline
line identity compared the y-position of collapsed whitespace against an atomic
child, declaring a single-line ruby annotation multi-line. Exclude collapsed
text in both the first-fragment lookup and that fallback. The source probe also
keeps ruby base and annotation contexts independent by reusing the existing
annotation classifier. An overflowing annotation child must not replace its
annotation font box. Authority: [CSS Ruby 1 §2.5](https://www.w3.org/TR/css-ruby-1/#white-space).

The exact gate exposed prospective-content and flattened-tree cases outside
the initial five inventories. A not-yet-materialized pseudo box is not empty:
retain its separator until its counter/quote scope produces content, and defer
slot-content decisions until projection. Continue the source probe through
`display:contents` ancestors; forced block/BR boundaries still end the line
before examining pending generated content. Native controls cover before/after
content and a contents ancestor. The first exact attempt failed these gates; its
result is not counted as a completed checkpoint.

The source probe treats an inline math box as an atomic boundary through the
existing MathML helper. Its source subtree can be empty while the box still
controls a preserved separator between math boxes; ordinary inline recursion
would incorrectly conclude that there is no following content.

All 68 native controls pass. Seven focused inventories retain their prior full
matches. `line-edge-white-space-collapse-001` becomes full, bringing this round
to **56 additional full matches**. The completed exact gate passes at **3,113
full / 181 partial / zero failures**. Paired Ahem decoration images agree on
glyph and border positions; antialiasing/line-edge raster differences remain.
The additional ruby control verifies its 10×5 annotation font box; its baseline
still differs from Chromium and is not claimed as a complete visual match.

### Float exclusions after a text preflight starts a new line: design

The next text node can break during preflight before it installs a new current
view. `adjust_line_for_floats` then consults the previous float view and returns
from its DOM-ancestry check, despite the restored block context describing the
parent line. The active block/BFC context already defines which float list owns
that line; remove the stale-view gate so overlapping left and right floats both
constrain it. Independent float contents retain their own BFC. Browser probes
cover three float heights, a later inline wrapper, same-line placement and the
float child context. Authority: [CSS 2.2 §9.5](https://www.w3.org/TR/CSS22/visuren.html#floats).

The terminal-whitespace checkpoint passes the full Radiant gate at **5,674
passed / 185 expected partial / one existing skip / zero failures**.

The active-BFC implementation passes all 69 native text controls. Six Ahem
controls match browser line and float geometry, including partially overlapping
float heights and text inside the float itself. Seven inventories retain all
prior full matches; `text-wrap-balance-float-005` becomes full. The unrelated
external-float balancing case still differs because balance probes estimate
line count and measure without the actual float-constrained sequence.

The active-float checkpoint passes the exact gate at **3,114 full / 180 partial /
zero failures**, bringing this round to **57 additional full matches**.

### Mixed hanging-space suffixes: next design

A trailing `break-spaces` space takes up space and prevents earlier U+3000
glyphs from hanging, but the current whitespace branch keeps the earlier
hanging ledger. Clear that ledger when accepting a non-hanging preserved
space. The forced-break path also clamps an unconditional U+3000 prefix
together with a conditional pre-wrap suffix. When the entire conditional
suffix overflows, both suffix and prefix hang; when some conditional spaces
fit, only the overflowing conditional portion hangs. Retain the unconditional
portion explicitly in the existing current/candidate/pre-SHY hanging state,
extracting their common record rather than adding a third scalar-copy variant.
Browser controls distinguish these cases and preserve later hanging spaces
after non-hanging content. Authority: [CSS Text 3 §4.1.2](https://www.w3.org/TR/css-text-3/#white-space-phase-2) and [§8.2](https://www.w3.org/TR/css-text-3/#hanging).

The float-context checkpoint passes the full Radiant gate at **5,676 passed /
184 expected partial / one existing skip / zero failures**.

A scalar unconditional subtotal is insufficient when conditional and
unconditional spaces alternate. Preserve immutable per-space records with the
policy and text rectangle that owns each advance. The current line and both
break candidates share one small hanging-state record. At a forced break, walk
back from the physical line end, hanging unconditional units and overflowing
conditional portions until a fitting conditional unit stops the suffix. Track
ASCII ink trimming on its owning rectangle. View-pool records follow D4.5.1v4
lifetime ownership, and candidate snapshots borrow immutable records. `pre` and
`break-spaces` spaces retain their advances and end an earlier hanging suffix.

Only the terminal pre-wrap sequence is directly followed by the forced break.
Earlier pre-wrap spaces followed by an unconditional hanging glyph also hang
unconditionally; the backward walk switches to that rule at the first such
glyph. Alternating-mode browser controls and WPT hanging-whitespace-002 verify
this distinction.

The RTL control exposes an independent optimization defect: an RTL paragraph
with Latin-only text still reorders its trailing neutral spaces by UAX #9.
The bidi early exit assumed that absence of a strong RTL code point implied
absence of visual changes. Restrict that shortcut to LTR paragraphs; retain
the existing atomic-flow limitations. Authority: [CSS Writing Modes 4 §2.4](https://www.w3.org/TR/css-writing-modes-4/#bidi-algo).

All 70 native text controls pass, including 26 mixed-space geometry checks,
three rectangle-owned trim checks and the RTL-neutral control. Sixteen source
cases cover fixed spaces, conditional suffixes, alternating modes and `pre`.
Seven inventories have no losses; both `hanging-whitespace-001.tentative` and
`hanging-whitespace-003.tentative` become full. Paired images retain the same
space/background positions with raster edge differences.

The first exact run rejects an RTL form-line regression: its first logical text
rectangle already follows the control physically, so using that rectangle as
the paragraph origin adds the control width a second time. Probes of both
atomic orders show mixed provisional coordinates: ordinary text still uses
logical advances while atomic boxes may already use RTL positions. For an
outer paragraph with atomic inline boxes, use its float/indent-adjusted flow
origin. Text-only paragraphs retain their measured origins, including shaping
and hanging-punctuation adjustments; nested isolates retain their own
interval. Native controls cover both orders and inline padding/borders.
Authority: CSS Writing Modes 4 §2.4.3. No rejected checkpoint is counted.

The completed exact gate passes at **3,116 full / 178 partial / zero failures**.
All 71 native text controls pass, and the eight focused inventories retain
every previous full match. The RTL form retains its previous element scores;
its existing fragmented text-range difference remains partial. The two hanging
whitespace tests bring this round to **59 additional full matches**.

The mixed-space/bidi checkpoint also passes the full Radiant gate: **5,680
passed / 182 expected partial / one existing skip / zero failures**.

### One-column overflow extent: design

The single-column shortcut formats ordinary flow and sizes the multicol from
the wrapper's normal-flow height. It omits visible in-flow overflow that must
fit the auto-sized column, even though the wrapper keeps its specified size.
Reuse the existing in-flow extent walker to size an unbounded column.
Positioned descendants and scroll-container contents remain separate; a
definite fragmentainer still caps its own size. Six Chromium controls cover
balance/auto filling, a definite height, clipping, positioning and following
normal-flow content. Authority: CSS Multicol 1 §§2, 7.1 and CSS Break 3 §2.1.

The one-column controls pass all 72 native text tests. The first inventory
attempt exposed a collapsed-margin regression: a placed child already includes
its leading margin, so its measured end adds only the trailing margin. Nine
controls now include collapsed margins and nested clipped content. The corrected
seven-category inventory has no losses; `multicol-span-all-children-height-005-ref`
becomes full. Its exact checkpoint is still pending.

### Whole line boxes before a spanner: design

Pre-spanner collection marks ordinary block wrappers as fragmentable. The
balancing search applies its one-line floor only to a lone flow item; two
one-line siblings can therefore receive a target smaller than either line.
Their published border boxes then split at arbitrary block offsets. Apply
the unbreakable line floor to each one-line item, while keeping line-boundary
rounding for a lone multi-line item. Only the group's first leading margin
sets a mandatory floor; later column-start margins may be discarded. Ordinary
blocks and buttons share this constraint. Authority: CSS Break 3 §4.1 and
CSS Multicol 1 §7.1.

All 73 native controls pass, including whole line geometry before/after a
spanner and discarded column-start margins. The seven inventories have no
losses, and `multicol-span-all-button-001-ref` becomes full. The two multi-column
fixes pass the exact gate at **3,118 full / 176 partial / zero failures**,
bringing this round to **61 additional full matches**. The two positioned
button references retain unrelated static-anchor differences and remain partial.

The multi-column checkpoint passes the full Radiant gate at **5,684 passed /
180 expected partial / one existing skip / zero failures**.

### Inter-character justification: design

The shared character-boundary predicate only admits CJK and clustered-script
pairs. `inter-character` needs every adjacent typographic character-unit pair,
including Latin, symbols and spaces, while keeping graphemes and cursive letter
pairs joined. Thread the computed method through counting and the semantic
paint run; use the same predicate for raster glyphs, shadows, outline/effect
paths, SVG dx and PDF paths. Inter-character expansion belongs to character
boundaries, so it must not also expand a word-space slot. Invisible formatting
controls do not supply a unit; ZWJ still participates in emoji graphemes.
Authority: CSS Text 3 §§6.4, 6.4.4 and 7.2.

The first raster comparison exposes a separate paint-policy defect: the
painter checks inherited `text-align`, ignoring a justified last line. Its
independent width scans also reinterpret shaped advances. Store the line's
per-opportunity expansion on `TextRect` and retain it in paint projections.
Raster, SVG and PDF then consume one shared spacing result instead of
re-measuring natural width. This removes three copies of the inference shape
and preserves the distinction between expanded ranges and actual justification
for `text-justify:none`. Eight controls check recorded expansion and glyph
placements; combining-mark fallback remains an independent visual difference.

All 74 native text controls, 152 vector tests and 83 display-list tests pass.
Seven inventories retain all previous full matches. `text-justify-distribute-001`
and `text-justify-inter-character-001` become full. Raster, SVG and PDF agree
with the browser on Latin/space/punctuation/invisible-control glyph positions;
the combining-mark case retains its independent cluster-fallback difference.
The exact gate passes at **3,120 full / 174 partial / zero failures**, bringing
this round to **63 additional full matches**.

The justification checkpoint passes the full Radiant gate at **5,687 passed /
178 expected partial / one existing skip / zero failures**.

### Nested block-column capacities: next design

`getclientrects-003` exposes correct positioned fragments but incorrect
in-flow nested boxes. The nested slot projector is limited to over-wide
content; ordinary block descendants use equal capacities and can count an
inner fragment as an outer fragmentainer. Reuse the existing unequal-capacity
slot iterator for uninterrupted nested block flow. Return inner rectangle
count separately from consumed outer flow and outer fragment count. The first
outer remainder applies to every inner column in that set; later sets use
full capacity. Unwrapped finite columns overflow in the inline direction
regardless of whether a flow child itself has columns. Ordinary/partial/full
first sets and an overflow continuation provide independent browser controls.
Authority: CSS Break 3 §2.2 and CSS Multicol 1 §§2, 8.1; CSS Multicol 2 §4.4.

The first generalized inventory is rejected: it improves two partial cases but
regresses existing balanced, contained and authored-height nested flows. A
resolved content sequence must not replace an authored nested block size or
a specified row track. Continuous sequential slots also do not represent the
consumption caused by forced breaks or a balanced last set. Keep those
constraints explicit while refining the projector; no gain from this failed
inventory is added to the manifest.

The authored-size refinements restore the native forced-break controls and
most inventory regressions. Sequential source overflow is distinct from a
fixed wrapper's own rectangles: reconstruct its last in-flow descendant end
with the existing static-flow/continuous-size helpers and include that visible
flow in the enclosing slot summary without enlarging the wrapper. Direct floats
remain parallel flows. Reuse one eligibility predicate for sizing and projection.

The default overflow-policy change remains deferred: it interacts with existing
parallel-float and fixed-row projection and still loses two prior full matches.
Restore that separate policy to the pre-change implementation. The overflow
control explicitly requests `column-wrap:nowrap`, isolating the completed slot
projection from the unresolved default-policy investigation. No test reference,
tolerance or baseline expectation is changed.

The explicit-nowrap overflow diagnostic exposes a separate nested break-before
limitation: the existing planner moves the whole auto-sized inner flow to a
new outer column and leaves it unfragmented. Keep that diagnostic under `temp/`
for the next investigation; the completed native control covers the partial,
full and single surviving column sets. The fragment-border image comparison
also retains a separate paint issue: its absolute border is drawn around the
fragment union instead of the individual sliced pieces. Neither visual issue
is claimed as fixed by the layout checkpoint.

All 75 native controls pass, and seven focused inventories retain every
previous full match. `getclientrects-003` and `multicol-nested-column-rule-002`
become full. The exact gate passes at **3,122 full / 172 partial / zero failures**,
bringing this round to **65 additional full matches**. The checkpoint covers
layout geometry; the recorded overflow-planner and fragmented-border visual
issues remain separate investigations.

The nested-slot checkpoint also passes the full Radiant gate at **5,690 passed /
176 expected partial / one existing skip / zero failures**.

### Auto-height monolithic wrapper fragments: design

The principal-box guard treats an auto-height wrapper's continuous content
height as an authored height. It skips the existing monolithic child planner
and creates six uniform slices instead of three overflowing pieces. An
authored height still owns its own sequence; auto height follows the child
planner. Preserve each piece's visible overflow separately from consumed
fragmentainer flow, so client rectangles can be 250px high while following
content advances through 100px column capacities. Reuse the existing planner
and fragment writer. Browser controls cover mixed/one/fitting children, an
authored height, leading padding and following flow. Authority: CSS Break 3
§§2.1, 4.1 and CSSOM View §6.

The first inventory gains `getclientrects-008` without regressions, and the
first three raster cases match Chromium exactly. The following-flow control
rejects the first attempt: the distributor only consumed the planner's result
for forced breaks, retaining the old continuous height for the monolithic
sequence. Consume explicitly recorded flow extents from the shared fragment
reader; existing trim-aware sequences keep their own extent. The fragment
record retains both visible height and consumed flow, including a single
visible piece, so later projection does not reconstruct flow from painted
overflow. The checkpoint is still pending.

All 76 native controls pass, including following flow. Seven focused inventories
retain all previous full matches. `getclientrects-008` becomes full. The exact
gate passes at **3,123 full / 171 partial / zero failures**, bringing this round
to **66 additional full matches**. The first three raster cases match Chromium
exactly; the following-flow case is checked by native geometry assertions.

The monolithic-wrapper checkpoint also passes the full Radiant gate at
**5,692 passed / 175 expected partial / one existing skip / zero failures**.

### Vertical nested column slots: design

`getclientrects-004` calculates nested column widths from a union height and
projects block descendants through equal capacities. Vertical layout needs the
same partial-first-set sequence as horizontal layout. Generalize the existing
slot planner in logical coordinates, convert its rectangles through one axis
mapping, and publish consumed outer flow separately from visible unions.
Browser controls cover vertical-rl and vertical-lr, partial/full first sets and
a short final piece. Authority: CSS Multicol 1 §§2–3, CSS Writing Modes 3 §6
and CSSOM View §6. No Lambda formal semantics or design ruling changes.

The initial generalized inventory is rejected: it loses one old nested full
match. Keep a principal box's size separate from visible descendant overflow,
and honor `vertical_auto_inline_size_constrained` when deciding whether an
inline size was authored. Physical fragment unions also change their local
origin: carry that origin and source block offset into recursive projection,
and avoid translating descendants a second time after their pieces have been
published. Both writing directions and a single surviving set now match the
browser's bounds and physical piece rectangles.

The paired images retain the existing fragmented absolute-border paint issue:
its border still uses the union. The CSSOM bridge also returns the bounding
rectangle rather than the fragment list. Positioned fragment records currently
use containing-block coordinates; native assertions check that representation.
Neither separate limitation is claimed as fixed by this geometry checkpoint.

All 77 native controls and the seven focused inventories pass with no lost
full matches. `getclientrects-004` becomes full. The exact gate passes at
**3,124 full / 170 partial / zero failures**, bringing this round to
**67 additional full matches**. Targeted dimension lint and diff checks pass.

The vertical-slot checkpoint passes the full Radiant gate at **5,694 passed /
174 expected partial / one existing skip / zero failures**.

### Relative offsets on fragmented flow items: design

The column cursor replaces a relatively positioned wrapper's block coordinate,
while the saved inline coordinate still contains the original relative inset.
This loses `top`/`bottom` and mixes visual offsets with normal flow. Resolve the
existing relative-position helper when collecting normal inline placement,
then translate each published flow item after the cursor has consumed it.
Browser controls cover both inset edges, percentages, fragment rectangles,
text and following flow. Authority: CSS Position 3 §3.4 and CSS Break 3 §5.4.

The initial focused inventory gains both move-after-paint cases with no losses.
The percentage control reveals a separate containing-size defect: relative
inline insets use the entire multicol content width instead of one anonymous
column. Share the column-inline-size helper with post-shrink child sizing,
including zero-width columns, and select the physical inline axis in the
relative resolver. Additional controls check both vertical writing directions.

All 78 native controls pass. Seven inventories retain all prior full matches
and gain `move-with-text-after-paint` and its reference. The paired raster
images agree on the positions; small Ahem edge differences remain. The vertical
diagnostics still expose a separate auto-inline-size union issue and remain
under `temp/`; they are not included in this checkpoint's claimed controls.

The exact gate passes at **3,126 full / 168 partial / zero failures**, bringing
this round to **69 additional full matches**. Native controls, targeted
dimension lint and diff checks pass; references and baseline expectations are
unchanged.

### Cleared float runs in intrinsic sizing: design

`letter-spacing-206` has correctly sized children but gives their floating
parent 1,144px instead of 266px. Intrinsic max-content sums all float children
even though `clear:left` puts each child below the preceding left float.
Track the active float sides and flush a run when the next float clears every
active side; retain the existing aggregate for partially cleared mixed sides,
whose overlapping heights need a separate model. Reuse one run-flush operation
for clearance, non-float blocks and the terminal run. Adjacent inline sizing
uses the widest completed float run rather than adding cleared rows together.
Browser controls cover left/right/both clearance, no clearance, irrelevant
clearance and mixed uncleared floats. Authority: CSS 2.1 §§9.5.2, 10.3.5 and
CSS Sizing 3 §§3.2.2, 5.1.

The relative-position checkpoint also passes the full Radiant gate at
**5,697 passed / 172 expected partial / one existing skip / zero failures**.

All 79 native controls pass, and seven inventories retain every old full match.
`letter-spacing-206` becomes full. The exact gate passes at **3,127 full /
167 partial / zero failures**, bringing this round to **70 additional full
matches**. The image capture finds a separate visibility-bounds defect: the
renderer requests out-of-flow bounds, but the shared helper unconditionally
excludes floats and culls their zero-height wrapper. Honor the existing flag
for floats; ordinary flow measurements keep excluding them. Add assertions
for both modes and compare the same raster control again. Authority: CSS 2.1
§§9.5, E.2 and CSS Overflow 3 §2.1.

After honoring floating overflow in requested paint bounds, the six-control
raster matches Chromium exactly: **zero differing channels out of 3,840,000**.
All 79 native controls pass, including distinct normal-flow and paint bounds
for the zero-height wrapper. The broader checkpoint rerun is pending.

Seven inventories show no gains or losses from the paint-bounds correction.
The exact gate rerun remains **3,127 full / 167 partial / zero failures**.
Targeted dimension lint and diff checks pass; the full Radiant rerun is pending.

The final float checkpoint passes the full Radiant gate at **5,699 passed /
171 expected partial / one existing skip / zero failures**. The long-running
objective remains active at 70 additional full matches.

### Root strut line height during inline breaks: design

Wrapped Markdown code advances by the smaller inline's computed line height
while the containing block owns a taller strut. Independent Ahem controls
show the same defect for automatic/forced breaks and explicit/normal heights.
Bind the block's font and line-height together at the existing block, flex and
grid content setup points. Line formatting uses that immutable root height,
then restores the inline's computed height when text layout resumes. Reuse
one binding helper for the three entry points. Authority: CSS 2.1 §10.8.1 and
CSS Inline 3 §§2, 5.3. No Lambda formal semantics or design ruling changes.

The first approach matches the five Ahem controls but is rejected: temporarily
replacing the entire line-height style loses a styled forced-break contribution
and regresses six existing shaping full matches. Keep the inline style intact.
Read the saved root height only for strut leading and the line's minimum CSS
height; normal-height root boxes also contribute their own ascent/descent.
The refined implementation is pending validation.

The reader still rejects the checkpoint because table-cell setup inherits the
table's saved root height instead of binding the cell's own height. Use the
shared root setup in all five existing cell metric/content entry points. The
forced-break control exposes the other missing input: an empty styled inline
also supplies line-height. The line minimum includes that recorded inline
contribution while retaining the immutable block strut. No test expectation
is changed.

All 79 old native controls pass after the cell/empty-inline corrections. The
focused comparison retains one baseline-export regression: `baseline-005`
ignores its direct `SS` text and exports the later spanner's baseline. The new
correct empty-line strut exposes that prior omission. Include direct horizontal
text rectangles and forced breaks in the existing positioned-baseline scan,
reusing the shared font-baseline resolver and whitespace-content classifier.
Vertical direct-run baseline export remains its own axis-mapping task.
Authority: CSS Align 3 §9.1 and CSS Multicol 1 §2. Temporary trace removed.

The direct-run scan restores `baseline-005` but reveals that empty block caches
can hold synthetic border-edge baselines. They are not formatted line baselines
and must not outrank a later real line (`baseline-007`). Require placed line
content before using that fallback; a computed zero baseline remains valid.
All previous native controls pass. Add six independent root/cell strut controls
and retain the existing WPT baseline cases as regression checks.

The forced-break exporter also needs its inherited font: BR nodes have no
text-font snapshot, and using the block's initial ascent can include leading
that is already present in the break's position. Resolve the inherited face
and use the shared font-baseline API without adding that leading twice. The
next inventory and required gates are pending.

The exporter trace confirms that `baseline-007` now exports its actual BR
baseline. The generic element-cache change does not address the active caller
and is reverted. Flex's `calculate_item_baseline` tries child synthesis before
its existing used first-line cache, selecting the preceding empty block's
border edge. Consult that cache first while preserving the established nested
flex and multicol export paths. The used formatted line then supplies the flex
item's baseline. Temporary exporter trace removed.

All 80 native controls pass. Eight focused inventories retain all prior full
matches. The strut controls match Chromium's geometry, and the paired images
agree on glyph positions with minor raster-edge differences (5,652 differing
channels out of 3,840,000). This checkpoint improves partial layout behavior
but adds **no new full match**; the manifest remains at 70. The exact and full
Radiant gates are pending.

The exact gate rejects this strut checkpoint: the already partial
`md_ky-readme` element score falls from its 36.4% baseline floor to 35.1%.
No full match is lost or gained, but the required gate has one failure. Save
the feature-only patch and six browser/native controls under
`temp/layout-analysis/round7/unaccepted-block-strut.patch`, then restore the
verified 70-pass source checkpoint. The independent controls establish the
strut defect; the remaining Markdown regression must be diagnosed before
this implementation can be accepted. No reference or expectation is changed.

The restored 70-pass checkpoint is reverified: exact layout **3,127 full /
167 partial / 0 failed**, full Radiant **5,699 passed / 171 expected partial /
1 existing skip / 0 failed**, including all 79 native text-query controls.

## Investigation: centered inline static positions around column spanners

Fresh Chromium captures reproduce the stored reference for both positioned
button variants. Direct inline auto-inset anchors use the center of the
369.344px multicol content box (absolute x=208px), at the preceding balanced
column line end (38.328px and 102.516px). Native anchors remain at a column
edge and/or the continuous pre-fragmentation block offset. CSS Position 3
§3.5.3 derives the static-position rectangle from the hypothetical formatting
context; CSS 2.1 §9.4.2 suppresses phantom line height but does not discard its
static cursor. Inspect phantom-line completion and later multicol projection
before making a correction. No implementation change is accepted yet.

The continuous trace proves two missing finalization paths: the next block
checks only `start_view`, and `layout_flow_children(..., true)` completes only
non-empty lines. Out-of-flow inline boxes can leave a deferred static cursor
without an in-flow start view. Promote the existing phantom-line completion
helper and use it at both boundaries; no line height is added.

Independent non-button controls clarify the position rule: the browser cursor
is centered **within the preceding column**, whose center happens to coincide
with the button center when the last block occupies column two of three. It
is not generally centered across the multicol container. Record the completed
phantom cursor's aligned inline offset and project it from the preceding flow
fragment. Use the actual positioning containing block for coordinates,
exclude floats from normal-flow predecessors, subtract a predecessor's
relative displacement, retain explicit insets and the anchor's margins, and
prevent the continuous out-of-flow projector from projecting it a second time.
The new native control covers left/right/center, unequal atomic block heights,
leading anchors, spanners, fixed positioning, relative predecessors, margins,
explicit insets, an exact fragment boundary, a float, and ordinary block flow.
All 16 anchor positions match current Chromium exactly.

The original temporary fractional empty-block controls also expose two
separate pre-existing limits: a rounded quotient can publish an empty extra
fragment, and a centered float-only phantom line can retain the full-width BFC
bound. Those controls remain diagnostic; neither defect is claimed fixed by
this cursor change. Real inline text after an anchor requires the existing
mixed-inline projection path and is likewise not claimed fixed here.

Checkpoint 72: exact layout **3,129 full / 165 partial / 0 failed**; all
80 native text-query controls pass. The 16-anchor control PNG is identical
to Chromium (0 differing channels out of 5,760,000). Eight inventories retain
all prior full matches and have no regression-floor failures. Targeted Radiant
integer-cast lint and `git diff --check` pass. The full Radiant gate is pending.

The full Radiant 72 checkpoint passes: **5,702 passed / 169 expected partial /
1 existing skip / 0 failed**.

## Investigation: zero-width flex flow in a narrow root column

Fresh Chromium confirms `multicol-dynamic-add-002-ref`: a 0px-wide body and
flex q, both 18px high. Native q expands to its content width and is 18.4px
high. The UA default switch incorrectly includes q among italic elements;
[HTML §15.3.4](https://html.spec.whatwg.org/multipage/rendering.html#phrasing-content-3)
limits that default to cite/dfn/em/i/var. Remove q from that list so normal
inheritance and authored font-style remain authoritative.

The flex initializer also requires a positive used width to recognize a
block flex container's resolved inline size as definite. A 0px width is a
valid definite size (CSS Sizing 3 §2), not an intrinsic sizing sentinel; the
available-space representation already records definiteness separately.
Inspect the resulting geometry and independent controls before changing
that classification. No additional full pass is claimed yet.

Recognizing the zero constraint fixes q's width to 0px, but its 18.4px
anonymous-item height still creates an erroneous second body fragment. The
anonymous text measurement uses a prior text view's height or the raw font
ink height. Independent Ahem controls with 40px inherited line-height render
30px flex boxes, while Chromium renders 40px; authored line-height:0 renders
20px natively and 0px in Chromium. Resolve the anonymous block's inherited
line-height with the existing cascade/value helpers, including normal font
metrics and valid zero. This is separate from the rejected block strut
experiment and does not modify the general inline strut implementation.

The anonymous-height attempt is rejected at focused verification: Markdown
loses `md_nuxt-readme` (100% to 99.3% element match) and crosses the existing
`md_test-emoji` floor (97.3% to 96.4%). It improves the 40px controls, but their
inline-flex baseline and zero-height ink-overflow cases still expose additional
ownership problems. Save the attempt in the temporary directory and roll back
that height reader. Retain only the separately diagnosed q UA inheritance and
zero-width definiteness changes for verification. No full pass is added.

The remaining zero-width controls isolate sizing with 20px Ahem line-height;
all eight browser boxes match natively, including auto/explicit width, padding,
column and reverse flex directions, an empty box, an intrinsic inline-flex
control, and ordinary definite width. Preserve the 40px, zero-line-height,
and baseline counterexamples under `temp/`; no existing expectation changed.
All 82 native text-query controls pass, including q inheritance/author-style
checks. The zero-width raster agrees on positions, with 738 differing edge
channels out of 3,840,000. Exact layout verification is pending, and the new
full-match manifest remains at 72.

The q UA/zero-constraint checkpoint passes the exact layout gate unchanged at
**3,129 full / 165 partial / 0 failed**. All 82 native controls pass; no new
full match is counted for these two corrections.

## Investigation: float lifetime at a column spanner

`float-with-line-after-spanner` has a 225px preceding float and a 65px normal
block. The zero-height spanner appears at native y=115px with a reduced inline
size; Chromium places it at y=275px at full width. The one-column fast path
uses a continuous BFC through the spanner. CSS Multicol 1 §6.1 ends the
preceding column line and starts another after the spanner; its floats cannot
intrude into the spanner or the subsequent column line. Six independent
controls confirm the same error for left/right/parallel floats, margins, a
later float, and the no-float control.

Reuse the normal child traversal with a before-child preparation callback.
The multicol callback completes the preceding inline line, accounts for its
float margin-box extent, and resets that line's float context before formatting
a direct spanner. Extract the existing spanner predicate to accept a DOM
element so it also works before the child's layout view is installed. The
normal traversal remains responsible for final line completion. Verify the
complete multicol category before accepting this correction.

The six control geometries now match Chromium exactly, including all span and
tail boxes, parent heights, and the later float's origin. All 83 native
text-query controls pass. Eight inventories retain prior full matches and
have no regression-floor failures; `float-with-line-after-spanner` is a new
full match pending the exact gate. Integer-cast lint and diff checks pass.

The paired control raster differs in 1,800 channels out of 3,840,000, entirely
where the later float overlaps the following block's background: Chromium
paints the float above that background, while native paints the background
over it. Geometry is correct; float paint ordering is a separate remaining
root and is not claimed fixed here. Exact layout verification is pending.

Checkpoint 73: exact layout **3,130 full / 164 partial / 0 failed**. The
manifest has 73 verified additional full matches and no lost full matches.
All 83 native text-query controls pass; the full Radiant gate is pending.

The full Radiant 73 checkpoint passes: **5,706 passed / 168 expected partial /
1 existing skip / 0 failed**.

## Investigation: nested auto columns constrained by remaining outer space

In `multicol-nested-019`, 75px of a 100px outer column is occupied. The nested
auto-height, auto-fill two-column box has two 25px lines and break-before:avoid.
It fits a whole outer column, so the ancestor classifies it as monolithic and
`should_fragment` is false. That prevents the existing nested fitting helper
from trying the remaining 25px, despite both inner columns being able to hold
the content there. `multicol-nested-020` has the same relationship with 90px
remaining and two 50px lines. The inline fragment collector also omits atomic
inline blocks, so a nested fitting attempt can move BRs while leaving their
inline-block contents in the original flow. These are distinct diagnosed
caller/inline-item gaps; confirm the classification before correcting them.

The targeted trace confirms h=50px, outer offset=75px, target=100px,
`should_fragment=false`, avoid=true, auto fill. Broaden the fitting caller for
an auto-fill inner context, leaving its own auto-height/capacity guards in
charge. Extend the inline leaf collector's optional horizontal atomic-item
mode and retain those boxes and their BRs in the same completed projection.
The initial build identified a second projector caller; the new optional
result parameter preserves its interface. Discard the first focused capture
as stale because that build failed before linking the new executable. Use
`nested-atomic-remainder-built` for actual verification.

The first built inventory loses `multicol-nested-015`, whose real block prefix
is not part of an inline-only projection. Keep mixed block-prefix flow with
its existing block projector. A trace then exposes a classifier detail: the
shared "block flow box" view predicate intentionally includes inline-blocks
for other consumers. The inline-only eligibility check must explicitly
exclude atomic inlines, so add an optional mode to the existing child scan,
retaining its original default for other callers. Nested BR markers continue
through their established column-edge owner; preserve their baseline offset
from the collected line when doing so. No new full match is counted until
the corrected classifier and native controls are verified.

The active atomic trace confirms two 50px lines and one line slot per 90px
inner column. Both atomic boxes now move to separate inner columns. The final
BR edge pass was still resetting the completed line's half-leading to zero;
retain its recorded local offset when the atomic projector owns that line.
Both nested WPT cases now fully match, with no regression floors crossed in
eight inventories.

The new authored-height control's empty 100px tail exercises a separate
remaining-space fragmentation difference: native moves it whole to the next
outer column, while Chromium splits its box. Keep that counterexample under
`temp/nested-inline-authored-height-empty-tail.html` (within the analysis
folder). Give the permanent authored-height control an explicit
break-inside:avoid tail to isolate the preserved-height contract; capture the
new browser expectation before updating the new assertion. Existing tests,
references, and expectations remain unchanged. Exact verification is pending.

Checkpoint 75: exact layout **3,132 full / 162 partial / 0 failed**. The
manifest has 75 verified additional full matches and no lost full matches.
All 84 native controls pass; the four nested-column control PNGs match
Chromium exactly (0 differing channels out of 3,840,000). Eight inventories
retain all previous full matches and have no regression-floor failures.
Integer-cast lint and diff checks pass. The full Radiant gate is pending.

## Investigation: collapsible separators next to out-of-flow siblings

Current Chromium and native renders of `text-transform-capitalize-034` show
`Abc Abc` versus native `Abcabc` for a separator following an empty absolute
sibling. The whitespace-only node is discarded before text layout.
`should_collapse_inter_element_whitespace` classifies either immediate sibling
as a block via the local display-only predicate. An absolute or floated inline
box is blockified but does not participate in normal block flow (CSS 2.1
§9.6 / §9.5), so it must not cause that separator to disappear. The same
predicate is also used by the quirks line-height scan, which already skips
positioned children. Filter out-of-flow
objects through the existing DOM/style helper; retained whitespace will also
restore the capitalization word-start state without changing transform code.
The full 75-checkpoint Radiant gate must finish before implementation changes.

The full Radiant 75 checkpoint passes: **5,709 passed / 166 expected partial /
1 existing skip / 0 failed**.

The separator filter restores the visible `Abc Abc` pairs and makes
`text-transform-capitalize-034` fully match the existing browser reference.
Nine inventories retain all prior full matches and have no regression-floor
failures. Independent controls verify one collapsed separator before/after
absolute boxes, fixed boxes, explicitly blockified positioned boxes, floats,
and a display:none sibling, plus a genuine in-flow block and a no-space
control. Disable optional kerning in these controls to isolate separator
advance; Chromium reports exactly 12px (5px space plus authored 7px spacing).

The no-space control with font-kerning:none also exposes a separate existing
capitalize initialization issue: the native glyph path restarts capitalization
at the next text node, while Chromium retains the continuous word. Preserve
that diagnostic; the new separator test asserts only that no separator is
invented in that case. No transform implementation is changed or claimed
fixed by this whitespace correction. Exact layout verification is pending.

All 85 native text-query controls pass after the separator correction. The
selected page now matches the browser on word positions and capitalization;
the paired raster retains font-edge differences (16,086 differing channels
out of 3,840,000). The exact layout gate is running; the manifest remains at
75 until its result confirms the new full match.

Checkpoint 76: exact layout **3,133 full / 161 partial / 0 failed**, all 85
native text-query controls pass. The manifest has 76 verified additional full
matches and no lost full matches. The full Radiant gate is pending.

The full Radiant 76 checkpoint passes: **5,711 passed / 165 expected partial /
1 existing skip / 0 failed**.

Arabic margin-boundary diagnostic: current Chromium and native both load the
same declared Noto Naskh Arabic WOFF2 face. Native uses isolated 72.5px forms
for all three letters in both the margin test and its ZWNJ control. Chromium
still joins the first test across positive margins (60.59px / 49.64px /
57.19px), while its ZWNJ control uses 72.48px forms. CSS Text §8.7 and the
fixture assertion require shaping to break at those nonzero margins. Keep
`shaping-009` partial; matching the joined browser forms would contradict
that requirement. Font loading/decompression is working in this case.

The core Lambda 76 checkpoint passes: **6,845/6,845** combined input and runtime checks (2,112 input /4,733 runtime). Production source edits can resume.

## Investigation: last line baseline inside a multicol inline-block

Fresh Chromium confirms `multicol-gap-percentage-001` has the same column widths as native. The remaining short inline-blocks align 20px above their expected position. The multicol baseline publisher calls the shared last-descendant text baseline helper after placement. That helper traverses descendants in reverse but reads the text node's union-box `y`, which is the first line top, instead of the last line fragment. A two-line child therefore exports the first line baseline. CSS 2.1 §10.8.1 and CSS Align 3 §9.1 require the last in-flow line baseline (the lowest baseline among multicol columns). Diagnose with unequal-height Ahem columns and ordinary nested block controls before changing the shared helper.

The fresh `getclientrects-001` native text fragments already match Chromium's four Range rectangles in each writing mode. Its remaining stored comparison combines discontiguous same-y text differently; do not alter correct native layout or the harness to obtain a score. `multicol-margin-003` may have the same extraction mismatch; retain for separate diagnosis.

The completed-fragment baseline change retains the existing recursive baseline walker and adds a block-end-most mode for horizontal multicol publication. It selects the lowest non-collapsed text fragment across eligible descendants; source-order traversal and vertical publication keep their existing contracts. Independent controls cover unequal heights, fragmented text, an unbreakable tall column followed by a shorter column, a spanner, positioned text exclusion, first-baseline selection, overflow:hidden synthesis, and ordinary block descendants. All **86 native controls pass** and nine inventories have no lost full matches or regression-floor failures. `multicol-gap-percentage-001` becomes full; exact verification is pending. Raster differences are 6,363/3,840,000 channels despite matching geometry, so no pixel-identical claim is made.

Checkpoint77: exact layout **3,134 full /160 partial /0 failed**, native **86/86**. Manifest77 gained, none lost. Full Radiant pending.

Further browser diagnostics: Chromium treats `text-align:justify-all` as unsupported and falls back to start; CSS Text3 §6.1 requires justify on both longhands. Do not remove valid native support merely to match those six partial references. Fresh Japanese `word-space-transform-022/024` boxes are486px wide with injected Liberation Serif/Noto Sans SC faces, matching native width; stored636.72px boxes have a different font environment. Do not change ch metrics to force those stored widths. Atomic-inline WJ/ZWJ/FEFF cases similarly need CSS Text line-break-detail verification before adopting Chrome's extra wrap.

Fresh `md_ky-readme` confirms a600×600 logo box, as native uses; its stored32.75×24 broken-image box comes from a different resource state. This explains the earlier strut attempt's Markdown floor conflict; changing correct image sizing would not resolve it legitimately. Keep the rejected strut experiment rolled back. CSS Text3 §5.5 explicitly suppresses wraps at atomic WJ/ZWJ/FEFF boundaries, so leave those Chrome-wrapping partial cases unchanged.

Full Radiant77 passes: **5,713 passed /164 expected partial /1 existing skip /0 failed**.

## Investigation: nested avoid-inside box in an unequal outer remainder

Fresh Chromium `multicol-nested-013` confirms the second nested200%-wide box remains one50×100 fragment in the next outer column. Native splits it into two50×50 pieces beginning in the remaining50px inner slot, making its union75px wide. Its `break-inside:avoid` is authored and a later100px outer slot can fit it. Diagnose why the existing nested unbreakable-slot selector is not applied before publishing those descendant fragments (CSS Break3 §4.4 / nested fragmentation). No reference change is needed.

The avoid constraint reuses the existing nested slot iterator and whole-box selector, retains forced-break precedence, and records consumed outer flow independently of visible child bounds. An oversized avoid box must use a complete slot before normal fragmentation relaxes avoidance; select with the largest available capacity instead of treating it as permanently monolithic. Native controls now match that oversized Chrome geometry. Vertical controls reveal that the old whole-box branch published logical inline/block coordinates directly as x/y and width/height; reuse `multicol_physical_fragment_rect`, already used by split pieces. The200% vertical control independently exposes an older percentage inline-size mismatch; retain it under temp and use explicit50px inline sizes to isolate the projection contract. The shorter same-outer control also exposes an older auto-fill wrapper-height difference (60px vs75px); its descendants agree, but do not claim its wrapper fixed.

The final nested avoid fixture uses eight independent cases: avoid, avoid-column, auto, avoid-page, oversized avoid, vertical-lr, vertical-rl, and a whole box with a local descendant. All **87 native controls pass**. Its1200×1200 raster is **pixel-identical to current Chromium (0/5,760,000 differing channels)**. Nine inventories retain all full matches and cross no regression floor; `multicol-nested-013` becomes full. Forced-break precedence is additionally checked under temp; the parent fragmentation agrees, while its nested descendant coordinates reveal a separate older sizing/projection issue. Keep that diagnostic distinct from the accepted whole-box contract. The exact gate is pending.

Checkpoint78: exact layout **3,135 full /159 partial /0 failed**, native87/87. Manifest78 gained, none lost. Full Radiant pending.

Full Radiant78 passes: **5,715 passed /163 expected partial /1 existing skip /0 failed**.

## Investigation: nested continuous flow in balanced wrapped rows

Fresh `column-height-008` has outer fixed row height60px and10px row gap, with360px uninterrupted nested content. Four inner columns consume240px in the first outer row; the remaining120px divides over four30px inner columns in the final row. Chromium exposes an100px union and eight leaf fragments. Native exposes130px and six60px leaf fragments. The existing continuous-row balancer excludes nested contexts, and the nested slot iterator accepts a uniform capacity only. Reuse the row group's capacity/origin helpers for eligible continuous nested flows and carry the same plan into descendant publication. CSS Multicol2 column-height/column-wrap and CSS Break3 §5.1 govern variable fragmentainer capacities.

The first row-plan phase fixes the parent union but leaves the nested leaf on its uniform projection: the local slot eligibility accepts only nested column-fill:auto. Extend it to uninterrupted nested flow in a wrapped parent, sharing that eligibility with publication. The first inventory also exposes `column-height-013`: a descendant spanner interrupts continuous row flow, so explicitly exclude spanners from the recursive eligibility predicate. No new full match is counted until both corrections pass. A separate new three-column control initially combines `columns:2` with inline `column-count:3` and reveals a shorthand/longhand cascade defect; preserve that counterexample and use one longhand declaration for the row-capacity control.

The corrected continuous-flow eligibility excludes spanners, discrete inline lines, authored non-leaf block sizes, margins/decorations, containment, scrolling, floats, and avoid/forced breaks. Nested wrapped flow uses the existing shared slot iterator with the row group's actual capacity and origin. Consumed outer flow now accumulates outer capacities rather than multiplying a uniform target; split leaf and parent unions keep that same plan. Varied two/three-column and auto-fill controls match Chromium geometry, including12 pieces in the three-column case and preserving the unbalanced auto-fill130px union. Nine inventories have no lost full matches or regression-floor failures; `column-height-008` becomes full. Native and exact verification pending.

All **88 native controls pass**, including varying final-row heights, three inner columns, auto-fill, ordinary continuous flow, and the spanner interruption. The final control raster differs by19,500/3,840,000 channels, beginning inside the spanner counterexample (y605); do not claim that existing spanner background painting is fixed. Exact verification is pending.

Checkpoint79: exact layout **3,136 full /158 partial /0 failed**, native88/88. Manifest79 gained, none lost. The first five row-control PNG regions are pixel-identical to Chromium (0/2,880,000 channels); the spanner background paint difference is separate. Full Radiant pending.

Full Radiant79 passes: **5,717 passed /162 expected partial /1 existing skip /0 failed**.

## Investigation: avoid floor exceeds a definite column capacity

Fresh `multicol-nested-028` and independent fixed/max/auto-height controls confirm the avoid target floor raises a definite100px column to200px, which preserves the oversize sibling whole and prevents the minimum125px nested box from fragmenting. The floor runs after the existing definite-target calculation and does not reapply its limit. Cap the floor with the existing content-box limit helper; unconstrained balancing still grows to200px. Reuse the avoid/avoid-column predicate added for nested slots. Native before/Chrome geometry evidence is saved under `avoid-fragmentainer-limits-*`.

The definite floor cap restores `multicol-nested-028` to full in all nine inventories without losses. The fragment records reveal a further principal/content distinction: nested content consumes105px of outer flow, but the wrapper's authored minimum requires125px. Publishing105px incorrectly shortens its second background fragment to5px instead of25px, even though the bounding union and following sibling placement look correct. Preserve the used minimum border-box flow and its provisional fragment count before replacing the wrapper records. Do not count the pass until fragment and raster controls verify this correction.

The seven final capacity/minimum controls pass all **89 native tests**, including25px and325px principal remainders and separate205px leaf flow. Nine inventories retain all full matches and regression floors; exact verification is pending. The larger minimum with following overflow siblings exposes an older finite-sequence continuation policy: native keeps the later box below the final nominal column while Chromium creates another inline overflow column. Preserve that full counterexample in temp; the permanent larger-minimum control isolates its four principal fragments without adding those unrelated overflow siblings.

Checkpoint80: exact layout **3,137 full /157 partial /0 failed**, native89/89. Manifest80 gained, none lost. The final capacity/minimum control raster is pixel-identical to Chromium (0/5,760,000 differing channels). Full Radiant pending.

Zero-height wrapper diagnostics: in `multicol-span-all-children-height-004b-ref`, Chrome getClientRects reports first zero-height box at x8 and another at x216, but getBoundingClientRect returns the latter; `multicol-fill-balance-023` similarly returns the last y28 box instead of its first y8 box. CSSOM View §6 bounding-box algorithm step3 requires the first rectangle when every rectangle has zero width or height. Keep these partial; do not force native bounding boxes to the browser's last empty rectangle. Native still needs a separate full client-rectangle-list audit, which is not fixed here.

Fresh Markdown diagnostics: `md_commander-readme` uses a remote packagephobia badge and current Chrome matches stored116.5×20 while native has its offline broken-image alternative. This is a resource-state difference, not an established CSS sizing root; do not enable network or fake a badge to pass it. `md_test-unicode` current arrows815.47px (stored809.05) versus native866.8px, and current Arabic code line156.69px (stored171.84) versus native215.1px. Chrome uses Menlo plus per-glyph system fallbacks; investigate native selected glyph faces and required shaping before a change.

Full Radiant80 passes: **5,719 passed /161 expected partial /1 existing skip /0 failed**. The long-running objective remains active at80 verified gains, with20 further full matches required.


## Investigation: multiline inline source edges and lookahead separators

Fresh `hanging-punctuation-allow-end-inlines` agrees with its reference: a wrapped span's final48px text fragment plus16px end border gives a64px bounding union. Native publishes48px because the multiline bounding-box branch omits decorations entirely. In the adjacent start-border case, lookahead detects overflow after the text node is exhausted but disables restoration of the chosen collapsible separator. Its first text range retains48px instead of32px. Use the same separator-restoration policy as immediate overflow (CSS Text3 §4.1.2), and add sliced decorations to their source-edge fragments instead of the widest line (CSS Break3 §5.4).

Independent RTL controls expose another edge-ownership defect: bidi reserves both physical decorations on every line, then subtracts a physical left edge from the provisional logical origin. Sliced RTL continuation text consequently lands16px on the wrong side. Track whether each source boundary belongs to the current line, keep its logical leading advance separate from its physical decorations, and reuse the existing inherited-direction resolver. The helper is promoted from multicol, not copied. Promote the existing box-decoration-break resolver as well. The shared source-edge leaf traversal supplies both bbox and bidi callers; nested relative offsets are excluded from the parent's normal-flow box.

Cloned automatic-wrap fragments require both edges on every line. Include their decorated content bounds without decorating an already complete decoration-only ledger twice. The first inventory caught two formerly full sizing cases, `slice-intrinsic-size` and `clone-intrinsic-size`: their leading space wraps after a border-only first fragment. Recording the span's opening line preserves that fragment's ownership instead of attaching its start edge again to the later text. The final nine-category inventory restores both and retains every previous full match, with no regression-floor failures.

`layout_inline_slice_edges.html` supplies ten browser-matching geometry controls: sliced right/both edges, unequal first/last lines, nested edges, forced breaks, RTL source boundaries, lookahead trimming, cloned automatic wraps, and relative descendants. All90 native text-query tests pass, including RTL text origins and the retained source-space byte range. The raster comparison still differs in12,734 of3,840,000 channels: the existing `render_text_inline_background` / `render_text_inline_border` path clones padding and borders on each direct text rectangle, so sliced start/end borders also appear on inappropriate lines. No claim of pixel equality or a painting fix is made.

Additional Ahem leading-space prefix controls expose an older wrap-preflight difference from Chromium; retain the complete counterexample under `temp/layout-analysis/round7/inline-slice-edge-leading-space-counterexample.html`. It is distinct from the formerly full monospace sizing controls and from the accepted ten geometry controls. Exact and full Radiant verification are pending; the goal remains active.

## Rejected experiment: DOM carriage returns

CSS Text3 §4 explicitly treats DOM U+000D as U+0020; HTML source CR/CRLF normalization already happens in `html5_tokenizer.cpp`. A candidate changed layout, bidi classes/advances, intrinsic edge detection, and painted text accordingly. Independent six-mode Ahem controls preserve equal CR/space layout in native, while current Chromium drops preserved CRs in pre/pre-wrap/break-spaces and exposes different ranges and intrinsic widths. The candidate adds no full match and lowers `control-chars-00D` text score38.1→33.3. Restore every production file to checkpoint80, retain the candidate and controls under temp, and leave the case partial rather than remove CR space advances to imitate Chromium. This experiment is not counted or left in production.

Checkpoint81: sequential exact layout **3,138 full /156 partial /0 failed**, native90/90, targeted lint and diff checks pass. Manifest81 gained, none lost. The first exact attempt overlapped a Radiant build that temporarily removed `lambda.exe`, producing ENOENT batch errors; it is invalid as a code regression result. Stop the overlapping gate and rerun sequentially. The isolated exact rerun passes; the isolated full Radiant gate is pending.

Full Radiant81 passes: **5,721 passed /160 expected partial /1 existing skip /0 failed**. The prior sequential run's seven view failures were CLI precondition errors: a forced post-load settle duration requires auto-close. All seven pass when both settings are supplied; the isolated complete rerun also passes. No engine/test change is needed. Goal remains active at81 gains, with19 required.


## Investigation: single-line grid items spanning column fragments

Fresh `multicol-dynamic-add-003-ref` confirms a40px two-row grid and row-spanning item in10px columns. Their first fragment consumes the whole18px line, followed by10px,10px,2px continuations; native publishes10px four times and clips both unions to10px. The existing row planner explicitly excludes spanning items. Its generic fallback consequently loses the unbreakable first-line constraint and the corresponding source-flow prefix. CSS Grid1 §12 and CSS Break3 §4.1 preserve indivisible line boxes in parallel grid flows.

Build a shared whole-prefix count/store path instead of adding a visual-height cap. For a first parallel grid group whose items start at the same content edge and contain one line or no content, resolve the real line advance and include its start decoration. Reserve that prefix before slicing the remaining grid area. Propagate the same capacity through the grid-item projection so empty stretched items and the visible spanning item share the parent plan. Later rows, multiline items, forced breaks and scrolling grids retain their existing planners. If earlier content leaves insufficient room for this minimum prefix, take the available break before the grid; the line can then overflow an empty short column. Zero-height fragmentainers retain their separate progress policy.

Reuse the prefix count helper for the existing ordinary single-line path; both count and stored piece extents now consume the same whole first line and remaining flow. Do not duplicate the prior ceil/remainder branch. Six independent Ahem controls vary line advances18/20/24/10, total flow40/60, and a5px preceding block. All twelve grid/item boxes and their first/last fragment extents match fresh Chromium. All91 native text-query tests pass. The raster differs in591 of3,840,000 channels, with matching visible box geometry; no pixel-identical claim is made.

The final nine-category inventory gains `multicol-dynamic-add-003` and its reference, loses no full matches, and crosses no regression floor. Targeted Radiant lint and diff checks pass. Exact verification is pending, so these two gains are not yet added to the verified manifest. Additional float-wrapper reference004 returns the last of three all-zero-height fragments from Chromium's bounding-box API; CSSOM View §6 step3 requires the first. As with the earlier zero-height diagnostics, leave that bbox difference partial instead of imitating the nonconforming last-rectangle choice.

Checkpoint83: exact layout **3,140 full /154 partial /0 failed**, native91/91. Manifest83 gained, none lost. Full Radiant pending. Goal remains active,17 more full matches required.

Full Radiant83 passes: **5,724 passed /158 expected partial /1 existing skip /0 failed**. The objective remains active at83 verified new full matches, with17 still required.


## Investigation: atomic inline boxes in anonymous multicol runs

A phase trace of `multicol-dynamic-change-inside-break-inside-avoid-001-ref` locates the first input correctly at local y18 after normal inline layout. The block distributor then resets it to y0: its broad block-storage predicate includes inline-block views. The mixed-inline collector also omits atomic boxes, preventing its anonymous-block planner from preserving the completed line. CSS2.1 §§9.2.1.1,9.2.2,9.4.2 require these boxes to stay in their inline formatting context.

Extend the existing mixed record collector to atomic boxes and use their completed source-line numbers when grouping baseline-aligned leaves. Retain the actual enlarged line advance and reuse the common inline position setter. Dispatch supported auto-sized horizontal mixed runs to that planner; nested fragmentainer capacities, constrained heights, spanner groups and unsupported block kinds retain their dedicated paths. The initial broad classification attempt lost nested/spanner full cases and was superseded before validation. A fixed-height mixed-run counterexample remains under temp for that separate planner; it is not counted as fixed.

Seven permanent Ahem controls cover inputs, inline-blocks, authored block display, a solely atomic run, a trailing run, baseline alignment, and a nested span. Their boxes match fresh Chromium; all92 native text-query tests pass. The first new unit invocation lacked the loader's expected `result` fixture root; adding that fixture root resolves it without changing the loader. Browser/native raster comparison differs in1,500 of3,840,000 channels, including existing span edge antialiasing; no pixel equality is claimed. Targeted lint and diff checks pass.

All nine inventory categories lose no full matches and cross no regression floor. Both dynamic avoid001 cases become fully matched. Checkpoint85 exact layout: **3,142 full /152 partial /0 failed**; manifest85 gained, none lost. Full Radiant is running with sources/libraries/executable frozen. The goal remains active, with15 more full matches required.

Read-only diagnosis leaves `justify-all` cases partial: CSS Text3 §6.1 still defines this value, while the browser reference ignores it. Atomic WORD JOINER/ZWJ wrap cases likewise conflict with CSS Text3 §5.5's explicit non-breaking exceptions. Transform overflow fixtures expose another reference mismatch: native executes test callbacks that update transforms to200/300px, while stored browser boxes retain20/30px. Do not disable those valid transforms to match stale script state. Root columns:0px uses the normative1px preferred minimum; Chromium's zero-width columns are not grounds to remove it.

Full Radiant85 passes: **5,727 passed /156 expected partial /1 existing skip /0 failed**. Sources are unfrozen; goal remains active at85 gained cases,15 more required.


User wrap-up checkpoint: **85 additional full matches**, independently recomputed against the original round7 inventory, with zero lost full matches. The100-test target remains incomplete. Work is stopped at the user's wrap-up request; the goal service reports no active goal. No diagnostic instrumentation remains. Exact layout and full Radiant gates both pass with zero failures; native92/92, targeted lint and diff checks pass. References, comparison tolerances and baseline skips are unchanged.

Next confirmed diagnosis, not implemented: percentage word spacing is excluded by the shared font-spacing resolver's letter-only percentage condition, its length percentage basis omits word spacing, and the intrinsic path also omits percentage/functions for word spacing. Fresh Chromium resolves the authored100% and calc(50% +0.5em) examples against each element's used font size. CSS Text4 §8.1 requires percentage-preserving inheritance. Complete a shared resolver design, including retained affine calc components and final font-size timing, before changing production; the stable85 source backups and next-investigation notes are under temp.

## Round 8: computed spacing percentages

Fresh starting gate (2026-10-18): **3,138 full /156 partial /0 failed**.
The tree has changed since round 7; this is the comparison checkpoint.

Priority and outcome: restore percentage word spacing in ordinary text and
shrink-to-fit measurement. The original case is `word-spacing-percent-001`.
CSS Text 4 §§8.1–8.2 requires inherited percentages to use the descendant's
used font size while inherited font-relative lengths retain their declaring
element's computed length. No Lambda semantic or design ruling changes.

Owner and design: promote the existing font-spacing resolver for reuse by
intrinsic sizing. Preserve the affine absolute and percentage components of
spacing values in `FontProp`, copy them with the existing font-value helpers,
and resolve their used advance only after font-size is complete. Keep zero
and normal explicit so inheritance cannot restore an earlier nonzero value.
This milestone covers percentages and affine `calc()` for both spacing
properties; nonlinear percentage expressions retain their existing resolver
and are outside this milestone.

Acceptance: native Ahem controls cover absolute expressions, pure and mixed
percentages, a changed child font size, explicit inheritance, inherited em,
negative percentages, and normal/zero resets. Browser geometry and PNGs live
under `temp/layout-analysis/round8/`. During implementation run the native
spacing control and focused WPT comparison; then run the full native text
suite, layout baseline, required Radiant baseline, integer-cast lint and diff
checks once the implementation is stable. Keep reference data and scoring
unchanged, and record remaining differences instead of forcing a match.

Diagnosis also found the first loss in declaration validation: both literal
percentages and percentage-bearing expressions were accepted only for letter
spacing. The shared validator now accepts the CSS Text 4 domain for word
spacing too; the parser regression checks both properties symmetrically.

Focused evidence: the original WPT case now matches every compared metric.
All 13 independent native controls match Chromium's widths, including the
actual text rectangles as well as the shrink-to-fit boxes. All 20 control
element rectangles match fresh browser x/y/width/height within 0.1px.
The complete native text/layout suite passes **138/138** and the CSS parser
suite passes **110/110**. Four affected category inventories retain their
recorded regression floors; CSS text improves **884→885** full matches.
Integer-cast lint, generated CSS inventory and diff checks pass.

PNG inspection confirms the intended spacing. The saved 1200×800 images
still differ in 6,276 RGB channels for the Ahem controls and 25,952 for the
WPT case (of 2,880,000 channels); geometry equality is not a pixel-equality
claim. Full Radiant qualification passes **5,885 full /159 expected partial
/1 existing skip /0 failed** across 6,045 tests, including the complete UI,
viewer, native layout, paged-media and render selections. Its layout selection
reports **3,139 full /155 partial /0 failed**. On 2026-10-19 the exact
`make test-layout-baseline` target independently confirms the same result
across all 3,294 recorded cases, with no skips.

The broader `make test-input` audit finishes with a qualification limit:
10 Wiki tests and 24 PDF/export tests fail, along with the 12 prose readback
checks already documented in `doc/Script_Formats_Support.md`. Its 23 CSS
suites pass **1,146/1,146**. Keep these ordinary failures visible; no input or
export expectations were changed for this milestone.

Seven Wiki CLI failures reproduce with the saved 2026-10-18 18:56 executable
(`temp/svg_enhancement/lambda-intake.exe`), reporting `unknown option
input_type`. Isolated current/pre-change CLI replays of paged Markdown,
FO/native page controls, and text-spacing export produce **identical failure
assertions**: respectively 1, 5 and 4. That executable predates the current
HEAD's SVG work as well as this spacing change; the replay is supporting
evidence for these three existing failures, not an attribution of every
remaining export failure. Logs and the assertion comparison are retained
under `temp/layout-analysis/round8/`.

The spacing milestone is complete: one recorded partial becomes full,
the exact layout and required Radiant baselines have zero failures, and the
new browser-derived geometry controls pass. The completion audit on
2026-10-19 confirms the executable is newer than the changed engine sources
and reruns the native spacing contract and original WPT case successfully.
The long-running goal's zero-failure result is verified; remaining
input/export failures retain the qualification limit above.

Next investigation: `anonymous-grid-item-001` is not solely an indentation
error. `collect_grid_item_nodes` collects only element nodes, and direct text
gets a special layout path only when no element items exist. A conformant
repair needs anonymous grid items participating in the existing track sizing
and placement algorithm (CSS Grid §6), rather than a post-layout text offset.
No grid production change is included in this milestone.
The follow-up inspection also confirms the grid style pass counts only
element items and its scratch capacity excludes text. A future implementation
should promote the existing scratch-lived anonymous-item factory from flex
instead of copying it, preserving the pass-owned lifetime in D4.5.1v4.

## Round 9: anonymous grid text items

Starting checkpoint (2026-10-19): **3,139 full /155 partial /0 failed**
from a fresh `make test-layout-baseline`. Selected milestone: make anonymous
text participate in grid placement and track sizing. CSS Grid 1 §6 and §8.5
require an anonymous block container for each child text sequence, with
whitespace-only sequences suppressed. CSS Text 3 §8.1 makes inherited
indentation apply to the anonymous block's first line, using its own width.
No Lambda semantics ruling changes. Scratch wrappers and run records remain
owned by the enclosing layout pass, following D4.5.1v4 and D4.1.4v5.

Diagnosis: flattened grid collection excludes text; the no-element fallback
formats direct text against the entire container instead of a grid area.
The existing flex implementation already provides scratch anonymous boxes,
flattened text adjacency, and original-node text layout. Extract shared
primitives rather than copying these. Grid will collect contiguous flattened
text sequences into scratch items, measure them through the inline formatter,
and retain ordinary grid placement/alignment. Measurements must preserve
retained source rectangles; scratch wrappers never enter the DOM/view tree.

Acceptance: `layout_grid_anonymous_text.html` covers text after/before an
element, auto and fixed columns, text-only explicit tracks, inherited fixed
and percentage indentation, block and inline alignment, display:contents text,
and whitespace-only suppression under white-space:pre. Capture browser and
Radiant geometry/PNGs in `temp/layout-analysis/round9/`. Focused WPT checks
include `text-indent/anonymous-grid-item-001.html` and the existing flex peer.
Verification budget: focused native/CLI checks while implementing; once
stable, full native text suite, integer-cast lint, complete layout baseline,
and required Radiant baseline. Keep corpus references and scoring unchanged.

Implementation: anonymous items now share the existing flex scratch factory,
run representation, flattened adjacency and text projection helpers. Grid
collects these items before auto-placement and measures their inline content
instead of using the former container-wide text fallback. Row contributions
reuse the resolved track-span size. Trial layout detaches existing text
rectangles, recycles only its own new rectangles, then restores source state;
no scratch item or run pointer is retained on DOM nodes. Collection storage
shares the grid pass lifetime so ending a nested node-buffer scope cannot
unwind live anonymous items.

Focused result: `anonymous-grid-item-001` and its flex peer both match all
compared geometry. The nine independent controls match all **18 element
rectangles** from fresh Chromium and pass three retained layouts. Their
1200×800 PNGs differ in **2,012/2,880,000 RGB channels**; the complete original
WPT PNGs differ in **30,980 channels**, including paragraph/font painting.
These are geometry results, not pixel-equality claims. Native text/layout
passes **139/139**; integer-cast lint and diff checks pass. The exact layout
gate improves to **3,140 full /154 partial /0 failed /0 skipped**, preserving
all 3,294 recorded cases and their scoring/reference data.

Read-only follow-up: fresh captures again confirm the previously recorded
atomic-inline GL/WJ/ZWJ browser/spec disagreement. The native no-wrap behavior
follows [CSS Text 3 §5.5](https://www.w3.org/TR/css-text-3/#line-break-details);
those browser-wrapping partials remain unchanged.
The evidence is saved as `atomic-diagnosis*` under the round 9 temp directory.

Review found a required alignment follow-up before completion: scratch items
have no DOM children, so the shared baseline finder synthesizes their bottom
edge. In the Ahem control with a 20px element beside 10px anonymous text,
fresh Chromium places the text at local y=11px; native places it at y=9px.
The formatter also exports a container-relative baseline because its text
origin is the grid item's position. Preserve measured anonymous baseline
metrics, normalize them to the item border-box origin, and teach the existing
baseline finder to consume those metrics. The inline-grid exporter must add
the item origin exactly once. Add this control to the finite acceptance set;
this is part of ordinary grid alignment, not an independent feature tail.
The in-progress first aggregate gate is retained as pre-follow-up evidence;
qualify the corrected implementation again after it finishes.

Baseline follow-up: measured metrics now remain on the scratch item and use
its own border-box origin. The shared baseline finder uses those metrics for
anonymous items; inline-grid export adds the item origin once (CSS Grid 1
§10.6). The tenth control checks both a shared baseline with a larger-font
item and inline-grid alignment beside an independent same-font reference.
All **20 element rectangles** match Chromium and **139/139 native tests**
pass, including three retained reflows. The final control PNG comparison is
**2,342/2,880,000 differing RGB channels**. A test initially used the generic
text-origin helper on an inline parent whose text remains in block coordinates;
that helper double-counted its parent offset although the layout dump and PNG
were correct. The contract now checks the independently captured element
boxes. This separate text-origin helper issue is outside the grid change.

The first full Radiant gate passed **5,887 full /158 expected partial /1
existing skip /0 failed**, across 6,046 tests. It predates the baseline
follow-up; the final implementation is undergoing fresh exact-layout and
Radiant qualification. The generated CSS inventory was refreshed only for
source/test line references; qualification states and layout score floors
are unchanged.

Next bounded investigation: `text-wrap-balance-004`. Fresh browser and native
PNGs are under round 9 (`text-wrap-balance-004-browser.png`,
`balance-radiant.png`). Native's blue forced-break block is unbalanced while
its orange independent blocks balance; Chromium balances both groups. The
current `text_wrap_balance_measure` in `layout_block.cpp` uses one width and
one target-line count for the whole block, while height probes include every
forced-break group. [CSS Text 4 text-wrap-style](https://www.w3.org/TR/css-text-4/#text-wrap-style)
requires groups separated by forced breaks to balance independently and
permits disabling balance only above ten lines; the current five-line cap
also needs examination. No balancing production change is included in round 9.

Design starting points for that next milestone: reuse/promote
`intrinsic_append_inline_text` / `intrinsic_text_br_content_height` in
`intrinsic_sizing.cpp`, which already preserve `<br>` as a forced marker;
keep per-group constraints in the owning inline formatting context and
advance at real forced breaks, without retaining temporary strings or scratch
pointers across their owner. `line_break` / `line_reset` and
`update_line_for_bfc_floats` already preserve active balance constraints.
Acceptance should compare forced-break groups with independent block controls,
including nested inline breaks, distinct paragraph lengths and retained reflow.
Existing float and clamp contracts must retain their floors. Complete this
finite paragraph-group behavior before expanding balance/clamp interactions.

An independent Ahem diagnostic (`balance-groups-diagnosis.html` under round 9)
confirms the paragraph-group cause without external font uncertainty: at
240px, six two-letter words break as **160px +160px** in Chromium and in
native independent block controls, but as **220px +100px** in native's
forced-break group. All three groups retain the same 100px total height;
the defect is the chosen soft breaks, not container height. The following
long-word group and single-line group already agree. Keep these controls as
the starting evidence for the next milestone.
This priority targets balanced headings with authored line breaks and gives
later clamp work correct paragraph boundaries; it is not selected solely to
increase the synthetic full-pass count.

Final round 9 qualification (2026-10-19): exact `make test-layout-baseline`
passes **3,140 full /154 partial /0 failed /0 skipped** across 3,294 cases.
The corrected implementation's full `make test-radiant-baseline` passes
**5,887 full /158 expected partial /1 existing skip /0 failed** across
6,046 cases, including **139/139 native text/layout tests**. Integer-cast
lint, CSS inventory freshness and diff checks pass. The executable is newer
than every changed engine source. Logs, browser/native PNGs and the independent
20-element geometry comparison are retained under `temp/layout-analysis/round9/`.
The anonymous-grid milestone is complete by behavior: text participates in
tracks and auto-placement, inherits its own line formatting, exports real
baselines, and survives retained reflow without retained scratch items.
The long-running goal stays active for the bounded forced-break balancing
investigation recorded above.

## Round 10: balance forced-break paragraphs independently

Starting evidence: round 9 verifies **3,140 full /154 partial /0 failed**
and the required Radiant gate. The worktree retains that implementation.
Priority: ordinary balanced headings with authored breaks. The owning stage
is the balance probe and its formatting-context state, not CSS parsing or
painting. CSS Text 4 `text-wrap-style:balance` requires separate forced-break
groups and unchanged auto line counts for groups of five or fewer lines;
its allowed disable threshold is above ten lines. No Lambda semantics or
formal design ruling changes. D4.5.1v4 / D4.1.4v5 govern temporary measurement
storage and prohibit retaining a string or scratch pointer beyond its owner.

Design: expose the existing inline-text collector for both intrinsic height
and paragraph probes. Preserve actual forced boundaries in that representation.
Store only the owning DOM block, paragraph index and available inline size in
BlockContext; compute each paragraph's constraint from temporary collected
text, releasing it before returning. Advance via the existing real-forced-break
flag, which distinguishes `<br>`/preserved newlines from terminal line flushes.
Use normal wrapping line counts rather than dividing an unwrapped width.
Keep the existing complex-content intrinsic fallback and apply remaining clamp
budget to each group. Share width activation between setup and continuation.

Acceptance: `layout_balance_paragraphs.html` pairs independent block controls
with direct/nested `<br>`, reversed group lengths, center alignment, preserved
newlines/blank groups, a six-line paragraph and an underestimated normal line
count. Include the original `text-wrap-balance-004` WPT. Mixed-font shaping,
paragraph-wide float optimization and precise clamp cutoff selection remain
separate limitations; preserve their existing cases while fixing the common
paragraph boundary model. Verification budget: focused native/CLI comparisons
and neighboring balance/clamp/float WPTs during implementation; then full native
text, layout baseline, required Radiant baseline, cast lint, inventory and diff
checks once stable. Keep browser reference files and score floors unchanged.

Implementation diagnosis: the first text inventory found four regressions.
Single-paragraph mixed-font/clamp runs need the existing DOM intrinsic provider;
the flattened string only supplies a different provider where a forced boundary
must split it. Complex block/float heights also do not directly encode an inline
line count. Reusing their existing intrinsic estimate resolves three regressions.
The remaining external-float case exposes a missing measurement dependency:
normal line counts must query the same shortened intervals and clearance as
rendering. The shared word-height probe now accepts line-width queries and
reuses the renderer's float-space/clearance helpers. A trial must preserve the
normal float clearance; narrowing a virtual measure must not clear the float
and manufacture a new paragraph origin. Two independent Ahem float/clearance
controls extend acceptance for this shared prerequisite.

Focused verification: **887 full /93 partial /0 failed** across all 980 CSS
text baseline cases. `text-wrap-balance-004` becomes full; the original float
and clamp regression floors are preserved. All **140 native text/layout tests**
pass. The eight behavior controls include six paragraph pairs, an external
float and a clearing word; the independent auto float supplies the comparison
for reduced variance in remaining space while retaining normal line count.
All **38 captured element rectangles** match fresh Chromium. The 1200×800
control images differ in **21,912/2,880,000 RGB channels**, including valid
UA differences in soft-break choice and the browser's later-group imbalance.
Do not interpret this as pixel equality. The first clearance-check build failed
because the existing subpixel tolerance was text-file-local; that diagnostic
ran the older executable and is labeled `before-clearance-build`. The constant
is now shared, and the successful rebuilt executable produced the focused
zero-failure result. Cast lint, inventory freshness and diff checks pass.

Qualification found a shared prerequisite: page snapshots regress because the
inline height probe preserves `pre` newlines but still permits soft wrapping.
Three isolated runs of the saved pre-round-10 executable pass the snapshot
average check; three runs of the changed executable fail it. The demo's code
cards acquire one extra estimated line per grid row, although their final text
layout is unchanged. Expectations and tolerances are unchanged.

A minimal Ahem grid distinguishes this cause: `pre`, `nowrap`, inherited `pre`
and `pre-wrap` with `text-wrap-mode:nowrap` require 20px for two forced lines;
the probe supplies 30px. Wrapping `pre-line`/`pre-wrap` controls correctly need
30px. [CSS Text 4 §5.1](https://www.w3.org/TR/css-text-4/#text-wrap-mode)
allows only forced breaks under `nowrap`. Repair the intrinsic inline height
query using the existing wrap-mode resolver and forced-boundary representation;
verify the grid contribution and retained reflow against these controls before
requalifying the snapshot and full gates. This is part of round 10's shared
measurement repair, not a new layout-score exception.

The repair keeps one word-height counter with an explicit soft-wrap switch;
the inline-height caller supplies the existing resolved wrap mode. Forced
boundaries still advance lines, and a terminal boundary no longer invents an
empty trailing line box. `layout_intrinsic_text_wrap.html` checks eleven grid
rows, including inherited/default `pre`, explicit nowrap mode, real blank lines,
and terminal newline/`<br>` boundaries over three retained passes. All **141
native tests** pass. The 980-case CSS text gate remains **887 full /93 partial
/0 failed**. All **21 browser-captured control rectangles** agree; the PNGs
still differ in **4,752/2,880,000 RGB channels**. The corrected page snapshot
check passes three isolated runs, with **90.9% average elements /91.4% text**;
demo recovers its **98.8% element /83.6% text** score. The full layout and
Radiant gates are undergoing fresh qualification on this implementation.

The corrected implementation's exact `make test-layout-baseline` passes
**3,141 full /153 partial /0 failed /0 skipped** across 3,294 cases. The first
qualification attempt could not bind WPT's local HTTP server inside the sandbox
and ran no layout cases; its log is labeled `baseline-sandbox-denied`. The
insufficient-permission Radiant attempt was terminated and labeled
`radiant-sandbox-stopped`. Qualification uses local-server access, without
changing the harness, reference captures, tolerance or baseline floors.

Read-only follow-up triage during qualification: fresh
`word-break-normal-zh-000` at 1200×800 gives both Radiant and Chromium 77px
span unions, versus 86px in the stored reference. Both engines give the two
bordered blocks 94px heights; current browser/native inline widths differ by
about 2.3px. Captures and native layout are under
`temp/layout-analysis/round11/`. Establish equal font inputs before attributing
the stored font-box discrepancy to layout. No font metric or reference change
is justified by this comparison. The reversed-counter capture also reconfirms
the Chromium syntax limitation already deferred in round 3; it is not a new
engine repair candidate.

Final round 10 qualification: exact `make test-layout-baseline` passes
**3,141 full /153 partial /0 failed /0 skipped** across 3,294 cases. The full
`make test-radiant-baseline` passes **5,890 full /157 expected partial /1
existing skip /0 failed**, across 6,048 cases, including all **141 native
text/layout tests**. The final logs are `baseline-corrected-final.log` and
`radiant-qualified-final.log` under `temp/layout-analysis/round10/`.
Integer-cast lint, CSS inventory freshness and diff checks pass.

This bounded milestone is complete by behavior: forced-break groups choose
independent balance constraints, normal line counts and float intervals feed
the probe, and shared intrinsic heights honor nowrap and terminal forced breaks.
The authored controls survive retained reflow. Mixed-font paragraph shaping,
full float optimization and exact clamped-prefix selection retain their partial
scope. The long-running goal remains active for the remaining recorded partials;
the read-only font/reference triage above does not justify a production change.

## Round 11: positioned inline extents in table-cell height

Starting evidence: round 10's exact layout and Radiant gates pass. A fresh
form/Markdown inventory retains **365 full /13 partial** and **48 full /10
partial** respectively, with no regression-floor failures. Several form
references describe a different DOM (the multiline textarea references contain
inputs), so they do not justify CSS changes. Markdown's sponsor table in
`md_remark-readme` reproduces with the same local image resources: native
599px versus fresh Chromium 436px. The actual images have the expected sizes.

Priority: ordinary Markdown tables containing linked images and authored line
breaks. The root is `measure_cell_content_height`: it adds an inline subtree's
height to its wrapper's font-box y, although the subtree's atomic descendants
have their own positioned top. A 40px atomic box after one 10px text line needs
52px including the root strut, but native measures 82px. This oversized minimum
then feeds the existing rowspan distributor; distribution itself is not the
cause. A second control exposes missing root-strut descent beside an unwrapped
atomic box (40px versus 42px). Explicit inline line-height controls expose the
same font-box/line-box origin distinction (45px versus 40px, 25px versus 23px).

Authority: [CSS 2.2 §17.5.3](https://www.w3.org/TR/CSS22/tables.html#height-layout)
and [§10.8](https://www.w3.org/TR/CSS22/visudet.html#line-height) size rows from
the cell's line boxes. Inline font boxes, glyph ink, atomic margin boxes and
line-height have distinct roles. Relative visual offsets do not enlarge normal
flow (§9.4.3). No Lambda formal ruling changes; existing temporary measurement
ownership remains under D4.5.1v4 / D4.1.4v5.

Design: retain the existing inline extent collector, use its positioned origin
when consuming its line-stack height, and center explicit line-height around
the measured font box for either sign of leading. Include in-flow atomic inline
cells in the existing cached flow-height/root-strut floor. Reuse the existing
hypothetical-position helper for relative atomic descendants. Do not alter the
rowspan algorithm or introduce image/tag-specific dimensions.

Acceptance: direct/nested atomic wrappers, an atomic run after a forced break,
a rowspan ending in an empty row, middle alignment against a taller cell,
explicit line-height and glyph overflow, and a relative atomic descendant.
Compare the isolated sponsor table and fresh native/browser controls under
`temp/layout-analysis/round11/`. Scope is horizontal cell content measurement;
column-width differences and unrelated image/resource/reference state remain
separate evidence. Verification budget: focused native geometry first, then
table/Markdown neighbors; once stable, exact layout, required Radiant baseline,
cast lint, CSS inventory and diff checks. Preserve every reference and floor.

Implementation: the positioned inline extent supplies both origin and height;
the wrapper's font-box y no longer shifts an atomic subtree's contribution.
Positive and negative leading use the same explicit line-height centering rule.
Atomic-only content retains the already-formatted root-strut minimum. The first
native pass exposed a one-pixel rowspan image shift: post-growth alignment used
only descendant ink/atomic bounds, so it now reuses the natural content-height
measurement. The neighboring table gate then caught
`visibility-collapse-rowspan-005`: that same alignment must include block-child
margins in its origin, matching initial alignment. Reusing its margin-aware
bounds fixes the regression and adds a permanent block-margin control.

All 57 table baseline cases pass. Markdown retains 48 full /10 partial with no
regression-floor failures; `md_remark-readme` improves from 94.7% to 99.7% of
elements and retains 100% text. The isolated sponsor table now has Chromium's
436px height, with exact row heights and image y positions; its one recorded
element mismatch is a separate 8.75px column-width difference. Eleven Ahem
controls cover the accepted height/alignment contracts through retained reflow.

The before/after/browser PNGs also expose an older table paint-order defect:
the empty second row's background paints over the first row's spanning-cell
content, and later row backgrounds cover other spanning content. Corrected
geometry does not repair that raster ordering. Treat CSS 2.2 §17.5.1 table
layers as the next table milestone; do not claim sponsor images are visually
matched. Keep this evidence under `temp/layout-analysis/round11/`.

Focused qualification passes all **142 native text/layout tests**; the final
eleven-control fixture also verifies block paragraph positions inside the
rowspan. All **56 captured element rectangles** match Chromium within 0.1px.
The control PNGs differ in **1,272/2,880,000 RGB channels**; the sponsor-table
PNGs retain the documented paint-order/column-width differences and are not a
pixel match. Page snapshots pass their unchanged average check. Integer-cast
lint, inventory freshness and diff checks pass.

Exact `make test-layout-baseline` passes **3,141 full /153 partial /0 failed
/0 skipped** across 3,294 cases. Markdown element fidelity improves while its
ten partial statuses remain: the original sponsor table now has one separate
column-width mismatch. Full Radiant qualification is running on the same
implementation; no baseline/reference/tolerance change is included.

The first required Radiant run found five paged-media alignment regressions.
Their exact assertions expose a shared precision dependency: text formatting
floors ascent half-leading, while the new inverse font-box conversion used an
unsnapped half. The 0.5px discrepancy moves middle/bottom-aligned glyphs and
repeated/split table content. Stop that failed aggregate before rebuilding;
retain `radiant-leading-failed.log`. Extract the existing leading calculation
for both font formatting and inverse table measurement, leaving the formatter's
behavior intact. The unmodified paged tests supply the neighboring precision
contract; no expected coordinate or tolerance changes are appropriate.

The rebuilt shared-leading implementation passes all **491 paged-media tests**
and all **142 native text/layout tests**. Existing formatter behavior is
unchanged; table origin recovery calls the extracted ascent-leading helper.
Integer-cast lint and diff checks pass. Fresh exact-layout and full Radiant
qualification run sequentially; the earlier exact pass predates this precision
correction and is not the final gate evidence.

Read-only paint triage: `table-layers-diagnosis.html` under round 11 isolates
three cases with no font/image dependencies. An empty later row paints blue
over an earlier green spanning cell and its black content. A nonempty later
row similarly covers the lower part of that cell. With a transparent spanning
cell, the originating red row's background must extend into the span; a later
row's blue rectangle must not replace that coverage. Fresh Chromium confirms
all three under [CSS 2.2 §17.5.1](https://www.w3.org/TR/CSS22/tables.html#table-layers).

The existing raster/export walks paint each row background together with its
children in DOM order. A next table-paint milestone must share table-layer
ordering and originating-cell coverage across backends, using existing
background emission/geometry helpers. Prepainting full row rectangles alone
would leave the transparent-cell case wrong. Do not add a rowspan-specific
culling exception or duplicate a raster-only traversal. This triage changes no
production paint code while the current qualification owns the executable.

Blocking prerequisite: the final exact layout gate retains **3,141 full /153
partial /0 failed /0 skipped**, but the full Radiant gate finishes with one
actual failure: `PageLoad/html5_kitchen_sink` intermittently crashes during
shutdown. All 491 paged-media tests and the other components pass. A worker
backtrace reads an AST function name while the main thread destroys the module
cache. LLDB ownership tracing proves the missing owner: `populate_layout_document`
receives a document-owned runtime created during embedded-load MathML layout,
then replaces it with its null runtime argument. Its registered compiler jobs
survive document cleanup and race input-cache destruction. Two other runtimes
are correctly drained. This is an ownership defect, not a compiler timeout.

Design: geometry publication must preserve evaluator identity. Remove runtime
assignment from that helper and initialize Markdown's optional runtime explicitly
at its loader boundary. Reuse existing document teardown and compiler draining;
do not add global shutdown ordering, delays, or disable compilation. Existing
D5.3.3 / D5.4.1 and D8.5.1v7 govern the continuous ownership and compiler-cache
lifetime; no formal ruling changes. Acceptance: transient JS plus an embedded
load blocker forces MathML layout during load, retains the same document-owned
runtime across geometry publication, behavior initialization and retained reflow,
then closes through existing teardown. The new deterministic regression must
fail before the fix. Verify that every runtime queuing jobs is cleaned before
input-cache teardown, repeat the originally crashing page, then rerun exact
layout and the required full Radiant gate sequentially. Keep failed logs and
diagnostic captures in round 11; unchanged memory limits and test floors apply.

The regression fails deterministically before the fix at the missing runtime
assertion, then passes after geometry publication stops mutating ownership.
All **143 native text/layout tests** pass. The fixed LLDB trace records cleanup
for every runtime that queues compiler jobs before both input-cache teardown
sites (`unclosed_compiler_runtimes=[]`); the earlier trace records the orphan.
Only Radiant loader ownership changes; no Lambda runtime/compiler or vendor
source is changed. Fifteen actual original-page process lifecycles exit cleanly.
The PageLoad runner pre-runs its pages once before GTest, so repeated assertions
over its cached results are not counted as repeated process coverage.

Final regular qualification on CLI SHA256
`8055731b234463dcf8dfac97ee74b7ba2bd146aedf579f8f635b6f218f590720`:
exact `make test-layout-baseline` passes **3,141 full /153 partial /0 failed
/0 skipped** across 3,294 cases. Full `make test-radiant-baseline` passes
**5,892 full /157 expected partial /1 existing skip /0 failed** across 6,050
cases, including all **143 native text/layout**, **491 paged-media**, **111
page-load**, **644 UI** and **208 viewer** checks. The gates ran sequentially;
the CLI remained unchanged throughout. Logs are `baseline-owner-final.log` and
`radiant-owner-final.log`. Integer-cast lint, inventory freshness and diff checks
pass. A separate supported ASan host is building for focused lifetime coverage;
that result is still pending and does not replace regular gate evidence.

The table-height/ownership milestone is complete by regular behavior checks:
inline descendant positions and snapped leading determine natural cell height,
root struts and block margins survive rowspan alignment, and post-load geometry
publication preserves the runtime whose packages and compiler jobs remain live.
The long-running goal remains active. For the next table paint milestone, reuse
`layout_table_each_column` / `layout_table_column_range` and retained column,
row-group, row and cell geometry instead of rebuilding layout during paint or
copying the table traversals. A shared background-layer/coverage design must
preserve image positioning, missing-cell coverage, separated-border gaps,
`empty-cells`, and effect/transform scopes. Normal DOM-order row rectangles
cannot provide that contract; the three saved raster/browser controls demonstrate
why both background order and originating-cell coverage matter.

ASan prerequisite follow-up: the original page passes three instrumented view
lifecycles and the minimal interactive view passes, but one-shot PNG rendering
of the same fixture exposes a second real lifetime gap. `dom_shutdown` calls
`reset_pending_iframe_loads` after `free_document` destroyed the document's
registry; a queued iframe event still owns a native node pin there. Preserve
`runtime-asan-results-before.json` and `load-runtime-asan-render.log`. The fix
belongs to the pending resource-load queue, not sanitizer settings or a global
pre-destroy reset: a document and its evaluator can retire in either order,
and one evaluator may queue events for multiple documents.

Design/acceptance: register one existing document resource per queue/document
pair, backed by the actual runtime capsule. Either owner cancels that document's
pending iframe/image entries and releases their pins while the registry is live;
document callbacks use the stored capsule without rebinding TLS (D5.3.3 / D5.4.1).
Reuse resource registration/release and the current queue shapes. Verify pin
release on runtime-resource retirement without touching a second document's
queue, then document-first retirement, normal draining and runtime shutdown.
Add a deterministic pin-count regression before editing production code.
Rerun focused ASan view/render lifecycles. This extends into `lambda/dom/`, so
fresh Lambda and Radiant baseline gates are required after the queue fix,
followed by the exact layout gate. Earlier regular passes remain evidence for
the preceding implementation, not the final queue repair.

Queue implementation: iframe and image enqueue paths share a document-resource
registration helper. Its stored capsule/document pair removes that document's
queued entries and releases their pins; runtime reset releases those same
resources instead of keeping a second unpin path. No public API or module
export changes. The new pin-count regression fails before the fix and passes
afterward, covering two documents in one capsule and both retirement orders.
All **144 native text/layout tests** pass. The separate supported ASan host
passes three original-page view lifecycles, the minimal interactive view and
the previously failing PNG render, with no ASan diagnostics. Coverage is the
instrumented CLI host; vendor libraries and the native GTest parent remain
uninstrumented, and leak detection is disabled. Preserve failed sanitizer logs;
the clean result is `runtime-asan-results-queue.json`. Fresh Lambda, exact layout
and Radiant gates now qualify this final queue implementation sequentially.

The first post-queue Lambda aggregate reports **7,891/7,892 passed**: the sole
failure is `BinarySchemaTest.LambdaPdfOffsetsAndImageStreamsAreValidInBothTiers`.
Both engine producers succeed; the Python validator exits at `from PIL import
Image` because the default Python 3.14 lacks Pillow. All 315 precise-GC stress
cases and the 1,620-case main Lambda corpus pass. Preserve `lambda-queue-final.log`.
Supply Pillow 13.0.0 in `temp/layout-analysis/round11/python-env` with system
site packages inherited; do not modify validators or global Python. The exact
failing case then passes in both tiers (`pdf-validator-local-python.log`).
Fresh gates run with that local environment's `bin` prepended to `PATH`, in
the order exact layout, Lambda, then Radiant. Their logs are
`baseline-queue-final.log`, `lambda-queue-python-final.log`, and
`radiant-queue-final.log`. Source, floors and expectations stay unchanged.

Round 11 final qualification is complete on CLI SHA256
`b03a592bce5720ff4712a3b7d45b85e623e83664cd3ad0919131312c28f7af5c`.
Exact layout passes **3,141 full /153 partial /0 failed /0 skipped** across
3,294 cases. Lambda passes **7,892/7,892**, with the local Pillow environment
above. Full Radiant passes **5,893 full /157 expected partial /1 existing skip
/0 failed** across 6,051 cases, including all **144 native text/layout**, **491
paged-media**, **111 page-load**, **644 UI** and **208 viewer** checks. The
normal CLI stayed unchanged across the serial gates. All five focused ASan
lifecycle controls pass with the documented coverage; integer-cast lint,
inventory freshness and diff checks pass. The consolidated evidence is
`temp/layout-analysis/round11/qualification-queue-final.json`. No required
qualification remains for this table-height/lifetime milestone. The overall
goal remains active for remaining partials and the table paint repair below.

## Round 12 read-only triage: table background propagation

Round 11 qualification remains active; its sources and executable stay frozen.
Twelve font-free native/browser controls under `temp/layout-analysis/round12/`
separate source coverage from paint order. Eleven differ in pixels. The
cross-cell overflowing block-background control already matches exactly:
globally prepainting every cell background would change that correct overlap.
The earlier label "table paint-order defect" was therefore too broad. The first
wrong stage is painting a row rectangle where the cell should paint propagated
background layers. No production paint code has changed yet.

[CSS Tables 3 draft §5.1 and §5.3.2](https://www.w3.org/TR/css-tables-3/#drawing-cell-backgrounds)
preserves DOM-order cell painting and assigns each cell the backgrounds of its
originating column group, column, row group and row, below its own background.
Positioning and image sizing use the source box, while coverage belongs to the
cell. Missing anonymous slots have no such backgrounds. This makes the
[CSS 2.2 §17.5.1](https://www.w3.org/TR/CSS22/tables.html#table-layers)
layer model concrete without replacing the tree walk with a global prepass.
The Tables 3 text is a working draft; keep that status explicit.

The controls expose rowspan content covered by later rows, missing extension
of the originating row's solid/gradient background, absent column/column-group
backgrounds, painted separated-border gaps, and paint in missing-cell slots.
An opacity control challenges effect ownership. The empty-cells control needs
separate treatment: Chromium computes `hide` but still shows the row background
behind a truly empty cell; both CSS 2.2 and the Tables 3 draft say the table
background should show through all internal layers. Preserve that browser
discrepancy instead of inventing a matching exception.

Design direction after round 11 closes: reuse the existing background producer
with distinct positioning and painting rectangles, selected from retained view
geometry. Reuse `view_geometry_ancestor_offset` and the existing column-source
lookup, promoting its static helper through `layout_table.hpp` if needed.
Source tracks/groups stop painting independent background rectangles. Cells
record their propagated layers in the existing raster/export background path,
preserving their DOM order, current transform/effect scope and canonical style
owners (D4.5.1v4 / D4.1.4v5). Fast boundary paths must use the same model; cells
without their own background or boundary still need propagated layers. Do not
copy traversal helpers, rebuild table layout during paint, or special-case
spanning-cell visibility. Include solid and gradient sources, both border
models, retained reflow, source-free cells, nested tables, opacity and exports
in the finite acceptance plan before coding.

Expanded triage now covers nineteen controls. Three are unchanged passing
guards: cross-cell overlap, CSS table cells without an authored background,
and nested alpha tables. Repeated `<col span>` and childless column-group spans
both use their published bounding box for gradient positioning in Chromium.
Additional controls cover five alpha layers, ignored track padding/borders,
and an SVG image source. The core source/painting rectangle split must handle
all of these through one producer, without mutating source styles. PDF's
`radiant_pdf_walk.cpp` uses the shared full boundary producer for its graphic
path but also has a direct simple-boundary dispatch; that optimization must
observe the same table-background policy. Preserve functional snapshot/export
ownership under S2.6.6v3 and the existing module boundary under D7.3.3–D7.3.4.

Active round 12 plan: restore usable striped/spanning Markdown tables by fixing
the background producer, after round 11's complete qualification. Acceptance
uses the nineteen controls in `layout_table_background_layers.html`: originating
solid, gradient and SVG-image sources; repeated columns and groups; separated
gaps and missing slots; source-free CSS cells; alpha/effect scopes; existing
cross-cell overlap and nested-table guards. `empty-cells:hide` follows the
explicit standards rule despite the saved Chromium discrepancy. Verify both
device densities and retained reflow, and preserve canonical source insets.

The shared producer will enumerate the originating sources and keep their
positioning rectangles separate from cell coverage. Raster, SVG and PDF's
graphic path must consume that same plan; primitive/direct fast paths may run
only where the plan has no extra table layers. Reuse the existing column lookup
and ancestor-offset helpers. Painting gets a temporary effective source box
with inapplicable track padding/borders removed, without altering its computed
style (D4.5.1v4 / D4.1.4v5). Keep source selection independent of backend and do
not introduce another renderer traversal or table-layout cache. The native
pixel test must fail first. Verification budget: focused pixel checks and
browser/native captures, targeted SVG/PDF exports, neighboring table/Markdown
checks, then exact layout and required Radiant qualification. Validate module
boundaries for the shared export policy; additional Lambda qualification applies
if the module-side implementation changes its affected path. Preserve all
references, floors and existing tolerances.

The first producer implementation fixes all pixel samples except the first
zero-height-row control. That row's spanning cell was never painted: shared
paint/scroll extent scanning uses the block-flow classification, which excludes
structural table rows/groups/cells. Correct the existing extent scan to traverse
all block boxes in its paint/scroll mode, preserving flow-sizing classification.
Do not add a special rowspan culling bypass. The same original control supplies
the regression, at both densities and through retained reflow. All background
source selection stays in the shared producer; the source image box and cell
coverage remain separate.

Round 12 focused implementation is complete. The shared source/coverage plan
now drives raster, SVG and PDF, including PDF's former direct simple-boundary
dispatch. The column lookup is promoted rather than copied; the third borrowed
boundary projection shape is extracted as `view_with_boundary`. Its synchronous
use preserves canonical source ownership under D4.5.1v4 / D4.1.4v5 and functional
PDF resources under S2.6.6v3. Existing source padding remains unchanged after
painting and retained reflow. The pixel assertion helper is shared with the map
tests rather than duplicated.

Two added zero-height source controls fail first in `native-zero-before.log`:
the tile planner rejects the source positioning box despite positive cell
coverage and explicitly sized tiles. Allow zero positioning dimensions while
retaining positive coverage and resolved-tile checks. Recheck size after `round`
resizing: CSS Backgrounds 3 §2.9 suppresses a resolved zero-sized image. No
rowspan-specific bypass or synthetic source dimensions are introduced.

The resulting 21 controls pass all 46 literal native samples at both densities
through two layout/paint passes. Full focused runners pass **145 native text/
layout**, **491 paged-media**, and **20 map** checks (`native-final.log`,
`paged-final.log`, `map-final.log`). Browser/native captures match exactly for
18 controls. Two standards discrepancies are preserved: Chromium retains row
paint behind a hidden empty cell and uses ignored separate-model row border
insets to shrink its gradient basis. The native producer follows CSS 2.2
§17.5.1/§17.6.1. A third control differs only by one 8-bit color level under
opacity; source opacity stays exactly 0.5.

Focused native PNG and PDF exports pass all 46 literal samples. The SVG export
passes 44/46 literal samples: its two opacity samples are 126 rather than
128±1, matching Chromium's HTML reference exactly. The exported `<g>` carries
`opacity="0.5000"`; preserve this rasterizer quantization difference without
changing the correct producer or the permanent native tolerance. All other SVG
samples, including both zero-height image sources, pass. The existing continuous
PDF session produces a 1250×800 page for a 1200×800 requested viewport; every
control is inside the checked region, and unrelated page sizing remains outside
this milestone. `export-pixel-results-final.log` preserves the raw differences.
The original sponsor-table PNG now shows all linked images instead of covering
them with later row backgrounds; remaining automatic column-width differences
are a separate geometry investigation.

Round 12 aggregate qualification is complete on CLI SHA256
`563d1bb9d745cbecfff93d14bb626ea9700b84a3effd8cbe76a86aaba44d5157`:
exact layout **3,141 full /153 partial /0 failed /0 skipped**, Lambda
**7,892/7,892**, and full Radiant **5,894 full /157 expected partial /1 existing
skip /0 failed** across 6,052 cases. The CLI hash is identical before and after
the serial gates. Native text/layout **145**, paged-media **491**, page-load
**111**, viewer **208**, UI automation **644**, and page-snapshot **51** checks
pass in the aggregate. The local round 11 Python/Pillow environment supplies the
PDF validator dependency without changing global Python or test expectations.

Module-boundary validation passes (134 deferred Class-F imports against the
unchanged 165 baseline). Current ASan CLI exports pass PNG, SVG and PDF with
`detect_leaks=0:halt_on_error=1`; prebuilt vendor libraries and native GTest
parents are uninstrumented. Cast lint, CSS inventory freshness and diff checks
pass. `temp/layout-analysis/round12/qualification-final.json` consolidates the
evidence, including the raw browser/export discrepancies above. This table
background milestone has no required qualification left; the overall goal
remains active for remaining partials and the intrinsic substitution repair.

## Round 13 read-only triage: intrinsic table widths

Round 12 sources remain frozen during aggregate qualification. Seven local
font-free table controls under `temp/layout-analysis/round13/` separate an
explicit width, `width:max-content` with a maximum, spans, rowspans, padding,
and custom-property substitution. Do not treat their initial large differences
as one percentage-distribution defect. Replacing the row variable with the
same literal padding removes most differences without any engine edit.

The variable controls produce `NaN` cell intrinsic minimums: debugger evidence
in `column-widths.log` shows that the nested 128px block is measured correctly,
but the cell's final box contribution is invalid. The cyclic-padding path
re-reads raw shorthand components in `get_intrinsic_box_side_widths_from_css`;
`resolve_length_value` substitutes through the active view. Verify the measured
element's owner context and substitute the entire shorthand before side
selection. The corrected debugger inspection (`variable-owner-valid.log`)
confirms `RDT_VIEW_TABLE` during the cell padding queries: the resolver looks
up a row's variable from the table instead of the measured cell. The earlier
`variable-owner.log` stops at an unreadable tagged pointer expression and is
not evidence for the owner. Whole-shorthand substitution and computed-invalid
handling follow [CSS Variables 1 §3.1–§3.2](https://www.w3.org/TR/css-variables-1/#shorthand-variables).
Do not replace invalid arithmetic with a clamp or treat valid variables as zero.

Literal-padding controls at 898px match explicit-width cell boxes within 0.33px,
but `width:max-content` controls underfill by up to 54.2px. The separate table
intrinsic routine in `intrinsic_sizing.cpp` sums cell widths without the used
table solver's intrinsic percentage plan; its independent per-row counters also
do not model occupied rowspan slots. These observations identify investigation
boundaries, not a verified repair for the original 8.75px sponsor-cell mismatch.
Reuse table metadata/column helpers when addressing that model; do not grow a
third table sizing algorithm or fit its distribution to the sponsor page.

The next finite milestone is owner-correct intrinsic box substitution: valid
inherited/overridden variables, multi-token padding, math/fallbacks, invalid
computed declarations, cyclic percentage padding and retained reflow. Reuse
the existing substitution, cascade, box-side and scratch-owner helpers under
D4.5.1v4 / D4.1.4v5. Establish native failures and browser controls before
editing. Focused intrinsic/table checks and cast lint precede exact layout and
required Radiant qualification; extend module/export checks only for changes
to those consumers or ownership boundaries. Percentage/rowspan intrinsic model
work remains separate until the substitution milestone has usable core coverage.

Fourteen bounded variable controls are now captured in
`intrinsic-box-variables.html` and `compare-variables-before.json` under the
round 13 temporary directory. Row-defined lengths, cell overrides, whole/pair
shorthands, arithmetic and cyclic percentages all expose zero-width cells;
ancestor-variable and fallback controls already pass. A logical-padding guard
is 80px instead of 104px: the intrinsic physical reparse misses the winning
logical declaration. The ordinary inline-block guard remains 104px in both
native geometry and the browser PNG. Reuse `layout_cascaded_style_declaration`
and `layout_logical_sides` for physical/logical competition, together with the
existing substitution/projected-shorthand helpers; do not copy the box-family
tables or bypass selected paged bindings. These are read-only diagnostics;
no round 13 production or permanent test edit has started.

Round 13 implementation starts with the failing permanent
`IntrinsicBoxVariablesUseTheMeasuredOwnerAndResolvedSides` regression and
`layout_intrinsic_box_variables.html` (raw red: `native-before.log`). Intrinsic
padding probes now use the measured element, the existing component cascade,
whole-declaration substitution/validation and shared physical/logical side
projection. Cyclic percentage terms resolve against zero while preserving
length terms. Invalid computed padding keeps its cascade win and initial zero;
canonical style ownership remains unchanged (D4.5.1v4 / D4.1.4v5).

The side projector is extracted from the existing scroll-spacing consumer,
and the repeated element-direction/writing-mode mapping is shared rather than
copied. The first implementation passes all fourteen width controls at both
densities and through retained layout. Neighboring native text/layout **146**,
paged-media **491**, and CSS **147** tests pass on that intermediate source.

The required PNG comparison finds a second instance of the same owner defect:
all cell rectangles match exactly, but two cyclic-percentage cells retain
padding from the outer 1200px constraint. Their children paint at x=120/130
instead of x=8/20. The final percentage-spacing updater substitutes through the
table's active view and skips its unresolved result. Extend the permanent test
with used padding and child-position assertions (`native-used-before.log` is
red), then share the owner-aware resolved-side helper between intrinsic and
final percentage stages. Keep the finalized table basis and width-distribution
algorithm unchanged. No broad qualification is claimed for the intermediate
width-only result; fresh pixel, neighboring, boundary and required aggregate
checks follow the complete value path.

The complete fourteen-control regression now passes widths, used padding and
child positions at both densities through retained reflow (`native-used.log`).
The 1200×800 native PNG matches Chromium exactly: **0 different pixels** in
`variable-pixels-used.json`, compared with 6,400 differing pixels in the
intermediate width-only implementation. Preserve that intermediate capture and
report under the `width-only` names. The new shared resolved-component helper
keeps whole-declaration validation/projection and selected paged substitutions
in the CSS owner; the raw variable resolver remains file-private. Final native,
paged, CSS, module-boundary, cast-lint and scoped ASan checks are running before
fresh aggregate qualification. The table width distribution is unchanged.

An adjacent font-owner control finds a remaining instance before qualification:
a 10px cell's `calc(1em + 10%)` uses the table's 16px font in the final update,
giving 26px instead of 20px padding. Stop the pending validation chain (task
group 60427, terminal 143) before its boundary/ASan stage, add the fifteenth
control, and preserve `native-font-before.log` as red. `LayoutFontScope` and
the existing `setup_font` bind math evaluation to the receiving cell and restore
the surrounding context afterward. The complete regression then passes
(`native-font.log`), and the fifteen-control 1200×800 PNG again matches Chromium
exactly (`variable-pixels-final.json`). Final checks restart against this source;
the interrupted and pre-font results do not qualify the font-owner repair.

Final round 13 preflight passes **146 native text/layout**, **491 paged-media**,
**147 CSS**, all **57 table** baseline cases and Markdown **48 full /10 partial**
with unchanged floors. Strict module boundaries, integer-cast lint and inventory
freshness pass. The current ASan CLI PNG is pixel-identical to Chromium across
all fifteen controls, with leak detection disabled and the previously documented
host/vendor instrumentation scope. The additional PDF validator passes in both
tiers in its actual `test_lambda_errors_gtest` runner. Preserve the initial
wrong-runner attempt separately; it never executed a test.

Final round 13 qualification completes on CLI SHA256
`b705ae06ba52fb71abc49bab123780ae3a490ec00ea834ed5100156168929dea`.
Exact layout passes **3,141 full /153 partial /0 failed /0 skipped** across
3,294 cases. Full Radiant passes **5,895 full /157 expected partial /1 existing
skip /0 failed** across 6,053 cases, including all 146 native text/layout,
491 paged-media, 644 UI automation, 208 viewer and 111 page-load checks. The
serial aggregate chain (session 74445) exits 0, and the executable hash remains
identical before and after both gates. `temp/layout-analysis/round13/qualification-final.json`
consolidates the current fifteen-control pixel, preflight and aggregate evidence.
This owner/substitution milestone has no required qualification left; the
overall partial-repair goal remains active for the responsive-image defect below.
No runtime or module-side source changed in this round, so its affected PDF
boundary receives fresh focused validation without repeating the unchanged
complete Lambda runtime baseline from round 12.

## Round 14 read-only triage: responsive image intrinsic minimums

The sponsor-table partial remains 8.75px in one cell. Cell measurements retain
the authored 128/64px image width in their intrinsic minimum, and the definite
width shortcut is reached during both whole-table and per-cell queries
(`sponsor-minima.log`, `sponsor-image-state.log`). Optimized debugger variables
are unavailable at that shortcut; do not infer their values from missing data.

Seven new font-free controls under `temp/layout-analysis/round14/` isolate the
behavior without the sponsor page or percentage-column distribution. A 40px
table with a 64px image and percentage `max-width` stays 40px in Chromium, but
Radiant forces it to 64px. Direct, anchor-wrapped, span-wrapped, 50% maximum and
`calc(100%)` maximum cases all differ. Unconstrained and explicit 64px minimum
guards agree. This is a common responsive-image sizing defect, not an offset
to fit the sponsor table.

The existing replaced-size path computes a zero min-content contribution for
cyclic percentage maxima, then applies the remaining constraints. The earlier
definite-width shortcut instead assigns its one clamped width to both minimum
and maximum. Investigate that first lost distinction before changing percentage
distribution or replacing the whole table intrinsic model. Reuse
`intrinsic_apply_definite_width_constraints` so an authored minimum still floors
the suppressed contribution (CSS Sizing 3 §5.2.1); keep the natural/explicit
max-content contribution. This is a hypothesis until a failing native test and
the measured shortcut state confirm the repair.

After round 13 qualification closes, the finite acceptance covers those seven
controls, inline wrappers, retained layout and both densities, plus the original
Markdown/table comparison. Focused intrinsic/replaced-size neighbors precede
exact layout and required Radiant gates. Keep all references and floors; use
additional boundary/ASan checks only if the implementation changes APIs or
ownership rather than arithmetic. The larger table percentage/rowspan intrinsic
model remains separate. No round 14 production edit has started.

Round 14 implementation adds `layout_responsive_image_minimum.html` with nine
controls and one shared SVG asset. The original seven receive two guards for
decoration and an authored CSS width, which already follows the general path.
The native regression fails first (`native-before.log`): hinted percentage-max
images force 40px tables to 64px, and a decorated image forces 76px. The definite
shortcut now honors the existing zero-content-minimum decision, starting from
its calculated padding/border floor and applying the same minimum constraints.
It preserves the maximum contribution. This reuses the existing helper and
does not change table percentage distribution or special-case image tags.

The added height assertions expose unrelated shorthand parsing: `font:0/0
sans-serif` is rejected because `css_parse_font_shorthand` does not accept a
zero NUMBER as its size token. Equivalent `0px` and explicit longhands yield
the expected zero strut (`font-length.json`, `font-longhands.json`). The new
image fixture uses the explicit zero length; original controls and the failed
unitless-shorthand reproduction remain saved. That parser gap is separate
backlog, not a responsive-image repair.

All nine table widths, used image widths/heights and table heights now pass
at both densities through retained reflow (`native-isolated.log`). The PNG
comparison retains a 40-pixel difference at y=12: native exposes the green cell
background where Chromium retains the first image's fractional red edge.
Other pixels match; no tolerance or reference is changed. Keep
`image-pixels-first.json` as raw evidence rather than claiming exact raster
parity. Remaining fractional paint coverage is outside the intrinsic-size fix.

The original `md_remark-readme` partial is now fully matched: **376/376 elements,
408/408 text nodes, 2/2 spans** with no differences. Markdown improves from
**48 full /10 partial** to **49 full /9 partial**, and all **57 table** baseline
cases pass (`inventory-first.log`, unchanged references/floors). Native text/
layout **147/147** passes; neighboring paged-media and final lint/inventory
checks precede fresh exact layout and required Radiant qualification. APIs and
ownership are unchanged by this arithmetic repair, so the prior boundary/ASan
coverage is not rerun or claimed as new source instrumentation.

Round 14 qualification completes on CLI SHA256
`26314f9400e3db1bc15e8afd7f4d040b042d8bc359f771a08560fcb779c848e8`.
Exact layout passes **3,142 full /152 partial /0 failed /0 skipped** across
3,294 cases. Full Radiant passes **5,897 full /156 expected partial /1 existing
skip /0 failed** across 6,054 cases, including 147 native text/layout, 491
paged-media, 644 UI automation, 208 viewer and 111 page-load checks. The serial
qualification chain exits 0, its process exits, and the CLI hash remains
unchanged. `temp/layout-analysis/round14/qualification-final.json` records the
focused checks, original Markdown improvement, raw 40-pixel paint residue and
aggregate results. This intrinsic-minimum milestone has no qualification left;
the overall partial-repair goal remains active.

## Round 15 read-only diagnosis: URL carrier retirement blocks table reduction

Reduced inline controls distinguish ordinary space wrapping from Chromium's
additional before-`<` punctuation opportunity. The platform line-break resource
does not supply that ASCII opportunity either. Do not infer a shared repair
for punctuation tailoring and table code fragments from those two observations.
The isolated original UUID table still reproduces its missing code fragment
with the baseline fixture-font stylesheet applied.

That reduction also exposes a deterministic teardown fault with `<base href>`.
The resource-free `temp/layout-analysis/round15/base-only.html` reproduces it:
normal layout exits 1 after writing its view tree; the existing round 13 ASan
CLI exits 134 with a heap-use-after-free. The loader frees the caller's original
URL from `superseded_html_urls` at `cmd_layout.cpp:2641`; CLI teardown later
destroys the same carrier. Redirect handling uses the same replacement shape.
The ASan executable predates round 14 arithmetic; this evidence qualifies the
unchanged URL path only. `diagnosis-pending.json` retains both executable hashes
and the raw crash logs. No round 15 engine edit has started.

Treat the owner defect as the next finite prerequisite. **D4.1.4v5 / D4.2.6**
require preserving the chosen lifetime owner through Input/pool teardown.
`dom_document_replace_url` already preserves URL carrier identity by exchanging
owned fields. Assess extracting/reusing that operation for redirects and base
processing, replacing the superseded-pointer list. Check borrowed href views
and early-load failures before editing. Acceptance covers carrier identity and
final contents, base-only CLI layout/PNG cleanup, redirected/base resource
resolution and failed loads. Focused URL/native loader checks and targeted ASan
precede the affected library, exact layout and required Radiant gates. Keep
punctuation tailoring and the broader document-URL/base-URI model separate from
this ownership repair.

Round 15 implementation first adds `layout_base_url_lifetime.html` and
`BaseReplacementPreservesTheLoadersUrlCarrier`. The native test fails before
the repair: the returned document uses a different carrier (`native-before.log`).
The existing DOM field-exchange operation is extracted to
`lib/url.c:url_replace_contents`, with its ownership contract in `url.h`.
HTML redirect/base handling and DOM replacement all use it. The bounded
superseded-pointer list disappears. HTTP error construction refreshes its
borrowed href after an exchange; self-replacement is an explicit no-op.

The native regression, all 36 URL-extra checks and the formerly crashing
base-only CLI layout now pass. A library regression checks that an existing
alias resolves relatives against all the replacement components and survives
self-replacement. Fresh ASan and strict module-boundary builds precede HTTP
redirect/base/error controls and required aggregate qualification. These first
passes do not qualify the still-pending sanitizer and aggregate checks.

Fresh preflight now passes **148 native text/layout** checks and the PDF offset/
image-stream validator in both tiers. The base-only 1200×800 PNG matches Chromium
exactly. Twelve CLI controls cover local base handling, the isolated UUID table,
HTTP success, redirect plus base-relative CSS, redirected HTTP failure and an
expected missing-file error, each with native and fresh ASan binaries. All have
the expected exits and no sanitizer fault; leak detection is disabled, and
prebuilt vendor libraries/native GTest parents are not instrumented. Preserve
the first cache-observation attempt: its request-count assertion detected reuse
of the native CSS response by the ASan process. Distinct resource URLs then
verify a separate CSS request for each binary (`url-lifetime-final.json`).

Strict module boundaries pass with 134 deferred Class-F imports against the
unchanged 165-entry baseline. Integer-cast lint and the regenerated 421-name
inventory pass. The first inventory check correctly reports stale source
locations before regeneration. Library/input neighbors are rebuilt; serial
library, input, exact layout and required full Radiant gates remain pending.

The library gate passes **1,659/1,659**. The broad input gate completes with
**4,340 passed /46 failed**: ten Wiki writer cases report an unknown
`input_type` option, 24 PDF/export cases fail, and twelve prose round-trip
cases fail. `input-summary-final.json` preserves every failing name. These
failures are not relabeled expected or covered by the passing layout result;
their before/after regression status has not been established for every path.
The original serial chain exits 2 at this gate. Exact layout and full Radiant
then run serially on unchanged source in session 43544; exact layout passes
**3,142 full /152 partial /0 failed /0 skipped**. Full Radiant remains pending.

Gate preparation changes the CLI fingerprint from
`4dd5fbfa7085a50f94dff2a554262b1ff06a7fe4502727404e49f70e05afa76d`
to `a73b5bdc8aa9bceacf603b7fb7788c05278ce4aabc4cabbf7b1752cc3f2156d8`.
Both build targets select `debug_native`; the Radiant target also forces a
relink. The post-input and post-Radiant-build fingerprints agree. Preserve the
failed whole-preparation hash assertion rather than claiming an unchanged
executable across those builds. All twelve CLI controls repeat successfully
against the current aggregate executable, and its base-only PNG again matches
Chromium exactly. Earlier controls remain in `pre-aggregate-controls/`.

## Round 16 read-only candidate: closing punctuation across inline boundaries

The isolated UUID table records `BRK_CL` after the final `)` in
`options.random = (random)`. That candidate replaces the earlier fitting space
because the closing-punctuation path checks only the remainder of its current
text node. It misses the following `]` outside the `code` element. The debugger
confirms the candidate kind and width; its attempted parent-sibling dump fails
on the tagged wrapper, so that failed dump is not evidence that a sibling is
absent (`round15/table-wrap-kind-debugger.log`).

Five Ahem controls under `temp/layout-analysis/round16/` isolate this loss:
plain, nested and padded split-node spans remain one fragment in Radiant while
Chromium splits them at the earlier space. Same-node and atomic-inline guards
already match. **CSS Text 3 §1.4 /§5.5** ignores inline box boundaries for text
adjacency, and **UAX #14 LB13** suppresses a break before the following closing
punctuation. Reuse the existing inline-adjacency lookup; the two existing
current-text/following-inline lookup pairs and this third consumer should share
their shape. Acceptance adds retained reflow and both densities, then the
original Markdown/table comparison, focused neighbors, lint and the required
layout/Radiant gates. `round16/diagnosis-pending.json` records measured geometry.
No round 16 engine edit has started while round 15 qualification is live.

Round 15's actual make gates finish with **3,142 full /152 partial /0 failed**
for exact layout and **5,898 full /156 expected partial /1 existing skip /0
failed** for full Radiant (6,055 cases). Session 43544 then exits 1 at its
historical pre-build hash assertion. The preserved post-input, pre-layout/
Radiant, final and repeated-control fingerprints all agree on
`a73b5bdc8aa9bceacf603b7fb7788c05278ce4aabc4cabbf7b1752cc3f2156d8`.
`round15/qualification-final.json` records both the failed original hash check
and the successful post-build comparison. The 46 broad-input failures remain a
qualification limit; this gate is not claimed green. The requested layout/
Radiant checks and URL/library/ASan controls are complete, and the process is
terminal before any text-layout edit begins.

Additional round 16 controls disprove a bracket-only repair. EX/IS/SY suffixes
and same-node `)!` still admit an illegal candidate, while nowrap and an atomic
inline already behave correctly. The closing-candidate guard duplicates only
part of the restrictions in `layout_text_allows_letter_break`; reuse that
existing contextual check and its `line-break:anywhere` override. Twelve finite
controls now include preserved spaces from the original Markdown style and an
anywhere override. The latter must retain a later explicitly allowed boundary,
not stop at the preceding letter. CSS Text 3 §5.5 and UAX #14 LB13/LB15d govern
these adjacency restrictions. The source change stays in the candidate path
and the shared current-text/following-inline lookup.

Round 16 adds `layout_closing_punctuation_context.html` and the native
`ClosingPunctuationRetainsAdjacentInlineContext` regression. Preserve two
incorrect test setups separately: the first reads the column/page-fragment
counter instead of text rectangles, and the second omits native `@font-face`
setup. With the canonical font-processing step and actual `TextRect` chain,
the unmodified engine fails nine controls and passes three guards at both
densities through retained reflow (`native-before.log`). Those are the red
results for the identified candidate defect.

The private `peek_next_inline_codepoint` now owns the current-text/following-
inline lookup for segment-break, letter-break and closing-candidate consumers.
Closing candidates reuse `layout_text_allows_letter_break` instead of their
incomplete duplicated CL/NS guard. All twelve native controls pass. The PNG
comparison records **1,182 differing pixels /960,000**, dominated by Ahem edge
coverage, with a separate padded-fragment paint difference. Do not claim exact
raster parity or change the reference/tolerance (`closing-pixels-first.json`).

Focused text, inline, table and Markdown baseline categories retain zero
failures. The first UUID code-fragment difference disappears: text improves
from 719/723 to **722/723**, and elements from 647/653 to **648/653**. Its second
table still has a stranded bracket and an undersized first column; the whole
page remains partial. Markdown therefore stays **49 full /9 partial**. This
remaining unbreakable-run intrinsic-width case is a separate next diagnosis,
not evidence that the repaired closing candidate still admits `)]`. Full native/
paged neighbors, lint/inventory and exact layout/Radiant qualification follow.

Round 16 preflight passes **149 native text/layout**, **491 paged-media**,
integer-cast lint, inventory freshness and diff checks after the complete native
build preparation. Focused categories pass **887 full /93 partial** text,
**169 full** inline, **57 full** tables and **49 full /9 partial** Markdown,
all with zero failures and unchanged floors. The change is private layout
computation with no API/ownership alteration; round 15's unchanged URL-owner
boundary/ASan evidence is retained without claiming new sanitizer coverage of
this text path. Exact layout and full Radiant qualification now run serially.

The remaining UUID table mismatch has five reduced controls under
`temp/layout-analysis/round17/`: plain, mixed-font, padded, nested and split-word
cases require widths **80/100/108/108/108px** in Chromium but receive
**60/60/68/68/40px** in Radiant. This is a substantial intrinsic-run loss, not a
fractional paint offset. `calculate_min_content_width` already delegates to the
central intrinsic-measurement path; trace its child sizes and run state before
claiming a duplicate walker or editing. `round17/diagnosis-pending.json` records
the measured widths/heights and next finite investigation. No round 17 source
change starts during round 16 qualification.

Read-only source tracing locates the table-specific loss in
`measure_cell_widths` (`layout_table.cpp:6463`): its `append_inline_widths`
accumulates max-content widths but reduces child minima independently with
`max`. The plain Ahem control's 10/60/10px pieces consequently permit a 60px
cell minimum instead of an 80px unbroken run. Inspect the runtime child sizes
and share the existing intrinsic run computation before editing; do not copy
the other walk or add a width floor tied to a tag or fixture. CSS Tables 3
§3.8.2 and CSS Sizing 3 §2.1 provide the min-content basis. CSS 2.2 §17.5.2.2's
particular automatic-layout algorithm is non-normative guidance, not a mandated
algorithm. This next repair remains read-only during round 16's live gates.

Round 16 qualification is complete: exact layout passes **3,142 full /152
partial /0 failed /0 skipped**, and full Radiant passes **5,899 full /156
partial /0 failed /1 existing skip** (6,056 cases). The complete serial chain
exits zero and the CLI SHA-256 remains `17bb5d7f…8386780c` through both gates.
`round16/qualification-final.json` retains the focused checks and raw pixel
residue. Round 15's broader input failures remain a separate qualification
limit; no claim of a green input aggregate follows from these layout gates.

Round 17 now targets min-content continuity through ordinary inline elements
in automatic table cells. Runtime tracing confirms the plain cell receives
10/60/10px child minima and selects 60px, losing the unbreakable 80px run.
The split-word inline itself reports 38px min-content versus 68px max-content,
so both the shared inline reducer and table consumer need a common run model.
The finite acceptance set is the original UUID options table, the five reduced
Ahem controls, and adjacent space, forced-break, atomic, nowrap, anywhere and
break-all controls at both densities through retained reflow. Preserve mixed
fonts, inline decoration and nesting. First run the native regression red,
then focused neighbors and affected baseline categories; qualify with lint,
inventory, exact layout and full Radiant after the implementation is stable.

The round 17 regression `IntrinsicMinimaRetainInlineWordEdges` first fails on
the old engine for whole and partial words through nested inlines. The shared
`LayoutIntrinsicMinRun` replaces the generic whole-child punctuation heuristic
and the table's independent-child minimum reduction. Text and element results
carry first/last unbreakable segment widths plus internal-break state, retaining
their font advances and inline box edges. Node ends do not introduce wrapping;
the seam reuses the intrinsic text boundary classifier. Real spaces, forced
lines, optional breaks, atomics and out-of-flow descendants remain distinct.
An existing `pre` neighbor caught a first/last-line normalization error in the
initial edit; preserving distinct forced-line segments fixes it rather than
changing the test's expectation. Empty-inline and optional-break controls then
challenge the new shared state directly.

All **18 browser-derived controls**, at 1×/2× through retained layout, now pass,
along with **150 native text/layout** and **491 paged-media** cases. Affected
baseline categories retain **887 full /93 partial** text, **169 full** inline,
**57 full** tables and improve Markdown to **50 full /8 partial**, all with zero
failures. The original UUID page is now fully matched: **653/653 elements,
723/723 text entries and 2/2 spans**, with no differences. The initial 16-control
raw PNG comparison retains 1,976 differing pixels; geometry parity does not
establish exact raster parity. Final rendering and exact layout/full Radiant
qualification follow after complete native build preparation. The complete
result is copied by the existing constraint-keyed cache, preserving selected
view/snapshot ownership (**D4.5.1v4 /D4.1.4v5**); the old width-pair-only cache
description in RAD_05 §2.2 was corrected against the current implementation.

The first exact round 17 gate stops with **two sizing regressions**
(`slice-intrinsic-size` and `clone-intrinsic-size`), so full Radiant does not
start. Preserve `baseline-first-failed.log` and
`qualification-first-failed.json`. Both lose 4px per adjacent unbroken span:
the first-segment summary had received only the start margin, although the
same unbroken segment owns both margins. The shared
`layout_intrinsic_add_min_segment_edges` now handles this distinction for
margins, padding and borders; real internal breaks still assign the edges to
separate segments. Two additional browser-derived controls reproduce the loss
in a cell and a direct min-content query before the correction
(`native-margin-before.log`). The finite set is now twenty controls. Recheck
the sizing category along with the earlier neighbors before repeating the
required gates; no floor, reference or tolerance change addresses this defect.

The margin correction clears both decoration cases and all **776 sizing**
cases. The widened inline check then exposes
`dominant-baseline-mixed-writing-modes-002`: the summary retained vertical
inline advances after the query had converted its numeric result to physical
block-axis width. That summary cannot participate as a horizontal word edge.
The existing vertical-axis conversion now maps the edge summary to the physical
width as well. Preserve the failed `wpt-css-inline-final.json` observation and
verify this case with the sizing/text/inline/table/Markdown neighbors before
the aggregate retry. No aggregate was started on the margin-only version.

After the axis correction, all affected neighbors pass: **776 sizing**, **169
inline**, **57 table**, **887 full /93 partial text**, **50 full /8 partial
Markdown**, **150 native text/layout** and **491 paged-media** cases. The twenty
controls retain 1×/2× and retained-reflow coverage. The final 1200×1000 browser/
Radiant pair has **2,440 differing raw pixels /1,200,000**, retained as raster
residue. Exact layout qualification passes **3,143 full /151 partial /0 failed
/0 skipped**; full Radiant qualification is live under the frozen implementation.

The next read-only candidate is `md_ts-node-readme`'s inline environment-variable
example (`round18/diagnosis-pending.json`). Fresh Chromium geometry agrees with
its reference: the code text has two fragments, while Radiant keeps it whole on
the next line. Seven Ahem whitespace controls match in both engines, disproving
preserved whitespace alone as the cause. Eight punctuation controls isolate a
normal wrapping difference before an opening symbol after another symbol;
individual symbols remain whole, while the hyphen guard wraps in both engines.
Chromium documents printable-ASCII compatibility tailoring before opening
punctuation ([primary implementation](https://raw.githubusercontent.com/chromium/chromium/main/third_party/blink/renderer/platform/text/text_break_iterator.cc)).
Unicode 17 assigns U+003C–U+003E to AL
([LineBreak.txt](https://www.unicode.org/Public/17.0.0/ucd/LineBreak.txt)), so this
extra opportunity is not a default UAX #14 mandate. Design a general shared
boundary policy within CSS Text 3 §5/§5.5's UA tailoring before implementing;
do not add a fixture/operator-pair exception or copy vendor code. No round 18
engine edit begins during round 17 qualification.

Round 17 qualification is complete. Exact layout passes **3,143 full /151
partial /0 failed /0 skipped** (3,294 cases). Full Radiant passes **5,901 full
/155 partial /0 failed /1 existing skip** (6,057 cases). The complete retry
chain exits zero and its CLI hash remains unchanged through both gates;
`round17/qualification-final.json` records the actual fingerprint, native
neighbors, twenty controls, boundary/lint/inventory checks and pixel residue.
The failed first layout attempt and the subsequent mixed-axis observation stay
preserved as evidence of the corrections. The UUID README is fully matched,
and the shared horizontal word-edge milestone is closed. Round 15's broader
input failures remain a separate qualification limit; this gate does not
relabel them expected or establish a green input aggregate. The long-running
partial-pass goal remains active for the next normal punctuation boundary
milestone, whose reduced evidence and design guards are under `round18/`.

## Round 18: normal punctuation boundaries in inline code

**Priority/outcome:** common README examples with operator/opening punctuation
should use a fitting normal wrap opportunity instead of moving the entire
inline to the next line. The ts-node environment-variable example is the
original failing case. Round 17's previous turn made verified progress and
closed its shared intrinsic-word milestone; no aggregate remains live.

**Policy and owner:** use one character-class predicate for normal breaks
before ASCII opening punctuation, shared by text layout, lookahead and
intrinsic measurement (including the inline seam reducer). The 376-control
browser matrix establishes a consistent compatibility policy for all four
openers and preceding printable characters. This is UA punctuation tailoring
under CSS Text 3 §5/§5.5, not a claim that default UAX #14 requires it. Keep
letters, digits, word prefixes, openers and protected character units intact;
retain existing hyphen/question-mark behavior. Do not copy third-party source
or use a fixture string/operator-pair exception. Reuse the fitting-candidate
path and shared adjacency lookup rather than duplicating their arithmetic.

**Finite acceptance:** `layout_ascii_opening_breaks.html` has 22 browser-derived
cases: four openers; nested/split seams; padding; normal, break-word, anywhere,
break-all, keep-all and nowrap; letter/digit/opener/currency guards; a candidate
past the available line edge; and bare/slice/clone/nested/cell min-content
queries. Test both densities through retained reflow. The unmodified engine
fails sixteen controls and passes six guards (`round18/native-before.log`).
The break-all guard exposes the existing distinction between Unicode letter
categories and ASCII symbols of line-break class AL. The padded min-content
controls also require real slice/clone edge ownership when the new normal
opportunity splits a word; numeric minima must agree with their propagated
edge summaries. These are shared consumers of this boundary, not fixture
width adjustments.

**Verification budget:** run the native regression red, focused edit-loop
checks, then native text/paged neighbors and affected text/inline/sizing/table/
Markdown categories. Compare fresh native PNG with the saved Chromium image
without changing tolerances. Run integer-cast lint, inventory/diff checks and
module-boundary validation for the public helper, then complete build
preparation and serial exact layout/full Radiant gates. Keep unrelated broad
input failures and independent Unicode/paint residue recorded separately.

The shared normal predicate, break-all ASCII AL units, fitting-candidate
helper and lookahead/intrinsic consumers first pass **20/22** controls. The
remaining two expose the shared numeric slice minimum: 58px instead of 54px.
The result now preserves first/interior/last segments, composes forced lines
through the same reducer, and applies slice/clone edges to that summary.
Terminal spaces no longer invent an empty final fragment. Numeric minima and
propagated edge widths therefore agree. A second focused check exposes the
corresponding used-layout error: reserving a sliced end edge at an internal
candidate rejects the valid 54px prefix. The fitting helper now walks inline
owners and reserves only edges belonging to that fragment; clone owners
reserve their repeated end edge. Existing break kinds remain intact.

The acceptance set grows to **25** controls with terminal-space and nested
clone checks. The latter records a browser intrinsic overestimate rather than
copying it: flat clone min-content is 78px; a nested query returns 82px, while
literal-width 78px and 82px controls render identical, non-overflowing fragments.
The native guard uses 78px, consistent with CSS Sizing 3 §2.1 and CSS Break 3
§5.4; the raw browser comparison retains the 4px discrepancy. All 25 native
controls pass at 1×/2× through retained reflow, along with **151 native** and
**491 paged-media** neighbors. The raw 1200×1400 pair differs at **7,497 pixels
/1,680,000**, dominated by edge coverage with additional decoration residue;
do not claim exact PNG parity (`round18/pixels-first.json`). Affected baseline
categories and required aggregate qualification are still pending. The existing
cache copies the complete result, including the new interior width, under
**D4.5.1v4 /D4.1.4v5**; no ownership ruling changes.

Affected categories first retain zero failures: **887 full /93 partial text**,
**169 inline**, **776 sizing**, **57 table**, and Markdown improves to **52
full /6 partial**. Both original common-page examples now fully match:
ts-node **1,062/1,062 elements and 1,490/1,490 text**, conventional-changelog
**295/295 elements and 296/296 text**, with no differences. UUID remains full.

Direct clone-fragment assertions then disprove an incomplete parent-only
qualification: the parent minimum is 78px, but the code union starts at -4px
and spans 82px because its second child starts at 0 instead of 4px. Preserve
`native-final.log` (150 pass /1 fail). `output_text` had retained clone start
padding only when the text's direct parent was cloned, clearing an outer
clone's pending edge through an ordinary nested span. The new shared
`inline_fragment_edge_width` ancestor traversal computes both continuation
starts and fitting-candidate ends. Consumed slice starts disappear; all active
clone owners retain their own edge. The native guard checks the code union
and both actual child fragments at both densities through retained reflow.
Repeat affected neighbors after this shared layout change before qualification.

The final fragment assertions and affected neighbors pass after the continuation
repair: **151 native**, **491 paged**, and unchanged green affected categories,
including **52 full /6 partial Markdown**. The supplemental ASCII sweep matches
wrapping positions/heights in **376/376** cases; exact boxes match **372/376**.
The four width-only apostrophe differences remain in the failed comparison
artifact. Reading Ahem's Unicode cmap confirms U+0027 is absent; the residual
comes from a fallback advance difference, whose actual fallback faces are not
yet traced. Do not relabel this sweep exact or change its geometry tolerance.
Final PNG comparison retains **7,412 differing pixels /1,680,000** and the
documented nested-clone intrinsic discrepancy. Boundary validation passes
(strict lib/core/io/radiant DSOs; Class-F deferment 134 against ratchet 165),
integer-cast lint and 421-name inventory freshness pass, and complete build
preparation succeeds. Exact layout and full Radiant qualification now run
serially under a frozen implementation (`round18/qualification-pending.json`).

The exact round 18 layout gate passes **3,145 full /149 partial /0 failed
/0 skipped**; full Radiant remains live. The next read-only candidate is the
Zod logo's legacy `align="center"` hint (`round19/diagnosis-pending.json`).
Fresh Chromium uses `-webkit-baseline-middle`, with a 164.234375px logo and
paragraph. Native layout adds a baseline descent, producing 170.2px. Nine
Ahem controls isolate the missing presentational hints: center/middle/top and
absolute-middle aliases leave native 47px line boxes where the browser uses
40px; ordinary CSS `vertical-align:middle` and an author baseline override
already behave as controls. The image branch of `resolve_htm_style.cpp`
handles only left/right floats.

This is not a page-height correction. HTML Rendering §15.4.3 specifies legacy
center/middle alignment on the parent's baseline
([primary spec](https://html.spec.whatwg.org/multipage/rendering.html#attributes-for-embedded-content-and-images)).
It differs from CSS middle: the browser's Ahem text offsets are 12px versus
16px, so future regression checks must observe the text as well as box height.
The browser's bottom hint currently computes baseline while the cited spec
maps bottom to bottom; retain that disagreement explicitly. Design shared hint
resolution and cascade/used-value handling before editing, covering applicable
replaced elements rather than an image/file exception. No round 19 engine
change starts during round 18 qualification.

Round 18 qualification is complete: exact layout passes **3,145 full /149
partial /0 failed /0 skipped** (3,294 cases), and full Radiant passes **5,904
full /153 partial /0 failed /1 existing skip** (6,058 cases). The full serial
chain exits zero; CLI SHA-256 stays `2ee00891…64e05c7d` through both gates.
`round18/qualification-final.json` records the native/paged/category checks,
25 controls with direct cloned-fragment bounds, 376-case wrapping sweep,
strict module-boundary/lint/inventory checks and raw image limits. Both ts-node
and conventional-changelog README pages now fully match. The shared normal
ASCII-opening boundary and associated first/interior/last fragment milestone
is closed; the failed intermediate checks stay preserved. Broader round 15
input failures remain a separate qualification limit. The long-running goal
remains active for the legacy replaced-alignment milestone in `round19/`.

## Round 19: legacy alignment hints for replaced elements

**Priority/outcome:** the Zod logo's `align="center"` must align its midpoint
with the parent's baseline and stop adding an unrelated text-strut descent.
This common documentation/image behavior precedes its independent remaining
table-width discrepancy. The preceding goal turn made verified progress and
closed round 18; both required aggregates completed with zero failures.

**Design/owner:** add a distinct typed baseline-middle value, appending its enum
and keyword entry so existing value IDs remain stable (**D4.6.1v4**). A shared
HTML presentational-hint adapter applies to `img`, `iframe`, `embed`, `object`
and image-button inputs before author CSS. Case-insensitive center/middle use
the legacy value; absolute-middle aliases use ordinary CSS middle; top/texttop
use their CSS values. Existing left/right float handling moves into that
adapter; floated legacy elements retain the observed top computed alignment.
Unknown/non-applicable hints remain inert. Preserve the separately recorded
bottom-hint spec/browser disagreement rather than claiming complete alias
conformance. Promote/generalize `layout_middle_inline_contribution` and share
midpoint math with inline line contributions and final placement. Parent-font
x-height affects CSS middle; it must not affect legacy baseline-middle. Keep
selected style groups and snapshots owned under **D4.5.1v4 /D4.1.4v5**.

**Finite acceptance:** `layout_legacy_replaced_alignment.html` has 37 controls:
ordinary baseline; center/middle and case folding; top/texttop; absolute-middle
and authored CSS legacy/ordinary values; stylesheet/inline/initial/unset/inherit
precedence; unknown/untrimmed values; small images; differing element/parent
font sizes; all applicable replaced tags; ordinary span/text-input exclusions;
existing floats; and the original SVG. Test geometry, relative text/image
offsets and computed values at 1×/2× through retained reflow. The unmodified
engine fails sixteen named controls plus the SVG line-height invariant
(`round19/native-before.log`). Container height alone cannot prove legacy
alignment: the Ahem text offsets distinguish its 12px from CSS middle's 16px.

**Verification budget:** native regression red, focused edit loop, native text
and paged neighbors, affected form/text/inline/sizing/table/Markdown categories,
browser/native image comparison without floor or tolerance changes. The CSS
enum/keyword addition requires CSS input checks as well as full Radiant;
retain the known broad input qualification limit honestly if it recurs. Run
integer-cast lint, inventory and module-boundary checks; complete builds before
fingerprinting and serial exact layout/full Radiant qualification. Original Zod
logo behavior must improve; unrelated table and resource fidelity remain
separate work rather than page offsets or asset-specific exceptions.

**Round 19 edit-loop findings:** extending the controls to origin/layer rollback
and causal mutations exposed two shared prerequisites. Treating the hint as a
UA default made author `revert` retain it; float dispatch read the old retained
`PositionProp` before the pending style refresh and asserted when the image
became inline. A borrowed declaration now enters the existing cascade lookup
with a distinct author-hint origin: CSS Cascade 5 §6.5 makes it independent for
normal ordering/layers and part of author for `revert`. The lookup applies the
existing rollback filters across the property, shorthand and `all`; it neither
writes to canonical style trees nor allocates retained payloads (**D4.5.1v4 /
D4.1.4v5**). Float dispatch reads the current cascade when used properties are
dirty. Keep the initial failed regression and assertion trace in `round19/`.
The test observer also now carries an explicit expected float value, so a
mutation of the row named `left` does not accidentally assert its old state.
Native/static and causal browser checks distinguish the alignment values;
full milestone qualification remains pending.

The first neighboring category sweep found a real Marked README regression
(one inline link widened from 0px to 898px). Its float became visible to display
blockification before a separate raw-declaration classifier could see the HTML
hint. Two minimal float-only wrappers reproduce the same 200px error
(`native-wrapped-red.log`). `inline_out_of_flow_kind` now delegates to existing
shared flow helpers, and `layout_element_is_floated` also queries hints when no
authored tree exists. This removes duplicated lookup logic rather than adding a
wrapper/page exception. Browser anchors remain 0×0 for both float directions.

**Round 19 completion:** the typed legacy alignment and borrowed hint-origin
cascade now reach all five applicable replaced-element kinds. Shared midpoint
math distinguishes the parent baseline from CSS middle's parent x-height;
unknown/non-applicable hints stay inert. Author stylesheet/inline/layer/defaulting
controls, hint removal, input-type changes, float-to-inline transitions and
float-only inline wrappers pass retained native checks at 1×/2×. Zod's logo
paragraph now equals its image height (164.2px serialized), without adding the
old text-strut descent. The intermediate stale-state, rollback, assertion and
Marked README failures remain preserved; Marked is restored to full matching.

The frozen CLI `a0a5a93a…d038a863` passes exact layout **3,145 full /149 partial
/0 failed /0 skipped** (3,294), and full Radiant **5,905 full /153 partial /0
failed /1 existing skip** (6,059). Both actual Make targets exit zero, and the
binary/source fingerprints remain unchanged. Native text **152**, paged **491**,
and cascade **42** pass, as do all six affected category inventories. Strict
module boundaries (Class-F 134 vs 165 ratchet), integer-cast lint and the 421-name
inventory pass. The required input gate remains **4,341 passed /46 failed**
(4,387): its failed test-name set exactly matches round 15; this is an honest
qualification limit, not an expected-failure relabel or a proof of every causal
before/after path. No new ASan/full Lambda qualification is claimed.

The 37 Chromium/native controls have **35 exact relative-geometry matches**.
The excluded ordinary text-input metrics and 0.034375px serialized SVG size
difference remain visible. Raw full-viewport screenshots differ at **19,162
/2,400,000 RGB pixels**; preserve form/SVG raster limits rather than changing
comparison tolerances. `round19/qualification-final.json` links the actual gate
summaries and failed intermediate evidence. The layout-only Make target leaves
shared `test_summary.json` stale: its final saved JSON is parsed from the actual
terminal breakdown, and the mistaken input-summary copy is retained separately.

The long-running goal remains active. Read-only round 20 diagnosis separates a
stale Zod table-width reference (saved 543px, fresh Chromium 527.390625px using
the current 206×33 SVG; native 528px) from its real height error (180px browser,
196px native). The final paragraph's DOM and CSSOM correctly resolve a zero
bottom margin; used block-flow margins differ after sibling collapse.
`measure_cell_content_height` then rescans these margins and appears to add a
promoted self-collapsing margin twice. Six minimal controls and a DOM/CSSOM
probe are saved under `round20/`; compare `view_boundary_flow_margin` with used
margin-chain placement before fixing the shared table extent owner. Do not
patch selectors, force an end-of-layout margin, distort image ratios or change
references to hide the discrepancy. No round 20 engine edit occurred during
round 19 qualification.

## Round 20: table row height after adjoining empty blocks

**Priority/outcome:** remove the real extra 16px in Zod's sponsor table while
preserving intrinsic image ratios. Round 19 completed exact layout/full Radiant
with zero failures. Its required input gate retains 46 failures with the same
recorded names; this table-only milestone does not silently reclassify them.
Fresh Chromium gives the sponsor table 180px height and 527.390625px width;
native gives 196px and 528px. The saved 543px width reference disagrees with
current local image dimensions and remains an independent evidence limit.

**Owner/design:** CSS 2.2 §8.3.1 collapses an adjoining margin set once;
§17.5.3 derives cell/row minimum height from its laid-out content. The shared
`layout_table_cell_content` already records the normal-flow advance in
`content_height`. `measure_cell_content_height` instead reconstructs block
height from consumed/promoted boundary margins, potentially counting the same
chain twice or treating negative overflow as extra height. Confirm the cursor
and preserved `view_boundary_flow_margin` before changing this owner; reuse
existing flow classification/metrics rather than adding a paragraph/tag rule.
Keep selected style/snapshot ownership unchanged (**D4.5.1v4 /D4.1.4v5**).

**Finite acceptance:** eighteen permanent controls cover ordinary/flex tables,
leading/trailing/no empty blocks, positive/negative/mixed margin chains,
all-empty/nested blocks, border/min-height separators, relative positioning,
separate borders and zero margins. Native geometry must match Chromium at both
densities through retained reflow and causal tail-margin changes. Verify the
original Zod table against fresh browser geometry; leave stale width/reference
and other page residues explicit.

**Verification budget:** native regression red, focused edit loop, native text
and paged neighbors, affected form/box/sizing/table/Markdown inventories, PNG
comparison and original-page measurement. Then lint/inventory/diff and build
preparation before frozen serial exact layout/full Radiant gates. No new
allocation/lifetime change or runtime semantics ruling is planned; do not
repeat unrelated input/ASan/Lambda gates absent a relevant new cause.

**Round 20 edit-loop result:** twelve initial controls fail while their cell
normal-flow `content_height` already equals the correct browser content height.
The table rescan, not CSS selector/author margin resolution, owns the mismatch.
Cells with normal-flow block content now use that recorded cursor extent; the
existing flow classifier distinguishes block from atomic inline content.
Float extent is retained at the cell-flow owner before row alignment shifts
children. Two added mixed/middle-aligned float guards bring acceptance to 20
row-height controls. Native **153**, paged **491** and all five affected
category inventories pass. The original sponsor table is now 180px high;
Zod improves from 1,935 to 1,946 matched elements out of 1,974 against the saved
reference, while its independent stale-width and other differences remain.

The geometry observer also preserves three pre-existing zero-area negative
empty-box position differences (unchanged relative coordinates before/after),
and raw PNGs differ at 2,400 of 2,400,000 pixels within two float/background
paint overlaps. These independent block-position/painter residues are not
claimed fixed or hidden by tolerance changes. The original analysis wrapper
could not handle the box category's missing saved baseline; its failed output
is preserved. A corrected temporary observer retains the ordinary category
verdict, and all 17 box cases pass. Aggregate qualification remains pending.

**Round 20 completion:** table cells with normal-flow block content now derive
natural row height from the formatter's completed flow cursor. Consumed margin
chains are not reconstructed as fresh space, and negative overflow does not
inflate the row. Floats are included before alignment shifts their visual
positions. All 20 row-height controls and causal margin transitions pass at
1×/2×, including positive/negative/nested chains, separators and middle-aligned
mixed floats. Native text/layout **153**, paged **491**, and all five affected
category checks pass. Zod's real sponsor-table error is fixed (196px →180px,
matching fresh Chromium), improving saved-reference element matches by eleven;
the page remains partial for independent differences.

Exact layout passes **3,145 full /149 partial /0 failed /0 skipped** (3,294).
Full Radiant passes **5,906 full /153 partial /0 failed /1 existing skip**
(6,060). Both actual Make targets exit zero. CLI SHA-256
`6b7c70bf…d925b703`, qualifying production/test diff and permanent fixture stay
unchanged throughout the gates. Integer-cast lint, the 421-name inventory and
diff checks pass. No API/import/ownership placement changed, so module-boundary
validation is not repeated. The last input run is still round 19's 46-failure
qualification limit; this table-only change does not rerun that gate or transfer
its totals to the new binary. No new ASan/full Lambda qualification is claimed.
`round20/qualification-final.json` records these scopes and the preserved failed
regression/observer output. Three unchanged zero-area empty-box positions and
the 2,400-pixel float/background overlap residue remain unclaimed, with no
baseline/tolerance edits.

The long-running goal remains active. Read-only round 21 captures confirm a real
Axios table-width discrepancy and a resource/reference-state discrepancy for
Commander's unavailable remote badge. An Ahem probe removes glyph uncertainty:
five 1px-bordered columns measure 536px in Chromium and ordinary native tables,
but native block/inline-block max-content table wrappers measure 540px. Generic
intrinsic sizing appears to count full cell borders while the used collapsed
grid shares resolved edges; verify and reuse the existing conflict geometry
before editing. Axios also has smaller glyph-metric differences, so do not
promise exact page raster equality from a baseline pass. Jest's adjacent-link
wrapping mismatch is saved as another candidate; simple opening/link controls
already pass, so reduce its mixed-font context before choosing a root. No
round 21 engine change occurred during round 20 qualification.

## Round 21: collapsed borders in intrinsic table wrappers

**Priority/outcome:** fix Axios's real excess intrinsic table width. Ahem removes
font uncertainty: five cells with 80px text, 26px padding and 1px borders occupy
536px in Chromium and native table layout, but native block/inline-block table
wrappers request 540px. Wider row/column borders are also absent from intrinsic
measurement. The first disagreement is generic intrinsic track contributions.

**Owner/design:** CSS 2.2 §17.6.2 places winning borders on shared gridlines;
§17.6.2.1 supplies conflict precedence. Extract the existing used-layout edge
winner into a shared borrowed source adapter. Intrinsic measurement supplies its
source rows, span occupancy and resolved style boundaries, replaces full cell
borders with winning half edges, and includes the table's outer half edges.
Keep the official style resolver and scoped scratch storage; do not create a
parallel border parser or mutate retained used cell metadata. Selected style
ownership remains governed by **D4.5.1v4 /D4.1.4v5**.

**Finite acceptance:** seventeen Ahem controls cover ordinary/block/atomic,
min/max-content, one/two/five columns, wider/mixed/hidden borders, col/row spans,
row/column borders and separate-border controls. Browser widths must match at
1×/2× through retained reflow and collapse→separate→collapse mutation. Recheck
Axios against fresh Chromium; its independent glyph residue remains explicit.

**Verification budget:** regression red, focused edit loop, native text/paged
neighbors and affected form/box/sizing/table/Markdown categories, original page
and PNG comparison. Shared API extraction requires strict module-boundary
checks. Complete builds, lint/inventory/diff before frozen serial exact layout
and full Radiant gates. No unrelated input/ASan/Lambda gate absent a new cause.


**Round 21 edit-loop evidence:** the original seventeen width controls pass after
sharing conflict geometry and selecting table behavior by CSS display. The
separate-border guard exposed a pre-existing retained error: the HTML tag path
wrapped an already repaired anonymous grid in another measured grid, adding
6px spacing. CSS display now leaves the outer block to normal measurement.
Five border-owner/span guards and fractional padding extend acceptance to 23
tables plus the shrink-to-fit parent (24 width assertions). They expose two
additional shared causes: integer rounding of each cell contribution, and
adding the table's own border after it already won a collapsed outer edge.
These are removed; the paged min-content assertion now expects exact measured
fractional words plus padding instead of documenting old per-cell rounding.
The failed intermediate paged check is preserved, not masked by tolerance.
Final native/category and aggregate qualification remains pending.


**Round 21 completion:** generic intrinsic table sizing and used collapsed-border
layout now share the borrowed grid edge resolver. Span occupancy uses the shared
bounded HTML span helpers, and current row/group styles are resolved before
measurement. Fractional contributions and outer half edges remain fractional;
the table border is included once. CSS display selects table versus ordinary
block measurement, so retained anonymous grids do not acquire extra spacing.
All 24 width assertions match Chromium at 1×/2×, including the causal width
sequence 540→536→158→150. Native text/layout **154**, paged **491**, strict
module boundaries, integer-cast lint, 421-name inventory and diff checks pass.
All five affected category checks pass; Axios becomes a full baseline pass,
with width 495px→486.9px (fresh Chromium 481.859375px). Its independent checkmark
metric difference remains explicit.

Exact layout: **3,146 full /148 partial /0 failed /0 skipped** (3,294).
Full Radiant: **5,908 full /152 partial /0 failed /1 existing skip** (6,061).
Both actual Make targets exit zero; session 94913 is terminal. CLI SHA-256
`fee080c8…4a8f9a6b`, qualifying source/test diff and all permanent fixture hashes
stay unchanged throughout both gates. The initial red regression, missing-header
build error, retained separate-border failure and old paged rounding expectation
are preserved under `round21/`. No tolerances or baseline verdicts changed.
Seven non-asserted cell geometry differences (span/internal-track allocation)
and **8,471 /2,400,000 RGB pixels** remain recorded; matching table widths does
not claim full cell/raster conformance. The last input qualification remains
round 19's 46 failures; no new input/ASan/full Lambda result is claimed.
`round21/qualification-final.json` records the verification scopes and limits.

The long-running goal remains active. Read-only round 22 reduction brackets a
hyphen lookahead error: at Ahem widths 120/129, the prefix fills the line but
the first legal hyphen break is at 130. Chromium moves the joined inline word;
native leaves the first span on line one. Widths 110/119 and 130/131/140 already
match. `text_has_line_filled` marks the hyphen opportunity before checking its
advance, a candidate shared root that still needs a red regression and finite
acceptance before editing. Nested/small-font controls reproduce it, while
space/break-all/nowrap controls match. A reduced copy of the original Jest
paragraph retains the browser's exact 866px width and mixed-font/link positions.
No round 22 production edit occurred during round 21 qualification.

## Round 22: lookahead admits only fitting hyphen breaks

**Priority/outcome:** fix Jest's adjacent-link wrapping and the same common
word split across styled inlines. Round 21 qualified 3,146 full /148 partial
layout cases with zero failures. Its CLI/source and all aggregate handles are
revalidated as terminal before this milestone.

**Owner/hypothesis:** `text_has_line_filled` sets `has_break_opportunity` before
measuring a literal hyphen, and accepts a soft hyphen without measuring its
generated mark. At Ahem width 120/129, letters fill 120px but the first hyphen
ends at 130px. The false fitting break keeps the preceding inline on line one.
Widths 110/119 and 130/131/140 already match Chromium. CSS Text 3 §5 and §5.3
ignore inline boundaries within words; visible hyphens consume width, and a
conditional hyphen's generated mark participates in fitting. Reuse
`measure_hyphenate_character` and the existing float-aware line edge/spacing
policy. Keep lookahead observational and style ownership unchanged
(**D4.5.1v4 /D4.1.4v5**); no selector offsets, extra breaks or tolerance changes.

**Finite acceptance:** 23 permanent controls cover widths bracketing the first
hyphen, nested/smaller-font inlines, actual earlier fitting breaks, separating
spaces, break-all/nowrap, literal `hyphens:none`, soft/empty/double generated
marks and disabled conditional hyphenation. Match browser line origins and
heights at 1×/2× through retained reflow and causal 120→130→129→120 width changes.
Recheck the reduced and original Jest paragraph, including the exact 866px
mixed-font context. Keep unrelated Unicode/bidi/shaping residue explicit.

**Verification budget:** red regression, focused text/paged neighbors, browser
and native PNGs plus original Markdown, then text/inline/box/sizing/table/
Markdown category gates. No API or allocation/ownership changes are planned;
repeat strict module boundaries only if that scope changes. Complete build,
lint/inventory/diff before frozen serial exact layout/full Radiant gates.
Prior input qualification remains round 19's 46 failures; no new input/ASan/
full Lambda gate is claimed for a text-layout-only correction.

**Round 22 adjacent causes:** after the fitting-only change, all original line
origins pass. The smaller-font guard still ends at 40px instead of browser 42px.
A temporary trace records a real 22px leading union (13px above/9px below the
baseline) against a 20px strut, with both font-difference flags set. The existing
2px rounding cutoff suppresses that union. Genuine size/face changes now use
the existing subpixel epsilon, as normal leading already does. Uniform-font
rounding policy stays in its existing path; this is a shared CSS 2.2 §10.8.1
metric decision, not a fixture adjustment.

Three added following-inline guards bring acceptance to 26 cases. They show
lookahead borrowing the preceding text's `hyphenate-character`/`hyphens` state.
The following inline now enters the existing view/font scopes and resolves
stale styles under `ComputeSize` with its flow context restored afterward.
This uses the official resolver and existing ownership scopes
(**D4.5.1v4 /D4.1.4v5**), not another CSS parser or temporary property pointers.
The first failed probe command expected a nonempty establishing-element id;
the saved raw metric trace still isolates the 22px union. No diagnostic logging
remains in production. Recheck native/paged and neighboring categories before
qualifying this broader shared-style/line-union correction.


**Round 22 edit-loop result:** all 26 controls pass at both densities through
retained layout and the causal top sequence 5→25→25→5→25. Browser/native
measurement agrees across all 77 selected boxes within the JSON serialization
bound. The original Jest paragraph and its 866px reduced copy now keep both
links on line two at browser positions (native 188.8px and 243.9px). Jest becomes
a full saved-baseline pass: Markdown improves from 53 full /5 partial to
54 full /4 partial. Native text/layout **155**, paged **491** and all seven
form/text/inline/box/sizing/table/Markdown category checks pass with zero
failures. Raw control PNGs still differ at 4,710 /2,400,000 RGB pixels; the
reduced Jest page has 16,686 differing pixels including header/footer/font
raster details. These raw artifacts are preserved without tolerance/baseline
changes. Aggregate qualification remains pending.


**Round 22 completion:** lookahead admits literal hyphen breaks after their
visible glyph fits and conditional breaks after the generated marker fits.
Following inlines use their current source styles under existing scoped
measurement; genuine mixed-font leading unions are not suppressed by uniform
rounding tolerance. All 26 controls/77 selected boxes match Chromium geometry
within serialization precision, at 1×/2× through retained width/marker changes.
Native text/layout **155**, paged **491**, strict module boundaries, integer-cast
lint, the 421-name inventory and all seven affected category checks pass. Jest
becomes a full baseline pass (Markdown 54 full /4 partial). Raw control/reduced
Jest raster residue stays explicit (4,710 and 16,686 /2,400,000 RGB pixels).

Exact layout: **3,147 full /147 partial /0 failed /0 skipped** (3,294).
Full Radiant: **5,910 full /151 partial /0 failed /1 existing skip** (6,062).
Both actual Make targets exit zero; session 95930 is terminal. CLI SHA-256
`5a456f50…dd9879e`, source/test diff and permanent fixture hashes remain unchanged
through both gates and the final audit. Expected-red and intermediate mixed-font/
marker-owner evidence remain under `round22/`; no baselines or tolerances changed.
The last input qualification is still round 19's 46 failures; no new input/ASan/
full Lambda result is claimed. `round22/qualification-final.json` records scopes.

The long-running goal remains active. Read-only round 23 probes find that the
current Chromium rejects `counter-reset:reversed(...)` in two remaining list
fixtures (`CSS.supports` false, computed reset `none`). Do not force valid
counter behavior to match that browser; native correctness still needs its own
spec/reference evidence. An initial cached native file was stale (2026-10-13),
and a fresh native file is now saved separately. Common Unicode Markdown is
more actionable: initial canonical fixture-font captures retain arrow/Arabic
width discrepancies, but the capture policy must match the saved reference.
Arrow widths are 866.8px native, 815.47px fixture-font Chromium, 809.05px saved;
the matched document-font Chromium width is 809.09375px (round 23 correction),
so this does not establish arrow reference drift. The Arabic code line is
213.8px native, 156.69px current Chromium and 171.84px saved. Both engines already
use system UI/Menlo for the arrow context, so do not assume a missing system-font
alias. Arabic fallback differs (native Courier New, browser Times New Roman).
Trace actual family/handle/advance and shaping paths before selecting a fix.
No round 23 production change occurred during round 22 qualification.

## Round 23: bidi text ranges retain shaped advances

**Priority/outcome:** remove a real width-publication error in mixed-script
code and inline text. The prior round qualified both aggregate gates with zero
failures. Current source/CLI fingerprints and terminal handles are revalidated.
The saved Unicode Markdown Arabic line is measured at 171.812881px, then a
hardware watchpoint catches `bidi_place_visual_line` publishing 213.780457px.
The first named-helper debugger breakpoint was stale; current source breakpoints
and the width watchpoint provide the actual write and stack. No font guess or
fixture-specific offset is needed.

**Owner/design:** CSSOM View §9 defines horizontal text range bounds by text
advance width (https://www.w3.org/TR/cssom-view-1/#dom-range-getclientrects).
Bidi finalization instead advances a second cursor by fallback bitmap ink
widths/bearings. A preserved newline disables fallback advances while keeping
those ink metrics, magnifying the error. Use the existing normalized shaped
advances for visual placement/range union; remove the competing ink cursor.
Visible glyphs beside segment breaks still need their fallback advances; the
existing line-break filter already excludes the breaks themselves. This is
local scratch/geometry work, with style/snapshot owners unchanged
(**D4.5.1v4 /D4.1.4v5**). No library/vendor, font-name substitution or baseline/
tolerance change is planned.

**Finite acceptance:** seventeen advance-range controls cover plain/trailing
LF/CRLF, preceding/following/repeated lines, Arabic/Hebrew, RTL, document fonts,
styled subruns and tab stops, plus primary Latin/space/bold controls. Complete
unwrapped ranges must equal the corresponding max-content advance, independently
of bitmap ink. Verify retained layout at 1×/2× and causal primary/fallback family
changes, original Markdown and browser/native PNGs. CRLF's pre-existing intrinsic
box overcount is separate from its text-range width and remains recorded. The
arrow paragraph's contextual fallback/shaping mismatch is independent backlog.

**Verification budget:** red regression; native text/paged neighbors; affected
text/inline/box/sizing/Markdown categories and document captures. Then build,
lint/inventory/diff before frozen serial exact layout/full Radiant gates.
No API/import/ownership placement changes are planned; strict module validation
is repeated only if that scope changes. No new input/ASan/full Lambda gate for
this geometry-only correction; prior input remains round 19's 46-failure limit.

**Capture-policy correction:** round 22's read-only arrow 'reference drift'
compared a fixture-font capture (815.47px) with a legacy document-font reference.
The matched document-font capture is 809.09375px, agreeing with the saved
809.05px within serialization precision. The canonical extractor now injects
fixture fonts unconditionally, so its output alone cannot establish drift in
legacy document-font references. Both policies and actual glyph/font reports
are saved under `round23/`. Arabic's current fallback face differs from the
saved reference; explicit-family controls isolate the advance-bound defect
without claiming that independent font matching is solved. No round 23 font
engine change has been made.

**Implementation/focused evidence:** removed the parallel fallback-ink cursor,
its side-bearing/bitmap fields and the newline-dependent fallback exception.
The existing fragment advance normalization and hidden trailing-space handling
remain the shared placement model. CSSOM View §9 range bounds now use that
advance union; bitmap glyph bounds stay in paint. The new regression fails
56 assertions before the fix, then passes fresh/retained 1×/2× layout and causal
Courier/Menlo family changes. All 156 native text/geometry tests and 491 paged
tests pass. Affected form/text/inline/box/sizing/table/Markdown categories have
zero failures. `boundary-shaping-009` becomes a full pass (all 18 elements,
16 text ranges and eight spans); several Arabic breaking/spacing cases improve.
Unicode Markdown retains all 129 element matches and improves from 143/145 to
144/145 text matches: its Arabic code range is 171.8px, previously 213.8px,
against the saved 171.84px. The arrow range remains independently partial.

Document-font browser/native captures under `temp/layout-analysis/round23/`
cover all seventeen controls plus the styled subrun and the full original page.
The reduced direct text ranges differ by at most 0.022px in x and 0.069px in
width after one-decimal native serialization. The control PNGs differ at
8,737/2,400,000 pixels; the original page at 384,525/6,526,800 pixels. These are
measured comparisons, not raster-equivalence claims: Arabic joining/paint and
contextual arrow font fallback remain open. CRLF range width is repaired, while
its intrinsic max-content box still overcounts one space. No fonts, references,
baselines, tolerances or vendor sources were changed.

**Evidence limits:** `native-red.log` was captured before replacing the fixture's
literal CRLF (normalized while copying) with explicit `&#13;&#10;`; its named
CRLF row therefore contained LF in that red run. Final native/browser captures
confirm actual CRLF DOM text, and the final regression checks its range width.
No claim is made that its separate intrinsic box width passed before or after.

**Aggregate qualification/completion:** terminal session 14651 exits 0 after the
serial exact `make test-layout-baseline` and `make test-radiant-baseline` gates.
Layout: **3,148 full /146 partial /0 failed /0 skipped** (3,294 total). Full
Radiant: **5,912 full /150 partial /0 failed /one existing skip** (6,063 total).
The new native test accounts for the extra total; `boundary-shaping-009` moves
one partial to full. CLI SHA-256 is
`8cda17c9c830d088ca1610743808cdca8e9baa14a894e19edcf9230469f7ccf7`.
Production/test diff and all untracked fixture fingerprints stay identical
through both gates. Build-test, the Radiant integer-cast lint, 421-name inventory
validation and diff checks pass. No API/import/ownership change required a new
module-boundary run; round 22's result is historical, not transferred. Input's
round 19 46-failure limit likewise remains historical; no input/ASan/full Lambda
rerun or qualification is claimed for this local geometry repair.

The bounded advance-range milestone is complete; the long-running goal remains
active. Next diagnosis is saved in `round23/diagnosis-pending.json`. Installed
font fallback may vary across user agents under
[CSS Fonts 4 §5.2](https://www.w3.org/TR/css-fonts-4/#font-style-matching), so the
remaining arrow discrepancy needs a proved engine defect before a font change.
[Blink's fallback iterator](https://chromium.googlesource.com/chromium/src/+/refs/heads/main/third_party/blink/renderer/platform/fonts/font_fallback_iterator.cc)
and its shaping overview describe run-level fallback iteration; that is
consistent with the joined/isolated glyph observations, not a qualified native
fix. A separate intrinsic-line helper splits preserved text only at LF and
leaves preceding CR in the measured slice; confirm normalization ownership
before repairing the CRLF box overcount. Commander’s remaining two elements
are a remote badge, so resource availability must be separated from layout.
No production edit followed the frozen qualification build.

## Round 24: HTML newline preprocessing and DOM carriage-return spacing

**Priority/outcome:** repair the shared source/DOM whitespace boundary exposed
by partial `control-chars-00D`, which grows a normal document by 450px, and by
Windows line endings in max-content code. Round 23's source/CLI/fixture hashes
still match, and process inspection confirms no aggregate is live. This clear
spec defect takes priority over the arrow's permitted installed-font variation.

**Owner/evidence:** HTML §13.2.3.5 normalizes literal source CR and CRLF to LF
before tokenization; character-reference CR remains CR in the DOM. The HTML5
UTF-8 iterator has normalization, but the actual byte-consumption/scanning path
bypasses it. CSS Text 3 §4 treats surviving CR identically to U+0020, rather than
as a segment break. Actual layout, bidi, paragraph collection and paint still
classify it as LF; intrinsic measurement mostly already counts it as a space.
A six-mode Ahem matrix under `round24/` separates literal HTML, escaped CR,
space, LF, CRLF and whitespace-only controls. Parser/source storage ownership
stays under **D4.2.6**; style/snapshot ownership stays under
**D4.5.1v4 / D4.1.4v5**. No DOM payload rewrite is allowed for CSS processing.

**Finite acceptance:** normalize literal newlines in DATA/RCDATA/RAWTEXT/script,
CDATA/plaintext, attributes and comments while preserving numeric-reference CR
and ordinary LF. Keep original DOM byte ranges after CSS processing. Across six
white-space modes, CR must match an equal U+0020 control in layout/intrinsic
sizes, wrapping, mixed bidi/styled spans and paint. Verify source-vs-reference
parser results, fresh/retained 1×/2× geometry and causal white-space/width edits.
Correct the older round 23 explicit-CRLF expectation if required by those
semantics, using a real space+LF control rather than removing coverage.

**Browser/spec limit:** current Chromium ignores DOM CR under pre/pre-wrap/
break-spaces rather than treating it as a space; `control-chars-00D` therefore
fails its own space reference in Chromium. Do not implement that discrepancy.
Round 23's 'intrinsic CRLF overcount' hypothesis was premature: its explicit
reference CR must first be evaluated as a CSS space. Keep the matched space
control and parsed DOM content as the acceptance authority. Sources:
https://www.w3.org/TR/css-text-3/#white-space-processing and
https://html.spec.whatwg.org/multipage/parsing.html#preprocessing-the-input-stream.

**Verification budget:** red parser/native controls, focused HTML/text/paged and
paint checks, affected layout categories, then frozen serial input/layout/full
Radiant gates. The input gate has a recorded round 19 46-failure limit; compare
actual failure identities rather than transferring that result. Changes to a
shared header require module-boundary validation; no new allocation or rooting
boundary is planned, so ASan is added only if that changes. No baseline,
tolerance, font-name or vendor edits.

**Qualification finding/scope update:** the focused category classification has
one failure, `control-chars-00D` text 38.1% →33.3%; earlier commentary reported
zero failures from an incomplete log tail. Both native sections now agree with
each other, but current/saved Chromium drops preserved CR. The space reference
also exposes a genuine shared wrap error: `layout_inline` grants a break after
an unbreakable inline solely from its parent's wrap mode. CSS Text §5.5 assigns
a disappearing space's opportunity to that space's own box. A pre/nowrap
separator must not acquire a break from its normal ancestor; a nested normal
separator may. Add these six bounded controls and repair this owning decision
before aggregate qualification. This is not permission to imitate Chromium's
CR omission or to change a baseline threshold.

**Reference-oracle correction:** the corpus already has explicit `.standards`
references with a specification, reason, fixture SHA-256, browser identity and
strict comparison; raw browser captures remain separate. Extend that existing
control format with validated scalar `text_codepoints` substitutions, shared
between capture and loading, instead of changing any engine result, tolerance or
baseline floor. The CR→SPACE control follows CSS Text §4 and the pinned WPT match
`css/css-text/white-space/reference/control-chars-00D-ref.html`. It retains the
element/style tree and records the original fixture hash. Invalid scalars,
duplicates, empty mappings, stale source and missing provenance are rejected.

The temporary candidate is generated from the actual original fixture by
Chromium, with fixture fonts matched in native capture. Ordinary comparison
scores are 13/15 elements, 18/25 text (72%) and 8/10 spans; remaining G/H wrapping
is separately visible. The original raw Chromium capture remains unchanged.
Installing this independently derived standards reference restores a meaningful
oracle without changing baseline acceptance thresholds or hiding the remaining
layout differences. This updates the initial no-reference-change plan because
the observed failure belongs to the browser oracle, not the CSS-correct engine.
The sibling `lambda-test/layout` harness/control changes are part of this
qualification and must be fingerprinted alongside the main engine worktree.

**Final consumer finding:** the 75-control browser space-reference comparison
isolates two 20px max-content errors: pre-line space/CR immediately before LF.
Actual layout already trims these separators, but intrinsic line slices retain
them. Add a cross-owner range/max-content assertion and trim the collapsible
line suffix in the shared forced-line measurement wrapper (CSS Text §4.1.1).
PNG/native-2×/SVG/PDF CR-versus-space controls were pixel-identical before this
additional correction; repeat affected captures on the final binary. Initial
edge regression y assertions used line origins instead of text-cell origins;
corrected to the measured first text cell. The atomic control expectation is
confirmed from Chromium (x=0), not inferred from its width.

**Stable implementation/qualification entry:** parser (69), native text/geometry
(158) and paged (491) suites pass after the wrap-owner repair. The source-owner
lookup handles actual DOM edges, avoiding a stale last-rendered text node across
atomic or collapsed descendants. Source CR bytes survive CSS layout. The final
pre-line regression is red at exactly 20px (60px intrinsic versus 40px laid-out
line), then green after shared suffix trimming. The affected text category is
888 full /92 partial /0 failures with the standards reference installed; other
affected categories retain their prior zero-failure results.

The frozen qualification now includes the sibling harness, new shared control
validator, raw and standards reference files, fixture source and unchanged
`wpt-css-text/baseline.txt` in addition to main production/tests and fixture
hashes. Run the source/control contracts, strict module boundary, build,
integer-cast lint and inventory before serial input, exact layout and full
Radiant checks. Input remains an independent required result; compare it with
round 19 by actual failure identities and counts, and do not abort layout
qualification merely for that recorded unrelated debt.

**Qualification restart for a consumer defect:** the first qualification was
stopped during input, before either layout aggregate, after final pixels exposed
an extra pre-line blank line at the corrected intrinsic width. Its confirmed
process group 68257 was terminated; session 51606 returned 143, and process
inspection found no survivors. `qualification-attempt1/` retains the interrupted
state/log and explicit abort reason; no input/aggregate result is claimed from it.

A new height regression fails at 72px versus the browser's 48px. The existing
whitespace-before-forced-break probe is generalized to the remaining source
slice and shared inline-edge lookup. Collapsible separators before preserved LF
now defer fitting until that forced break trims them, preventing a soft break
followed by an extra forced break. No source bytes or preserved spaces are
removed. All 158 native and 491 paged tests pass after this fix. The final 75
control boxes match the browser's space-equivalent reference in x/y/height,
with maximum width delta 0.041px from serialization. Native CR/SPACE PNG 1×,
PNG 2×, SVG raster and direct Poppler PDF pairs each differ at zero pixels;
raw browser-space comparison still differs at 12,270/4,800,000 padded pixels,
so no full browser raster-equivalence claim is made. Restart the frozen gates
only after this final consumer repair.


**Vector qualification finding:** the first complete Radiant aggregate reports
one failing export-slice assertion: direct DOM `A\r\nB` under `pre-wrap`
expected `A` instead of `A `. HTML source preprocessing does not apply to this
unit's direct DOM input; CSS Text 3 §4 requires CR to have SPACE semantics.
Correct this expectation, add matched SPACE/LF and direct preserved-CR controls,
and retain slice byte-offset assertions. All 169 vector cases pass afterward.
The unchanged CLI passes the exact layout gate again (3148 full /146 partial,
zero failures); the full Radiant recheck also passes (5914 full /150 partial,
zero failures /one existing skip; 6065 total). Session 19148 is terminal exit 0.
The separate input run is 4388 total /4342 passed /46 failed, with exactly the
same failing identities as round 19 (no added/removed failures). Both aggregates
and all final CR/SPACE renders used CLI SHA-256 `2a10fa081ea5dbbae4f59174557c21592226f4ec71512bafd2546d6dc33a04c8`.
`temp/layout-analysis/round24/qualification-final.json` preserves the initial
failed aggregate, corrected recheck and independent input result. No ASan or
full Lambda qualification is claimed; the long-running partial-pass goal remains active.

## Round 25 — nowrap separators join the surrounding inline word

**Scope/evidence:** round 24's read-only 16-case browser/native reduction shows
`G<span style="white-space:nowrap">     </span>H` split across lines at 80px,
where NBSP and whole-nowrap controls stay together. At 100px all controls fit;
normal-space controls remain breakable. `pre` SPACE exhibits the same error.
The original `control-chars-00D` WPT retains this G/H difference against its
normative SPACE reference. Work begins after round 24's aggregate handle ends.

**Authority/design:** CSS Text 3 §4.1.1 collapses the source separator before
fitting; §5.5 assigns its break permission to its own box and forbids breaks
created solely by inline boundaries. The lookahead's ASCII-run predicate rejects
all whitespace before consulting that permission, and its text probe separately
returns NOT_FILLED at every ASCII space. Repair those shared decisions, retaining
preserved LF as a forced boundary. A measured continuation must count one
collapsed separator with the source font/spacing rather than all raw bytes or
the ancestor font. Reuse existing whitespace/advance helpers; do not replace
lookahead with min/max equality or introduce a fixture-specific boundary.
Style/view probes remain observational (**D4.5.1v4 /D4.1.4v5**).

**Acceptance/verification budget:** a permanent Ahem fixture and native geometry
regression compare nowrap SPACE (one/five), collapsed TAB/LF, `pre` SPACE,
whole-nowrap and NBSP controls at a fitting and constrained width. Add a nested
normal override and a styled separator that disprove treating the whole inline
as atomic. Check fresh/retained 1×/2× layout plus causal width/white-space edits.
Capture browser geometry/PNG and Radiant PNG under `temp/layout-analysis/round25`;
run the original WPT comparison and affected text/inline categories. After a
stable implementation run native text/paged coverage, required integer-cast
lint, exact layout and full Radiant gates on one frozen binary. No source parser,
reference, threshold, baseline-floor or ownership change is planned. Preserved
TAB stops and arbitrary Unicode lookahead are not the selected milestone.


**Red evidence and scope refinement:** the permanent fixture contains 27 cases.
Its native regression reaches 88 failing geometry assertions before the fix;
the first fixture-load failure was a missing harness `result` ID and is not
behavioral evidence. Chromium confirms the expected head/tail positions. Explicit
head/tail spans expose the same boundary error with NBSP; extend the existing
conservative run classifier to shared GL/WJ/ZWJ information (plus the atomic
NBSP exception), preserving break-all/anywhere and forced-LF limits. No new
Unicode range table is introduced. The classifier and text probe honor source
wrap permission; the latter carries its measured, collapsed advance across
siblings. Preserved TAB measurement is moved from actual layout into one shared
helper, rather than approximated in lookahead. Arbitrary Unicode/preserved-TAB
cross-node shortcut expansion remains outside this milestone.

The source-font challenge identifies a separate shared aggregation defect:
`intrinsic_collapsed_space_width` uses the parent font when replacing an inline
space-only contribution. Resolve the actual leading text owner through the
existing inline-edge query and measure with that already resolved owner's font
under `LayoutFontScope`. Use it both when adding a surviving separator and when
subtracting a duplicate leading separator. A smaller-font and negative
word-spacing control each fit exactly at 90px. At 1×, all 108 row/following/head/
tail boxes across 27 cases match Chromium in x/y/width/height without tolerance
changes.

**Density consumer finding:** two remaining 2× assertions show a 49px instead of
48px block. LLDB finds a 19px second-line ascent while the block strut stays
24px: the negative-word-spacing source selects Times New Roman instead of its
inherited Ahem. Switching the intrinsic active element/view together does not
fix it; that candidate is removed. A hardware watchpoint finds the assignment
in `DomElement::ensure_font`: after a retained reset, `lycon->font.style` and the
source's font group are the same cleared object. Testing that object for an
inherited family falls through to the initial Times font even though its DOM
parent still owns Ahem. Exclude this self-alias and obtain the parent's family
before falling back to the initial font. Add a source/parent family assertion
alongside unchanged two-density geometry tolerances. This retains style owners
under **D4.5.1v4 /D4.1.4v5** without changing either ruling or the intrinsic scope
header. Temporary logger/debugger probes are removed from source before the
final build. The focused two-density regression remains a qualification gate.


**Direct-sibling acceptance:** the original WPT remains at 72% after the first
27 grouped controls pass because `span_has_line_filled` replaces a standalone
nowrap space span with its zero trimmed intrinsic width. Two direct-sibling
controls expose eight red assertions. Propagate the recursive source probe's
measured advance through inline boxes, retaining existing source font/style
scopes and shared decoration-edge resolution. `LineFillProbe` carries collapsing
state across owners so adjacent nowrap space spans contribute one separator.
The final fixture has 30 cases; a NBSP control checks the two-owner tail x as
well as line membership. Preserve forced LF, nested normal overrides and
source-owned spacing. The original WPT then reaches 100% elements/spans/text
with its existing standards reference and unchanged strict comparison. Broad
qualification follows final native/paged coverage; no new reference/control,
threshold or baseline-floor changes are made in round 25.


**Round 25 qualification:** all 159 native text and 491 paged cases pass. The
exact layout gate is 3149 full /145 partial /zero failures; full Radiant is
5916 full /149 partial /zero failures /one existing skip (6066 total). Session
41189 is terminal exit 0. Strict original `control-chars-00D` also passes 100%
elements/spans/text after both aggregates on the same frozen CLI. Thirty
controls compare 117 boxes against Chromium with zero x/y/width/height deltas;
fresh/retained 1×/2× source-font and causal-edit assertions pass. All captures
and gates use CLI SHA-256 `cc24b90961e3bf9163d989220bc5d52a6b27e65f5b39eeb5066b73702ed63a38`.

The module boundary, reference contracts, build, integer-cast lint, inventory
(421) and both repository diff checks pass. There are no round 25 reference,
threshold or baseline changes. Raster comparisons retain 5896/3120000 changed
pixels at 1× and 11536/12480000 at 2×; the inspected glyph ink bounds agree but
edge antialias shades differ (e.g. 196 versus 202), so pixel equivalence is not
claimed. `temp/layout-analysis/round25/qualification-final.json` and the
before/fixed/Chromium image comparison preserve the evidence. No input, ASan
or full Lambda result is claimed for this Radiant-only milestone; round 24's
input limit remains separate. The long-running goal remains active.

## Round 26 — textarea placeholder reference collisions

**Read-only diagnosis while round 25 is frozen:** all three
`form/the-textarea-element/multiline-placeholder{,-cr,-crlf}.html` fixtures
contain three `textarea rows=5` controls. Their legacy basename browser
references instead contain three `input` controls and a 37px html box. The
same basenames under `data/baseline/` are the actual input fixtures. This is a
confirmed source/reference collision, not evidence that textarea layout must
imitate input geometry. `reference_paths.js` and both capture/loading paths
already support nested category-relative keys; the textarea-qualified files
are missing. Keep the valid input references and all acceptance thresholds.
`round25/next-form-reference-diagnosis.json` records source/reference hashes and
control trees; `next-form-collision-audit.json` bounds the three affected form
cases. Other form partials have no matching baseline-source basename.

**Finite acceptance/design:** after round 25's aggregate handle is terminal,
capture the actual three textarea documents through the existing extractor,
with matched fixture fonts, into project temp first. Assert the resulting
reference control tags, preserve input-reference hashes, and compare Radiant
geometry plus browser/native images. Install only independently captured
qualified references through the existing resolver. The missing local
`support/placeholder.css` contains only the pinned WPT's placeholder color and
opacity reset; resolve/import the unchanged support asset if needed for faithful
paint capture. Do not alter source markup, reference geometry, comparison rules,
skip lists or baseline floors.

The textarea paint loop currently splits logical lines only at LF. Fresh
LF/CR/CRLF images must establish whether referenced/dynamic CR needs the HTML
placeholder display normalization independently of CSS Text's ordinary DOM-CR
SPACE rule. Source attributes must remain unchanged. If a real paint defect is
confirmed, find/reuse the shared text-control normalization/line iterator before
adding code, and define a small causal native raster regression. Use HTML's
`attr-textarea-placeholder` and input-placeholder rules as the authority; do not
apply a textarea rule to ordinary DOM text.

**Verification budget:** capture/reference contracts and focused form
comparisons first; any production repair gets native control/vector coverage,
1×/2× retained checks, required lint, exact layout and full Radiant gates on a
frozen binary. A reference-only correction needs its source/reference checks,
affected category and exact layout gate; earlier full-Radiant evidence remains
explicitly tied to its original configuration. No approval checkpoint is
inferred for this already-authorized layout task.


**Reference correction scope:** fresh independently captured qualified references
contain exactly three textarea controls and compare at 100% elements/spans/text
for all three fixtures on the unchanged round 25 CLI. The original pinned WPT
placeholder color/opacity stylesheet is restored without modification. Install
six full/compact Darwin references through the existing directory-qualified
resolver; source markup, the input references and all baseline floors remain
unchanged. Binding checks canonicalize the corpus symlink and prove the input
bindings retain their prior paths/hashes. The form category is 368 full /10
partial /zero failures (378 total). The exact layout gate passes at 3152 full
/142 partial /zero failures /zero skips (3294 total); session 63428 is terminal
exit 0. Engine SHA-256 remains `cc24b90961e3bf9163d989220bc5d52a6b27e65f5b39eeb5066b73702ed63a38`.
`temp/layout-analysis/round26/qualification-final.json` records the source/reference
fingerprints, source-unchanged engine/test diff, bindings and gate scope.

The font-matched image comparison also exposes existing textarea paint issues:
CR-only placeholders join logical lines, and LF/CRLF placeholder line spacing
differs from Chromium. These are visible in `round26/placeholder-comparison.png`;
geometry-only full matches do not establish paint equivalence. No engine change
is made in this reference-identity milestone. Keep those consumer defects as a
separate bounded paint investigation rather than modifying reference geometry
or stretching this source/reference correction into all text-control editing.
The round 25 full Radiant result remains tied to its pre-correction reference
configuration; this reference-only milestone requires fresh form/exact-layout
checks and does not claim a repeated full Radiant/input/ASan gate.


## Round 27 — source-owned fonts in inline lookahead

**Priority/design:** fix the common README case where an inline code word and
its attached period fit the current line but move together to the next line.
`markdown/md_zod-readme.html` exposes this in its nullish-schema paragraph.
The line-fill probe exits the code span while retaining its 13.6px monospace
font, then measures the parent's period (16px proportional) in that stale font.
A reduced Ahem control confirms the causal direction: the 20px child probes a
10px parent period as 20px and rejects an exactly fitting line. The selected
repair scopes each text probe to its actual owner's computed font, preserving
the active first-line font and restoring the caller through `LayoutFontScope`.
No boundary, width, offset or fixture-specific exception is added; selected
style ownership remains **D4.5.1v4 /D4.1.4v5**. CSS Text 3 §5.5 and §7 govern
cross-inline opportunities and source spacing.

**Finite acceptance/verification:** ten fitting/overflowing controls cover
direct/nested exits, inline padding, word spacing and the reverse size relation.
The original native regression has red assertions before the production edit;
then fresh/retained 1×/2× cases and causal width/font changes pass. All 160 native
text and 491 paged tests pass. Twenty row/head boxes match Chromium with zero
x/y/width/height deltas. Zod's original text score increases from 99.758% to
100%; its remaining sponsor-image sizing difference is independent. Required
lint, exact layout and full Radiant gates follow on the frozen configuration.
A new letter-spacing boundary control exposed the existing terminal-spacing
fit policy versus Chromium; its source and diagnosis remain in round27 temp.
CSS Text 3 §7.2 removes line-edge spacing, so that policy is not changed to
imitate this browser difference. The permanent spacing controls use word
spacing to isolate the selected source-font defect.

**Independent capture correction:** the Commander broken remote badge is
93.078px under the checked-in remote-blocking policy, rather than the legacy
116.5px loaded badge. The Zod CodeRabbit local SVG is intrinsically 206×33 and
uses 499.391×80, rather than the legacy 218.3×80. Current Chromium and native
captures agree on both facts. Two independent full captures have identical
compact comparison trees for each source. Install the normal full/compact
Darwin references for both pages, including Zod's still-partial table; retain
all default/Linux reference hashes, fixture sources and baseline floors.
No comparator, capture policy or threshold changes are made in this round.
`round27/reference-repeatability.json` and `reference-install-plan.json` record
provenance; the original full captures remain in temp.

**Separate remaining work:** the Numeric sponsor PNG is 22620×6640 and exceeds
the configured image limits. Do not raise limits or substitute a fixture size;
first determine whether bounded metadata/decode ownership can preserve
intrinsic dimensions without admitting the oversized allocation. Markdown emoji
and arrow-symbol partials also remain independent font-fallback measurements.
The long-running goal remains active after this finite repair's qualification.


**Round 27 qualification/completion:** session 20046 is terminal exit 0. The
exact layout gate is 3153 full /141 partial /zero failures /zero skips (3294
total). Full Radiant is 5921 full /145 partial /zero failures /one existing
skip (6067 total), including 160 native text and 491 paged cases. Module
boundary, reference contracts, build, integer-cast lint, inventory and both
repository diff checks pass. CLI SHA-256 is
`d7ec01fe58aed99da215cd3d6d4a1c6d73a88db5bbf40ac4df70d52a365408b4`.
`temp/layout-analysis/round27/qualification-final.json` binds the engine,
fixture and reference hashes to these results. The new regression has 40
failing assertions before the repair. Ten final controls compare 20 boxes
with zero geometry deltas; raster comparisons retain 1670/2400000 differing
pixels at 1× and 3284/9600000 at 2×, so raster equality is not claimed.
The markdown category is 55 full /3 partial /zero failures. No input, ASan or
full Lambda result is transferred to this binary. The long-running goal stays
active; fresh emoji/arrow font captures start only after both aggregate handles
are terminal.


## Round 28 — system color-font tracking

**Priority/root evidence:** fresh Chromium repeats the legacy emoji line
memberships both with and without fixture fonts. Radiant measures the same
16px system color glyph as 21px, while Chromium uses 20px. A standalone
CoreText probe proves automatic unscaled tracking contributes that extra pixel;
a zero-tracking font copy matches browser captures at 12/13/15/16/17/20px,
with unchanged 32/64px metrics. Raw hmtx would give 16px at 16px, so replacing
platform optical metrics with table-only advances would be incorrect.
`temp/layout-analysis/round28/` retains browser glyph ranges/platform fonts,
the platform probe, reduced native geometry and images. Skia's primary
`SkCTFontCreateExactCopy.cpp` similarly disables unscaled tracking during font
construction. No fixture width/advance compensation is proposed.

**Finite design/acceptance:** normalize automatic tracking when installing a
system color-font face into the platform backend, retaining the same face for
measurement/rendering. Share the descriptor-copy shape before adding its third
variant. Preserve non-color system UI optical/weight state and exact document
font data. Returned CoreText references remain explicitly owned; retain the
selected face before releasing old aliases (**D4.5.1v4 /D4.1.4v5** ownership).
Fourteen native controls cover eight sizes, concrete/fallback families, an
extended emoji cluster, CSS letter spacing, a width that fits two emoji, and an
ASCII control, with fresh/retained 1×/2× and causal size edits. The original
`md_test-emoji` paragraph must match browser line memberships. The independent
arrow-family discrepancy stays separate unless the evidence connects it.

**Verification budget:** confirm new font/context and native text regressions
red before production edits; focused green, font/text/paged neighbors and
browser/native comparisons first. Then build all required runners and run
`make test-library`, integer-cast lint, exact layout and full Radiant on a
frozen source/binary/reference configuration. No reference, threshold or
baseline-floor changes are planned for this repair.


**Retained-font prerequisite:** the color-metric repair makes the unchanged
original emoji fixture pass 100% elements/spans/text and the platform regression
passes. Four retained mutation checks still fail. Additional computed-value
assertions prove the parent becomes 20px while the unstyled child stays 16px.
The implicit font-size inheritance path treats any positive retained computed
size as if it were an own declaration. Own cascade/shorthand and HTML UA size
rules were already checked above that guard. Remove only that stale-cache guard
so the current parent computed size is inherited on each resolution (CSS
Cascade 5 §7.2); preserve existing own-size and UA-default handling. Add ASCII
inherited-size and fixed author-size controls to the same causal mutation.
This shared CSS consumer repair expands qualification to the full input suite,
with its previously recorded failures compared by identity; no failure masking
or baseline/reference edits are allowed. The font-context runner's initial
link failure and stale zero-selected invocation are diagnostic setup errors,
not red behavioral evidence. Valid red logs have 12 platform and 52 native
assertions before the tracking fix; the retained prerequisite has independent
computed-size red assertions after it.


**UA-default neighboring coverage:** inherited-size refresh correctly preserves
own CSS sizes, but the existing terminal-decoration test exposes an `rt`
annotation growing from its UA half-size to its parent's size. Replace the
resolver's duplicated heading/small-text preservation list with the existing
`HtmlUaElementDefault` scale/medium-lineage metadata, which also covers ruby.
A valid legacy HTML font-size hint remains local through the existing parser,
now promoted to the module header; missing/invalid hints inherit normally.
Keep native red evidence for this prerequisite and add retained ruby/fixed-size
controls. This shares the UA rules instead of adding font-size exceptions for
individual failing fixtures.


**Final focused evidence and limits:** all 161 native text and 492 paged tests
pass after the UA preservation repair. The markdown category is 57 full /one
partial /zero failures; the original emoji page is strict 100% elements/spans/
text with unchanged references. The Unicode page also scores 100% under its
unchanged comparator, but the arrow string remains 864.8px versus Chromium's
809.05px. Two repaired color-arrow advances reduced the prior 866.8px width by
2px, crossing the existing comparison tolerance; do not claim resolved Unicode
fallback fidelity. Preserve this remaining 55.75px discrepancy for diagnosis.
All 35 compared control boxes agree horizontally within 0.038px and have equal
widths within 0.035px. The ruby-containing row retains a 12px block-axis
geometry difference; downstream y positions inherit that offset. Correct ruby
font-size preservation does not establish complete ruby line-box layout. Keep
these raw images/deltas and do not shift output or change tolerances to hide it.

**Qualification completed:** round28 session 36880 terminated successfully on
CLI `f29d51c0a5c72d6d9836f9d39d7e2e11db7344f90c15c23066dc6d26d1b88b12`.
Exact `make test-layout-baseline`: 3155 full /139 partial /zero failures /zero
skips. Full Radiant: 5925 full /143 partial /zero failures /one existing skip.
All 1659 library checks pass. Full input reports the same 46 failures as round19,
with no added or removed failure identities; it is not a passing gate. The
driver verifies unchanged source/reference fingerprints and CLI across all
aggregates. `temp/layout-analysis/round28/qualification-final.json` retains
the terminal stage results and visual limits. No references, floors, comparator
tolerances or capture policies changed in this round. The long-running partial
repair goal remains active.

## Round 29 — preserve joined color clusters through paint

**Priority/evidence:** the original emoji comparison exposes a shared paint
defect: layout measures the service-dog cluster as 20px, but ordinary/effect
painting walks its Unicode scalars and paints separate dog and vest glyphs.
The same defect appears before round28. A direct CoreText probe confirms that
service-dog, family, skin-tone profession, flag, keycap and VS16 sequences shape
to one glyph in the exact selected face; an unsupported composition shapes to
two glyphs and 40px. Preserve both outcomes, rather than forcing a cluster to
one em. Browser controls at 1×/2× agree in CSS geometry. Raw probes, captures and
the finite milestone plan are in `temp/layout-analysis/round29/`.

**Design/acceptance:** expose selected-face glyph IDs and positions from the
existing CoreText shaping-line factory, preflighting every run before visiting
glyphs. Reuse the raster kernel with font-local glyph IDs, with an explicit
cache-key namespace separate from Unicode scalars. Keep bitmap buffers in the
existing font-cache lifetime, since display lists borrow them
(**D4.5.1v4 /D4.1.4v5**). Share cluster/face selection across layout and paint.
Cover ordinary/shadow/effect ink, following ASCII with CSS letter spacing,
supported and unsupported compositions, two densities and retained repaint/cache
reset. CSS Fonts 4 §5.3 governs cluster matching; CSS Text 3 §7.2 applies spacing
to typographic units. SVG source export is a neighbor; general contextual-script
painting is a separate milestone.

**Verification budget:** require a red existing-API paint regression before
production edits, then focused font/cache/paint checks, native text/paged/vector
neighbors and raw browser/native images. Qualify stable changes with library,
module boundary, lint/inventory, exact layout and full Radiant. Repeat full
input only for relevant parser/cascade/input changes or new failure evidence.
No reference, tolerance, capture-policy or baseline-floor changes are planned.

**Root repair/focused evidence:** the selected-face shaping visitor preflights
CoreText runs, and indexed glyph rasterization shares the existing kernel/cache
with an explicit namespace. A shared color-cluster classifier and paint visitor
now feed ordinary text, native shadow collection, overflow fitting and effect
raster lowering. The initial existing-API regression reports 52 assertions;
its repaired glyph-count/advance checks pass at both densities and after cache
reset. A six-assertion independent pixel regression exposes a prerequisite:
the coverage sampler omitted BGRA alpha, and sharp shadow replay inferred color
mode instead of retaining mask mode. Both are repaired in their shared owners.
All 172 vector, 161 native text and 492 paged neighbors pass on the first
integrated build. Subsequent retained-selection controls and aggregate gates
are still pending; these counts are not final qualification.

**Visual scope/limits:** all 72 compared row/inline boxes in the 36-row control
have equal x/y/height and widths within 0.029px of Chromium. Raw 1×/2× pixels
remain different; no raster-equality claim is made. The before/browser/after
panel preserves raw coordinates. Ordinary supported clusters now show the
joined glyph, while the unsupported composition keeps both glyphs. General
contextual-script paint and complete SVG color/shadow export remain separate.

**Build-boundary prerequisite:** the first aggregate preparation stops at two
lightweight runner links: shared `text_metrics.cpp` reaches grapheme segmentation
in streaming `layout_text.cpp`, and the standalone display-list runner omits the
new classifier's owner. Move the existing grapheme helper and its BA classifier
into `text_metrics.cpp`, promoting the latter through `layout.hpp`, and declare
that shared source in the display-list runner's JSON build configuration.
Regenerate through `make`; no duplicate helper or test stub is introduced.
The preparation failure is not an aggregate test result. Rebuild the affected
consumers and requalify the resulting binary before recording completion.

**Qualification completed:** session 17791 terminates successfully on CLI
`9b904d5ebcc4eb3b8118429821aa64b85285d4ac49ed02a244d593264a65dc2e`.
Exact layout: 3155 full /139 partial /zero failures /zero skips. Full Radiant:
5928 full /143 partial /zero failures /one existing skip. All 1659 library
checks pass. The final native neighbors are 172 vector, 161 text-query,
492 paged, 86 display-list and 25 state-store checks. The retained paint matrix
also preserves its recorded 16px selection after the active font changes to
32px. Fresh final captures are byte-identical at both densities after the
shared-module move; all 72 control boxes retain the geometry agreement above.
Unshadowed SVG export retains complete source clusters. The original Markdown
emoji comparison remains strict 100% elements/spans/text. Source, build-config,
fixture/reference fingerprints and CLI remain unchanged throughout the gates.
`temp/layout-analysis/round29/qualification-final.json` records the terminal
results and limits. Input is not repeated in this font/layout/paint round;
round28's 46 unchanged failures remain prior evidence, not a result on this CLI.
No reference, tolerance, floor or capture-policy changes occur. The long-running
goal remains active.

**Next read-only evidence:** `round30/next-canonical-shaping-diagnosis.json`
narrows the remaining arrow width difference. Chromium renders precomposed
U+219A and its decomposed arrow/overlay using two primary SFNS glyphs at
14.1875px. Whole-run CoreText uses Menlo at 9.6328125px for U+219A, while its
decomposed form uses SFNS at 14.1796875px. This establishes a normalization/
fallback difference, not a per-symbol width adjustment. CSS Fonts 4 §5.3–§5.4
must guide any selected-font canonical fallback; preserve DOM bytes, explicit
fonts' supported composite glyphs and source-owned range indices. Do not
normalize the shared CTLine factory without an index map. Known atomic-inline
and reversed-counter browser/spec disagreements stay separate; no engine change
is justified by their scores alone.

Fresh native source-pair captures after qualification also expose an independent
paint cause: decomposed arrow/overlay and not-equal controls keep their shaped
widths but lose the visible combining overlay. Ordinary paint skips
`text_codepoint_has_zero_advance`, which includes visible combining marks.
Prioritize a finite base/mark paint milestone using the selected-face glyph API
before the separate canonical fallback/index-map problem. The round30 plan and
raw images record this distinction; no round30 engine changes are made here.


## Round 30 — preserve ordinary combining-cluster ink

**Priority and red evidence:** native paint drops visible combining marks even
when layout measures the full cluster. The ordinary loop equates zero advance
with no ink, while the effect scalar loop loses shaping substitutions. This
breaks decomposed names, stacked accents, Kana and symbol overlays. The existing
paint API produces 96 failing assertions before the repair. Browser platform
font/glyph probes, native captures and the finite plan are retained under
`temp/layout-analysis/round30/`. Precomposed arrow fallback is independently
narrowed by the previous round's diagnosis; it is not a width-table repair.

**Design and acceptance:** generalize the shared emoji visitor to ordinary
extended graphemes, retaining selected-face glyph IDs, positions and actual
advance. Reuse the existing indexed raster/cache and native/shadow/effect emitters.
A typographic unit receives one letter-spacing increment (CSS Fonts 4 §5.3;
CSS Text 3 §7.2). Reuse the existing transform segment builder with original word
context; case expansion is preflighted unit by unit before ink emission. Keep
DOM bytes and source-owned ranges unchanged (CSS Text 3 §2.1;
**D4.5.1v4 /D4.1.4v5**). Contextual scripts need their full run and remain on the
existing path. Canonical scalar fallback and SVG outline cluster placement stay
separate, with no claim of complete multilingual shaping.

**Fallback prerequisite:** the first generalized visitor leaves 12 Kana checks
red. A captured Arial handle with a missing style family is re-resolved as the
UA Times face before fallback selection. Its base and full cluster choose
different fallback faces, so exact-face preflight correctly rejects the result.
Promote a source-aware wrapper over the existing font fallback resolver and use
the captured handle as its origin. Do not alter the control's style to hide the
ownership defect. General grapheme segmentation checks its ordinary boundary
before the BA exception, avoiding the BA scan for a normal ASCII boundary while
retaining the same break decision; no debug performance claim is made.

**Focused verification:** all 10 paint tests pass, including the previous color
controls, cache reset, 1×/2× density and captured-font replacement. All 172 vector,
162 native text-query and 492 paged neighbors pass. The new native ink test
covers 42 composed/decomposed pairs, shadows, spacing, uppercase/lowercase,
capitalize and case expansion through fresh/retained repaint and a causal font
size edit. Its first whole-column crop included the next column's negative
bearing; first-difference coordinates and identical CLI/unit images establish
that test-region contamination. Comparing padded bounds of each sample's own
ink retains exact RGBA equality and passes. This crop correction is not a
rendering tolerance change.

**Verification budget/status:** qualify the stable source and binary with module
boundaries, build-test, lint/inventory, library, exact layout and full Radiant.
Input is not repeated for this font/layout/paint change; round28's 46 unchanged
input failures remain prior evidence, not a result on this binary. No reference,
threshold, floor or capture-policy changes are planned. Fresh browser/native
images at both densities and final aggregate results are pending below.


**Fresh visual evidence:** all 126 row/ink boxes match Chromium in x, y and
height; widths differ by at most 0.046875px. All 42 source pairs have identical
native RGBA ink at each density. The browser/native full images still differ
at 36,951 of 2,448,000 pixels (1×) and 79,448 of 9,792,000 pixels (2×); preserve
those raw raster differences rather than claim browser pixel equality.
`comparison-final.png` shows the missing-mark repair without moving coordinates.
Source, fixture, references and binary are now frozen for aggregate qualification.


**Qualification completed:** session 36373 terminates successfully on CLI
`4e87a357b5f31c9f25fc09676d051ca6855e7487f0f0f97ec106d6cb7fbdfd66`.
Exact `make test-layout-baseline`: 3155 full /139 partial /zero failures /zero
skips. Full Radiant: 5929 full /143 partial /zero failures /one existing skip,
across 6073 tests. All 1659 library checks pass. Module boundaries, reference
contracts, all runner builds, integer-cast lint, CSS inventory freshness and diff
checks pass. The driver verifies unchanged source/build/fixture/reference
fingerprints and CLI throughout the gates. Final evidence is retained in
`temp/layout-analysis/round30/qualification-final.json`. No references,
thresholds, floors or capture policies changed. Clusters needing multiple font
faces keep the existing fallback path; complete cluster font matching is not
claimed. The documented contextual-script, canonical scalar fallback and SVG
outline limits remain separate. The long-running goal stays active.

**Next finite diagnosis:** fresh browser/native images for
`text-wrap-balance-float-006` show a paint defect independent of its legal
soft-break differences. The later paragraph's red background covers a preceding
silver float, despite correct text exclusion geometry. CSS 2.2 Appendix E.2
steps 4/5 require ordinary block backgrounds/borders below non-positioned float
subtrees. Three independent opaque, translucent and nested controls are
pixel-identical to their expected positioned-layer controls in Chromium; native
differs over all 4800 pixels of each 80×60 float. The translucent control requires
blue-over-red composition, so clipping a background around float geometry would
be wrong. The same defect survives SVG export and Chromium replay, identifying
the shared paint walk rather than the raster backend. The existing child walk
mixes ordinary blocks and floats in source order, and hit testing reverses that
same sequence. Reuse stacking classifiers, scoped ancestry replay and the block
phase drivers; preserve view/style ownership (**D4.5.1v4 /D4.1.4v5**) and module
boundaries (**D7.3.3–D7.3.4**). The finite acceptance/verification plan and raw
captures are under `round31/next-float-paint-diagnosis.json`; no round31 engine or
test changes have been made. Valid combined text transforms need a separate
spec milestone: Chromium rejects them, so its rejection cannot justify dropping
valid native support to improve a stored score. Fresh clamp-control line breaks
already agree; its span union discrepancy remains distinct.


## Round 31 — float paint phases

**Priority and acceptance:** the qualified round30 binary paints a later normal
block's background above an earlier float. Independent opaque, translucent and
nested controls fail over each entire 80×60 float in PNG and SVG, while Chromium
matches independent positioned-layer references. The new native regression is
red before engine edits and includes inline overlap, clipping, an opacity owner,
pointer-events:none, 1×/2×, retained repaint and a causal background edit. CSS
2.2 Appendix E.2 steps 4/5/7/8 require block backgrounds, floats, inline content
and positioned layers in that order; hit testing must traverse the painted
layers in reverse. The float's translucent background must compose over the
ordinary block background, so clipping or overwriting geometry is not a repair.

**Structured design:** retain the shared child walk and introduce background,
float and foreground phases with an all-paint state for atomic subtrees. Shared
stacking classifiers decide which visit owns a float, ordinary scaffold or atomic
paint root. Activate extra passes when floating descendants require them, while
ordinary wrappers inherit the current phase. Enter the existing font, ancestry,
clip and effect scopes in each visit. Boundary paint belongs to the background
pass; replaced pixels, inline content and positioned work retain foreground
ownership. Keep real stacking contexts and inline/flex/grid atomic roots intact.
Reuse these classifications for reverse hit testing, including normal block
self-hit deferral. Do not copy the negative-layer ancestry walker.

Partial visits no longer own a complete contiguous subtree fragment. Guard
whole-fragment reuse and element markers by that ownership property instead of
replaying a whole cached subtree in one phase. Whole atomic/root visits retain
the existing cache contract (**D4.5.1v4 /D4.1.4v5**); a future cache for individual
phases would need explicit identity and is not implied by a view ID. Preserve
module boundaries (**D7.3.3–D7.3.4**) and retain float exclusion geometry and
source bytes. Independent soft-break optimization and no-float stacking debt
remain separate; no fixture-specific order or background clipping is allowed.

**Verification budget:** focused red/green native paint/hit tests, neighboring
text/vector/paged and retained checks, fresh browser/native 1×/2× controls and
shared SVG/PDF consumers. Then build all required runners and qualify the frozen
source with relevant boundary/lint/inventory checks, exact layout and full
Radiant. No reference, tolerance, floor or capture-policy changes are planned.


**Shared-cause refinement:** an ordinary later block with a negative top margin
also covers earlier inline ink and takes its hit when no float is present. The
new independent control fails before activation changes, and Chromium confirms
the earlier ink belongs above that background. This is the same CSS phase owner,
so retaining float-only activation would leave the root shape incomplete.
`radiant_stack_flow_phases` now computes a shared phase bitset for ordinary block
sequences as well as floats. Inline-only sequences keep the single-pass path;
contexts without floats omit that pass. The earlier plan's no-float deferral is
superseded by this evidence, not adopted as a compatibility path.

**Direct consumer prerequisites:** the native red control has 132 assertions.
A block SVG/HTML neighbor already owns its internal phase correctly. The direct
iframe path exposes 12 paint and six hit assertions: an embedded document must
restart its complete stack inside its frame's foreground content. Add scoped
resets at the existing render/document-hit entries. Generic export callbacks
also report three semantic visits for one ordinary block (six red assertions).
Restrict `visit_element` to foreground, preserving one destination/link/callback
per logical element. The fixture and tests include these ownership controls.

**Export limit:** the opaque/translucent/nested float, inline overlap, clipping,
positioned overlay and SVG/HTML samples agree in PNG/SVG/PDF (opacity rounding
can differ by one channel). SVG/PDF omit embedded iframe contents on both the
archived round30 and repaired binaries. This pre-existing export limitation is
recorded separately; native iframe paint/hit ownership is repaired. Do not
claim complete iframe export from the float phase checks.


**Stable focused/visual acceptance:** all 163 native text-query, 172 vector,
492 paged, 86 display-list, 30 retained-list and 25 state-store checks pass after
the complete flow activation. The final 14 compared case boxes have identical
x/y/width/height to Chromium, and both browser densities retain identical CSS
geometry. All 15 sampled native colors match within one channel value. All
SVG/PDF samples except the independently unchanged iframe-content limitation
also match within one channel value. Raw full-image native/browser differences
remain 14,340/2,400,000 pixels at1× and26,760/9,600,000 at2×; no full raster equality
claim is made. In the original balance/float page, the formerly red point(30,100)
now matches Chromium's silver(192,192,192). Legal soft-break differences remain
separate. Native retained/causal paint and hit checks cover the repaired layers.
The initial range-for build needed a C-style shared phase-order helper; the
semantic probe also needed explicit project borrowed/view casts. Those build
errors are setup results, not red behavioral evidence. Source/binary/fixture/
references are now frozen for required aggregate qualification.

**Terminal qualification and wrap-up:** the exact `make test-layout-baseline`
exits zero with **3,155 full /139 partial /0 failed /0 skipped**. Library passes
all 1,659 checks. Module boundaries, reference contracts, runner build, integer
cast lint, inventory and diff checks pass. The full `make test-radiant-baseline`
exits 2 with **5,926 full /143 partial /4 failed /1 skipped** out of 6,074.
The four failures are `svg_foreign_object`, `svg_foreign_object_css`,
`svg_foreign_object_interaction` and `svg_foreign_object_namespaces` in the UI
automation runner (11, four, seven and three failed script assertions). They
passed in round30; their cause has not yet been established. Focused native and
export controls do not qualify those broader integration cases.

The qualification driver is terminal (session53995 exit1); source and CLI
fingerprints remain unchanged after the failed aggregate. Evidence and the UI
log are archived under `temp/layout-analysis/round31/qualification-final.json`
and `ui-automation-final.log`. **Round31 remains unqualified.** At the user's
wrap-up request, work is paused with the current changes preserved. Investigate
these four failures before resuming partial-pass repairs. The read-only round32
form-owner diagnosis is deferred; no form-owner engine edits were started.

## Round 31 follow-up — SVG/HTML paint boundaries

**Done behavior:** `foreignObject` captures restart a complete HTML paint stack
in their copied local context. Outermost SVG fragments and `foreignObject`
establish stacking contexts under
[SVG 2 §3.4.1](https://www.w3.org/TR/SVG2/render.html#EstablishingStackingContex).
This also prevents the host background pass from painting transformed foreign
content in HTML coordinates. The four reported UI failures reproduce before
the repair; after both ownership changes, all 11 existing foreignObject fixtures
and three scheduler checks pass, including scrolling and live move/remove/
reinsert behavior. No assertions or expectations change.

**Module ownership:** the existing SVG namespace predicate moves to the shared
CSS DOM data helpers; the public C DOM API delegates to that implementation.
An initial full build exposed the dependency on the high-level DOM module in
standalone layout runners. The shared helper removes that dependency under
**D7.3.3–D7.3.4** without copying the predicate or adding test stubs. Selected
view/style ownership remains **D4.5.1v4 /D4.1.4v5**.

**Open export evidence:** 16 of 18 static fixture/scale/format combinations pass
across PNG/SVG/PDF at 1×/2×. One 2× text sample differs in SVG and PDF; all six
embedded SVG capture images have identical decoded RGBA to the archived round30
renderer, including the discrepant text sample. This does not claim full export
parity. Evidence is under `temp/ui-foreign-object-fix/`; test expectations and
qualification declarations remain unchanged. The generated CSS inventory only
refreshes source locations.

**Verification:** module boundaries, integer-cast lint, inventory and diff
checks pass. `make test-radiant-baseline` exits zero: **5,939 full /143 partial
/0 failed /1 skipped** out of 6,083. The complete UI automation selection passes
**644/644**, clearing all four reported failures. Its layout selection retains
**3,155 full /139 partial /0 failed /0 skipped**. Source and CLI fingerprints
remain unchanged throughout the successful gate (session18298 exit0). This
follow-up is qualified for the reported UI regressions; the separate export
sample limit above remains open. Verification is recorded in
`temp/ui-foreign-object-fix/qualification.json`; the neighboring export reports
preserve the discrepant sample and archived-image comparison.
