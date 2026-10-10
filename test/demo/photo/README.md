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
  and per-control/panel resets. A completed slider gesture creates one edit.
- **Filters:** Original, Mono, Warm, Cool, Vintage, Vivid, real image thumbnails,
  and adjustable intensity.
- **Crop:** freeform, original, square, 4:3, 3:2, 16:9; eight draggable handles,
  frame movement, percent fields, quarter turns, flips, and straighten.
  Apply commits the draft; Cancel or Escape restores the previous recipe.
- **Workspace:** Fit, 100%, zoom buttons/wheel, drag to pan, transparent pixels,
  source dimensions, and press-and-hold comparison of original colors using
  the current crop/orientation. The Compare button also accepts Space/Enter.
- **History:** undo/redo, up to 100 recipe snapshots, and undoable reset.
  Ctrl/Cmd+Z undoes; Ctrl/Cmd+Shift+Z redoes.
- **Save edits:** versioned JSON recipes with source dimensions and two encoded
  content checksums. Restore requires the matching source to be open; a moved
  file can be opened at its new path. Checksums detect accidental changes and
  provide no authentication guarantee.
- **Export PNG:** evaluates from original pixels, optionally resizes with aspect
  lock, then publishes a completed PNG through a neighboring temporary file.
  Width and height zero preserve the cropped source size. The source path is
  protected from overwrite. Failed exports keep the active edits.

The preview's longest edge is 960 pixels; export uses the original resolution.
Below 900 CSS pixels, the inspector becomes a drawer; use **Tools** or a tool
rail button to open or close it. Crop Apply/Cancel returns to the workspace.
Encoded inputs are limited to 32 MB and decoded/output images to 16 megapixels.
The current pipeline uses float64 intermediate arrays and synchronous CPU
kernels. Large adjusted exports can use substantial memory and pause the UI;
release performance targets from the plan remain to be measured. The demo
exports PNG only. Native file pickers, drag-and-drop files, layers, text,
stickers, and AI tools are outside this version.
Radiant currently paints the CSS checkerboard as broad gradient regions rather
than 20-pixel tiles; transparent pixels retain correct alpha in uploads/exports.

The application keeps its DOM tree and canvas stable while handlers publish
new pixels (**S12.1.3**). History stores immutable recipes (**S1.4**); it does not
retain a separate image per edit. The host copies borrowed pixels into
Radiant's document-owned surface (**D4.5.1v4 / D4.5.2**), and native allocations
use precise roots (**D5.3.3**).

| File | Responsibility |
| --- | --- |
| `photo.ls` | Stable view, event handlers, crop gestures, file commands |
| `mod_scene.ls`, `photo.css` | DTNA buttons and native editor layout |
| `mod_model.ls` | Recipe validation and transactional history |
| `mod_geometry.ls` | Normalized crop coordinates and drag geometry |
| `mod_pipeline.ls`, `mod_presets.ls` | Shared preview/export math and alpha handling |
| `mod_present.ls` | Coalesced frames, canvas uploads, control synchronization |
| `mod_io.ls` | Bounded snapshot decode, recipes, PNG publication |

Run the native replay and registered tests:

```sh
mkdir -p temp/photo
./lambda.exe view test/demo/photo/photo.ls --headless --no-log \
  --event-file test/demo/photo/tests/photo_smoke.json \
  --event-result temp/photo/photo_smoke_result.json
./test/test_lambda_gtest.exe \
  --gtest_filter='*tests_model_test:*tests_geometry_test:*tests_pipeline_test:*tests_image_io_test:*proc_export_test'
./test/test_radiant_view_gtest.exe --gtest_filter='RadiantViewTest.Photo*'
```

The Lambda tests are auto-discovered by the core baseline. The Radiant tests
register the 60-event editor replay and a pixel-level RGBA upload fixture with
forced collection, poisoned frees, promotion, and clean document teardown.
An additional 23-event replay covers crop dragging/cancellation, keyboard
comparison, and closing with a pending frame in automatic and forced-JIT modes.
The 24-event narrow-window replay checks drawer controls and export dialog size.
Its small RGBA fixture is generated from numeric pixels in Lambda.
A glyph-geometry probe checks that native button text retains authored flex/grid
alignment instead of overlapping neighboring labels.
IO tests check all eight EXIF orientations, malformed/oversized headers,
exported pixels, recipe matching, and write errors. Engine regressions also
cover shape-preserving numeric unary operations and `last` on call receivers.

See [the implementation plan](Lambda_Impl_Photo_Editor.md) for the architecture
and remaining performance work, and [asset attribution](assets/ATTRIBUTION.md).
