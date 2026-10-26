# EPUB package implementation

**Updated:** 2026-10-26. **Status:** initial reader implemented; broader acceptance incomplete.

The public package is `lambda.doc.ebook` (D7.2.4). It loads EPUB 2/3 and
presents chapters through Radiant. The supported profile is horizontal LTR
prose with chapter scrolling/column pagination, plus single-page fixed-layout
XHTML/SVG with fit/zoom. The broader target remains recorded in
[Lambda_Pkg_EPUB.md](../Lambda_Pkg_EPUB.md). No formal ruling changes.

## Scope revision

At the user's request, MOBI and Kindle AZW/AZW3 (KF8) are excluded. Their
adapter, routes, fixtures, build configuration and libmobi have been removed.
EPUB uses existing ZIP/XML/CSS/Radiant facilities and no new ebook decoder.

## Publication and resources

- `read` captures local bytes procedurally; `parse` accepts immutable bytes or
  a captured ZIP root. Captured-member parsing never reacquires the source
  (S12.1.1v3, S12.4.1v2, S14.3.1v2, D7.1.2v2).
- OCF rendition selection, relocated OPF, metadata/cover, manifest IDs, spine
  occurrences/non-linear entries, XHTML/SVG fallbacks, EPUB 3 contents,
  landmarks/page lists, and EPUB 2 NCX are implemented in Lambda Script.
- Shared XML normalization and checked URI resolution are reused by EPUB and
  DOCX. Local CSS imports/URLs, images, sanitized SVG and unprotected fonts
  become captured data URLs; active content and external resources are excluded.
- Native CSS token projection preserves raw spelling and decodes escaped
  values. Rewriting uses bounded imports/resources and logarithmic traversal
  depth. Raw-text CSS serialization cannot introduce a closing style tag.
- Required protected spine content fails explicitly. Protected optional
  resources use diagnostics/fallbacks. Package errors occupy 7800–7809.

## Reader and ownership

The CLI `view`, `layout`, and `render` routes recognize EPUB. File-browser
selection uses the same reader and exposes original OPF source. It retains
captured publication data and the active projected chapter across controls.

The reader provides author/cover, nested contents, landmarks, publisher page
links, chapter/page changes, internal anchors, back/forward history, whole-book
exact search, bookmarks, font family/size, line spacing, margins, publisher
style selection, light/sepia/dark themes, fixed-page fit/zoom and resize.
Generated column pages are labelled separately from publisher page links.

Saved locators identify SHA-256 publication snapshots, rendition, spine
occurrence, canonical href, original source node and Unicode-scalar position
with quote validation. Atomic sidecars preserve preferences, bookmarks and
last-read location when an unchanged file moves. Embedded browser previews do
not persist. Reader mutation stays in procedural handlers (S12.1.3); retained
values keep their Input/resource owners (D4.1.3, D4.5.1v4).

Native integration fixes are shared with ordinary iframes:

- Full srcdoc loading/replacement and viewport refresh are shared by block
  and flex layout. Observing a renderer-owned document does not reparse it as
  a body fragment. Successful replacement retires old interactions and glyph
  caches before releasing the old document.
- Child load/teardown restores the embedding evaluator at the quiescent layout
  boundary (D5.4.1). Deferred DOM-package admission no longer marks an unstarted
  load complete, so inputs remain focusable after chapter navigation.
- CSSOM and iframe scroll ranges share descendant-overflow measurement, so
  generated columns can actually scroll. `dom.scroll_metrics` exposes viewport
  and content extents; catalog entries are appended to preserve ABI offsets.
- Generic declarative iframe navigation queues an event in its embedding
  document instead of synchronously invoking another evaluator. This has a
  standalone non-EPUB regression fixture.
- Native policy admission rebinds its document at quiescent event boundaries;
  scrolling in existing HTML/XML previews continues to work. A support-only JS
  capsule can exist before a heap; batch reset now respects that lifecycle
  instead of attempting to allocate a realm-cache root without a heap (D5.3.3).
- Publication URLs encode decoded member names/fragments when serialized, so
  literal percent escapes survive navigation. Resolving an anchor updates the
  source locator before subsequent readiness/resize restores it.

## Reader presentation revision

The reader chrome now uses a compact icon toolbar, a scoped neutral palette,
a book/contents sidebar with current-chapter indicators, a separate search
panel and **Aa** reading settings, and footer navigation. Controls have labels
and tooltips, unavailable chapter/history actions are disabled, and chrome
follows the selected light/sepia/dark theme. Narrow windows collapse the sidebar;
its contents toggle restores it. Search receives focus and Enter submits it.
Settings remain ordinary view/handler policy (D7.2.4, D7.2.5, S12.1.3).

Radiant focus is painted through CSS, including a UA `:focus-visible` rule
that author outline declarations override. Focus transitions now refresh
matching focus selectors. Native form rings and the unconditional focus overlay
are removed. Only a collapsed selection in the focused, editable surface
paints an insertion caret; ordinary reading text, readonly/disabled fields,
noneditable islands and blurred editors do not. Drag selection/copying retains
its canonical DOM boundaries (D7.2.5). `assert_caret` can now check visibility
independently of those boundaries. Browser behavior references and the shared
mechanism are documented in [RAD_17](../../doc/dev/radiant/RAD_17_Interaction_State.md#3-the-data-model).

## Presentation follow-up verification (2026-10-15)

The presentation revision builds in the main checkout. All **19 EPUB/DOCX
package tests**, **81 focused reader/focus UI assertions** across nine fixtures,
the three-process persistence workflow, and Radiant's dimension/cast lint pass.
The new focus fixtures exercise ordinary text selection, editable inputs and
textareas, readonly fields, noneditable islands, blur, empty editing hosts,
author outlines, keyboard focus, and actual pixels for inline outlines and
`outline:none`. Search-panel focus, Enter submission, settings/sidebar toggles,
current-chapter indicators, and narrow-window chrome have native UI coverage.

The full Radiant gate completed at **5,253 passing, 153 partial, 8 failures and
1 skipped**. It passes 156/156 DOM UI, 201/201 native view, 109/109 page-load,
116/116 CSS animation/ownership, the unchanged CSS cascade memory budgets,
and all 32 selected WPT input-event baseline files (20 pass; 12 known skips).
Log: `temp/epub-polish-radiant-qualified.log`. Focus restyles are batched after
selection ownership settles and only for changed pseudo-states. UA focus rules
are parsed alongside stylesheet inputs and cascaded together with author rules;
nonfocusable blocks retain their clean-layout reuse. No WPT harness or memory
budget was changed.

The final inline-painter connection was then checked with the complete explicit
UI baseline invocation (**494/498**, four existing editor preview failures),
the render baseline (**206 passing, 4 partial, 1 failing, 1 skipped**), and the
81 focused assertions. Logs: `temp/epub-polish-ui-last.log`,
`temp/epub-polish-render-last.log`, and `temp/epub-polish-inline.log`.
Three context-menu goldens change only the text-control `focus-visible` flag and
its focus presentation field. Other expected results remain unchanged.

Seven remaining full-gate failures were already reproduced on the clean
pre-EPUB base: the view-reuse/table-caption crash, four editor previews, and
the WPT text/Markdown layout groups. The eighth is `bg_image_01`: its inline
PNG's compressed payload fails zlib checksum validation, and the current image
pipeline paints no image. The earlier validation binary renders it and passes
the reference check (`temp/epub-polish-bg-base.log`). This revision changes no
image decoder. The previous Lambda forced-GC timeout below remains unresolved;
this presentation-only follow-up does not rerun the full Lambda gate.

Desktop, settings and narrow-window native captures are under
`temp/epub-polish-{reader,settings,narrow}.png`.

## Native-window crash correction (2026-10-16)

Chapter replacement reproduced a segmentation fault in
`register_named_elements_recursive`, reached through `timer_runtime_exit`
while `free_document` retired the previous iframe's runtime. Scheduler
teardown pumps the shared libuv loop, which can fire a timer owned by another
document. Timer entry captured the retiring document before switching runtime;
exit then installed that pointer in the timer's realm after its DOM had been
destroyed. The fix captures the restoration document after the callback's
retained owner is bound, preserving the quiescent-handoff contract (D5.4.1).
The CoreText font-name message also appears in passing runs.

The new native `CrossRuntimeTimerKeepsItsOwningDocument` regression detects
the wrong document binding in the earlier implementation and passes with the
fix. All 117 CSS animation/ownership tests and six existing JS timer tests pass.
Native-window reader navigation, search/settings, paged chapters and fixed
layout each pass three repeated runs: 12 processes, 126 assertions, no crashes.
The pre-fix headless reader workflow passed, so the visible-window event loop
is necessary reproduction coverage. Evidence is in `temp/epub-crash/`:
`reader-lldb-2.log`, `unit-before-output.log`, `unit-fixed-output.log`,
`css-animation-tests.log`, `js-timer-tests.log`, and `native-summary.json`.

`make test-lambda-baseline` now passes **7,492/7,492**, including **2,112/2,112
input cases** and **313/313 forced-GC cases**. The earlier music-layout stress
timeout completes successfully in this run; this does not classify its earlier
timeout or attribute that improvement to the timer fix. The log is
`temp/epub-crash/lambda-baseline.log`.

The isolated-report Radiant rerun completes at **5,266 passing, 153 partial,
10 failures and 1 skipped**. It passes all EPUB/focus fixtures, persistence,
**203/203 native view cases**, **109/109 page loads**, **156/156 DOM UI cases**,
and unchanged CSS cascade memory budgets. The failures comprise the eight
previously recorded cases/groups above plus `timeline_navigation` and
`timeline_resources`. Both timeline crashes reproduce three times on the
saved pre-fix executable and three times with the fix; no timeline code or
expectation changes are included. Reports: `radiant-baseline-isolated.log`,
`radiant-results/test_summary.json`, and `timeline-comparison.json`, all under
`temp/epub-crash/`. The first full Radiant attempt was invalidated by a
concurrent build removing `lambda.exe` and shared result files; its launch
failures are not used as qualification evidence.

## Books demo and real-world publications (2026-10-26)

`lambda demo book` (`test/demo/book/`) is a library host for the reader: seven
Project Gutenberg EPUB 3 editions with images, a Continue Reading row with
progress, collections, sorting and search, and the package `reader` for
reading. These are the first real-world publications the package has loaded;
all seven parse, chapter and resume without package errors, and parsing Moby
Dick plus one 136K-character chapter takes about 0.1 s in a debug build.

Two package changes came with it. Every Gutenberg chapter showed "Excluded
active/unsupported element meta" because plain `<meta charset>` was reported as
blocked content; `meta` without `http-equiv` is now dropped silently, and
`http-equiv` meta stays reported. The state directory lookup in `state.ls` is
now `pub fn directory()`, so a host keeps its own records beside reading
positions without repeating the environment fallback chain; `restore` uses it.

The demo reads saved places through `state.restore` and weighs progress by
chapter size plus the text passed in the current chapter. Its fixture
`test/ui/book/library.json` (suite `book`) passes 29/29 with persistence off.
The three `test/lambda/ebook` goldens pass on both tiers, the 32 EPUB and
doc-viewer UI fixtures pass, and `test/epub_reader_check.py` passes.

### Highlights and frame selection (2026-10-26)

Drag selection in the chapter frame landed offset from the pointer by the
frame's window position. `editing_geometry_dom_text_boundary_from_point` and
`editing_geometry_hit_test_boundary` resolve points in the text's own document,
but the press and drag paths in `event.cpp` passed window coordinates. They now
subtract the frame translation: `view_frame_translation` (factored out of
`caret_project_visual_from_block`) on press, and the selection's recorded
`iframe_offset` while dragging. A probe page with a scrolled iframe 273 px
from the window edge selected exactly under the pointer after the fix, versus
274 px to the right before. The `edit`/`editor` UI suites fail the same 37
fixtures with and without the fix (pre-existing state-dump and form failures);
`dom` (165) and `hit-test` (12) pass.

Highlights are `{id, spine_occurrence, href, start, end, color, text}` in
chapter-text offsets, saved in the reading state. `chapter.load` takes them,
keeps those whose stored text still matches, and lowers each source string's
ranges as `<mark class="ebook-highlight">` runs (segments now carry their
lowering path as `key`). The reader learns of a settled selection through
`data-selection-event` on its frame: the DOM mouse policy queues that event on
the embedding `<iframe>`, like `data-navigation-event` for links, because
pointer events do not cross the frame. A boundary maps back to an offset from
the nearest `data-ebook-node` element's first source segment plus the DOM text
before it, so selections across inline elements map exactly. DOM offsets are
UTF-16 units; they equal the source map's scalar counts only for BMP text, so
astral characters before a selection shift it (open). Coloring absorbs
overlapping highlights; a click on a mark opens the bar to recolor or remove;
a highlight re-render keeps the exact scroll offset. `test/ui/book/highlight.json`
(24 assertions) covers drag, cross-`<em>` mapping, recolor, merge, removal and
jumping from the sidebar.

### Page layouts, paper and turns (2026-10-26)

Reflowable chapters have three layouts (`mode`): `scroll`, `paged` (one column
page fitted to the window) and `spread` (two columns per viewport, so every
viewport step turns two pages; a 2-column body of width `100vw - 2m` with gap
`2m` keeps the period at exactly one viewport). Fixed-layout chapters keep
fit/zoom. Paper (`texture`: none, antique, parchment, linen, newsprint, ivory)
is host-side layers in `ebook/page.ls` under a transparent chapter frame, so it
stays put while columns scroll; the chapter takes the paper's ink. Grain and
mottle are two seamless RGBA noise tiles from `utils/generate_ebook_textures.py`
(SVG `feTurbulence` tiled with visible seams in Radiant). Antique adds a leather
board, a double rule and four mirrored corner flourishes, and a minimum 76px
margin in paged layouts.

Turns (`turn`: none, slide-left, slide-up, turn, dissolve) play in paged
layouts. A second "ghost" frame with the same srcdoc is scrolled to the page
being left and animated out over the new page by generated keyframes (two
parities per name so a repeat restarts). At a chapter boundary the ghost holds
the old page (`data-turn=hold`) while the next chapter loads, the turn plays on
`ebook_ready`, and `animationend` hands the ghost the new chapter. Turn folds
the page toward the spine with 2D `scaleX`/`skewY`; a perspective `rotateY`
version rendered the nested paper and frame with horizontal stripe artifacts
in Radiant (open: 3D compositing of nested content). `transform`/`opacity` set
directly on an `<iframe>` are ignored by Radiant, so animations run on wrappers
(open). Settings use option buttons (`data-ebook-pref`/`data-ebook-value`)
instead of the old cycling mode button; `store.preferences` validates the three
new fields, which persist with the reading state.

Two engine fixes came with it. Frame scrollbars now follow the embedded root's
overflow (`layout_iframe_viewport_overflow`, RAD_20), so paged chapters scroll
by script without a scrollbar. And `node_for` resolves a locator owned by the
chapter's `<html>` (`n`), which `query_selector` cannot match: a new chapter
whose first text is whitespace under `<html>` kept the previous chapter's
page offset. Paged layouts exposed a Radiant multicol defect that predates them: a
paragraph nested in a wrapper (`div.chapter`) that crossed a column break
overflowed the column and left a blank band at the top of the next page, and
the error accumulated across paragraphs. `multicol_project_fragmented_descendants`
now measures nested flow from the column top (forced-break flows included),
carries the space a split leaves at a column bottom to later siblings, pushes a
block whose first line cannot fit, and truncates margins at unforced breaks
(css-break §5.2). Alice ch. I now ends every one of its 18 columns inside the
page. `test/ui/multicol_nested_paragraph_fragmentation.json` (14 assertions)
covers it; the multicol WPT suite is unchanged apart from one improvement.

`test/ui/book/page_options.json` (28 assertions) covers layouts,
papers, option state, turns within and across chapters, and the ghost handoff;
`test/epub_reader_check.py` now persists `texture` and `turn`.

### Last spread and front matter (2026-10-26)

A chapter whose columns end mid-spread left the last spread unreachable: the
scrollable overflow of a multicol ends at the last column's edge (Chrome:
`scrollWidth` 1972 for Peter Pan's front matter at 998px, so `scrollTo(998)`
clamps to 974), and a turn showed the previous page's line ends at the left
edge. The reader now appends blank trailing columns in paged layouts (one per
page, two per spread to round up an odd count) and counts pages from where
the first blank starts (`page_geometry`; `dom.bounding_box` is in chapter
document coordinates). Three Radiant defects surfaced on the way and are
fixed with fixtures in `multicol_nested_paragraph_fragmentation.json`
(21 assertions, checked against Chrome headless): `dom_scrollable_extent`
ignored ancestor offsets (the body's inset was lost from `scrollWidth`); a
pushed monolithic block in a split wrapper overlapped the next line ("Language"
over "Other information"); and `multicol_has_forced_break_descendant` also
matched later siblings, so trailing `break-before:column` blocks re-projected
an earlier table 105px low. Gutenberg's `#pg-machine-header p {font-size:medium}` was
sized at the body's 20px (Chrome 16px): `medium` is listed in the
border-width keyword group, so the font-size validator and the `font`
shorthand parser rejected it and the element inherited. Both now use
`css_keyword_is_font_size` (`test/ui/css_font_size_medium.json`), and the
header breaks across columns as in Chrome.

### Icon tooltips (2026-10-26)

The settings panel's reading-layout choices use the toolbar's three icons
(`layout_switch`). Every icon-only control (toolbar, footer, search toggles,
layout icons, highlight swatches, **Aa**) carries a `.ebook-tip` span shown
while its button is hovered, below the control (above it in the footer).
Radiant shows no native `title` tooltip and generates no `::after` box for
`<button>`, and `top: calc(100% + 6px)` on the absolute tip did not resolve,
so the tip is a real child positioned with `top:100%` plus a margin (open:
the three Radiant gaps).

### Settings panel and the footer (2026-10-26)

With the **Aa** panel wrapped to two rows, the footer was pushed below the
window. The reader's column flex took the panel's height as its tallest
setting group (one line), because Radiant measured a wrapping row flex item's
content height without wrapping. Column flex items that are wrapping rows now
use the same width-aware height query as blocks (RAD_08). The page-options
fixture asserts the footer stays at the window bottom with the panel open.

### Search options (2026-10-26)

`search` takes `match_case` (default true, the old exact behavior),
`whole_word` (no letter, digit or underscore on either side) and `chapter` (one
spine index). Matches come from `find`, so a chapter is scanned once; case
folding compares lowercase text, and a chapter whose lowercase form changes
length (U+0130) is searched exactly so offsets stay valid. Each result adds
`chapter` and a `before`/`match`/`after` excerpt (about 40 and 70 characters,
cut at word boundaries, line breaks and space runs collapsed); `snippet` stays.
The reader puts three icon toggles beside the query (case, whole word, current
chapter; book-wide case-insensitive by default) and renders results as a
"Ch. N" label (chapter title on hover) plus the excerpt with the match in a
`<mark>`. `ebook_locations` gains seven checks; `test/ui/book/search.json`
(13 assertions) covers the toggles and results.

## Verification

Validation runs in `temp/epub-validation`, detached at `e7e133727` with the EPUB
changes applied. This isolates a concurrent image-library refactor in the main
checkout. The main checkout also builds successfully with that refactor and
passes the 19 focused package tests, 40 reader assertions, six existing-preview
scrolling assertions, and the persistence workflow.

- Main and isolated native builds pass; Radiant dimension/cast lint passes.
- The heapless JS-support teardown regression passes; all three page-load
  crashes exposed by policy admission are fixed.
- All 19 focused EPUB/DOCX GTests pass. EPUB goldens include metadata/spine,
  CSS/resources, malformed/protected content, option validation, Unicode
  locators and search.
- Six native UI fixtures pass 40 assertions: reader navigation/search/history,
  pagination/typography/resize, fixed layout, generic iframe delegation, and
  file-browser preview/source. These are manifest-discovered baseline tests.
- The registered persistence workflow passes save, moved-file resume,
  preferences/bookmarks, and altered-passage rejection across three processes.
- All 14 original fixtures regenerate identically. A native 1100 × 800 PNG was
  inspected for chapter text, Unicode, local CSS/images, cover and navigation.
- Test262 passes **40,261/40,261** baseline entries with zero regressions,
  including the selected asynchronous entries; its log is
  `temp/epub-validation/temp/epub-js262-baseline.log`.
- `make test-lambda-baseline` completed at **7,139/7,140**, including
  **2,112/2,112 input** and **1,566/1,566 functional** cases. The sole failure was
  the idle timeout in forced-GC `regression_music_layout`; no stress test was
  modified. Logs: `temp/epub-validation/temp/epub-lambda-baseline.log` and
  `temp/epub-validation/test_output/test_mir_gc_stress_gtest.log`.
- `make test-radiant-baseline` completed with **5,250 passing, 153 partial,
  8 reported failures and 1 skipped**. The eight failures reproduce on the
  clean `e7e133727` build in `temp/epub-base-validation`: CSS cascade memory
  budgets, the view-reuse/table-caption crash, four existing editor previews
  (DCT PDF, HTML/plain EML and TSV), and the WPT text/Markdown layout groups.
  No baseline or expected output was changed to hide those failures.
- The final Radiant run passes **109/109 page loads**, **156/156 DOM UI cases**,
  **116/116 CSS animation/ownership cases**, **201/201 native view cases**,
  the reader fixtures and the registered persistence workflow. Logs and summary:
  `temp/epub-validation/temp/epub-radiant-baseline.log` and
  `temp/epub-validation/temp/epub-radiant-summary.json`.

The Lambda forced-GC gate remains an unresolved idle timeout. Native and
MIR-interpreter forced-collection schedules for music layout both pass when
run individually against the clean base; this comparison does not establish
a complete stress-case pass or classify the gate timeout as pre-existing.

Validation required macOS GUI-service access for native Scene3D tests. Initial
disk exhaustion was resolved by sharing storage for identical older UI captures
with APFS clones, preserving their paths and bytes. A zero-length failed link
output was rebuilt. DocBook's existing source UI fixture now declares its actual
XML source path, fixing manifest preflight. These corrections did not change
expected results.

## Remaining profiles and limitations

The initial reader does not establish complete EPUB Reading Systems
conformance. Continuous cross-chapter scrolling, spreads, RTL/vertical
pagination, CFI interchange, obfuscated-font decoding, scripts/media overlays,
DRM decoding and annotations remain outside the implemented profile.

Resume/search currently reveal the containing source text node rather than an
exact rendered character. Unresolvable saved bookmarks are dropped on restore;
retaining an explicit unresolved-bookmark list remains follow-up work. Book
progress currently reports spine position rather than a whole-book text
percentage. Complex paged CSS, full fixed-page pan/overflow, selection/touch,
font-readiness changes and independent-reader visual parity need additional
acceptance evidence. `layout`/`render` project the initial reader viewport,
not a complete-book export.
