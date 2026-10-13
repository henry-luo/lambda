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
