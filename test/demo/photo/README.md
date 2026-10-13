# Photo Studio

A native Lambda/Radiant photo editor inspired by the workflow of
[IMG.LY Photo SDK](https://img.ly/products/photo-sdk/). All editing runs locally
through Lambda numeric arrays and Radiant's retained canvas.

From the repository root:

```sh
make build
./lambda.exe view test/demo/photo/photo.ls
# Start with another local image:
PHOTO_SOURCE=path/to/photo.jpg ./lambda.exe view test/demo/photo/photo.ls
```

With the named-demo CLI enabled, `./lambda.exe demo photo` is a shortcut.

The bundled coast photo opens immediately. **Open image** offers two samples
and a local JPEG/PNG path field. Loading a bad image preserves the active photo.
Asset URLs resolve from the entry script, so direct launches also work from
another working directory. Default export and recipe paths are relative to the
launch directory; create `temp/` there or choose an existing destination folder.

- **Adjust:** exposure, brightness, contrast, saturation, temperature, gamma,
  black/white input levels, shadows, highlights, a luminance histogram, and
  per-control/panel resets. A completed slider gesture creates one edit.
- **Filters:** Original, Mono, Warm, Cool, Vintage, Vivid, real image thumbnails,
  and adjustable intensity.
- **Crop:** freeform, original, square, 4:3, 3:2, 16:9; eight draggable handles,
  portrait 4:5 and story 9:16, frame movement, percent fields, quarter turns,
  flips, and straighten.
  Apply commits the draft; Cancel or Escape restores the previous recipe.
- **Effects:** vignette, soft focus, sharpening, and a panel reset. Color
  filtering accounts for alpha and preserves the source transparency.
- **Text:** add a caption, edit its content, choose sans/serif/monospace and
  left/center/right alignment. Native font measurements fit the text into its
  box without stretching glyphs. Text is single line, up to 160 characters.
- **Shapes & stickers:** rectangles, circles, triangles, arrows and lines;
  original vector hearts, stars, sunshine, lightning, map pins and smileys.
- **Layers:** click artwork to select it, drag to move, or drag its corner to
  resize. The inspector provides position/size percentages, rotation, opacity,
  color swatches and a custom hex color. Choose any layer from the layer list,
  duplicate, delete or move it backward/forward. Arrow keys nudge the selected
  layer; Shift makes larger steps. Delete/Backspace removes it. Input fields
  retain their normal keyboard editing. Up to 24 layers share undo/redo.
- **Workspace:** Fit, 100%, zoom buttons/wheel, drag to pan, transparent pixels,
  source dimensions, and press-and-hold comparison of original colors using
  the current crop/orientation. The Compare button also accepts Space/Enter.
- **History:** undo/redo, up to 100 recipe snapshots, and undoable reset.
  Ctrl/Cmd+Z undoes; Ctrl/Cmd+Shift+Z redoes.
- **Save edits:** versioned JSON recipes with source dimensions and two encoded
  content checksums, including text and vector layers. Version 1 recipes are
  upgraded when loaded; new saves use version 2. Restore requires the matching source to be open; a moved
  file can be opened at its new path. Checksums detect accidental changes and
  provide no authentication guarantee.
- **Export PNG:** evaluates from original pixels, optionally resizes with aspect
  lock, paints the same vector artwork used in the preview, then publishes a
  completed PNG through a neighboring temporary file.
  Width and height zero preserve the cropped source size. The source path is
  protected from overwrite. Failed exports keep the active edits.

The preview's longest edge is 960 pixels; export uses the original resolution.
Below 900 CSS pixels, the inspector becomes a drawer; use **Tools** or a tool
rail button to open or close it. Adding artwork and Crop Apply/Cancel return to
the workspace. Layers use normalized coordinates in the final composition;
cropping/orienting changes the photo underneath them. Crop mode hides artwork
while framing; comparison temporarily hides artwork and color adjustments.
Encoded inputs are limited to 32 MB and decoded/output images to 16 megapixels.
The pipeline uses a fused native CPU evaluator and rolling-sum box filtering.
Double-precision calculations use bounded row/ring storage instead of full-image
floating-point intermediates. Preview jobs cache geometry, color and blurred
color within a 64 MiB budget; vignette/sharpen changes reuse the blurred result.
Separate native workers process previews and exports. New preview requests
supersede old ones, and results are published only for the current source and
recipe. PNG encoding also runs in the export worker. Text/vector export
composition still runs synchronously through Radiant's document renderer.
The demo exports PNG only. Native file pickers, drag-and-drop files, custom sticker
uploads, multiline typography, and AI tools are outside this version.
Radiant currently paints the CSS checkerboard as broad gradient regions rather
than 20-pixel tiles; transparent pixels retain correct alpha in uploads/exports.

The application keeps its DOM tree and canvas stable while handlers publish
new pixels (**S12.1.3**). History stores immutable recipes (**S1.4**); it does not
retain a separate image per edit. The host copies borrowed pixels into
Radiant's document-owned surface (**D4.5.1v4 / D4.5.2**), and native allocations
use precise roots (**D5.3.3**).
Workers retain native copies of input pixels and coefficients, with no borrowed
GC pointers, evaluator context or DOM access (**D4.5.2 / D5.3.3 / D7.5.3**).
Document resources cancel and join their workers at teardown. Their pending-work
hook lets headless assertions wait for real background work before advancing
the virtual clock. Cooperative Lambda tasks are unchanged (**S13.1.3v2**).

| File | Responsibility |
| --- | --- |
| `photo.ls` | Stable view, event handlers, crop gestures, file commands |
| `mod_scene.ls`, `photo.css` | DTNA buttons and native editor layout |
| `mod_model.ls` | Recipe validation and transactional history |
| `mod_geometry.ls` | Normalized crop coordinates and drag geometry |
| `mod_layers.ls` | Layer validation, selection/transforms, vector assets, shared SVG composition |
| `mod_pipeline.ls`, `mod_presets.ls` | Native processing options and shared preset coefficients |
| `mod_present.ls` | Coalesced frames, canvas uploads, control synchronization |
| `mod_io.ls` | Bounded snapshot decode, recipes, PNG publication |
| `benchmark.py` | Release timing, peak RSS, and reproducible baseline comparisons |

Run the native replay and registered tests:

```sh
mkdir -p temp/photo
./lambda.exe view test/demo/photo/photo.ls --headless --no-log \
  --event-file test/demo/photo/tests/photo_smoke.json \
  --event-result temp/photo/photo_smoke_result.json
./test/test_lambda_gtest.exe \
  --gtest_filter='*tests_model_test:*tests_geometry_test:*tests_pipeline_test:*tests_image_io_test:*proc_export_test:*tests_photo_layers_test:*tests_photo_effects_test:*proc_layer_export_test:*tests_photo_performance_test:*typed_array_blur_running'
./test/test_radiant_view_gtest.exe --gtest_filter='RadiantViewTest.Photo*'
```

The Lambda tests are auto-discovered by the core baseline. The Radiant tests
register the 60-event editor replay and a pixel-level RGBA upload fixture with
forced collection, poisoned frees, promotion, and clean document teardown.
An additional 23-event replay covers crop dragging/cancellation, keyboard
comparison, and closing with a pending frame in automatic and forced-JIT modes.
The 39-event narrow-window replay checks drawer controls, adding/dragging
artwork, and export dialog size.
Its small RGBA fixture is generated from numeric pixels in Lambda.
The creative replay covers text editing, layer gestures/history, shapes and
stickers, levels, portrait crop, recipe restoration and composition export in
automatic and forced-JIT modes (139 events, 36 assertions). Golden tests verify layer validation, alpha
preservation, effects, legacy migration and exported text/shape pixels.
The performance replay checks color/blur cache reuse, changing filters during
a large export, and closing with pending work. A separate forced-GC replay
checks superseded jobs, owned input pixels, painted colors and zero tracked
allocations at teardown. Reference pixels captured before optimization cover
20 recipes; non-neutral RGB comparisons allow one 8-bit value of rounding
difference. Neutral output and untransformed alpha remain exact.
A glyph-geometry probe checks that native button text retains authored flex/grid
alignment instead of overlapping neighboring labels.
IO tests check all eight EXIF orientations, malformed/oversized headers,
exported pixels, recipe matching, and write errors. Engine regressions also
cover shape-preserving numeric unary operations and `last` on call receivers.

Measure a release host from the main checkout:

```sh
make release
python3 test/demo/photo/benchmark.py --output temp/photo/performance/results.json
# Measure encoding, histogram and text composition separately:
python3 test/demo/photo/benchmark.py --cases encode histogram composition --iterations 3
```

The report records the binary hash, machine, image sizes, cold/warm timings and
process peak RSS. Each sample has a separate procedure activation so its output
does not remain in the benchmark loop's local bindings; printing happens after
the timed procedure returns. Peak RSS includes the
runtime, decoder/source setup and GC storage; it is distinct from the evaluator's
reported live scratch/cache bytes. Capture a release binary before changing
the evaluator to compare against its scripts with
`--binary path/to/old-release --reference-revision REV`.
Native replay attributes expose processing, upload, histogram, export encoding
and composition times. `data-image-preview_upload_ms` includes scheduling and
upload; it does not measure display presentation latency.
Recorded measurements are in [performance-results.json](performance-results.json).

See [the implementation plan](Lambda_Impl_Photo_Editor.md) for the architecture
and remaining performance work, and [asset attribution](assets/ATTRIBUTION.md).
