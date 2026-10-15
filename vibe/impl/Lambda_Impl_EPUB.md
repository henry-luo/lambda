# EPUB package implementation

**Updated:** 2026-10-15. **Status:** initial reader implemented; broader acceptance incomplete.

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
