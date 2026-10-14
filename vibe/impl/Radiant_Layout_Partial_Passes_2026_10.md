# Radiant layout partial passes — October 2026

Status: layout fixes through round 5 implemented and verified, 2026-10-13.

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
