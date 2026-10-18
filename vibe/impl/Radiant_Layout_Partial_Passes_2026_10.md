# Radiant layout partial passes — October 2026

Status: round 8 spacing fix passes the layout and Radiant baselines,
2026-10-19. The full input audit is recorded in round 8 below.

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
