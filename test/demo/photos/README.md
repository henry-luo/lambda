# Photos

A native Lambda/Radiant layered raster editor. The initial implementation uses
the shared image/PSD package and reusable `lambda.editor.raster` commands.
Design and later milestones: [proposal](../../../vibe/Lambda_Demo_Photos.md),
[implementation record](../../../vibe/impl/Lambda_Impl_Photos.md).

Run from this checkout:

```sh
mkdir -p temp/photos
make build
./lambda.exe view test/demo/photos/photos.ls
# After rebuilding the CLI, the catalog also accepts:
./lambda.exe demo photos
PHOTOS_SOURCE=path/to/image.psd ./lambda.exe view test/demo/photos/photos.ls
```

The coast sample is shared with the existing photo demo and keeps its original
[attribution](../photo/assets/ATTRIBUTION.md). Open accepts PNG/JPEG and supported
layered PSD. Place Image flattens a supported still input into a new raster layer.
Paths resolve from the launch directory; bundled assets resolve from the entry.

- Add, duplicate, rename, reorder, hide, change opacity and delete raster layers.
- Shift-click layer rows to select siblings; group/ungroup or merge adjacent
  layers. Nested groups retain foreground-first order and document coordinates.
- Select a mask thumbnail to target mask samples. Add/remove/toggle a raster
  mask, fill it white or clear it; pixel and mask histories remain independent
  snapshots of the same image tree.
- Drag to move the chosen layer/group, or use X/Y fields and arrow keys.
  Shift+arrows moves ten pixels. Escape cancels a drag. V selects Move, H selects
  Hand; Fit, 100% and zoom buttons control the retained canvas.
- Apply baked exposure, monochrome and sharpening to the active pixel layer.
  Resize image samples or change canvas bounds; flattening is explicit.
- Undo/redo through buttons or Ctrl/Cmd+Z / Shift+Ctrl/Cmd+Z. A completed drag
  records one edit. Camera and layer selection do not dirty image content.
- Save the qualified layered PSD profile, or export the composite as PNG.
  Save/reopen preserves supported hierarchy, names, masks, offsets and current
  pixels. Writes publish atomically. Failed reads/writes retain the active edits.
- New/Open/Close protect unsaved edits with Save/Discard/Cancel. File dialogs
  support Tab/Shift+Tab and Escape.
- Paint with Brush (B) or Eraser (E); configure size, hardness, spacing, flow
  and stroke opacity in the paint dock. Brackets change brush size. Cancel a
  preview with Escape; one completed stroke creates one undo item.
- Rectangle (M), Ellipse (O), Lasso (L) and Wand (W) create selections. Choose
  replace/add/subtract/intersect, invert or feather them, convert them to masks,
  or crop the canvas to their bounds. Selection edits have history without
  marking image pixels unsaved. Ctrl/Cmd+A selects all; Ctrl/Cmd+D deselects.
- Bucket (F), Gradient (G) and Eyedropper (I) share foreground/background colors.
  Gradients support linear and radial ramps. Painting, filling and baked
  adjustments honor selection coverage and the current pixel/mask target.
- Ctrl/Cmd+C / Shift+C copies the active layer / merged composition, X cuts,
  and V pastes a new layer with its document-space origin. The current clipboard
  is internal to the document session. X without a modifier swaps colors.

Use the dock scrollbar to reach image sizing and history on shorter windows.
The current workspace has one document and a fixed tool rail/dock; the richer
responsive dock and multiple document tabs remain later work. Native file
pickers, blend modes, affine transforms,
retained effects, text/shapes and rich-project persistence are not delivered yet.
Imported unsupported visual content fails strict opening instead of disappearing.
Masks/groups use the current shared package's qualified PSD subset; inverted/group
mask writing is not yet available. Unlock layers before PSD saving because the
current writer cannot retain editor lock metadata.

The package's 16-million-pixel/64-MiB per-raster and aggregate PSD budgets apply;
this demo makes no photo-scale performance claim. The host receives copied pixels,
never borrowed GC pointers (**D4.5.2 / D5.3.3**), and views retain their canvas and
focus owners while procedures publish edits (**S12.1.3**).

Focused verification:

```sh
./test/test_lambda_gtest.exe --gtest_filter='*photos_*'
./test/test_ui_automation_gtest.exe --suite photos --jobs 1
```

Package tests pass with automatic and `LAMBDA_EXEC_BACKEND=jit` execution;
strict `interp` currently encounters the shared image package's unsupported
multidimensional indexing (E501), also reproduced by its existing composition test.
Ownership checks use `LAMBDA_GC_FORCE_EVERY=1 LAMBDA_GC_POISON_FREED=1`.
Native view callbacks use the host's supported UI execution path. Generated files/captures stay under
`temp/photos/`. The baseline UI suite includes the Photos replay.

Measure the bounded headless layered scenario with an optimized host:

```sh
make build-release-compile
python3 test/demo/photos/benchmark.py
```

The report identifies the binary, source package, scenario, phase timings and
maximum child-process RSS. It measures construction, repeated moves (including
current eager validation/composition), and PSD encoding; it does not establish
interactive display latency. Do not run `make release` in a worktree.
