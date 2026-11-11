# Paint

Run `./lambda.exe demo paint` (or `./lambda.exe view test/demo/paint/paint.ls`).

A painting app built on [`lambda.paint`](../../../doc/Lambda_Packages.md#30-paint--painting-with-brushes)
and its interactive session (Lambda_Pkg_Paint.md PT10). Pointer handlers turn each event's pen
samples (`dom.pointer_samples`) into canvas pixels and feed the stroke. A frame handler then
presents only the changed rectangle with `dom.set_canvas_pixels_rect`, so each frame uploads and
repaints just the pixels a stroke touched.

## Using it

- **Tools:** Brush (B), Eraser (E), Picker (I) takes the colour under a click, Hand (H) drags the
  canvas. The middle button or a held space bar also pans.
- **Brushes:** Round, Airbrush and Marker (a wash that does not darken where it overlaps) are dab
  brushes. Pencil, Ink, Charcoal and Blender are MyPaint-model brushes loaded from `brushes/*.myb`.
  Blender smears the paint under it. Type a path to any version 3 `.myb` file and press
  **Import .myb** to add it to the palette; settings this engine does not render are listed in the
  status line.
- **Size and opacity** belong to the selected brush; `[` and `]` resize it.
- **Colour:** the swatches, the H, S and V sliders, or the picker.
- **Pen:** pressure, tilt and the eraser end work with a macOS tablet. A mouse paints at full
  pressure.
- **History:** Undo (⌘Z) and Redo (⇧⌘Z) step one stroke at a time; Clear can be undone too.
- **View:** zoom with −/+ (or ⌘ plus the wheel, about the pointer), Fit (0) and 1:1 (1). A plain
  wheel pans.
- **Files:** type a path ending in `.painting` (relative to the working directory) and press
  **Save** (⌘S) or **Open** (⌘O). A painting file stores the pixels, so it opens at once with an
  empty history. **Export PNG** writes the painting, flattened over its paper, beside the file.

The canvas is 1200 × 800 pixels. The bundled brushes are original settings written for this
demo.

## Tests

- `test/ui/paint/demo.json` replays the app: a brush stroke, undo and redo, a pen stroke with
  pressure samples, the pen's eraser end, the picker, size keys, zoom, saving, exporting and
  reopening a painting, clear, `.myb` import, and region-only repaints checked against full
  renders (`RADIANT_DAMAGE_VERIFY=1`).
  Run it with `./test/test_ui_automation_gtest.exe --suite paint`, or directly with
  `RADIANT_DAMAGE_VERIFY=1 ./lambda.exe view test/demo/paint/paint.ls --headless --event-file test/ui/paint/demo.json`.
- `test/lambda/paint/paint_session.ls` covers the session API the app uses.

Simulated pen batches (`event_sim`'s `samples`) should span no more time than the gap to the next
dispatched move, as real ones do. Otherwise sample times overlap between batches, and brushes with
slow tracking stall.
