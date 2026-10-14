# Draw

A native Lambda/Radiant diagram editor inspired by draw.io. Open it from the
repository root:

```sh
make build
./lambda.exe demo draw
# Or open the source directly:
./lambda.exe view test/demo/draw/draw.ls
```

The starter flowchart is editable. Choose a Process, Start / End, or Decision
shape in the library, then click the page to place it. Drag a shape to move it;
drag its bottom-right selection handle to resize. Snap & grid toggles the
10-pixel placement grid and its visual guide.

Choose Connect, then click a source and a destination shape. Arrows stay
attached when either shape moves or resizes. Select a connector to change its
label or switch between elbow and straight routing. Deleting a shape also
deletes its attached connectors; Undo restores the entire edit.

The inspector edits labels, palette colors, geometry and stacking order.
Apply changes commits the label and geometry together. Duplicate offsets a
copy of the selected shape. New blank diagram and Load example are undoable.

| Shortcut | Action |
| --- | --- |
| V / C / H | Select / Connect / Pan |
| Arrow keys / Shift + arrows | Move the selected shape by 1 / 10 pixels |
| Delete / Backspace | Delete the selection |
| Ctrl/Cmd + D | Duplicate the selected shape |
| Ctrl/Cmd + Z | Undo |
| Ctrl/Cmd + Shift + Z, Ctrl/Cmd + Y | Redo |
| Ctrl/Cmd + S | Save dialog |
| + / −, mouse wheel | Zoom; the wheel keeps the pointer's diagram point fixed |
| Shift + drag, Pan tool | Pan the page |
| Escape | Cancel a drag, placement, connection or file dialog |

Shortcuts leave native text controls alone. File dialogs keep Tab navigation
inside the dialog and return focus to their opening button when closed.
Touch pointer drags use the same move/resize path as a mouse.

Save writes an editable, versioned JSON file; Open validates and restores it.
Export SVG writes a standalone vector image without the grid or selection
handles. Paths are editable; defaults are `temp/draw/diagram.json` and
`temp/draw/diagram.svg`. Parent directories for a custom path must already
exist. Writes use the runtime's atomic output option. Changes stay in memory
until saved; closing the window does not autosave.

## Implementation

`draw.ls` owns event procedures and session state. `mod_scene.ls` produces the
HTML shell and SVG; `mod_present.ls` updates retained controls and the page.
The shell and pointer-capture owner stay stable while SVG content changes,
following **S12.1.3** (pure views, mutations in handlers) and **D4.5.1v4**
(document-owned native resources). The model uses immutable document values
(**S1.4**) and reuses `lambda.editor` drawing commands, transactions, history,
geometry and connector routing. Each completed drag records one transaction;
Escape restores the pre-drag document. File restore validates geometry,
unique identities, supported styles and connector endpoints before replacing
the current document.

This first version has one 1120 × 760 page and up to 200 objects. Selection
is single-object. Connector routing avoids crossing its endpoint boxes; it
does not route around unrelated shapes. Groups, freehand paths, images,
collaboration, and draw.io file compatibility are outside this demo's scope.

## Verification

```sh
make -C build/premake config=debug_native test_lambda_gtest
./test/test_lambda_gtest.exe --gtest_filter='*tests_draw_model*'
./test/test_ui_automation_gtest.exe --suite draw --jobs 1
```

Repeat with `LAMBDA_EXEC_BACKEND=interp` and `LAMBDA_EXEC_BACKEND=jit` to
exercise all execution tiers (**D8.1.1v18**). The golden test lives in
`tests/draw_model.ls` with its expected `draw_model.txt`; native replays are
in `test/ui/draw/` and are registered in the Radiant baseline manifest.
They cover moving/resizing, connectors and deletion, inspector edits, undo,
touch, cancellation, keyboard focus, file round trips, SVG export and a
compact window. Replay screenshots and generated files go under `temp/draw/`.
