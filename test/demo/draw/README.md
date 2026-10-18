# Draw

A native Lambda/Radiant diagram editor inspired by draw.io. Open it from the
repository root:

```sh
make build
./lambda.exe demo draw
# Or open the source directly:
./lambda.exe view test/demo/draw/draw.ls
```

The starter flowchart is editable. Choose a shape in the library, then click
the page to place it. The twelve shapes are Process, Start / End, Decision,
Rounded, Data (parallelogram), Manual input (trapezoid), Preparation (hexagon),
Triangle, Database, Document, Note and Text. Drag a shape to move it;
drag its bottom-right selection handle to resize. Use the circular handle above
the shape to rotate it (hold Shift for 15° increments), or enter an angle in
the inspector. Rotate 90°, Flip H and Flip V work on selected shapes; text
rotates without being mirrored. Snap & grid toggles the
10-pixel placement grid and its visual guide.

Choose Connector (**C**, the bent-arrow icon), then drag between shapes or
empty points. A live line follows the pointer; release over a shape to attach
it, or on empty space for a free endpoint. Escape cancels the draft. You can
also click a source shape and then a destination shape. Each new connector
creates one undo item. The four-way arrow is Pan (**H**).
Arrows stay
attached when either shape moves or resizes. Select a connector to edit its
label, source/destination shapes, attachment sides, and straight, orthogonal
or curved routing. Both ends independently offer no arrowhead, triangle,
open arrow, diamond, circle or bar. Both arrowhead pickers show the actual
connector graphics, with keyboard navigation and accessible names. Drag a selected connector's blue endpoint
anywhere on the page to move it freely, or drop it onto another shape to attach.
The inspector also offers Free endpoint. Green circles with a white × mark
connected endpoints and confirm a valid target as you drag onto its boundary.
Drag any line segment, or its cyan handle, to move that segment; orthogonal
segments slide perpendicular to the line while attachments stay fixed. Terminal
segments gain short connecting stubs. The gold handle still sets a single bend.
A floating three-icon toolbar configures the start arrowhead, end arrowhead,
and routing (click the elbow icon to cycle orthogonal, straight, curved).
Reset bend clears segment edits and restores automatic routing. Reverse swaps the source and destination
attachments while retaining start/end styles, reversing the arrow direction.
The label position runs from 0 (source) to 1
(destination). Deleting a shape also
deletes its attached connectors; Undo restores the entire edit.

The inspector edits shape type, custom hex colors, geometry and stacking
order. Shapes and connectors support solid, dashed, dotted, dash-dot or
hidden outlines, 0–12 pixel weights, and opacity. Shape fills support solid,
diagonal hatch, crosshatch, dots, grid and horizontal lines, with independent
background and pattern colors. Use `none` for a transparent background.
Text has size, bold weight, alignment and color controls.

Inspector fields apply immediately. Typing previews valid values while preserving
incomplete drafts; leaving a field commits one undo item. Invalid final values
return to the last valid value. There is no Apply button. Copy style / Paste style
transfers appearance without changing geometry, labels or attachments;
between shapes and connectors it transfers common line/text properties.
The object picker selects thin connectors or covered shapes directly.
Drag an empty part of the canvas to enclose objects with a selection rectangle.
The Rectangle select tool (M) also starts a rectangle over an existing object.
Shift-click toggles objects; Shift-drag adds enclosed objects. Ctrl/Cmd + A
selects the entire page. Drag, nudge, delete and duplicate act on the selection.
Duplicating shapes includes their internal connectors and reconnects the copies.
The inspector offers left, center, right, top, middle and bottom alignment for
multiple shapes, plus horizontal/vertical distribution for three or more. Each
operation is one undo item, and alignment respects rotated bounds.
Duplicate offsets the selection or adds a bend to a lone copied connector. New blank
diagram, Load example and Style showcase are undoable. The showcase presents
all twelve shapes with patterns, outline styles and arrowheads.

The shape library and inspector scroll independently in short windows, with
visible scrollbars when their contents overflow. The canvas has horizontal
and vertical scrollbars for navigating the page, including at high zoom.
The page fits and centers automatically on startup. Fit page recenters the
diagram; scrolling and panning do not add undo items.

| Shortcut | Action |
| --- | --- |
| V / M / C / H | Select / Rectangle select / Connect / Pan |
| Ctrl/Cmd + A | Select all objects |
| Arrow keys / Shift + arrows | Move the selection by 1 / 10 pixels |
| Delete / Backspace | Delete the selection |
| Ctrl/Cmd + D | Duplicate the selection |
| Ctrl/Cmd + Z | Undo |
| Ctrl/Cmd + Shift + Z, Ctrl/Cmd + Y | Redo |
| Ctrl/Cmd + S | Save dialog |
| Ctrl/Cmd + Shift + C / V | Copy / paste style |
| + / −, Ctrl/Cmd + mouse wheel | Zoom; the wheel keeps the pointer's diagram point fixed |
| Mouse wheel / trackpad, scrollbars, Pan tool | Pan the page |
| Escape | Cancel a drag, placement, connection or file dialog |

Shortcuts leave native text controls alone. File dialogs keep Tab navigation
inside the dialog and return focus to their opening button when closed.
Touch pointer drags use the same move/resize path as a mouse.

Save writes an editable version-4 JSON file; Open validates the demo profile and restores it,
including styles and segment waypoints. Version-1, version-2 and version-3 files remain readable
and acquire defaults for styles, free endpoints and orientation.
Export SVG writes a standalone vector image without the grid or selection
handles. Paths are editable; defaults are `temp/draw/diagram.json` and
`temp/draw/diagram.svg`. Parent directories for a custom path must already
exist. Writes use the runtime's atomic output option. Changes stay in memory
until saved; closing the window does not autosave.

## Implementation

`draw.ls` owns event procedures and session state. `mod_scene.ls` produces the
HTML shell and SVG; `mod_present.ls` updates retained controls and the page.
Page sizing publishes first; a requested frame applies the camera against the
committed layout. Reading bounds immediately after sizing still exposes the
previous layout, so startup scrolling must wait for that commit (**D7.4.6,
S12.1.3**).
Connector previews retain the page, shapes, other edges and inspector. They update
only the active edge, selection overlay and endpoint fields. Target contours and
the page transform come from the press snapshot; committing refreshes the full
inspector and records one edit. The shell and pointer-capture owner stay stable while SVG content changes,
following **S12.1.3** (pure views, mutations in handlers) and **D4.5.1v4**
(document-owned native resources). Reusable model, shape, paint, SVG scene, geometry, routing, commands, and gesture
code now lives under `lmd/package/editor/draw/`, exposed through
[`lambda.editor.draw`](../../../lmd/package/editor/draw/README.md). The demo
retains its examples, application shell, inspector, file actions, and theme; its
old helper modules forward to the package. The model uses immutable document values
(**S1.4**) and reuses `lambda.editor` drawing commands, transactions, history,
geometry and connector routing. `mod_shapes.ls` shares contour geometry between
painting, hit tests and boundary attachment; `mod_paint.ls` supplies SVG
patterns, marker definitions, line styles and text. Each completed drag records one transaction;
Escape restores the pre-drag document. File restore validates geometry,
unique identities, supported styles and connector endpoints before replacing
the current document.

The demo has one 1120 × 760 page and up to 200 objects. Multi-selection is
transient; it does not create persistent nested groups. Automatic routing uses the existing endpoint-box router;
manual bends and explicit attachment sides are user controlled. Routing does
not avoid unrelated shapes. Persistent groups, freehand paths, images,
collaboration, and draw.io file compatibility are outside this demo's scope.

## Verification

```sh
make -C build/premake config=debug_native test_lambda_gtest
./test/test_lambda_gtest.exe --gtest_filter='*tests_draw_*'
./test/test_ui_automation_gtest.exe --suite draw --jobs 1
```

Repeat with `LAMBDA_EXEC_BACKEND=interp` and `LAMBDA_EXEC_BACKEND=jit` to
exercise all execution tiers (**D8.1.1v18**). The golden tests live in
`tests/draw_model.ls`, `tests/draw_styles.ls` and `tests/draw_selection.ls`,
each with its expected `.txt`; native replays are
in `test/ui/draw/` and are registered in the Radiant baseline manifest.
They cover moving/resizing, connectors and deletion, inspector edits, undo,
touch, cancellation, keyboard focus, file round trips, SVG export, custom
styles, patterns, arrowheads, free endpoints, attachment/bend editing,
marquee selection, group alignment/duplication, transformed geometry, live
inspector updates, independent panel scrolling, canvas scrollbar dragging,
selection immediately after scrolling, and page overflow at high zoom.
Replay screenshots and generated files go under `temp/draw/`.

Measure connector pointer-handler time with an optimized release host:

```sh
make build-release-profile
python3 test/demo/draw/benchmark.py --binary lambda-profile.exe
```

The benchmark replays the same endpoint drag on 9- and 127-object pages. It
instruments temporary script copies under `temp/draw/performance/`, measures
handler time (excluding rendering after the handler), and accepts
`--reference-dir` plus `--reference-home` for a saved demo/package comparison.
