# Sketch

A native Lambda/Radiant vector illustration and sketching demo, inspired by
the drawing workspaces in Illustrator, CorelDRAW and Krita. Run from the
repository root:

```sh
make build
./lambda.exe demo sketch
# The source also opens directly:
./lambda.exe view test/demo/sketch/sketch.ls
```

The starter poster, **Somewhere, slowly**, is editable vector artwork. Its
paper and frame layer starts locked; its landscape and lettering layers are
unlocked. **New** starts a blank 960 × 800 artboard. **Sample illustration**
restores the poster. Both actions are undoable.

## Drawing and editing

| Tool | Key | Behavior |
| --- | --- | --- |
| Select | V | Pick and drag objects. Drag empty canvas to select fully enclosed objects; Shift-click toggles membership. A single object's corner square resizes it |
| Rectangle select | M | Drag a selection rectangle, including over existing artwork. Shift-drag adds to the selection |
| Edit nodes | A | Select a path and drag its square nodes, including beyond its existing bounds |
| Brush | B | Draw a round-capped vector stroke, with adjustable width and opacity |
| Pencil | N | Draw a fine vector stroke, at most 2 px wide |
| Pen | P | Click to add straight nodes; Enter finishes an open path. Click the first node, or Close path, to make a filled closed path |
| Rectangle / Ellipse | R / O | Drag to draw. Hold Shift for a square / circle |
| Line | L | Drag a line; Shift constrains it to 45° increments |
| Text | T | Enter text and size in Properties, then click the artboard to place it |
| Object eraser | E | Remove whole vector objects under the pointer, with one undo step per gesture |
| Eyedropper | I | Pick an editable object's fill, or its stroke when unfilled |
| Hand | H | Drag to pan the workspace |

The right panel has **Properties** and **Layers** tabs. The Properties tab
contains the color palette and edits the selected object's fill, stroke, opacity, line
width, position, dimensions and text. Fields preview valid values as you type;
leaving a field records one edit. An invalid draft preserves the last valid
document and resets on blur. Text resizing scales its vector lettering.
Duplicate, Delete, Send to back and Bring to front affect the selected object.

A rectangle can start on the pasteboard around the artboard. Its live preview
selects fully enclosed objects across visible, unlocked layers, including after
pan/zoom and with touch input. Press Escape to restore the previous selection.
Selection changes do not dirty the project or create undo entries (**S12.1.3**).
Switch to Select (**V**) and drag any selected object to move the selection
together. Arrow keys, duplicate, delete, stacking order and fill/stroke/opacity
edits apply to the entire selection, with one undo item per completed edit.
Geometry fields, resize handles, node editing and text controls operate on a
single object. Multi-selection remains transient; it does not create a group.

Palette clicks set ink; Shift-clicks set fill. With text selected, a palette
click changes the lettering's fill. Colors accept six-digit hex or `none`.
Brush and Pencil leave finished marks unselected so palette changes configure
the next mark; use Select to recolor an existing stroke.

The Layers tab selects, adds, renames, reorders, hides, locks, changes opacity,
and deletes layers. Rows list the top layer first. Hidden and locked layers
do not intercept drawing or picking; hidden or locked active layers refuse new
objects. The last layer cannot be deleted. Scroll the dock to reach controls
on smaller windows. The tab bar stays visible while panel contents scroll.
Arrow keys, Home and End switch tabs when a tab has keyboard focus.

Right-click the canvas for Undo/Redo, Cut/Copy/Paste, Paste here, Duplicate,
stacking order, Delete, Select all, Deselect and Fit artboard. Right-clicking
an unselected object selects it; a member of the current selection preserves
the entire selection. Empty canvas clears selection and offers canvas actions.
Unavailable actions are disabled. **Shift+F10** or the Menu key opens the same
menu for the current canvas selection. Arrow keys and Home/End navigate;
Enter/Space activates; Escape, Tab or an outside click dismisses the menu.

The Sketch object clipboard is internal to this window. Copy preserves the document
and history; Cut removes the selection as one edit. Pasting a single copied
image creates a new layer. Other object selections paste into the active, visible, unlocked layer with fresh identities and a 20 px offset;
repeated pastes increase the offset. Paste here puts the copied selection's
top-left corner at the right-click point. Relative object placement and styles
are retained. Clipboard contents are not part of saved projects or the system
clipboard. Cut and Paste each create one undo item.

| Shortcut | Action |
| --- | --- |
| Ctrl/Cmd + Z / Shift + Z, Ctrl/Cmd + Y | Undo / Redo |
| Ctrl/Cmd + D | Duplicate selection |
| Ctrl/Cmd + C / X / V | Copy / Cut / Paste objects; paste images from the system clipboard |
| Ctrl/Cmd + Shift + I | Import a raster image as a layer |
| Ctrl/Cmd + A | Select all objects on visible, unlocked layers |
| Shift + F10 / Menu key | Open the canvas context menu |
| Delete / Backspace | Delete selection |
| Arrow / Shift + Arrow | Nudge selection 1 / 10 px |
| Ctrl/Cmd + S / Shift + S | Save project / Export SVG |
| Ctrl/Cmd + O | Open project |
| [ / ] | Decrease / increase stroke width |
| + / − / 0 | Zoom in / out / fit artboard |
| Wheel / trackpad | Pan |
| Ctrl/Cmd + wheel | Zoom about the pointer |
| Escape | Cancel the gesture, pen draft or dialog; otherwise clear selection |

Native text inputs keep their usual editing shortcuts. Mouse and touch share
the captured-pointer gesture path. Cancelling a drawing, move, resize, node edit
or erasure restores the pre-gesture document. Pan/zoom and selection do not add
history or dirty the project.

## Raster image layers

Use **Import image** (the picture button beside Open, **Ctrl/Cmd+Shift+I**,
or the canvas context menu), enter a local image path, and confirm. **Ctrl/Cmd+V**
pastes an image from the system clipboard. **Paste here** anchors its top-left
corner at the context-menu point. Each import or image-only paste creates one
selected layer above the existing layers, even when the active layer is locked.
Small images keep their natural pixel size; larger images fit within 80% of
the artboard while retaining their aspect ratio.

On macOS, copying one image file in Finder pastes that file's image content.
The file URL takes precedence over Finder's PNG/TIFF icon previews. Unreadable,
non-image, or multiple-file copies do not paste a file icon. Image-only clipboard
data from screenshots and image editors remains supported.

Move images with Select or the arrow keys. The corner handle preserves the
current aspect ratio; hold Shift to stretch. Geometry fields allow explicit
width/height edits. Object opacity and layer visibility, locking, opacity,
renaming, ordering and deletion work as usual. Fill, stroke and text controls
do not change raster pixels. Each insertion or completed edit is undoable.

Images are decoded for validation and embedded as PNG data, preserving transparency.
Existing PNG bytes and color profiles are retained, so screenshots copied from
Preview or the macOS screenshot thumbnail can be placed without entering the
sRGB-only painting compositor. Other supported formats are converted to PNG and
their source orientation normalized. The original file can move or disappear after
import: project files and exported SVGs contain the image bytes. PNG, JPEG
and other still formats supported by `lambda.image` can be imported. Animated
images are rejected. Limits are 16 million decoded pixels, 32 MB per encoded
source/PNG, and 64 MB of embedded data URI text per project (copies count too).
Invalid images leave the current document intact. Clipboard HTML can supply
an embedded image; remote image URLs are not downloaded.

An internal Copy/Cut takes priority over the unchanged system clipboard.
A changed external clipboard containing a raster image takes priority at the
next paste. Internal object copies remain local to the window.

## Files

**Save project** writes a JSON `.sketch` file with the `lambda-sketch` format
marker: version 2 when images are present, version 1 for vector-only documents.
Open accepts both versions. It preserves layers, styles, text, shapes, path
nodes and embedded raster images. This is the demo's own project format, separate from files made by
the commercial Sketch app. **Open** validates the entire project before
replacing the document; Undo can restore the preceding illustration.

**Export SVG** writes a standalone, clipped vector image with the artboard
background, embedded images and visible layers. Guides, selection handles and hidden layers
are excluded. Saving/exporting uses atomic output. Defaults are
`temp/sketch/illustration.sketch` and `temp/sketch/illustration.svg`; enter a
different path in the dialog to keep multiple projects. Parent directories
for custom paths must exist. Tab/Shift+Tab stays inside the file dialog;
Escape closes it and returns focus to its opening control.

Changes stay in memory until saved. Closing the window does not autosave.

## Implementation and scope

`mod_model.ls` owns immutable documents and pure transitions (**S1.4**),
reusing `lambda.editor.draw.geometry` for picking and paths, and
`lambda.editor.mod_history` for snapshot undo/redo. `mod_artwork.ls` builds
the starter from ordinary objects. `mod_scene.ls` produces pure HTML/SVG;
`mod_present.ls` updates retained groups and controls. `sketch.ls` owns pointer,
keyboard, DOM and file effects (**S12.1.3 / D7.4.6**). `mod_raster.ls` reuses
`lambda.image` for decoding/PNG encoding and `lambda.doc.media` for data URIs.
`dom.clipboard_read()` supplies an owned native snapshot; the demo decides what
to paste (**D7.2.5**). The shell, stage and
native input controls stay mounted during gestures, following the
document-owned resource lifetime in **D4.5.1v4**. Each completed gesture adds
one history item; previews and cancellation add none.
Context-menu controls mount for each invocation with their initial enablement
and retire on dismissal. The shared `mod_context_menu.ls` helper places and
focuses each popup after its layout commits (**D7.4.6**).

This milestone covers one artboard, rectangle and multi-object selection, editable straight-node
paths, variable-width strokes by object, basic shapes/text, raster image layers, history,
project persistence and SVG export. It accepts up to 1,000 objects, 64 layers,
and 4,096 sampled points per path. It does not implement raster painting,
pressure-sensitive brushes, Bézier control handles, persistent groups,
rotation, boolean path operations, arbitrary SVG import or native
Illustrator/CorelDRAW/Krita file compatibility. Text uses the host font system.

## Verification

```sh
make -C build/premake config=debug_native test_lambda_gtest -j8
./test/test_lambda_gtest.exe --gtest_filter='*tests_sketch_*'
LAMBDA_EXEC_BACKEND=interp ./test/test_lambda_gtest.exe --gtest_filter='*tests_sketch_*'
LAMBDA_EXEC_BACKEND=jit ./test/test_lambda_gtest.exe --gtest_filter='*tests_sketch_*'
./test/test_ui_automation_gtest.exe --suite sketch --jobs 1
sh test/demo/previews/capture.sh sketch
```

The paired `.ls`/`.txt` tests in `tests/` cover drawing and cancellation,
history branching, shape/line constraints, movement/resizing, node editing,
style validation, text sizing, layer visibility/locking/order and strict project
round trips across execution tiers (**D8.1.1v18**). The registered native replays
in `test/ui/sketch/` cover visible artwork, captured mouse/touch gestures,
inspector edits, layers, pen nodes, rectangle selection and grouped edits,
context-menu targeting, vector clipboard edits, menu focus and window bounds,
undo, file dialogs, image import/paste, visible image pixels, forced-GC clipboard
ownership, embedded project persistence, and a
760 × 720 window. Replay artifacts stay under `temp/sketch/`.
