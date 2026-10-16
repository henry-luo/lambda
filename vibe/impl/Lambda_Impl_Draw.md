# Lambda Drawing Package — Native Implementation

**Date:** 2026-10-16

**Status:** native vertex–edge core and reusable canvas implemented; focused
qualification passes and aggregate gates remain failing. This record covers the finite initial profile,
not full maxGraph parity.

**Design:** [drawing proposal](../Lambda_Pkg_Draw.md).
**Public API:** [package reference](../../lmd/package/editor/draw/README.md).
**Spec linkage:** **D7.2.4v3, D7.2.6** for source/package ownership;
**S1.4, S12.1.1v3, S12.1.3** for values, effects, and handlers;
**D4.5.1v4–D4.5.3v2** for retained native presentation;
**D8.1.1v18** for tier qualification. No formal ruling changed.

## Delivered behavior

Connector interaction milestone (2026-10-16): show connection crosses at bound
endpoints and valid drop targets; drag every orthogonal/straight route segment
without moving its attached endpoints; offer start/end arrowhead and routing
controls beside a selected connector. Preserve cancel, one-step undo, JSON
round trips, zoom and pointer capture. The application currently republishes
the complete SVG and inspector for each pointer move; measure that path with
an isolated release host, then retain unaffected drawing nodes during connector
previews (**S12.1.3, D4.5.1v4–D4.5.3v2**). Qualification budget: focused route
goldens in interpreter/auto/JIT, Draw UI replays, before/after release drag
measurements and the existing aggregate gates. Custom cubic path control-point
editing remains outside this polyline-segment milestone.

The public `lambda.editor.draw` facade operates on the shared editor's plain-map
node tree. Vertices retain `shape` storage and edges retain `connector` storage;
ID/topology queries derive from that document. `open_session` is the actual
session constructor because `open` is a reserved keyword.

Custom serializable polygon descriptors supply normalized contours and named
ports. Built-in contours, custom contours, transforms, SVG paint, picking, and
boundary attachment share the promoted geometry path. Named ports follow
movement, resizing, rotation, and flips; input/output direction is validated.
Edges support free/boundary/named-port endpoints, waypoints, line/cubic custom
paths, loop policy, distinct parallel routes, labels, and independent arrowheads.
Custom endpoint controls follow their attachments, and reverse/duplicate edits
retain path and topology consistency.

Model commands validate the prospective drawing before returning a shared
transaction. Standalone sessions use the existing editor history; embedded
calls use `transaction(root, {drawing_path, selection}, command)` and leave
history ownership with the host. Begin/preview/commit/cancel interaction groups
one gesture into one history entry; previews and cancellation preserve history.

Version-4 archives preserve custom geometry, ports, routes, metadata, and page
settings. The codec accepts archive values directly or after JSON parsing,
reads demo versions 1–3, and refuses lossy legacy-v3 encoding. The Draw
application now writes v4 to preserve segment edits, reads versions 1–3, and
validates its fixed page and supported inspector objects before opening. The
legacy `model.archive`/`restore` helpers retain their version-3 contract.

`draw.view.surface` retains its capture/scroll owner while publishing inner SVG
paint. Each surface has an explicit unique ID prefix for controls, markers,
patterns, overlays, and presentation objects. Its basic toolbar, palette,
label/orientation inspector, selection, gestures, keyboard commands, native
scrolling, and zoom buttons work independently in two canvases containing the
same document IDs. A native screenshot was inspected for that fixture.

## Migration and ownership

- Promoted the old shared drawing geometry, router, commands, and schema into
  `lmd/package/editor/draw`. Their former module paths explicitly forward every
  existing public export.
- Moved reusable demo model, shape, paint, and SVG scene code into that package.
  Demo wrappers keep the starter/showcase data and initial application copy;
  the demo keeps its branded shell, full inspector, CSS, file dialogs, and I/O.
- Extracted pure gesture transitions and viewport calculations. Demo native
  geometry reads now have procedural signatures, matching **S12.1.1v3**.
- `mod_doc_schema` composes the legacy drawing schema from its new owner;
  `draw.schema.draw_schema` separately describes the normalized profile.
- Kept generic document/Step/Transaction/history modules in their existing
  shared location. No vendor, parser, C++ engine, or build-configuration change
  was needed for the package itself.
- Kept SVG source-editing adapters in `lambda.edit`: their source-preserving
  tree operations and round-trip rules differ from semantic vertex/edge edits.
  Their legacy geometry imports continue to resolve through the forwarding
  modules; no forced source conversion was introduced.

## Finite milestones and verification budget

| Milestone | Delivered outcome | Focused evidence |
|---|---|---|
| A — Extraction | Shared package ownership with legacy imports and demo behavior preserved | Existing drawing goldens, embedded drawing-block integration, full native Draw suite |
| B — Vertex/edge core | Custom declarative vertices, topology, ports, richer routes, transactions, archive v4 | Four package goldens for core behavior, routes, interaction, and codecs |
| C — Reusable native canvas | Owner-scoped paint/control IDs and independent canvas state | `test/ui/draw/package_canvases.json`, including label editing and undo isolation |

The edit loop used focused golden and native checks. Aggregate qualification
runs once at this feature boundary; repeat only for a relevant change or a
failure requiring diagnosis. Artifacts and logs stay under `./temp/`.

## Qualification

- Before extraction: seven drawing goldens passed; all eleven original native
  Draw fixtures passed.
- After implementation: twelve package/legacy/embedded golden cases passed on
  interpreter, automatic tiering, and MIR JIT. The four new package scripts each
  have a checked-in expected `.txt`.
- Full Draw suite: all twelve native fixtures passed, including the new
  two-canvas fixture. The harness also ran its three scheduler checks.
- The two-canvas fixture passed separately under interpreter, automatic
  tiering, and MIR JIT.
- Every original public export from the migrated legacy/demo modules remains
  present; whitespace, golden pairing, and documentation links were checked.
- Adjacent SVG source editor: all five `edit_edit_svg_*` native fixtures passed,
  covering move/save, resize/style, text, zoom/picking, and create/undo.
- `make test-lambda-baseline` completed with failures. Its later script batches
  were invalidated when another build removed/replaced the shared `lambda.exe`;
  the log records missing executables and missing batch results. JS satellite,
  profiling, optimizer, and three MIR-GC cases also reported failures.
- A copied executable plus an isolated launch directory linked to the original
  package/fixture/corpus directories was then used to rerun all 1,541 discovered
  script goldens. 1,539 passed; `binary_js_bridge` mismatched and the LaTeX
  phase-3 corpus timed out. On an individual rerun, the LaTeX corpus passed;
  the binary/JS bridge mismatch persisted. The isolated JS satellite probe also
  reproduced independently of the drawing package; the profiling probe passed
  when rerun on the stable executable.
- `make test-radiant-baseline` built its targets, but its sandboxed native phase
  encountered AppKit/XPC graphics-service failures and idle timeouts. A failing
  Scene3D case passed immediately with graphics-service access, so that invalid
  run was interrupted and the already-built aggregate was rerun through
  `make run-radiant-baseline` with graphics access and the copied executable.
- The graphics-enabled Radiant aggregate remains failing in unrelated UI,
  view-reuse, CSS-syntax, layout/render, and input-event qualification. It passed
  all 205 Radiant View cases and all 28 Scene3D cases. No drawing/SVG-editor
  failure appeared in its failure inventory. The complete Draw suite and five
  adjacent SVG-source fixtures also passed separately on the stable executable.

Artifacts: `temp/draw-final-{interp,auto,jit}.log`,
`temp/draw-ui-final.log`, `temp/draw-svg-source-qualified.log`,
`temp/draw-canvas-qualified-{interp,jit}.log`,
`temp/draw-script-qualified.json`, `temp/draw-independent-rerun.log`,
`temp/draw-radiant-qualified.log`, and `temp/draw/package-canvases.png`.

These aggregate failures remain qualification limits; no unrelated runtime,
layout, test harness, golden, or vendor code was changed to make the gates pass.

## Connector editing and drag performance

The connector milestone is complete. Selected endpoints show green circles
with white × marks, including live boundary attachment within six screen
pixels of a target contour. Invalid self-attachment does not advertise a valid
target. Every straight/orthogonal route segment can be dragged directly or by
its cyan handle; terminal segments insert stubs and keep both attached endpoints
fixed. Rounded routing edits the same orthogonal skeleton. Waypoints survive
reverse, undo/redo and version-4 JSON round trips. Reset bend clears waypoints;
Escape restores the press snapshot without adding history.

The selected connector's floating toolbar contains start arrowhead, end
arrowhead and routing controls. Arrowhead buttons reuse the existing picker,
keyboard navigation and return-focus behavior. The routing button cycles
orthogonal, straight and curved. The toolbar follows native scrolling, hides
during gestures and excludes multi-selection and file dialogs.

Profiling isolated two costs: whole-scene SVG/inspector publication on every
pointer move, and rebuilding every target contour during endpoint picking.
Connector previews now update only the changed edge's children, selection
overlay and four endpoint fields. Static shapes, unrelated edges, SVG definitions
and the native capture owner retain identity (**D4.5.1v4–D4.5.3v2**). Target
contours come from the immutable press document (**S1.4**). The cached page
transform adjusts for native scroll positions and refreshes on resize. Selected
handle picking also avoids a full-scene hit test on press. These changes stay in
package/demo Lambda code; no C++ engine or vendor fix was needed.

An isolated `make build-release-profile` host was copied before testing to avoid
concurrent replacement of `lambda.exe`. `test/demo/draw/benchmark.py` measured
three alternating before/after runs of an identical endpoint replay. Values
below are medians of the three per-run medians; p95 values are medians of the
per-run p95s. They measure pointer-handler time, excluding subsequent rendering.

| Page | Before | After | Before p95 | After p95 |
|---|---:|---:|---:|---:|
| Starter, 9 objects | 10.181 ms | 3.059 ms | 12.544 ms | 5.200 ms |
| Dense, 127 objects | 193.620 ms | 5.933 ms | 217.366 ms | 8.604 ms |

Qualification (**D8.1.1v18**): all 14 focused package/legacy/embedded golden
cases and all 13 Draw UI fixtures pass in interpreter, automatic and JIT modes.
The new `draw_segments.ls`/`.txt` covers terminal/internal/diagonal moves, fixed
endpoints, no-op/zoom/scroll gestures, cancel, one-step history, codecs and clean
export. The new demo `draw_archive.ls`/`.txt` checks v4 waypoint round trips,
legacy reading and rejection of unsupported demo profiles. `segments.json`
passes 35 assertions for the three-icon toolbar, keyboard focus, live crosses,
segment editing, retained unrelated node identity, save/reopen, undo/cancel,
popup positioning and scrolling during capture. Screenshots were inspected.

The graphics-enabled `make run-radiant-baseline` completed with 5,641 passing,
183 failing, 158 partially passing and one skipped result. All 205 Radiant View,
28 Scene3D, 160 DOM UI and 51 page-snapshot checks passed. Failures remain in
view reuse, unrelated UI, CSS Syntax, input-event timeout and layout/render
qualification; no gate or expected output was altered to hide them. A preceding
sandboxed attempt stalled in macOS graphics initialization and was interrupted
before rerunning with graphics-service access. The earlier Lambda aggregate
limits above remain; this milestone changes package/demo scripts only.

Artifacts: `temp/draw-enhance/final-goldens-{interp,auto,jit}.log`,
`temp/draw-enhance/final-draw-ui-{interp,auto,jit}.log`,
`temp/draw-enhance/comparison/results.json`,
`temp/draw-enhance/radiant-qualified.log`,
`temp/draw/connection-cross.png` and `temp/draw/connector-menu.png`.

## Startup centering

Startup-centering follow-up: the page must fit and center before the first
canvas interaction at normal, short and large window sizes, preserving pan,
zoom, Fit and gesture history. The startup repro showed `canvas-space` still
measuring the previous layout (height zero) after its size declarations changed.
The same callback clamped both intended scroll offsets to zero, leaving the
page at the half-viewport spacer origin. DOM geometry reads expose committed
state; treating them as a synchronous layout flush was the application defect
(**D7.4.6, S12.1.3**).

`mod_present.sync` now publishes page dimensions and requests `draw_camera`.
That frame calls `apply_camera` after layout, applies the intended pan/centering,
repositions the connector toolbar and refreshes an active connector's cached
transform. Resize uses that same commit boundary. No delay, retry loop, engine
change or hard-coded scroll offset was introduced.

The new `test/ui/draw/startup.json` asserts fitted, centered geometry before any
click and confirms unchanged history. Its assertion failed on the unmodified
application and passed after the fix. Startup passed at 1280 × 820, 1135 × 550
and 1600 × 1000; the centered screenshot was inspected. All 14 Draw fixtures
pass on the current host. Focused interpreter/JIT checks cover startup,
scrollbars, pan/Fit/resize and segment capture. Evidence is under
`temp/draw-startup/`; screenshot: `temp/draw/startup-centered.png`. This follow-up
changes the demo's camera publication only, so the focused UI suite is the
verification budget; earlier aggregate qualification limits remain unchanged.

## Connector-tool drawing gestures

Connector-tool gesture milestone: dragging from a shape or empty page point
must show a live line, attach to valid target shapes, and commit one history
entry on release. Keep the existing source-click/destination-click workflow,
free endpoints, mouse/touch capture, Escape/pointer cancellation and object
limits. The original tool returned from pointerdown after choosing a source,
without establishing a drag; pointermove therefore cannot produce a preview.
Implement the gesture transition in the shared package, reuse the existing
endpoint editor and transaction path, and retain the demo's incremental paint
path (**S1.4, S12.1.3, D4.5.3v2**). Verification budget: focused creation goldens
in all tiers, native creation/cancel replays, full Draw UI and adjacent drawing
goldens. This milestone exposed a native state prerequisite, described below.

The demo and reusable canvas now use the same connection begin/preview/commit
transitions. A draft has a collision-free ID and source endpoint; pointer moves
reuse the endpoint editor for a free or attached destination. Four screen pixels
distinguish a drag from click jitter. Releasing a valid line commits once and
selects it; a zero-length line or disallowed self-attachment leaves history
unchanged. Two separate shape clicks still work. Escape and pointer cancellation
restore the document, selection and ID allocation, and release capture.

The demo's first preview installs an SVG draft edge and markers in a dedicated
layer. Subsequent moves reuse the retained edge/selection update path; they
preserve the root SVG and the draft edge identity. Commit/cancel republishes
the normal scene. Standalone SVG excludes the draft layer. Help and tooltips
identify Connector as the bent-arrow icon (**C**) and Pan as the four-way arrow
(**H**). Object-picker markup now uses bare XML option fragments, avoiding the
document wrappers produced by HTML formatting, and compares live select values
before updating their selection.

The creation replay also exposed a hidden-select bug. The DOM's selectedness
changed correctly while the inspector was hidden, but
`form_control_set_selected_index` rejected a node without a layout block. An
existing native form-state cache therefore kept the old painted index even
after revealing the inspector. The setter now updates retained form state
without requiring a layout box; it also preserves that update after layout
releases `FormControlProp` (**D4.5.1v4**). DOM setters normalize the index before
the cache update when no layout-owned option count exists. The new native test
failed before the fix and passes with both retained and released form properties.
Select layout initialization now uses the retained selection getter, which reads
DOM defaults only for a new state owner. Rebuilding a layout property must not
reset an uncontrolled user's choice. DTNA pagination explicitly synchronizes
its page-size selector on render so a controlled request that the parent rejects
returns to the accepted value (**S8.2.2v5, D7.2.5**). Existing control/navigation
replays exposed and verify both cases. No vendor code or formal ruling changed.

Qualification: all 15 focused golden cases and all 15 Draw UI fixtures pass in
interpreter, auto and JIT modes (**D8.1.1v18**). The new `draw_connect.ls`/`.txt`
checks attached/free drawing, previews, one-step undo, cancel, click jitter and
two-click compatibility, invalid/zero-length paths, capacity, unique IDs, archive
round trips and zoom. `create_connector.json` passes 40 assertions for live
drawing, retained SVG identity, attachment crosses, mouse/touch, history,
undo/redo, Escape and inspector values. The expanded two-canvas replay passes
21 assertions, including connection creation/undo without changing the other
canvas, in all three tiers. Screenshots were inspected. All 25 state-store
tests and the Radiant integer-cast lint rule pass. Controlled, uncontrolled and
pagination dropdown checks pass in all three tiers. A concurrent DTNA run had a
single control-process abort without a machine result; its isolated three-tier
replay passed. The rebuilt graphics-enabled Radiant aggregate still reports
existing view-reuse, CSS syntax, layout and render failures, plus the existing
WPT input-event timeout. It also caught the select initialization regression;
the retained-state and controlled-pagination fixes above resolve it.
The final UI baseline rerun passes 593 of 644 fixtures (plus three scheduler
tests); its 51 failing fixture names exactly match the previously recorded
baseline, with no additional failures.
The rebuilt main `lambda.exe` also passes the complete creation replay (40
assertions), independently of the optimized qualification snapshot.
All 29 DTNA source-contract tests also pass, including the contract replay
across interpreter, auto and JIT modes.

Artifacts: `temp/draw-connect/{goldens,ui,canvases}-{interp,auto,jit}.log`,
`temp/draw-connect/state-before.log`, `temp/draw-connect/state-qualified.log`,
`temp/draw-connect/create-final.json`, `temp/draw-connect/radiant-baseline.log`
and `temp/draw-connect/{free-preview,attached-preview,created}.png`.
Final-build checks: `temp/draw-connect/ui-qualified-{interp,auto,jit}.log`,
`temp/draw-connect/controls-qualified-{interp,auto,jit}.log`,
`temp/draw-connect/ui-baseline-final.log` and
`temp/draw-connect/lint-qualified.log`. Source contracts:
`temp/draw-connect/dtna-source-qualified.log`; main-app replay:
`temp/draw-connect/app-created.json`.

## Remaining feature extensions

The initial profile supports one page and one editable layer, plain multiline
labels, declarative custom polygons, and structured line/cubic edges. Rich
vertex content, executable descriptor/router registries, persistent groups,
obstacle avoidance, automatic graph layout, collaboration, arbitrary SVG path
syntax, and mxGraph/draw.io XML interchange remain separate milestones.

The new canvas exposes basic controls; the existing demo retains its fuller
style inspector, wheel/pan navigation, file dialogs, and accessibility behavior.
The package preserves the legacy schema rather than silently normalizing its
richer group/text-frame vocabulary. Custom path sampling reuses the existing
bounded motion table rather than claiming exact analytic curve picking at every
scale. These support boundaries are documented in the package reference.
