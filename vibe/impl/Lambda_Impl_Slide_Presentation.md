# Lambda Slide Presentation — Implementation

> **Status:** presenter milestones 13.1–13.5 met on the measured decks, 2026-10-07; core slide editor delivered 2026-10-25 (see below).
> **Authorized scope:** implement [the package design](../Lambda_Pkg_Slides.md);
> no PPTX import/export.
> **Contracts:** D7.2.4/D7.5.3 package and host boundaries; S12.1.1v3/S12.1.3
> pure transformations and procedural event handlers; S2.6.3 element splicing;
> D4.5.1v4/D5.3.3 ownership and precise roots; D7.2.2 boxed import entries; D2.1.3 tagged string leaves.

## Editing cards and preserving unchanged content

The editor temporarily hides the original object while projecting its draft.
Closing an unchanged or canceled draft now restores the saved visibility before
publication: the minimal-write publisher deliberately reuses identical markup,
so it cannot undo that temporary mutation by itself. This preserves both the
authored document and the existing card DOM (S1.4/S12.1.3).

Each rich-text entry binds the newly loaded model generation, then places its
caret from the entry gesture. It no longer stages a bookmark restore in that
same callback: native completion applies that restore after the callback and
would overwrite the user's pointer position with the new model's default
selection. That invalid container selection disabled later edits.

Blank-space hit testing reuses the existing text-edge search inside the active
surface, with the same inverse client transform as direct glyph hits. Explicit
empty/atomic boundaries retain precedence; an editor-scoped DOM query cannot
fall through to another surface's text. Native caret painting uses the painted
client matrix and the shared transformed-rectangle painter, keeping its scale,
rotation and overflow clips aligned with its text (D7.2.5/D4.5.1v4).

Acceptance covers both Northstar market cards, clicks beside their short number
lines, caret pixels, no-op/canceled edits with pixel parity and retained DOM
identity, plain-text teardown, actual typing and formatting, repeated entry,
undo/redo and save/open. `slide_card_editing.json` passes 64 checks in default
and JIT modes; nine focused UI replays pass 383/383 assertions. Native regression
coverage includes leading/trailing blank space and rich caret pixels at two
device scales under scale and rotation; both defects fail before their repairs.
The focused DOM range suite passes 79/79. Required gates are the Radiant and
Lambda baselines; artifacts are under `temp/slides/cards/`.

Qualification: the Radiant baseline passes with 6,201 passing cases, 141 known
partial cases and two skips, including all 699 UI fixtures. The Lambda/Input
baseline passes 8,257/8,257 with local HTTP access enabled. Integer-cast lint
and `git diff --check` pass.

## Resizing standalone SVG graphics

The Northstar hero's placement box resized during a drag while its authored SVG
kept a fixed 380-by-430 viewport. The shared slide renderer now identifies a
`content` object with one SVG root and makes that viewport fill its existing
definite content wrapper. This reuses the percentage-sized SVG layout used by
the drawing demo and inserted graphics. The original `viewBox`, aspect-ratio
policy and source subtree stay intact (S1.4/D7.5.3). SVGs embedded in ordinary
HTML flow keep their own dimensions.

Acceptance covers shrinking during pointer capture, retained SVG identity and
paint inside the resized outline, cancellation, commit/undo/redo, inspector
dimensions, a different editor zoom, thumbnails, save/open and presentation.
The original live-resize replay fails before the change: the outline shrinks,
but the SVG viewport and pixels remain at the original size. The repair belongs
to the package projection and requires no native engine change (D7.2.5).
Verification uses `slide_graphic_resize.json` in default and JIT modes, nearby
editor/ribbon/inline/preview replays, and the required Lambda package baseline.
Artifacts are under `temp/slides/resize/`.

Verification: all 41 resize checks pass in default and JIT modes, including an
inline SVG that retains its own dimensions. Eight UI replays pass 385/385
assertions. The package baseline passes 8,253/8,257 checks in the sandbox; its
only four failures are local HTTP socket denials. Rerunning that entire HTTP
group with loopback access passes 152/152, covering all four denied cases.
`git diff --check` passes. No native engine files changed.

## Caret placement in scaled slide text

Acceptance covers pointer entry and subsequent clicks on both headline lines,
typing, committing and undo, keyboard entry, UTF-16 offsets, two canvas zooms,
and rich-card entry. Native checks cover a scaled/rotated input and textarea at
device scales 1 and 2, projected caret pixels, and both rich-text rows. Focused
checks precede the required Radiant and Lambda baselines.

Native text-control hit testing now inverts the painted client matrix rather
than subtracting an unscaled layout origin. Carets and composition underlines
use the existing transformed rectangle painter, matching glyphs and selection.
Rich editing scopes the shared text-rectangle search to its active surface and
resolves glyph positions through that surface's CSS plane. This prevents an
untransformed thumbnail from supplying the editor's boundary (D7.2.5/D7.5.3).

The existing text-control boundary reader is published through `dom` with a
realm-neutral result and UTF-16 offsets. The demo resolves its entry point after
the new control has committed layout, cancels pending placement on exit, and
uses the existing text-control selection writer. Rich entry uses a DOM text
boundary; the range-slider pointer setter cannot place a text caret. Keyboard
entry reuses the DOM behavior package's UTF-16 conversion.

Committing an edit also exposed a retained-node reparenting failure: the Mark
importer recursively inspected applied template children before its live UI
identity check. That check now precedes plain Mark ownership inspection, so the
registered editor subtree retains its identity and existing lifetime owner
(D4.5.2). No conservative GC scanning or new ownership exception is introduced.

`slide_caret_placement.json` checks click offsets and pixels separately, including
typing on each row and committing a rich-card edit. The native regression fails
for both hit offsets and caret pixels before the geometry repair. Qualification
artifacts are under `temp/slides/caret/`.

Verification: the 27 interaction checks pass in default and JIT modes. The
Radiant baseline passes with 6,197 passing cases, 141 known partial cases and
two skips; all 695 UI fixtures pass. The Lambda/Input baseline passes 8,226/8,226
cases, and the focused Mark editor/deep-copy suites pass 99/99. Integer-cast lint
and `git diff --check` pass. The wider input audit fails in PDF/render fixtures,
wiki formatting and broader document-format round trips, outside this change
(4,515 passing, 42 failing/timed-out cases, including a stalled PDF batch).
A freshly rebuilt module boundary audit still rejects the existing `doc_state_request_repaint` and
`radiant_dom_m4b_src_get` imports; both call sites are also present in HEAD.

## Thumbnail context menu

Right-clicking a thumbnail selects that slide and opens the existing menu host.
Shift+F10/ContextMenu opens it for the keyboard-focused thumbnail. New, duplicate,
delete, move up/down and present-from-here reuse the existing slide commands;
menu construction shares ribbon items and the core slide-action rows (D7.2.5).
Optional item ID prefixes keep popup controls distinct from matching ribbon and
navigation controls. The shared menu helper measures, clamps, focuses and
keyboard-navigates the popup (D7.5.3).

Secondary pointer/click events leave the newly opened menu intact because native
contextmenu precedes the secondary press. Commands close it and return focus to
the active thumbnail; presentation retains its player focus. Escape/Tab and
outside clicks dismiss it. Opening and dismissing are view state only, while
edits use the existing immutable history (S1.4/S12.1.3). Inline drafts finish
before changing the context target. Delete is disabled for the final slide;
move actions are disabled at the corresponding deck boundary.
The presenter host schedules activation from its render event after the nested
player is projected. Scheduling directly from the presentation request could
run before that player existed, leaving Present from Here uninitialized.

Acceptance covers clicked/focused targets, every command, history and pending
text, disabled actions, keyboard navigation, viewport clamping, distinct control
IDs and pixel parity after dismissal. Qualification uses the context replay in
default/JIT, adjacent ribbon/navigation/editor replays and relevant model goldens.
No engine or package changes are required.

Verification: all 77 context-menu assertions pass in default and JIT modes,
including presentation from a noninitial slide and clean popup dismissal pixels.
Eleven UI replays pass 532/532 assertions, covering neighboring ribbon, sidebar,
inline editing, file and preview behavior. All nine relevant model goldens,
including Word Home, and `git diff --check` pass. Captures and logs are under
`temp/slides/context/`.

## Deleting slides from navigation

Delete/Backspace in the navigation panel routes to the existing `delete-slide`
command, using the focused thumbnail's index when keyboard focus differs from
the active slide. Publication restores focus to the next surviving thumbnail;
the existing model provides undo and the last-slide safeguard. Native text
controls and canvas object deletion retain their own keyboard contexts
(S12.1.3/D7.2.5). `slide_sidebar_delete.json` covers these boundaries.

Verification: the original Delete reproducer fails before the fix. All 50
deletion assertions pass in default and JIT modes, including repeated deletion,
last/middle slides, undo/redo, keyboard-only focus and the last-slide safeguard.
Five focused UI replays pass 231/231 assertions; both related model goldens and
`git diff --check` pass. Logs are under `temp/slides/delete/`.

## Dragging slide thumbnails

The navigation panel captures thumbnail pointers on its retained scrollport.
A movement threshold separates clicks from drags; previews preserve thumbnail
nodes and authored data (S1.4/S12.1.3). A line shows the insertion slot. Frame
callbacks scroll near the panel edges through the existing DOM scroll-state API
and recompute the slot from committed thumbnail geometry (D7.5.3). Escape,
pointer cancellation/capture loss, resizing and outside drops cancel the gesture
and its pending frame. Capture release reuses the panel splitter's helper
(D7.2.5).

`reorder_slide` removes and inserts the source slide rather than swapping distant
positions. Move up/down uses the same transformation. Committing a drop selects
the moved slide and creates one undo entry; unchanged/canceled gestures create
none. Existing deck rebuilding renumbers generated page fields, retains slide
IDs/content, and supplies the saved and presented order. The gesture consumes its
follow-up click, and thumbnail replacement restores keyboard focus for undo/redo.

Acceptance covers upward/downward moves, retained previews and drop markers,
clicks and no-op drops, cancellation, scrolling both ways, pending inline edits,
history and saved/presented order. Qualification uses the model golden, the new
gesture replay in default/JIT and neighboring editor/sidebar/ribbon replays.
This milestone requires no engine or package changes.

Verification: all ten editor replays pass 416/416 assertions. The drag replay
passes 53/53 in both default and JIT modes, including moving an unselected slide,
scrolling in both directions, pending inline edits, save/open and presentation.
Eight relevant goldens pass; the reorder golden also passes in JIT mode.
`git diff --check` passes. Captures and logs are under `temp/slides/reorder/`.

## Closed ribbon menu decoration — 2026-10-25

Milestone: closing Shapes removes its entire popup, including the border and
shadow, while reopening remains usable. The demo already publishes `display:none`
and removes menu items. Layout suppresses the box as `RDT_VIEW_NONE`, but shared
stacking collection classified its retained style storage as a positioned layer.
Raster dispatch could then paint that storage as an inline element. The selected
view retains its storage by D4.1.4v5; that lifetime does not grant paint membership.

Signed stacking collection now skips hidden records before classification or
descent. The positioned-child chain likewise omits suppressed records. The shared
lists serve raster/export and hit testing; no demo styling or timing workaround
is needed. Acceptance covers positive/zero/negative stacking, reopening, Escape,
trigger toggling, outside clicks and choosing a shape. Qualification is bounded
to the failing native pixel case, neighboring publication and stacking cases,
the slide ribbon replay with pixel comparisons, lint and the Radiant baseline.

Verification: the native case fails against the old collection and passes with
the fix, including preserved positive/zero/negative z-index on hide and reopen.
Six focused native checks and five UI replays (168 assertions) pass. The full
Radiant baseline completes with 6,192 passing checks, 141 recorded partials and
two skips, with no new failures. Integer-cast lint and `git diff --check` pass.
Before/after captures and qualification logs are under `temp/slides/menu/`.

## Direct text editing — 2026-10-25

The canvas enters text editing through a double-click, a second click on the
selected text object, or F2/Enter. Selection-only gestures retain the painted
nodes so native double-click targets survive. Actual drags still move/resize
objects. Plain text retains its native textarea path; styled text, HTML cards
and table cells use a retained `lambda.editor` surface. Word's styled projection
is promoted for reuse, including model source paths and authored classes
(D7.2.5/D7.5.3). Opaque graphic islands remain outside the editable text range.

The rich editing host stays alive and is parked outside the procedural canvas
before that canvas is replaced. Reopening uses `controller.replace` and restores
the logical surface's generation/selection. A commit reads an explicit export
snapshot from the immutable editor model; it never reconstructs authored data
from painted DOM. Unchanged sessions leave source/history intact. A completed
session is one slide undo step; Escape discards its draft (S1.4/S12.1.3).
Clicking outside, Tab and Ctrl/Cmd+Enter finish editing; Save/Open/Print shortcuts
finish the current session before their file action.
Inserted tables synchronize their dialog's row, column and cell data from the
edited source in the same commit, so reopening settings retains direct edits.

Acceptance covers native hit identity, plain text, styled card headings without
changing their neighboring content, repeated sessions, inline emphasis and table
cells, cancel/undo/redo, and save/open. Qualification uses focused direct-edit
replays in default/JIT, neighboring editor replays, Word's Home replay and the
affected model goldens. No package or engine source change is needed.

Verification: 11 editor replays pass 433/433 assertions, including 57 direct-edit
checks for continued typing/deletion, formatting, authored and inserted tables,
history, slide navigation and saved decks. Both direct-edit replays and file
operations also pass in JIT mode. Word's Home replay passes 22/22 in both modes; all six affected model
goldens pass. Visual captures retain the cards' typography and layout.
`git diff --check` passes; logs and captures are under `temp/slides/inline*`.

## Home quick actions — 2026-10-25

Home starts with Undo/Redo above Save/Print. Word and Slides reuse the same
`mod_ribbon.history_tools` builder and existing colored SVG contours (D7.2.5).
The title bar keeps Present; Undo, Redo and Save live in Home. Slides publishes
history availability to the ribbon controls after edits and tab changes.
Save uses the existing deck dialog;
Print and Ctrl/Cmd+P save slide-sized PDF pages with completed builds.
The shared `mod_export.pdf` extracts Word's atomic PDF publication helper;
both demos use its pending-file/rename path through Radiant (S12.1.3/D7.5.3).
Printing preserves authored data, saved state and undo history (S1.4).

Acceptance covers the two-row layout, history enablement across edits/tabs,
Save, print-path validation, keyboard access, PDF output and preserved history.
Focused Slides and Word print replays and PDF page/text inspection qualify this
demo-level change; no package or engine source changes are required.

Verification: quick actions pass 28/28 assertions in default and JIT modes;
neighboring editor, file, Home and sidebar replays pass. Word's print replay
passes 26/26 assertions with isolated output paths, and the slide Home golden
passes. PDF inspection confirms two starter pages and 18 Northstar pages at
960 × 540 points, with selectable slide text. `git diff --check` passes.
The captured ribbon and qualification artifacts are `temp/slides/home-actions*`.

## Scaled slide text — 2026-10-25

Milestone: preserve text strokes and stable weight in fitted slides and resized
thumbnails. The owning stage is raster glyph replay: bilinear point sampling
cannot integrate a pixel that covers many source pixels. A thin stem can vanish
or become several times too dark depending on its source position.

The shared coverage sampler integrates the source footprint for axis-aligned
minification. Image and glyph sampling reuse one pixel-cell intersection visitor;
glyphs include transparent space outside their bitmap in the footprint's area.
Magnification, rotation and projective sampling retain their existing paths.
Authored data and layout geometry remain unchanged (S1.4); retained replay keeps
source ownership and tile coordinates (D4.1.4v5).

Acceptance: synthetic thin strokes at 1/8, 1/4 and 1/2 scale retain their coverage
at every source phase; fractional edges have proportional coverage and identical
full-surface/tiled replay. Reproduce Presenter tools' Questions slide and its
thumbnails before/after, then check neighboring slide interactions. Verification
budget: focused native replay tests, visual captures, relevant UI replays and the
required Radiant baseline.

Verification: both regression cases fail with the old sampler and pass with the
fix; all 106 display-list tests pass. Six focused slide replays pass 199/199
assertions. The full Radiant baseline passes with macOS graphics/audio access:
6,187 passing checks, 141 recorded partials and two skips, with no new failures.
Its UI suite passes 690/690 checks. Integer-cast lint and `git diff --check` pass.
Before/after captures are `temp/slides/text-before.png` and `text-after.png`;
qualification logs are under `temp/slides/text-*`.

## Resizable slide navigator — 2026-10-25

The left Slides panel has a draggable, keyboard-focusable vertical separator.
Its width is a session view preference, separate from authored data and history
(S1.4/S12.1.3). Pointer capture, width/ARIA publishing and directional key sizing
are extracted from Finder into `test/demo/mod_splitter.ls` and reused by both
demos (D7.2.5). Escape restores the gesture's starting width; pointer release
and capture loss end it. Bounds reserve the studio's minimum width and the
visible inspector/Animation Pane. New/Open/Undo preserve the chosen panel width.

Thumbnail boxes use the panel width; a coalesced frame callback reads committed
geometry, scales previews and refits the editing canvas. Window resize requires
an explicit subscription on the retained editor owner because it does not bubble
from the window. That subscription keeps panels within the available space after
shrinking the window.
Acceptance covers retained thumbnails during drag, pointer/keyboard resizing,
cancel and clamps, pane changes, viewport changes, and unchanged document dirty
state/history. Qualification uses the focused resize replay in default/JIT,
neighboring editor replays, Finder's existing preview replay and relevant goldens;
no native engine change is required.

Verification: eight editor replays pass 348/348 assertions; the resize replay
also passes 30/30 in JIT mode. Finder's preview replay passes 28/28 in both modes.
All 25 relevant goldens pass, with three focused goldens also passing in JIT mode;
`git diff --check` passes. Captures and logs are under `temp/slides/sidebar-*`.

## Presenter toolbar — 2026-10-25

The shared player uses one 52px row of transport and presenter icons, including
Previous. `slide/toolbar.ls` reuses `ui.dtna.icons` and emits the existing command
attributes, accessible names, shortcut tooltips and toggle state (D7.2.5).
Speed, slide count and jump input remain readable controls. Overflow stays in
one horizontal scroller while Hide stays visible outside it. Full-screen changes
retain both SVG glyphs and select them through pressed state; live handlers
update the action label rather than replacing children (S12.1.3/D7.5.3).
Acceptance covers icon navigation, slider input, fixed-height resize and
hide/reveal behavior at desktop and narrow widths. Verification uses existing
slide goldens and UI replays, focused JIT checks and the required Lambda baseline.

Verification: all 24 slide goldens pass. All 30 slide UI replays pass 724/724
assertions, including Previous icon navigation, slider pointer/keyboard input,
toolbar restoration and resize at 480, 724 and 1280px, and retained full-screen
glyphs. The six focused presenter replays pass 128/128 assertions in JIT mode;
the three HTML/presenter/theme goldens also pass in JIT mode.
`make test-lambda-baseline` passes 8,225/8,225 checks; `git diff --check` passes.
Logs and native captures are under `temp/slides/toolbar-*`.

## Animations ribbon — 2026-10-25

The finite milestone covers the reference entrance and emphasis galleries,
exit effects, motion paths, effect options, Add Animation, Animation Pane,
object-click triggers, Animation Painter and Start/Duration/Delay. Acceptance
requires saved editable effects, preserved imported groups, atomic undo including
anonymous target IDs, independent triggered playback, deterministic rendered
masks and paint, preview without a source edit, and compatibility with the
existing editor and player. Verification budget: focused package goldens in
default and JIT modes, editor and pixel replays, neighboring slide replays, then
the required Lambda baseline. No native engine change or performance claim.
Pure transformations follow S1.4/S12.1.1v3; live DOM and frame ownership follow
S12.1.3/D7.5.3; shared Word ribbon controls follow D7.2.5.

`slide/animation.ls` supplies the shared catalog, defaults and motion presets.
The effects compiler adds deterministic blinds/checkerboard/dissolve masks,
growth, outline color and opacity. Masks reuse the transition path builder.
The scheduler merges With/After Previous source cues into one navigable build,
checks overlapping channels after offsets, keeps address aliases, and prepares
paint continuity chronologically. Trigger cues compile separately; the reducer
owns their independent clocks and reuses pause, rate, stop and frame delivery.
Latest-fired tracks overlay written channels, and cached outgoing scenes retain
their trigger state. Seeking and navigation reset activations. Snapshots and
handouts sample the main build timeline without firing triggers.

`mod_animations.ls` edits nested source leaves without flattening sequence or
parallel groups. Cue-level start, trigger and ordering are explicit in the pane;
effect-level settings edit a single leaf. Stable IDs are pinned in the same
transaction, including inherited/anonymous objects. Painter appends fresh IDs
and ordinary cue bindings. Preview reuses the retained player and existing
cancelable monitor for either the selected cue or all builds. Fill and line
color are limited to native shape/text paint; embedded graphics support other
effects. PPTX, freehand path drawing and a graphical time ruler remain outside
this milestone.

Verification: `make test-lambda-baseline` passes 8,225/8,225 checks. All 24 slide
goldens pass in default and JIT modes; the new animation golden contains 80
behavior checks. All 30 slide UI replays pass 711/711 assertions. After final
option-panel and duration refinements, the two animation replays pass 96/96
assertions in each mode (57 rendered/lifecycle checks and 39 editor checks).
They cover masks, independent outline paint, transparency, growth, paths,
nested trigger hits, pause/resume, repeated firing, outgoing transitions,
ordering, timing, Painter, save/reopen and preview without an undo edit.
`git diff --check` passes. Logs are under `temp/slides/animations-*`.

## Transitions ribbon — 2026-10-25

The finite milestone covers the reference gallery (None, Fade, Push, Wipe,
Split, Reveal, Cut, Random Bars, Shape, Uncover, Cover) plus the existing Morph;
effect options, duration in seconds, sound, On Mouse Click, After, Preview and
Apply to All. Acceptance requires distinct sampled/rendered effects, preview
without document/history changes, per-slide timing with pause/resume and speed,
manual navigation when stage clicking is disabled, one-transaction Apply to All,
and save/reopen/presentation of all settings. Verification budget: focused
package/editor goldens on both tiers; editor and transition live/pixel replays;
the required Lambda baseline at the package milestone boundary. No native paint
engine change or performance claim is planned. Package transforms remain pure
under S12.1.1v3; media, DOM and preview scheduling use S12.1.3/D7.5.3; shared
ribbon widgets and the package transition catalog follow D7.2.5.

The shared package catalog now supplies the gallery, defaults and effect options.
`mod_transitions.ls` owns pure authoring changes and one-transaction Apply to All;
`mod_transitions_ui.ls` uses the shared ribbon widgets. Masks are deterministic
CSS inset/path/polygon/circle samples; movement and layer stacking remain outside
object effects. Completion restores ordinary clips and stacking on cached layers.
The preview reuses the live player, playing a two-scene deck of final objects.
Its monitor lives on the retained editor shell so monitoring cannot remount and
restart the player. It returns without a document edit and can be canceled.

The scene compiler retains validated variants, sound and advancement fields;
master composition shares the attribute list. The reducer and deck clock use
`advance_after` after the final build, including pause, Play, speed, seek and
zero-delay cases. Stage-click disabling does not block explicit controls.
`start_slide` initializes the actual first layer before entry activation, and
embedded `activate`/`stop` commands avoid briefly playing another slide's cue.

`slide/sound.ls` owns one native byte-backed cue voice. It loads file/URL sources
procedurally, decodes embedded data URIs through the existing Mark binary codec,
and uses the same DOM audio interface as DOOM. Native voice polling continues
after visual playback parks, so a preview of None can still finish its sound.
Pause, navigation, Stop and window close handle the voice's lifetime. Errors do
not stop visual playback; the player status and editor's completed-preview status
report them. The three original WAV cues have a reproducible Python generator.
No native or vendor source was changed for this milestone.

Verification: `make test-lambda-baseline` passes 8,224/8,224 checks (2,112 input
baseline and 6,112 runtime). All 23 slide goldens pass on default and JIT tiers;
the new transition golden also passes after the final timing/lifecycle cases.
The editor/Home/Insert replays retain their 230 passing assertions. A standalone
first-slide After replay passes 7 checks without Play or an autostart attribute.
The two new
transition replays pass 104 assertions on default and JIT tiers, including 60
checks of rendered clip/movement/black-cut pixels and final cleanup, plus preview,
settings, error reporting, Apply to All and persistence. Five neighboring live
player replays pass 122 assertions for feature rendering, navigation, Northstar,
playback and speed. Ribbon/preview/contact-sheet captures under `temp/slides/` were reviewed;
whitespace checks pass. Focused final runs supplement the aggregate gate for the
last status/voice-polling changes.

**Native audible output remains unqualified in this environment.** The strict
`test/demo/slides/tests/transitions_audio.json` replay reports 1 passed and 2
failed checks: AVFoundation cannot prepare the byte-backed cue. The untouched
DOOM `fixtures/_audio.ls` / `replay/audio.json` probe reproduces the same
`audio-native: cannot decode source: (null)` preparation failure with its shipped
WAV. The generated PCM and embedded decode round-trip are valid; baseline tests
exercise No Sound for visual/timing playback and separately exercise a missing
file's surfaced error. The strict output test remains present and failed, rather
than accepting an unavailable audio backend as successful audible playback.
Artifacts: `transitions-audio*`, `transitions-native-audio*`, `transitions-baseline.log`,
`transitions-ui-*.log`, `transitions-player-ui.log`, and `transitions-goldens*.log`
under `temp/slides/`. Full broad input/legacy Word failures from the earlier
Insert milestone are unchanged and recorded in that section.

## Insert ribbon — 2026-10-25

The Insert milestone implements the nine groups in the reference ribbon:
slides/tables, images, illustrations, links, comments, text, symbols and media.
The acceptance cases are editable table dimensions/data, retained graphics
settings, field/numbering updates, private comments, rendered screenshots/Zoom,
working presentation navigation and native media controls, and save/reopen with
all graphics intact. Focused goldens and UI replays cover these cases, followed
by the required Lambda/input gates for the shared serializer and package changes.

`mod_insert_data.ls` owns dialog descriptors, `mod_insert_ui.ls` builds the ribbon
and forms, and `mod_insert.ls` lowers validated settings into ordinary objects.
Under D7.2.5, the existing Word chart, shape, SmartArt, WordArt and equation
renderers supply the graphics; Word's validation helpers are public, and its
symbol catalog and stateless ribbon widgets are shared. Tables use the existing
editor table command, HTML adapter and Word table style painter. No renderer or
catalog is copied. Pure document/history changes follow S1.4/S12.1.1v3;
procedural form, resource and screenshot work stays at S12.1.3/D7.5.3's boundary.

Objects accept a map-valued `metadata` annotation, retained in compiled `source`
and ignored by layout/paint. Insert stores its typed parameters there so ordinary
formatting cannot erase editability. Footer updates reuse object IDs and prune
removed animation targets; slide numbers follow ordering. Comments are separate
marked notes and do not overwrite speaker notes or enter the audience canvas.
Internal navigation pins generated destination IDs before insertion. Thumbnail
activation uses a focusable role-button container, so embedded links/buttons
cannot prematurely close an outer HTML button; preview content is inert. Media clicks
are excluded from stage advancement, and outgoing cached layers pause playback.

The save/reopen acceptance exposed unquoted namespace attributes (`xml:space`)
in the shared Mark printer. The printer now quotes names outside the Mark name
token grammar, sharing that grammar's character helpers with the reader. The
regression covers unusual map keys, namespace tags/attributes and imported SVG.
This preserves authored data through serialization; no namespace semantics ruling
is changed (S1.4, S16.9.3; D7.5.3 for the input/output boundary).

Screenshot inserts accept image files or capture the current slide through
`slide.snapshot` and the CLI renderer. Zoom uses the same rendered image path.
Object embeds accept HTML, SVG or Mark. Operating-system screen capture, OLE,
PPTX interchange and spreadsheet-style chart data editing remain outside this
milestone. Media uses manual native playback; no autoplay policy is introduced.

Verification: the five editor/Home/Insert UI replays pass 230 assertions;
the two Insert replays pass 102 assertions in JIT mode. The 27 focused slide,
Mark and neighboring Word goldens pass on default, interpreter and JIT tiers.
The final SVG-definition regression also passes in default and JIT modes: edits
and reopened decks allocate fresh definition IDs, preventing WordArt gradients
from sharing another object's paint. The serializer regression and the five
existing goldens whose output needed name quoting pass. The aggregate Lambda
baseline's identical runner passes 8,223/8,223 checks (2,112 input baseline and
6,111 runtime); `make test-lambda-baseline` built and exercised those gates,
and its runner was repeated after updating the affected textual expectations.
The native build and whitespace checks pass. Ribbon and chart-dialog captures
were visually reviewed under `temp/slides/`.

The broader `make test-input` remains failed outside this milestone: Wiki tests
report the existing `input_type`/runtime-format adapter mismatch, a PDF reference
reader cannot load `tide-table.pdf`, and the render/PDF/WebGL groups report
image/reference differences and idle timeouts. See
`temp/slides/insert-input-final.log`. Word's shape and bookmark/symbol UI replays
also retain 6 and 5 failures respectively (native-value and modal expectations);
the same 31/37 and 16/21 results reproduce using untouched HEAD Word/demo sources
under `temp/insert-reference/`. Their goldens pass, and neither those fixtures
nor their unrelated behavior was changed. Full logs are under `temp/slides/`.

## Home ribbon — 2026-10-25

The Home milestone replaces the initial text-button row with Clipboard, Slides,
Font, Paragraph and Drawing groups, using Word's ribbon icon system and shared
two-row group layout. All controls have authoring behavior, selection-aware
disabled/pressed states and accessible labels. Home changes apply to the whole
selected text object or shape, preserving the existing object-selection model.
Menus share the demos' focus/navigation and measured viewport-clamping policy.

Under D7.2.5, `mod_text_format.ls`, `mod_shape_presets.ls` and `mod_ribbon.ls`
in `test/demo/` now own Word's typography rules, case conversion, palettes, shape
presets, font options and stateless ribbon layout. Word retains its existing
module exports through delegation. Slides calls those helpers plus the existing
`lambda.editor.mod_inline_format` CSS setters and
`lambda.editor.mod_alignment` geometry. The shared menu's fit procedure is
public so retained slide markup uses the same placement policy as Word.

`slides/mod_home.ls` adapts those pure helpers to authored elements under
S1.4/S12.1.1v3; publication and native field/menu events remain procedural under
S12.1.3/D7.5.3. The compiler and file format are unchanged. Structured list,
highlight and vertical-alignment content is ordinary Mark/HTML, so the same
data displays in standalone presentations. Rich runs retain their wrappers when
case changes or newlines split into list items. Copy freezes layout-derived
bounds and typography, including nested objects, before paste; Format Painter
copies formatting while preserving the target's content and geometry.

Acceptance cases cover the visible grouped ribbon, font family/size and emphasis,
editable list/vertical-alignment combinations, palette paint, clipboard/painter,
shape styles/outlines/alignment, placeholder reset, keyboard menu focus and
`.slides` save/reopen/present. Verification uses the new Home golden and UI
replay, existing editor/file replays, the neighboring slide goldens, and Word's
Home/font/shape goldens and Home/context-menu replays. The changed helpers are
demo modules; no native, vendor or package source changes require engine gates.

The three slide editor replays pass 128 assertions. Home's 45-assertion replay
also passes with `LAMBDA_EXEC_BACKEND=jit`; it checks menu focus, native font
drafts, rich/list formatting, visible shape paint, clipboard/painter, and a saved
deck reopened and shown by the live presenter. The 24 selected slide/Word
goldens pass in default and JIT modes. Word's unchanged Home and context-menu
replays pass 22 and 40 assertions. Starter, text-formatting and drawing captures
were visually reviewed under `temp/slides/`. Whitespace checks pass.

## Slide editor — 2026-10-25

The demo entry is now `test/demo/slides/slides.ls`; `lambda demo slides` opens an
authoring application. Northstar and the three smaller decks remain independent
`.slides` samples and retain their direct presenter entry points. The editor
reuses the slide compiler, baseline visual sampler, HTML renderer and live player
under D7.2.4/D7.5.3; immutable document edits follow S1.4/S12.1.1v3 and procedural
DOM handlers follow S12.1.3. No semantics or host API ruling changes are needed.

The finite authoring milestone covers:

1. Select slides and nested objects; edit plain text on the canvas, rich content
   as Mark, appearance, geometry and notes; move and resize with one undo step
   per gesture. A stationary pointer event only selects.
2. Insert, copy, duplicate, delete and reorder objects/slides; maintain unique
   object IDs and remove deleted targets' effects and empty cue wrappers.
3. Choose themes/layouts, entrance builds and transitions; preserve explicit
   authored overrides. Editing composed content detaches the affected slide
   from its master while retaining other slides and shared definitions.
4. Open/save `.slides`, keep deck-relative image resources across Save As,
   export HTML handouts, and present the in-memory edited deck without a save.
5. Launch the editor through the demo catalog and keep standalone sample
   presentation behavior intact.

`mod_model.ls` owns validated value transformations and bounded undo history;
`mod_view.ls` owns editor markup; `mod_app.ls` publishes retained regions and
adapts commands; `slides.ls` owns event routing and the independent presenter
session. Existing source helpers supply element construction, slide compilation,
sampling, geometry and minimal-write DOM publication. The photo album's image
source resolver moved to `test/demo/mod_resources.ls` so both embedded hosts use
the same resolution policy. Authoring drafts never rewrite the sample files.

Verification budget: the three discovered editor goldens and neighboring slide /
photo-album goldens in interpreter and JIT modes; focused editor UI replays for
rendered editing/gestures/history, inline editing, files, samples and presenting;
the existing standalone presenter replays; one focused GC/poison check; a CLI
build and the demo-catalog native launch check. No layout/runtime/package engine
source changes or performance claims are part of this milestone. Broad engine
baselines are reserved for a change in those owning stages.

Delivered scope excludes PPTX interchange, multi-object selection, a visual
master editor, an animation timeline and autosave. Rich HTML/SVG is preserved
and edited as Mark rather than through a rich text toolbar. These are subsequent
authoring milestones, not presenter regressions.

Acceptance evidence: the CLI and native catalog runner build; the catalog launch
case passes with the editor selector. The two editor UI fixtures pass 83
assertions in JIT mode, including native field drafts, visible object paint and
geometry, inline commit/cancel, current-slide presentation and Escape ownership.
The standalone Northstar and playback-control replays pass 38 and 23 assertions.
Neighboring slide/photo-album goldens pass in interpreter and JIT modes, including
the three editor goldens for value/history/composition and transformed resize
anchor invariance. A focused every-allocation GC plus freed-memory poisoning
replay passes edited text, undo/redo and presenter teardown. Visual reviews of
the starter and Northstar editor are under `temp/slides/`; whitespace checks pass.

## Presenter tools, themes and composition — 2026-10-07

SLD7 adds a docked presenter console with rich notes, a cached final-build preview
of the next slide, and a wall-clock timer with pause/resume/reset. Overview title
cards, number/ID navigation, toolbar hiding, blackout and a laser pointer share
one per-instance session with playback. Shortcuts preserve text editing, jumps
restore root focus, and pointer exit clears the marker. Narrow embedded players
stack the console below the canvas. This delivery uses the same viewer; an
independent second-display speaker window is not implemented.

`presenter.ls` owns tool state and markup, `compose.ls` resolves named layouts
and inherited masters, and `theme.ls` resolves palettes/typography. Composition
validates unused masters, cycles and duplicate local IDs before scene compilation.
Local IDs replace inherited top-level objects; cues can target those objects.
Theme-resolved font size/family participate in Morph compatibility. Audience
layers and inert preview layers have separate lazy caches with instance-specific
IDs, preserving SLD3/SLD4. All orchestration remains in the source package under
D7.2.4/D7.5.3 and S12.1.3; no native host or formal-spec changes were needed.

Toolbar restore follow-up: reading the toolbar bounds immediately after changing
`display: none` to `flex` returned the previous committed, zero-height box. The
player therefore filled the viewport with the stage and put its restored controls
below the window. Visibility changes now request a `slide_fit` frame; resize
also measures after the new toolbar wrapping commits. This respects the
post-layout geometry contract (ES30) through the existing D7.5.3 host boundary.
`slide_toolbar_restore.json` checks button/H toggles, rapid toggles, resize while
hidden, wrapping changes, painted toolbar pixels and a working Next button.

| Verification | Result |
|---|---|
| Package scripts, interpreter/JIT/auto | 15 scripts pass in each tier, including 16 presenter-state checks and 32 theme/master checks |
| All slide UI replays | 12 fixtures, 293 assertions pass, including the 18-slide Northstar deck and toolbar restoration |
| Presenter/toolbar replays with forced GC and freed-memory poisoning | 3 fixtures, 64 assertions pass; notes/preview reuse and independent players covered |
| Lambda baseline | 6,294 checks pass (4,190 runtime/script checks plus 2,104 input checks) |
| Visual review | Docked notes/timer/next preview inspected at 1280 × 800; blackout and pointer pixels asserted |

Artifacts are `temp/slide_baseline_repeat.log`, `temp/toolbar_fix_*`,
`temp/toolbar_restore_{before,after}.png`, and `temp/slide_console_review.png`.
The Northstar fixture now uses a 900px-tall
viewport so the expanded two-row toolbar leaves room for its 1280 × 720 logical
canvas; its original geometry and pixel assertions remain unchanged.
This round makes no new frame-time or retention-byte claim. The earlier
acceptance measurements below remain specific to their measured decks.

## Acceptance round — 2026-10-07 (frame cost and retention)

This round closes the 13.5 storage and frame-cost gates on the measured decks.
The changes are commits `e9063e4bf`, `3bd129379`, `c985b7a27`, `2409c4488`,
`9384097ad` and `8f6bad70d` on the `worktree-slide-acceptance` branch (not yet
merged); the older progress notes below are history.

| Gate | Change | Evidence |
|---|---|---|
| Computed font-family ownership | Joined family lists are canonical per view tree (`view_tree_canonical_font_family`); equal lists share one tree-lifetime string, so inherited/copied borrowers stay valid (D4.5.1v4) | 100-target opacity reproducer holds view-pool live bytes flat over 60 callbacks (was +3.4 KB/frame, +5.2 KB/frame with a family list) |
| Other view-pool growth | Motion lists resolve in a per-call scratch pool; `line-height: normal` is one static value; selector queries/matchers use call scratch and `selector_matcher_destroy` frees; retired registry records are freed on slot recycle | Same reproducer flat; 20-round navigation holds document and view pools flat with collection |
| Wrapper-stranded detached DOM | SLD6 idle-time collection: the lifecycle registry charges wrapper-only rejected candidates once, and an idle point collects when 64 KB is newly stranded | 20-round navigation: zero collections and +3.7 MB before; a bounded 120–560 KB document-pool band with six idle collections after |
| Published-fragment / navigation retention | SLD1–SLD5: decks are `.slides` files presented by the `slides` transform; a slide renders once, on first view, and later navigation changes only state (D2.9.3) | 20-round two-slide navigation holds Input 92,480 B, document pool ~130 KB and view pool ~53 KB flat after warm-up (was +17.5 KB Input per round, never reclaimable) |
| Per-frame cost | One host style context per inline-SVG paint pass; a paint-only commit for presentation `opacity`/`transform` turns; inline-SVG layers keyed on content (`style_content_epoch`, `doc_state_content_version`) and folding a uniform stage scale (RAD_14 §4.4, RAD_16 §8) | Release timing below: turn + paint p50 and p95 under 16.67 ms at both viewports on a quiet machine |

Release timing: Apple M4, `temp/slide/perf_mixed.ls` (100 simultaneously
animated text/shape/image wrappers, a one-second fly-in on a fitted stage), 60
aligned callback/paint pairs per run, three interleaved runs per binary taken
when the 1-minute load average had fallen to about 10 (other sessions were
compiling earlier). *Before* is `lambda_release_batch2` (the retention commits,
none of the frame work); *after* is the branch head. Cells are p50 / p95 ms of
the run with the lowest turn + paint median; the last row is the median across
the three runs.

| Phase | 1280 × 720 before → after | 1920 × 1080 before → after |
|---|---:|---:|
| Script + synchronous host | 4.90 / 6.04 → 4.91 / 5.61 | 4.73 / 5.10 → 4.71 / 5.41 |
| Cascade | 2.31 / 2.64 → 0.20 / 0.22 | 2.26 / 2.68 → 0.17 / 0.26 |
| Layout | 10.04 / 10.55 → 0 / 0 | 9.56 / 10.19 → 0 / 0 |
| Paint record | 10.10 / 10.49 → 0.98 / 1.49 | 9.74 / 10.08 → 0.97 / 1.40 |
| Paint replay | 4.56 / 4.90 → 3.71 / 4.20 | 5.04 / 5.34 → 3.98 / 4.47 |
| Paint total | 16.08 / 16.60 → 6.08 / 6.51 | 16.11 / 16.73 → 6.30 / 7.30 |
| Paired turn + paint | 33.40 / 37.67 → **11.18 / 15.67** | 32.67 / 37.77 → **11.20 / 15.25** |
| Median of three runs | 33.63 / 37.87 → **11.33 / 15.67** | 34.10 / 40.44 → **11.49 / 15.74** |

Turn + paint is under 16.67 ms at p50 and p95 at both viewports. One 1920 run
had a 22.1 ms p95 from a single paint-replay outlier, and the same binaries
measured under load average 30–60 give p50 11.8–15.3 ms with p95 up to 40 ms:
the budget holds on a quiet machine, not under heavy contention. The measurement
excludes GUI presentation and event-pump latency. Remaining per-frame cost is
Lambda sampling plus host writes (~4.8 ms) and raster replay of 100 objects
(~4 ms). Artifacts: `temp/slide/quiet_{1280,1920}.txt`, `ab_q_*` logs and
`final_table.py`.

| Verification on the branch head | Result |
|---|---|
| Focused fixtures, forced GC + freed-memory poisoning | 15 slide/host/transform fixtures, 273 assertions pass (126 Northstar, 32 speed, 14 navigation-state, 6 paint-only) |
| Package scripts | 13 scripts × JIT/interpreter = 26 runs match their expected output |
| Navigation retention | 20 rounds / 40 assertions: Input 92,480 B, document pool 129.6 KB and view pool 53.1 KB flat after the first round |
| SVG paint parity | `test_svg_paint.cjs` and `test_svg_smil.cjs` pass at 1× and 2× with layers off and eager; a 0.907-scaled stage cached vs direct differs only by the whole-pixel snap (ink centroid ≤0.46 px, ink totals within 0.05%) |
| Full Lambda baseline | 6,272 / 6,275; the three REPL session failures are the locale cases recorded as environmental |
| Full Radiant baseline | 4,042 pass, 350 partial, 8 fail. All 8 are environmental or pre-existing: `tetris_smoke`; `doc_editor_text_to_pdf` and `page_facatology` (the pre-branch binary fails identically in this worktree); four `dom_jquery_ui_*` (the tracked `test/jquery-ui` symlink resolves only from the main checkout); `bootstrap_5_kitchen_sink_` page load (15 s timeout under load; passes standalone in 10 s); CSS cascade memory (jqueryui `css_live` 378,762 B and linuxmint 668,986 B, identical with the pre-branch binary) |
| Lint | Radiant float/int-cast rule passes; the full sweep's mem-kind, header-ratchet and editable-registry findings name no field or header this branch adds |

## Progress before the acceptance round — 2026-10-07

*Superseded by the acceptance round above; kept as history.*

Proposal milestones **13.1–13.4 are implemented within their documented scope**.
Milestone **13.5 remains open**: release playback exceeds the 16.67 ms frame
budget, and repeated navigation has not demonstrated bounded storage. Functional
success and clean forced-GC diagnostics do not close those acceptance gates.
PPTX import/export remains outside the authorized phase.

Merge `971779a27` is complete with no unresolved index entries. Integration
included dynamic transition-track ownership and retirement. The CSS-delta package
change compares sampled channels before CSS formatting in `html.ls`; `live.ls`
applies those deltas directly. The static projection still emits a full map.
Those changes and their regression fixture are committed in `2a3c41a6d`.
The subsequent sample relocation, Northstar deck, styled controls, animated chart
marks, object containing-box fixes, playback-control corrections, speed slider
and range-thumb correction are committed in `c67751443`. No native font-family
ownership fix has been implemented yet.

The toolbar now includes a 0.5×–4× speed slider with a multiplier readout.
Lambda owns rate changes and clock reanchoring for cues, transitions and dwell,
including paused playback. Restart and navigation retain the rate. The native
input behavior supplies pointer/keyboard stepping through the existing DOM
package; no slide-specific host operation is added (S12.1.1v3/S12.1.3, D7.5.3).
Forced collection exposed a borrowed synthetic event-name string in
`dom_engine_dispatch_event`: creating the nested input event could reclaim its
name before dispatch copied it, suppressing author input delivery. The bridge
now roots both target and name throughout dispatch under D5.3.3. The same
forced-GC reproduction passes after that ownership fix.

The range-thumb correction fixes layout restoring the authored `value="1"`
after input changed the live value. Range initialization now reads an existing
live value without creating state for untouched HTML defaults. Input commit also
gives the view document-owned value storage instead of a pointer into its stack
buffer (D4.5.1v4). The generic input fix preserves the package boundary (D7.5.3).
The toolbar range is 0.5×–4× in quarter steps. Pixel assertions now verify actual
thumb movement on clicks and drags, both endpoints, and persistence through
Restart and later layout updates; value/readout assertions alone missed the bug.
The native range thumb now uses the existing circle fill/outline helpers in
`render_form.cpp`, preserving its size and interaction geometry (D7.5.3).
The toolbar preview was visually checked and all 32 speed assertions still pass.
The full Radiant baseline retains 4,031 passes / 350 partial / 4 failures; both
HTTP setup failures pass with loopback access, leaving the existing Tetris and
LaTeX iframe failures. Float/int-cast lint passes. Artifacts:
`temp/slide/circle_thumb_*` and `temp/slide/northstar/circle-thumb-controls.png`.

| Latest verification | Result and boundary |
|---|---|
| Package scripts | 13 fixtures × JIT/interpreter = 26 matching expected-output runs, including chart samples, autoplay clocks, pause/resume, restart and reduced motion |
| Focused UI | 14 slide/host fixtures, 255 assertions pass with forced collection and freed-memory poisoning, including 126 Northstar checks and 32 speed-slider checks |
| Merged native checks | 405 cases pass, including transition-track retirement |
| Full Lambda baseline | 6,297/6,297 pass after the range-thumb and value-storage fixes |
| Full Radiant baseline | 4,031 pass, 350 partial, 4 fail after the range-thumb/value-storage fixes; both HTTP setup failures pass with loopback access; Tetris smoke and LaTeX iframe navigation remain failed |
| Navigation stress | Normal and forced-GC/poison runs each pass 40 assertions over 20 rounds / 120 callbacks; forced run has no raw-pointer or inconsistent-focus-ancestry diagnostics |
| Latest lint / diff | Radiant float/int-cast lint and whitespace checks pass |

All layout, render, DOM UI and memory baseline gates pass. Full Lambda and
Radiant baselines were rerun sequentially after the range-thumb/value-storage fixes; the
405-case native row is the earlier merge verification. Older rounds below are
retained as history and do not describe the current failure set.

Latest release measurements use `temp/slide/lambda_release_merged`, an Apple M4,
and 100 mixed text/image/SVG targets, with 60 aligned callback/paint pairs per
viewport. After CSS deltas, paired callback plus paint is **23.693 / 27.139 ms
(p50 / p95) at 1280 × 720** and **23.910 / 26.627 ms at 1920 × 1080**. The
measurement excludes GUI presentation/event-pump latency. Script plus host
median improves modestly, while tails remain inconsistent; this does not meet
the frame budget. Detailed comparisons and artifact paths are recorded below.

| Outstanding issue | Evidence | Next action / completion condition |
|---|---|---|
| Computed font-family ownership | Opacity-only reduced probe: a family list adds 106,200 bytes beyond a single family over 60 callbacks, exactly 59 × 100 × 18 bytes | Give computed family storage explicit ownership across inheritance, copies, temporary fonts and secondary views; prove release and borrower safety with native regressions, then rerun the reduced probe |
| Other view-pool growth | Single-family probe still grows 48,648 bytes; full mixed-target build grows about 215 KB | Attribute remaining live allocations and demonstrate a plateau after warm-up; fixing joined names alone is insufficient |
| Published-fragment / navigation retention | Normal Input/DOM end at 10,889,024 / 28,935,841 bytes; forced-GC run ends at 7,801,088 / 8,221,595 bytes; neither proves a bound | Audit publication and every surviving Mark/DOM owner, implement a compliant lifetime fix, and repeat long navigation with pool and arena accounting |
| Per-frame cost | About 24 ms callback plus paint; layout alone remains about 7.7 ms | Investigate generic style invalidation and avoid unnecessary reflow only where used-value, overflow, paint and hit-test behavior remain correct; repeat release measurements at both viewports |
| Final acceptance | Retention and timing gates open; two previously observed baseline failures remain | Repeat relevant regressions after each fix, then sequential full baselines; report remaining failures and map final evidence to proposal milestones |

The next implementation task is the computed-family ownership fix. It follows
D4.5.1v4's **“pin, gen-check, copy-as-value”** seam contract and D4.1.4v5's
**“Pool = individual allocation + individual free”** rule. Published Input
storage requires a separate boundary audit under D4.1.3; D4.1.1v2 says tracing
stops at non-managed pointers. An ownership change must preserve that boundary
and D5.3.3 precise roots. No formal ruling changes are proposed.

## Delivered package

The explicit entry module is `lmd/package/slide.ls`, resolved as `lambda.slide`.
The supporting modules under `lmd/package/slide/` contain the implementation in
Lambda: checked scene normalization, themes, HTML/CSS/SVG lowering, cue
compilation, easing, typed keyframes, deterministic sampling, state reduction,
transitions, color interpolation, paths, Morph, paragraph builds, controls and
live frame orchestration. There is no dedicated native slide object or parser.

[The public reference](../../doc/Lambda_Slide.md) describes the implemented API.
[The introductory deck](../../test/demo/slides/slide_presentation.ls) covers entry/click
builds and slide transitions. [The package-content deck](../../test/demo/slides/slide_package_content.ls)
embeds a chart and uses the paragraph helper.

[The Northstar deck](../../test/demo/slides/slide_northstar.ls) adds an 18-slide
fictional leadership briefing. Its reusable source module supplies consistent
layouts, cards, SVG charts/diagrams, roadmap and scorecard tables, five click
reveals and speaker notes, using ordinary element composition (S2.6.3) and the
existing package/host boundary (D7.2.4, D7.5.3). It needs no external assets.
Visual inspection exposed a missing containing box in the group's inner HTML
wrapper: absolute children were present but Radiant omitted their zero-height
ancestor. The package renderer now supplies relative positioning and full group
bounds before authored styles, making local coordinates explicit. No native
engine change is required. The new `slide_northstar.json` replay covers all
18 slides, five reveals, Previous/Restart and group geometry/pixels. Normal and
forced-GC/poison release runs each pass 92 assertions with no raw-pointer or
focus-ancestry diagnostics. All 18 final-state snapshots render to PNG and were
visually inspected; their HTML matches byte-for-byte in JIT and interpreter.
Existing package fixtures still match in both tiers (22 runs), and the previous
11 focused UI fixtures pass 74 assertions under forced GC and poisoning after
the group fix. Artifacts are `temp/slide/northstar/`, `northstar_ui*_result.json`
and `northstar_regression_*` under `temp/slide/`. These checks do not close the
separate timing/retention gates.

The subsequent controls/chart update replaces native-looking button defaults with
an explicitly styled dark toolbar, rounded buttons, a mint Next action, hover and
focus states, and a slide-count badge. Northstar's five data charts now separate
static axes/legends from animated marks: a clockwise donut composed of 24 arc
sectors, a left-to-right line/area reveal, staggered vertical bars revealed from
the baseline, and sequential horizontal bar fills. Labels fade in after the
data. Geometry, sequencing and easing remain Lambda authoring over existing
clip/opacity tracks (S12.1.1v3/S12.1.3, D7.5.3); no native animation extension is
needed. Percentage-sized SVGs and images also receive definite inner-wrapper
dimensions, preserving the authored object bounds during clipping.

Normal and forced-GC/poison release replays each pass 121 assertions, including
paused intermediate chart pixels, completion pixels, staged labels and existing
navigation. The previous eleven UI fixtures still pass 74 assertions under
forced GC and poisoning, with no raw-pointer or inconsistent-focus-ancestry
diagnostics. Twelve Lambda fixtures match in both tiers (24 runs); the new fixture
checks initial/intermediate/completed samples and reduced-motion snapping for
all five charts. All 18 final HTML snapshots match byte-for-byte between tiers
and render successfully. Chart/control renders were visually checked. Artifacts:
`temp/slide/northstar_chart_reveals*`, `chart_regression_*` and
`northstar/controls.png`. Earlier timing measurements precede these changes;
performance and retention acceptance remain open.

The playback-control correction fixes two reducer limitations: Play previously
ran only entry cues, becoming a no-op at ordinary click boundaries and leaving
paused cues paused; Restart reset only the current slide. Play now runs the deck
with a configurable positive `autoplay_dwell_ms` (default 3000), resumes paused
cue/transition/dwell clocks, and replays from the beginning after completion.
Restart resets the deck; `restart-slide` retains explicit current-slide replay.
Manual navigation cancels autoplay. Readiness activation still runs only entry
cues through `activate`. Dwell uses a separate elapsed value so completed chart
visuals do not rewind while waiting. The reducer carries missed-frame time
through automatic advances and parks at the final build. These changes stay
in Lambda under S12.1.1v3/S12.1.3 and D7.5.3; no host extension is required.

`test_slide_playback_controls` initially checked 23 boundaries, including repeated Play,
paused cues/transitions/dwells, manual cancellation, late frames, reduced motion,
restart scope and invalid dwell values. All 13 package scripts match their
expected output in JIT and interpreter (26 runs). The new UI replay passes 23
assertions under release forced GC/poison and also with the workspace debug
executable; this is functional validation, not a performance measurement.
Existing focused UI checks pass with forced GC/poison. The extended Northstar
replay passes 126 assertions, including Restart to slide 1 and automatic
playback through all 18 slides; all 13 focused fixtures total 223 assertions,
with no raw-pointer or inconsistent-focus-ancestry diagnostics.
Artifacts: `temp/slide/controls_*`, `controls_regression_*` and
`playback_controls_*`. Timing and retention acceptance remain open.

The subsequent speed update expands that reducer fixture to 36 checks, adding
mid-cue, transition and dwell rate changes, paused-rate updates, missed-frame
carry, restart persistence and invalid-rate diagnostics. All 26 JIT/interpreter
runs pass. The new `slide_speed.json` checks default/readout values, pointer
selection, keyboard stepping, preserved pause, completion at 2× and restart
persistence. It passes 22 assertions under release forced GC/poison after the
native name-root fix; all 14 slide/host fixtures pass 245 assertions, with no
raw-pointer or inconsistent-focus-ancestry diagnostics. The toolbar was rendered
and visually checked at 1280 × 800 (`temp/slide/northstar/speed-controls.png`).
Artifacts: `temp/slide/speed_*`. The full Lambda baseline passes 6,297/6,297.
The full Radiant baseline reports 4,031 pass / 350 partial / 4 fail, with all
layout, render, DOM UI and memory gates passing. Two HTTP fixtures cannot start
their local servers in the sandbox and both pass with loopback access; the
previously recorded Tetris smoke and LaTeX iframe failures remain. The speed
fixture records forced-GC/poison environment settings for regular UI-runner
coverage. Float/int-cast lint and whitespace checks pass.
An additional existing plain-JS range replay passes 26 assertions normally;
under forced GC/poison its JS preamble fails before input dispatch, leaving five
assertions failed. That separate bootstrap failure is outside the slider's
Lambda handler path and remains unresolved.

The subsequent thumb correction adds painted-thumb assertions: before the fix,
the value/readout checks passed but the endpoint pixel failed because reflow
restored the authored value. After the generic live-value and storage fixes
(D4.5.1v4, D7.5.3), the release speed replay passes 32 assertions, including
dragging to both endpoints and retaining the thumb through later layout. The
0.5×–4× toolbar range is covered by a 38-check playback reducer fixture, including
4× completion timing. All 13 package scripts pass in both tiers (26 runs), and
all 14 slide/host release replays pass 255 assertions with forced GC/poison.
Existing plain-JS range and pointer replays pass 26 and 11 assertions normally.
The regular UI runner also passes all 32 speed assertions. Sequential full
baselines retain 6,297/6,297 Lambda passes and Radiant's 4,031 pass / 350 partial /
4 fail result. Both sandbox HTTP-server setup failures pass on a loopback-enabled
rerun; the previously recorded Tetris and LaTeX iframe failures remain. Float/int-cast
lint and whitespace checks pass. Artifacts: `temp/slide/thumb_*`.

| Milestone | Status |
|---|---|
| Checked scene model, themes, static snapshots and public module | Implemented |
| Pure cue compiler, keyframes, deterministic sampler and reducer | Implemented |
| Generic Lambda frame delivery and realm-neutral style writes | Implemented; lifetime checks continuing |
| Live player, controls, viewport fitting, transitions and error recovery | Implemented |
| Wipe, authored paths, paragraph builds and explicit-ID Morph | Implemented within documented limits |
| Release profiling, retention and final baseline verification | In progress |

Compilation prepares per-target track lists and initial visuals once. Presets
expand to typed tracks; the sampler shares interpolation with keyframes and
transitions. Parallel conflicting writers fail with source-path diagnostics;
ordered sequential writes retain continuity. Color tracks carry typed RGBA
values through intermediate samples and serialize only at the output boundary.

Live templates cache the compiled plan and initial rendered tree in source
attributes. They retain ordinary DOM nodes during a build and patch only changed
visual properties. Scene replacement occurs at slide/transition boundaries.
Each player owns its playback state, frame token, generation and last successful
ordinary visual sample. Frames compare against that retained GC value instead of
resampling the previous time. Scene replacement, Morph and failed commits invalidate
the comparison; the cache retains one sample rather than a frame history.
Each player retains its own sample independently. Players waiting for manual
input, paused players and completed decks stop requesting frames; stale deliveries
are ignored. Failed commands or
refused requests pause and report an error, with Restart available for recovery.

Standalone pages fit logical coordinates into the current viewport and refit on
resize. Embedded players require an explicit unique instance ID. Hidden objects
and transitioning layers use pointer exclusion, `aria-hidden` and `inert`.
Keyboard routing preserves embedded controls and modified shortcuts.

## Shared host changes

- The existing DOM operation catalog adds `request_frame`, `cancel_frame` and
  `viewport_size`. The first two are procedural operations, with zero denoting a
  refused request. Viewport reads return null without a suitable top-level host.
- A document-owned frame queue stores generation-checked native node references,
  copied event names and reusable request slots. It retains no script `Item` or
  closure. Delivery is FIFO, requests made during a callback belong to the next
  frame, and cancellation can suppress a later callback in the current batch.
  Document teardown releases the queue; detached owners are pruned.
- Lambda-only documents bind the shared host context for load, resize, input and
  frame events. Virtual headless time and window-loop time use the same delivery
  path. The current JavaScript adapter remains separate from package policy.
- Realm-neutral `style_set_property` uses precise roots and the existing CSS
  mutation machinery rather than synthesizing a JavaScript style wrapper.
- Transformed SVG viewport clips retain the active CSS transform. CSS clip
  serialization preserves its inverse transform across display-list storage,
  replay offsets and raster clipping; focused pixel fixtures cover these paths.
- Imported MIR calls now pass `ITEM_MISSING_ARGUMENT` for omitted Lambda boxed
  arguments. Explicit null remains null, and cross-language calls retain their
  host ABI. This repairs the skipped-default bug exposed by `slide.compile(deck)`
  in eager JIT mode (D7.2.2); the package does not supply a workaround.

Behavior-handler model values now receive a precise root before the event record
is allocated (D5.3.3). A forced-GC double-click regression reproduced a collected
DOM wrapper with the previous binary; rooting the model fixes word selection and
subsequent caret collapse without a JavaScript runtime. Both canonical DOM and
projected selection assertions pass. The handler retains the rooted value through
its call and render-map dirty notification.

An opt-in `RADIANT_FRAME_PROFILE=1` trace measures each delivered turn and its
nested reconcile: script plus synchronous host writes, cascade, layout, repaint
request, event/mutation/commit counts, queue slots and live DOM/view pool bytes.
Paint execution is outside these turn timings. Script sampling and synchronous
DOM writes are currently combined; they must not be presented as separate
measurements. The trace is disabled by default.

A line shape now samples its authored stroke as its paint baseline, and static
SVG snapshots use the sampled stroke just as live patches do. The paint
continuity regression includes line highlighting. Eager JIT snapshots exposed
an unrelated markup guard that assumed eight-byte `String` alignment. It now
checks the actual `String` ABI alignment, with a focused four-byte-aligned string
escaping regression; valid static MIR strings are no longer discarded.

## Earlier verification rounds

These observations precede the merged-tree verification in the current progress
summary. Failure counts and investigations here are historical; consult the
summary and later merge results for the current state. Retention acceptance
is still open.

| Check | Recorded result |
|---|---|
| Ten slide Lambda regression scripts | All expected outputs match in eager JIT and interpreter modes with the character-backing release, including 14 helper assertions and 9 paint-continuity assertions |
| Focused live/pixel fixtures | All eight pass with the handler-allocation release binary: presentation, two players, motion/highlight/Morph, reduced motion, autostart/resize, FIFO cancellation, transformed SVG and CSS clipping, and failure/restart. Features and frame delivery also pass under forced GC. The mouse-root fixture passes 2 assertions. Handler allocation passes 10/10 with explicit forced GC and poisoning, including a retained detached wrapper and an independent DOM attribute edit. All ten focused fixtures pass under explicit forced GC and poisoning with the character-backing release (66 assertions). |
| Native regressions | AVL 42/42, retained-view/ownership 165/165, GC 83/83, MarkEditor 55/55, markup 44/44, HTML parser 55/55; latest focus-boundary retained-view/ownership suite 166/166 and JS script suite 192/192; clip serialization/offset regression passed |
| Float/int-cast Radiant lint | Latest round passed |
| Latest Lambda baseline | Character-backing round: Input 2104/2104; Lambda 4167/4170 (2026-10-07). Slide regressions pass, including the full 1154-case Lambda executable and 296 forced-GC cases. The same three REPL prompt expectations fail. |
| Radiant baseline | Sequential character-backing round: all layout/render gates passed; 4010 pass, 350 partial, 14 fail (2026-10-07). UI automation passes 335/346, including the extended allocation fixture at 10/10 under forced GC and poisoning. The same 11 unrelated UI failures remain. The updated large-batch fixture preserves its eight focus/state assertions and passes on the incremental path. DOM integration passes 126/127; the existing context-menu mismatch remains. View command tests pass 65/67; the HTTP-image placeholder mismatch remains, and the LaTeX iframe navigation failure seen in earlier rounds recurred (it passed the preceding backing-refresh round). |

Earlier Lambda failures comprised three REPL prompt mismatches and LaTeX/math
cases. A focused rerun passed the seven slide cases then present and a LaTeX
case; the math HTML golden remained different. The workspace contains unrelated
LaTeX/TikZ work, which this implementation leaves intact. No golden or runner
was changed to suppress those failures.

Earlier Radiant failures were document-editor UI fixtures, Tetris smoke, Todo
file-switch/textarea, a context-menu snapshot, and an HTTP image fixture whose
server failed to start under the environment's socket restrictions. A focused context-menu replay shows stale state-dump paths: expected
`html.body...` versus actual `#document.html.1.body...`. The editor filter replay
updates the native input value but its reactive filter model does not reflect
the edit, so its clear button is never rendered. That handler reads `evt.char`;
the exact dispatch failure remains open. These findings identify failed
contracts without changing goldens or adding an input compatibility workaround.
The later round adds two LaTeX view failures caused by a parse error in the
concurrently edited `latex/packages/page_style.ls`, a LaTeX iframe navigation
failure and a Bootstrap page-load timeout. That timeout reproduces in the debug
page-load runner; a focused release view completes successfully. The relationship
of the remaining failures to the host changes still needs review.

`make build-test` also encountered an unrelated display-list test link failure:
`render_glyph_path.o` now references `layout_utf8_next_codepoint`, which that test
link does not supply. The changed glyph-path source is outside this package's
edits. Building the retained-view target separately succeeds; the latest 149 tests pass.

## Retained presentation values

`dom.presentation_style_set_property(node, property, value)` writes an owned CSS
value at animation origin, without replacing an authored Mark attribute.
`dom.presentation_style_clear(node)` removes the whole transient layer. Both are
shared, realm-neutral host operations (D7.5.3). Unknown properties, invalid
values, `!important` writes and values unsupported by the existing ownership
copier are rejected before changing the previous sample. Same-value writes are
no-ops. Author `!important` rules retain precedence. Recascade preserves this
layer and authored inline declarations; node removal/document teardown use the
existing owned declaration release path (D4.5.1v4).

Live sampled effects, layer transitions, Morph placement/background and SVG
fill/stroke now use this channel. Viewport sizing remains an authored style
update on load/resize. Host rejection propagates to the player's pause/error
recovery path. SVG paint reads the presentation winner alongside the existing
CSS cascade. Pure timing and interpolation remain Lambda code.

Two focused native tests cover unchanged authored attributes, cascade/recascade,
clear and rejected updates, plus 256 transform/opacity samples with stable Input
and node arenas and bounded pool storage. Live fixtures cover HTML opacity,
SVG paint, author `!important`, recascade and clear. `style_get_property` is now
published to Lambda and precisely roots its arguments. Its inline read excludes
presentation values; an extended regression exposes and checks stylesheet
`!important` precedence without losing the authored CSSOM value.

The channel exposed an incorrect presence scan: `style_tree_foreach` counts
callback results but does not stop on false. A later stylesheet declaration
could erase an earlier inline/presentation match. The scan now accumulates its
result, preventing canonical stylesheet binding from discarding authored Morph
insets. The mixed inline/stylesheet recascade regression passes; both normal
and reduced-motion Morph fixtures pass.

A separate release retention probe found that first buffers of temporary runtime
arrays used the document arena whenever UI mode was active. Allocation now
selects that arena only when it owns the collection header; explicit Input
construction retains its existing allocation path (D4.1.1v2, D4.3.1). Native
regressions verify a GC array survives collection with its heap-owned buffer and
an arena Element retains an arena buffer. All 83 GC tests pass.

Event source-position/selection snapshots now use ordinary GC maps and arrays,
with exact roots during construction (D4.1.1v2, D5.3.3). Existing source-schema
builders initialize private immutable seeds once per document. The delivery
fixture retains a selection snapshot across later frames and checks its schema
and values. All nine assertions pass normally and with `LAMBDA_GC_FORCE_EVERY=1`.

The reconcile journal keeps inline small-turn storage and grows reusable,
document-owned storage for larger turns. Growth preserves order and reconcile
pins; reset releases pins without discarding capacity; teardown frees the
buffer. Allocation failure retains the broad fallback. Mutation-root scratch
uses the actual record count. The retirement regression checks 256 ordered
records and storage reuse. The bounded repaint-region collector can still
request a full repaint safely; it does not truncate reconciliation.

The 70-row DOM mutation fixture now expects incremental reconciliation rather
than a record-overflow fallback. Its focus, retained state and identity assertions
remain intact; all eight pass. This expectation changes because the journal now
retains the whole batch, rather than to suppress a regression.

Repeated inline attribute add/remove operations exposed unpublished shape
drafts retained in Input's arena and transition steps retained in its pool.
MarkEditor now owns a lazy draft arena and releases it at destruction. Persistent
types and pooled keys retain their established owners (D3.4.3v3, D4.1.4v5).
A 256-cycle regression checks exact warm-state arena/pool bounds and surviving
attributes; all 55 MarkEditor tests pass. Reconcile selector matchers now use
caller-owned storage through the shared initializer; release profiling had
attributed 104 bytes per turn to their former retained allocation.

## Release probe and remaining work

Release executables are linked to separate paths under `temp/slide/` because
workspace debug/release builds share the root executable. A probe uses 100
simultaneous text/image/SVG targets, a one-second fly-in sampled 60 times, then an
idle second. The macOS/arm64 host's CPU model is unavailable under the sandbox.
The latest completed channel/array-ownership measurements are:

| Viewport | Turn p50 / p95 | Script + host p50 | Cascade p50 | Layout p50 |
|---|---|---|---|---|
| 1280 × 720 | 18.733 / 33.806 ms | 9.951 ms | 1.185 ms | 7.502 ms |
| 1920 × 1080 | 24.615 / 32.627 ms | 12.785 ms | 1.337 ms | 9.761 ms |

Percentiles use 60 sorted samples (p95 index floor(59 × 0.95)). These measure
Lambda plus synchronous writes, cascade and layout; paint is excluded. They are
not displayed frame rates and do not meet a 60 Hz budget of 16.67 ms including
paint. Both deliver 60 events/commits, reuse two frame slots, perform one full
reflow per turn and deliver zero events in the idle second. These measurements
precede the growing journal, which removes the 64-record overflow fallback.

| Storage | 1280 first → last sample | 1920 first → last sample |
|---|---|---|
| DOM pool live bytes | 1065754 → 1141998 | 1067034 → 1143278 |
| View pool live bytes | 388648 → 388022 | 388440 → 387814 |
| Input arena used bytes | 432000 → 469792 | 432000 → 469792 |
| Node arena used bytes | 496 → 496 | 496 → 496 |

Retention is still open. Before array ownership was fixed, Input grew by
roughly 57 KB per frame; transient event metadata then accounted for 640 bytes
per frame. Both ordinary-frame growth sources are fixed without omitting fields
or rewinding published data (D4.1.4v5, D5.3.3). The event-snapshot release probe
at 1280 × 720 records Input 431232 → 431264 bytes (the final parked diagnostic
write), node arena 496 → 496, and two reused frame slots. Turn p50/p95 is
18.711/20.330 ms, with paint excluded.

A 600-frame, ten-restart probe passes all ten assertions and delivers no idle
callbacks. Before the draft fix it records Input 431232 → 705440, DOM pool
1065760 → 2008495 and view pool 388648 → 480038. Those numbers do not establish
long-session bounds. The growing-journal probe removes full-layout fallback:
layout generation stays unchanged, with one incremental reconcile per frame.
Its 1280 × 720 turn p50/p95 is 18.775/21.042 ms; script/host, cascade and layout
p50 values are 9.451, 1.947 and 7.213 ms. Instrumentation attributed 1316
bytes per turn to reconciliation: 104 during cascade and 1212 afterward.
Caller-owned matchers remove the former. Positioned layout accounted for the
latter: its static-position direction query allocated initial-value strings and
cast them to `CssValue`. It now uses the existing typed keyword/inherited
resolver. Ordinary-frame commit growth is zero. The per-node audit logging was
removed after attribution.

DOM attribute writes now reuse equal immutable values in a document-owned index
whose strings stay in the same Input arena (D4.1.1v2, D4.1.4v5). This does not
free published strings or put arbitrary values into NamePool. The index is
released at document teardown and is keyed by the owning Input. A regression
checks 256 alternating visibility/inert writes, exact warm Input bounds and an
older returned string that remains unchanged. Storage is proportional to distinct
values, not a guarantee for applications continually writing unique strings.
All 150 retained-view tests pass.

The post-fix 1280 × 720 probe records DOM 1060698 → 1059298, view
388536 → 379558, Input 417024 → 417056 and node arena 496 → 496 bytes.
Turn p50/p95 is 19.506/26.323 ms; paint remains excluded. A 600-frame,
ten-restart run passes all ten assertions, records zero ordinary-frame commit
growth and two reused slots, but records DOM 1060704 → 1180283, view
388536 → 392032 and Input 417024 → 460832 bytes. Restart/command retention
remains open. The previous-sample cache experiment passed live and forced-GC
checks but did not demonstrate a release speedup, so the package keeps its
original sampling path. Native style-channel bounds alone cannot establish
whole-player retention.

With attribute reuse, the same 600-frame/ten-restart run passes ten assertions
and records Input 415392 → 415456 bytes, node arena 496 → 496 and zero ordinary
frame commit growth. DOM/view growth remains. A 100-restart run delivers 1400
callbacks and records DOM 1060704 → 1910183, view 388536 → 433972 and the same
Input bound. These measurements do not establish whole-player bounds.

An experiment keeping every scene mounted passes the existing feature fixture
and a 20-round two-slide navigation probe, but still grows the document pool by
about 5 MB. It also exceeds the proposal's active/partner subtree policy, so it
was removed. Style-cache counters remain at five canonical entries, zero cold
bytes and 11803 live bytes during that experiment; the growth is elsewhere.
The restored two-subtree implementation passes 40 navigation assertions, but its
20-round probe records Input 1858144 → 51155168 and DOM 2995118 → 32473247
bytes. Fragment parsing/source lifetime and boundary reconciliation are the next
ownership targets. Profiling now also reports journal capacity, canonical style
counts/live/cold bytes and evictions. None of these navigation numbers satisfy
the retention criterion.

Fragment parsing bypassed the private token arena already used by full-document
HTML parsing. It now uses the same begin/recycle/end helpers (D4.1.4v5), while
published Mark values keep their original owner. The new 1000-element fragment
regression checks a bounded token footprint, published attributes and continuation
after token scratch release. The 20-round navigation probe still passes 40
assertions and reduces its final Input footprint from 51155168 to 37505264 bytes.
Published source and document-pool growth remain open; this is not whole-session
acceptance.

The latest sequential attribute-reuse probes record turn p50/p95 of
19.176/21.777 ms at 1280 × 720 and 21.420/24.630 ms at 1920 × 1080. Both deliver
60 events/commits, keep journal capacity at 1024 and canonical style entries at
five, and deliver no idle callbacks. Input increases by only the final 32-byte
diagnostic write, node arena is unchanged, and ordinary-frame commit growth is
zero. An earlier 1920 run in this round was much slower (53.202/119.737 ms);
host-load variance is uncontrolled, so these are observations rather than a
stable hardware guarantee. Paint is excluded in every case; 60 Hz acceptance
remains unmet.

Earlier investigation fixed overwritten transform chains, unlinked cascade
records and AVL wrapper removal. Default motion values now use a per-document
immutable cache. Inline CSS parsing uses scratch plus the existing owned value
copier; rejected cascade copies and optional keyword spellings are released
correctly. Those checks and the layout sizing gate passed before the channel.
The simultaneous Lambda/Radiant channel baseline build shared the root binary;
its widespread Radiant failures are not accepted. The sequential rerun restored
every layout/render reference gate, with the 14 failures recorded above.
No tests or goldens were altered to suppress failures.

A Lambda-only reactive scene-template experiment passes the feature checks and
40 navigation assertions, but its 20-round probe still records Input
981728 → 19781232, DOM 3448417 → 36341140 and view 1089178 → 7122996 bytes.
It reduces published-source growth while increasing view retention. The experiment
was removed; only the active/transition-partner fragment implementation remains.
All ten package scripts match their expected outputs in both JIT and interpreter
modes with the clean handler-root release binary, and all eight existing fixtures
plus the two-assertion forced-GC mouse fixture pass. These functional results do
not close the retention or frame-time criteria.

A temporary release allocation-stack audit of one navigation round passes both
assertions. Between its first and last frame the live document pool increases
2995118 → 4106227 bytes. Major newly retained groups include cascade declaration
records, owned inline declarations and values, wrapper initialization, per-element
transition snapshots and style resolution. The trace begins after the first scene
replacement and ends before the final Previous command, so it cannot prove which
objects are leaks or establish a session bound. Stack tracing changes execution
cost drastically; its timings are excluded from performance evidence. All temporary
allocator/event tracing has been removed. Per-element extension teardown is the
next ownership check; canonical style-cache growth is not the observed driver.

The per-element transition snapshot was allocated in the document pool but was
not released when its extension retired. The new lifetime regression fails with
the old library and passes after the missing release. Pinned detached nodes keep
their snapshot; retirement releases it. A 1,024-retirement warm run preserves the
same live-pool footprint. All 152 retained-view cases pass. This changes ownership
at retirement only (D4.5.1v4); snapshots still survive relayout. A new release
20-round navigation run passes 40 assertions and records Input
1514032 → 36235088, DOM 2995118 → 25465587 and view 728664 → 385294 bytes.
Whole-session retention remains unbounded.

Transition tracks now grow in a separate document-pool array. The DOM owner
header defines the snapshot storage so retirement releases both that array and
the outer state without depending on Radiant's track implementation (D4.5.1v4).
The pinning and retirement plateau regressions allocate actual dynamic tracks
and check their lifetime as well as the outer snapshot.

Handler execution previously enabled UI allocation for its entire duration, so
temporary elements constructed only to format HTML became fat nodes in the retained
Input arena. Allocation is now scoped: handlers use runtime GC ownership, and
template retransform enables the retained output arena only while producing its
render result (D4.1.1v2, D4.5.1v4). Nested scopes restore the prior context and
thread-local input owner. A four-callback probe constructs and formats 1,000 nested
elements per callback without inserting them. Previously Input grew by 1,056,000
bytes per callback; the changed release keeps it at 6,816 bytes across all four.
This is an Input-arena bound for that probe, not a bound on the whole document.
The new `lambda_handler_allocation` UI fixture passes its eight publication
assertions with forced GC and freed-memory poisoning. Its ninth assertion, a
retained detached wrapper’s old attribute after subsequent replacements, fails:
expected `value 1`, observed `value 3`. Earlier direct-run reports of nine forced-GC
passes were incorrect: the CLI ignores fixture `env`, while the baseline launcher
applies it. Explicit process environment variables reproduce the failure in both
the SVG-lazy and parser-work releases. A native DOM backing root fix is under
validation. Existing feature and frame-delivery fixtures pass with explicit forced GC.

The retained-wrapper failure comes from a plain GC element created by a handler
and then borrowed by a native DOM wrapper. After the model replaces the element,
the registry’s untraced backing pointer allowed GC to reclaim and reuse its
address. The registry now owns a stable precise Item root for managed backing
sources only (D4.1.1v2, D5.3.3). Attached and wrapper-pinned detached nodes retain
that root; retirement releases it. Runtime teardown releases all remaining roots
before destroying the heap. Parser/Input values remain outside GC (D4.1.3).
The root-only candidate passes all nine retained-wrapper assertions under explicit
forced GC and poisoning, plus a tenth assertion in an extra HTML serialization
probe. Three native tests pass: pinned survival/unpinned collection, 1,024
recycle/collect cycles with zero retained roots after each cycle, and idempotent
teardown. The full native ownership executable passes 155/155 with that candidate.

A separate native test then exposes a second ownership issue: the rooted source’s
content buffer moves during GC, while the copied DOM Element header still holds
its old pointer. The before-fix test fails with distinct source/borrower addresses.
The refresh fix follows D4.3.1’s single-owner compaction contract:
`dom_element_to_element()` reloads borrowed data/content pointers from the rooted
source on access. Per-buffer pointer identity detects when a DOM edit has installed
independent storage and ends that borrow. A node flag keeps ordinary Input-backed
access out of the registry path. No external native header participates in tracing
or receives collector fixups. Six native GC-backing cases now pass, including
content/attribute compaction and independent DOM buffers; the full ownership
executable passes 158/158. The permanent UI fixture passes 10/10 under explicit
forced GC and poisoning, including its independent-attribute mutation. All ten
focused fixtures pass under the same explicit stress environment. Ten package
scripts match expected outputs in both JIT and interpreter tiers; lint and diff
checks pass. Artifacts are `temp/slide/dom_backing_refresh_*`. Sequential full
baselines for this fix finished with the results in the table above: no new
allocation/UI failure remains. The historical parser-work round below records
the retained-wrapper failure before this fix.

The adoption audit reproduced three further failures: cross-runtime adoption
dropped the source heap's root, same-runtime adoption retained a stale borrowed
buffer after compaction, and document resource callbacks ran before backing roots
were withdrawn. Transfer now refreshes the native header before adopting it and
inherits the physical GC owner when registering the destination record. Returning
to an earlier owner also preserves that heap. Destruction withdraws roots before
resource callbacks, which can destroy adopted source documents and their heaps
(D4.5.1v4, D5.3.3). All three new cases pass; the ownership suite reached 161/161.

Three character-data cases then reproduced absent roots for detached GC-backed
text and comments. The shared registry now accepts a backing Item: a Text node
retains its String, and a Comment retains its source Element. Buffer refresh
applies only to copied Element headers; String object addresses and inline bytes
remain stable under D4.3.1/D4.3.2v2. Text mutation updates the retained value so
the old GC String becomes collectible. A fourth case verifies String adoption
between separate heaps and release at retirement. All 165 ownership cases pass.
The release candidate `temp/slide/lambda_release_character_backing` passes all
ten focused fixtures (66 assertions) with explicit forced collection and freed
memory poisoning, and all ten package scripts match expected outputs in both
JIT and interpreter modes. Radiant dimension lint and diff checks pass. Full
sequential baselines finished with the results in the table above: the same
three REPL and eleven UI failures remain, along with the context-menu and
HTTP-image mismatches; the previously observed LaTeX iframe navigation failure
recurred. All layout/render gates passed. No test expectation was weakened.
Artifacts are `temp/slide/*baseline_character_backing*` and
`temp/slide/character_backing_*`.

Published fragment storage remains an open ownership boundary (D4.1.3).
A private parser Input cannot be released merely when its DOM nodes retire:
MarkEditor inserts its values into other Mark trees, and scripts may retain raw
Mark values independently of DOM wrappers. Such a cohort would violate static
Input lifetime (D4.1.2). `fn_mutable_value` is also insufficient for an owned
transfer: it clones containers while borrowing types and scalar Strings. An
owned-GC transfer would need stable owned type/name metadata, owned text and
attributes, precise destination roots, and an audit of GC edges inserted into
non-managed parent trees (D4.1.1v2). Neither that transfer nor private-cohort
release is implemented. Parser work cleanup and native backing roots alone do
not establish a bound on repeated navigation.

The earlier two-round navigation audit passed four assertions but emitted two
`unknown map item type raw_pointer` diagnostics under forced GC and poisoning,
and ten focus-within diagnostics with or without stress. Passing UI assertions
were insufficient to close these diagnostic failures. The root-cause fixes and
latest replay are recorded below; the earlier storage observations remain in
`temp/slide/character_backing_navigation_*`.

A new 20-round navigation probe passes 40 assertions and records Input
942736 → 10873664, DOM 2995118 → 14765548 and view 728664 → 385294 bytes;
node storage stays at 496 bytes. Remaining fragment backing and document storage
still grow, so long-session acceptance is open.

HTML parser lifetime now separates work from published Mark data (D4.1.4v5).
Stacks, tokenizer buffers and error records use a parser-owned arena; stack
growth and adoption-agency insertion use that same arena. Parser destruction
releases the arena, builder headers and untransferred buffer storage, leaving
published strings and elements with their Input owner (D4.1.1v2, D4.1.3).
Full parses, DOM fragments and runtime fragment parsing use a shared scope guard.
The markup parser retains its fragment parser for continuation and destroys it on
reset/teardown. A foster-text merge also releases its temporary builder after
transferring or copying the published string.

The new native ownership test fails before the fix: 64 parser lifetimes grow
Input 704 → 794304 bytes and pool live storage 825 → 30009 bytes without publishing
any new nodes. After the fix, both return to their starting footprint. Continuation
and a 9,000-character attribute/RCDATA regression verify that published values
survive parser destruction, including a failed RCDATA end tag. All 55 HTML parser
cases pass. The changed release passes all focused fixtures, plus the feature and
frame-delivery fixtures under forced GC; ten package scripts match their expected
outputs in both tiers. Sequential full baselines have finished: the Lambda gate
retains its same three REPL prompt failures; Radiant includes the retained-wrapper
regression described above in addition to its previous failures.

A 20-round release navigation probe still passes 40 assertions and records Input
937088 → 10482544, DOM 2994662 → 14715868 and view 728664 → 385294 bytes; node
storage remains 496 bytes. Private parser work is bounded, but published fragment
backing remains retained. These results do not establish a whole-session bound.
Artifacts are `temp/slide/parser_work_*` and `profile_parser_work_navigation*`.

Release phase measurements now include paint, on Apple M4 / 16 GB / Mac16,10.
The 100-target mixed text/image/SVG fixture runs 60 explicit advance steps with
integer 16/17 ms intervals totaling 1,000 ms. Each step forces one paint. Existing
`--event-log` render statistics supply CPU paint record/replay/total times; the
frame profiler supplies cascade/layout and the combined Lambda/host turn. A
temporary copy of the Lambda package times old/new sampling and patch preparation
with synchronous host calls separately. It does not alter the shipped package.
The click and its initial input-drain callback are excluded; the 60 subsequent
callbacks are paired with the 60 explicit paints. Both viewport runs pass their
final-state assertion. Stdout and stderr are captured separately to preserve the
Lambda JSON timing records.

| Phase, p50 / p95 milliseconds | 1280 × 720 | 1920 × 1080 |
|---|---:|---:|
| Lambda old/new sampling | 5.774 / 14.241 | 5.916 / 7.686 |
| Lambda patch preparation + synchronous host writes | 4.174 / 7.134 | 4.268 / 4.934 |
| Cascade | 1.989 / 3.558 | 2.051 / 2.270 |
| Layout | 8.089 / 14.958 | 7.853 / 8.509 |
| Paint record | 23.577 / 49.711 | 23.436 / 24.964 |
| Paint replay | 2.806 / 11.558 | 3.112 / 3.432 |
| Paint total | 27.754 / 62.546 | 27.123 / 29.483 |
| Paired turn + paint total | 48.534 / 100.281 | 48.102 / 51.416 |

The operation counters identify 33 inline SVG paints taking about 23 ms of a
24 ms recording pass in the 1920 run. Each SVG indexed the whole host DOM and
parsed every node's inline CSS during style initialization. `SvgStyleEntry` now
parses its inline declarations once, when the shared entry lookup first queries
that node. The complete node index remains available for ancestors, custom
properties and references outside the current SVG; isolated image documents use
the same lookup. No retained cross-frame CSS cache is added (D4.5.1v4).

Sequential release comparisons with the handler-allocation build record:

| p50 / p95 milliseconds | 1280 before → after | 1920 before → after |
|---|---:|---:|
| Paint record | 23.963 / 25.373 → 5.739 / 6.205 | 24.290 / 25.913 → 5.924 / 6.390 |
| Inline SVG operations | 23.250 / 24.625 → 5.111 / 5.500 | 23.587 / 25.136 → 5.275 / 5.692 |
| Paint total | 28.042 / 29.966 → 9.352 / 10.341 | 28.685 / 31.027 → 10.462 / 11.193 |
| Paired turn + paint | 48.363 / 51.242 → 29.981 / 33.256 | 49.523 / 52.791 → 31.368 / 33.451 |

All four runs deliver the same 60 paired callback/paint samples and pass their
final-state assertion. Timing instrumentation and limitations match the method
above; the 16.67 ms target remains unmet. Artifacts are
`temp/slide/svg_lazy_{before,after}_{1280,1920}`. All focused fixtures pass with
the changed release. The native SVG paint gate passes 12 rows / 680 assertions
across layer-off/eager modes at 1×/2×. All 120 selected SVG UI fixtures pass,
including cascade mutations, inherited variables and use references. The render
reference gate also preserves all 211 recorded tests; it compares existing
reference captures rather than launching a fresh browser.
The browser
export gate cannot launch either default Chromium or bundled headless Chromium
in this sandbox: macOS denies its bootstrap port (1100). This prevents claiming
a fresh independent browser export comparison; the gate and goldens are unchanged.

The first phase table uses the transition-retirement release, before the handler
allocation scope change; the comparison table uses the two later builds stated
above. Host load is uncontrolled. Patch timing includes Lambda
formatting/query work and is not an exclusive native-commit measurement. Paint
total includes record/replay and surface work, but excludes window presentation
and event-loop overhead; the paired sum is not complete GUI latency. Lambda timing
output adds overhead to the combined turn. Individual phase quantiles must not be
summed to estimate a total quantile. Both runs exceed the chosen 16.67 ms target;
the result does not establish 60 Hz playback. Paint recording is the largest
measured phase. Artifacts are `temp/slide/profile_phases_separate_{1280,1920}`
logs, event JSONL, result JSON and aligned summaries.

The focus-event GC diagnostic was an unrooted `relatedTarget` wrapper while
native FocusEvent construction allocated its init record. The trusted-event
factory also left init/type/event values unrooted across allocating calls, and
the FocusEvent constructor did not retain its inputs and result while adding
`relatedTarget`. Each scope now uses precise `RootFrame`/`Rooted` ownership
(D5.3.3). A native test forces collection at every allocation, drops the caller's
related-target root, then verifies identity and a marker before and after another
collection. It fails before the fix and passes after; all 192 JS script cases pass.

Focus pruning uses the owned document root, while validation formerly walked
above it to the physical synthetic `#document` ancestor. Focus validation now
shares the existing owned-root selection with hover and active validation
(D4.5.1v4). Its native test fails before the fix and passes after, and also checks
that corrupt ancestry inside the owned root is still rejected. All 166 retained
view/ownership cases pass. The release candidate
`temp/slide/lambda_release_focus_boundary` replays the two-round navigation audit
under explicit forced GC and freed-memory poisoning: four assertions pass with
zero raw-pointer or focus ancestry diagnostics. This closes those two diagnostic
failures, not the independent navigation-storage acceptance.

The live player now retains the last successful ordinary sample in Lambda view
state and samples only the new frame. It discards that comparison on layer
replacement, Morph and failed commits. The procedure result is captured through
local assignments inside the statement handler (S7.6.7v4); assigning the handler
expression itself silently produced null and lost both cached success values and
host errors. A host-rejected raw CSS color now pauses and reports the rejection;
that regression fails all three assertions with the old expression handling and
passes after the correction. The existing multi-player fixture also checks
Previous, Restart and immediate build completion after Morph. Eleven focused
fixtures pass 74 assertions with explicit forced GC and poisoning. Ten package
scripts match their expected outputs in both execution tiers. The float/int-cast
lint and diff checks pass. Artifacts are `temp/slide/frame_cache_gc_*`,
`focus_boundary_*`, and `host_rejection_before*`.

A paired release comparison uses the same focus-boundary executable and separate
module copies before/after sample caching. Both viewports deliver 60 measured
callback/paint pairs and pass the final-state assertion. The initial click/drain
callback and first two paints are excluded as in the earlier method; idle time
emits no frame callbacks. The package is uninstrumented in this comparison, so
sampling and synchronous host work remain a combined measurement.

| p50 / p95 milliseconds | 1280 before → after | 1920 before → after |
|---|---:|---:|
| Script + synchronous host | 8.710 / 17.736 → 6.768 / 8.270 | 8.659 / 10.097 → 6.805 / 8.257 |
| Cascade | 1.847 / 3.556 → 1.890 / 2.109 | 1.847 / 1.947 → 1.934 / 2.215 |
| Layout | 6.924 / 7.528 → 7.163 / 8.865 | 6.937 / 7.294 → 7.229 / 7.879 |
| Paint total | 8.740 / 9.507 → 9.025 / 10.215 | 9.186 / 9.484 → 9.429 / 10.154 |
| Paired turn + paint | 26.340 / 38.594 → 25.262 / 28.501 | 26.780 / 28.266 → 25.258 / 27.510 |

Across the 60 sampled callbacks, changed Input grows by 32 bytes at the completion
boundary, node storage stays at 496 bytes, and DOM live storage grows about 42 KB
in both versions. Initial forced visual publication adds about 83 KB to the
changed version's warm footprint. These observations do not prove a long-session
bound; navigation's published-fragment growth remains open. Two request slots
are reused. Host load is uncontrolled, and paired turn plus paint still excludes
presentation/event-pump latency; 16.67 ms remains unmet. An initial cache trial
with incorrect statement-handler capture was rejected because its comparison was
always null and it repeated writes; only the corrected runs above describe the
shipped package. Artifacts are `temp/slide/frame_cache_{before,after}_{1280,1920}`
and `frame_cache_summary.json`.

After integrating merge `971779a27`, all 405 focused native cases pass, including
dynamic transition-track retirement. The full Lambda baseline passes 6,294/6,294.
The Radiant baseline records 4,028 passes, 350 partial passes and four failures:
the previously observed Tetris smoke and LaTeX iframe navigation cases, and two
loopback HTTP fixture setup failures. Both HTTP cases pass outside the sandbox
that denies loopback bind with EPERM. All layout, render, DOM UI and memory gates
pass. Logs are under `temp/merge-slide/`.

The Lambda adapter now compares sampled channels before formatting CSS rather
than formatting both complete property maps on every changed object. Static
projection still emits the complete map. Visibility and pointer routing share
the opacity/visibility threshold; a zero clip ignores direction changes. The new
delta regression checks that applying a patch equals a fresh static projection,
including hide/show, each transform channel, clip reset and clip direction.
All eleven package fixtures match their expected outputs in JIT and interpreter
tiers (22 runs), and eleven focused UI fixtures pass 74 assertions under explicit
forced GC and freed-memory poisoning. No native mechanism changes in this step.

Paired release measurements use the same merged executable on the Apple M4
machine and the existing 100-target mixed text/image/SVG deck. Each row has
60 aligned callback/paint samples, excluding the first callback and two initial
paints as above. The same two frame-request slots are reused; idle waits emit no
callbacks.

| p50 / p95 milliseconds | 1280 before → after | 1920 before → after |
|---|---:|---:|
| Script + synchronous host | 4.983 / 5.422 → 4.745 / 5.201 | 4.909 / 5.339 → 4.690 / 6.951 |
| Layout | 7.659 / 7.913 → 7.692 / 8.339 | 7.603 / 7.898 → 7.661 / 8.066 |
| Paint total | 9.329 / 9.720 → 9.360 / 10.024 | 9.744 / 10.101 → 9.820 / 10.122 |
| Paired turn + paint | 23.831 / 25.543 → 23.693 / 27.139 | 24.140 / 25.269 → 23.910 / 26.627 |

The median script reduction is modest; the tail does not improve consistently.
Host load is uncontrolled, and the earlier measurement limitations still apply.
The result does not establish 60 Hz playback. Input grows 32 bytes at completion
and DOM grows about 42 KB in both versions, but merged view-pool usage grows
about 215 KB during these 60 callbacks. Artifacts are
`temp/slide/property_delta_{before,after}_{1280,1920}` and
`property_delta_summary.json`.

A separate 60-callback reproducer changes only opacity on 100 text wrappers.
Using `font-family:Arial` grows view-pool live bytes by 48,648; using
`Arial,sans-serif` grows them by 154,848. The 106,200-byte difference is exactly
59 turns × 100 names × 18 bytes for the joined family string. Inspection finds
`css_join_font_family_parts` allocating in the view pool while
`radiant_retain_font_family` stores a borrowed pointer and font teardown releases
no family storage. This identifies one contributor, not all growth. A fix must
preserve inherited borrowers and secondary-view ownership (D4.5.1v4); freeing
the pointer indiscriminately would be invalid. The temporary reproducer is
`temp/slide/family_growth_{single,list}.{ls,json,log}`.

The merged release also passes 40 assertions over 20 navigation rounds and 120
callbacks, while Input grows 936,864 → 10,889,024 bytes and DOM grows
3,165,518 → 28,935,841 bytes. Node arena usage stays at 496 bytes and two request
slots are reused. This does not satisfy the navigation retention bound; the
published-fragment ownership audit remains necessary (D4.1.3, D4.1.4v5).
Artifacts are `temp/slide/merged_navigation.{log,result.json}`.
The 20-round repeat under forced collection and freed-memory poisoning also
passes 40 assertions and 120 callbacks, with no raw-pointer or inconsistent
focus-ancestry diagnostics. Input grows 936,864 → 7,801,088 bytes and DOM grows
3,165,518 → 8,221,595 bytes. View-pool usage reaches the same 1,154,374 bytes as
the ordinary run, so GC does not reclaim that native growth. Neither repeat
establishes a retention bound. Artifacts are `merged_navigation_gc.log`, its
result JSON and `merged_navigation_gc_summary.json` under `temp/slide/`.

## Pre-acceptance plan (completed)

*Items 1–4 were completed in the acceptance round at the top of this report; item 5 is that section.*

1. **Repair computed font-family ownership.** Audit `css_join_font_family_parts`,
   family selection and shorthand paths, `FontProp` copies, inherited values,
   pseudo/placeholder fonts, intrinsic-sizing temporaries and secondary views.
   Computed strings need an explicit lifetime shared by valid borrowers;
   authored/static names remain borrowed. Handle replacement and full font
   teardown have different responsibilities. Add a native regression covering
   repeated resolution and borrowers surviving parent restyle/view retirement.
   Rerun both reduced opacity probes and account for the remaining single-family
   growth before claiming view retention is bounded (D4.5.1v4, D4.1.4v5).
2. **Bound published-fragment retention.** Trace Input, Mark and DOM ownership
   across repeated scene replacement. DOM retirement alone cannot authorize
   releasing a fragment that escaped into another Mark parent or script value.
   A move to GC-owned values needs owned strings/type metadata and an audit of
   non-managed parents that hide managed children from tracing. Ordinary arena
   blocks cannot be individually discarded (D4.1.1v2, D4.1.2, D4.1.3,
   D4.1.4v5, D5.3.3). Repeat normal and forced-GC/poison navigation; require a
   sustained plateau in live owners after warm-up, not just passing assertions.
3. **Reduce measured frame cost.** Investigate
   `post_html_handler_incremental_rebuild`, which still invokes layout after
   style mutations. The mutation journal currently lacks property IDs; repaint
   classification alone does not establish that layout can be skipped.
   Visibility collapse, transforms, containing blocks and scroll overflow need
   correct handling. Reuse existing cascade/paint helpers and verify transformed
   bounds, clipping, retained paint and hit testing. Timing and effect policy
   remain in Lambda (D7.5.3). Repeat aligned release callback/paint measurements
   at 1280 × 720 and 1920 × 1080; the 16.67 ms gate remains open until measured.
4. **Revalidate changed ownership and invalidation paths.** Run meaningful
   native regressions, all package scripts in both execution tiers, and focused
   live/pixel fixtures with explicit forced GC and poisoning. Inspect diagnostics
   as well as assertions. Run lint and required full baselines sequentially after
   native changes. Investigate regressions attributable to the implementation;
   retain explicit results for the existing Tetris/LaTeX failures and HTTP setup
   constraints rather than treating them as passes.
5. **Close the proposal with evidence.** Map results to milestones 13.1–13.5,
   document timing method and retention bounds, update the public API if behavior
   changes, and mark the implementation complete only when required acceptance
   work is fulfilled. Existing focus-event rooting and owned-root ancestry fixes
   are delivered; storage retention and performance remain independent open gates.

The package excludes arbitrary masks, SVG path deformation, text reshaping,
automatic paragraph measurement, PPTX and new PDF pagination. Morph requires
explicit IDs on flat compatible participants. These are documented scope limits.
