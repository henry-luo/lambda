# Photos

A native Lambda/Radiant layered raster editor. The initial implementation uses
the shared image/PSD package and reusable `lambda.editor.raster` commands.
Design and later milestones: [proposal](../../../vibe/Lambda_Demo_Photo_Editor.md),
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
- Add reversible exposure, monochrome and sharpening effects to the active layer.
  Resize image samples or change canvas bounds; flattening is explicit.
- Undo/redo through buttons or Ctrl/Cmd+Z / Shift+Ctrl/Cmd+Z. A completed drag
  records one edit. Camera and layer selection do not dirty image content.
- Save editable `.photos` projects or the qualified layered PSD profile. Export
  PNG or JPEG with independent output dimensions and an optional aspect lock.
  PNG can preserve transparency or flatten over an opaque background; JPEG
  requires that background and exposes quality from 0 to 100. Export keeps the
  document, saved checkpoint and history unchanged.
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
- Crop (C) draws a bounded canvas rectangle without changing pixels. Apply crop
  (or Enter while the stage has focus) commits one edit; Cancel/Escape discards
  the draft. Undo restores the canvas, layer/group-mask offsets and saved coverage
  channels. The crop bar offers Free, Original, Square and common portrait/landscape
  aspect ratios; changing preset clears the old draft. Shift makes a Free crop
  square. Constrained crops stay inside the canvas and round to whole pixels,
  within half a vertical pixel of the chosen ratio.
- Align selected layer roots to the canvas or their combined bounds in the lower
  Align & distribute dock. Distribute three or more roots with equal horizontal
  or vertical gaps. Locked roots reject the entire edit; selected parents and
  children move once. Placement rounds to whole pixels.
- Bucket (F), Gradient (G) and Eyedropper (I) share foreground/background colors.
  Gradients support linear and radial ramps. Painting, filling and baked
  adjustments honor selection coverage and the current pixel/mask target.
- Ctrl/Cmd+C / Shift+C copies the active layer / merged composition, X cuts,
  and V pastes a new layer with its document-space origin. The current clipboard
  is internal to the document session. X without a modifier swaps colors.

Use the dock scrollbar to reach the lower controls on shorter windows. The
workspace supports independent document tabs, a fixed tool rail and a scrolling
dock. Native file pickers and configurable docks remain outstanding.
Imported unsupported visual content fails strict opening instead of disappearing.
Masks/groups use the current shared package's qualified PSD subset; inverted/group
mask writing is not yet available. Unlock layers before PSD saving because the
current writer cannot retain editor lock metadata.

The package's 16-million-pixel/64-MiB per-raster and aggregate PSD budgets apply;
this demo makes no photo-scale performance claim. The host receives copied pixels,
never borrowed GC pointers (**D4.5.2 / D5.3.3**), and views retain their canvas and
focus owners while procedures publish edits (**S12.1.3**).

Image & canvas offers nine canvas anchors. Top left keeps the previous behavior;
Center adds/removes space around the content, while corners/edges keep their
chosen side fixed. An odd center pixel goes to the right/bottom on expansion and
is removed from the left/top on shrink. Canvas resize moves artwork, group masks
and guides, reframes active/saved selections, and retains source samples and local
layer masks. Undo restores the previous placement and coverage. Image resize
continues to resample. Size fields reject fractional pixels. Anchor and crop
choices are per-tab settings rather than project edits.

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

Transform (T) shows eight resize handles and a rotation handle for selected
layers or groups. Resizing keeps the opposite edge/corner fixed, including on an already
rotated layer. Shift preserves corner proportions or snaps rotation to 15°;
Alt resizes about the center. Escape cancels; completion creates one undo item.
Handles follow the combined document-space bounds. Nested groups and selected
parents/children transform each leaf once, retaining source pixels, artwork and
leaf masks. A locked descendant rejects the whole transform. Groups carrying
group masks/effects/styles or adjustment-layer descendants require separate support and reject transforms;
perspective/warp remain outstanding. Dragging the image scales radially; Shift-drag rotates. Numeric
controls apply relative angle/X/Y scale; reset/flip and explicit rasterization
are available. Reset requires one leaf layer; use Undo to revert a group transform.
Blend modes, clipping, group isolation and ordered effect controls
share the headless command registry. The histogram reuses the presented raster.

Straighten (Q), also available in the lower Guides & snapping dock, aligns a drawn
reference to horizontal or vertical. It transforms the selected roots about their
combined center, preserving source pixels (**S9.1.2 / S9.2.2**). Release commits one
edit; Escape cancels. The canvas keeps its size; crop afterward if needed.

Guides & snapping adds up to 64 integer-position horizontal/vertical guides.
Drag a guide with Move, remove it in the list, or clear all guides. Guide edits
have undo and persist in `.photos` metadata; crop/resize updates their positions.
Image exports omit this editor metadata. Hide guides and enable snapping
independently; these view settings do not dirty the document. Enabled move drags
snap selected bounds' edges/centers to canvas edges/centers or visible guides
within six screen pixels. Alt bypasses snapping. Magenta lines show snap targets.
Layer snapping can be toggled separately. It targets other visible layers/groups
and excludes selected relatives, hidden/zero-opacity content and empty bounds.
Targets use authored geometry, including transforms/styles, rather than pixel
alpha or clipping silhouettes. Candidates freeze at drag start; equally close
targets prefer canvas, guides, layers, then grid subdivisions. Placement rounds to whole pixels,
including fractional transformed bounds.
Guide drags reuse the composition and histogram.

Navigator & precision adds pixel rulers, grid spacing/subdivisions and a
composite thumbnail. Drag the top ruler into the image for a horizontal guide,
or the left ruler for a vertical guide. Release commits one guide edit; Escape
or a drop outside the canvas cancels it. Grid visibility and grid snapping are
independent; snapping also uses the main Guides & snapping switch. Dense grid
lines hide at small zoom, and whole-pixel layer placement rounds fractional
subdivision targets. Rulers/grid follow Fit, zoom and pan without changing the
stage size (**S9.1.2 / S12.1.3**).

Click/drag the navigator to center the camera; Escape restores the previous pan.
With navigator focus, arrow keys pan by 40 screen pixels and Home centers the
view. The red box shows the visible document region. Thumbnail updates wait
until a live edit finishes, then refresh from the cached composite. View controls
are retained per tab and do not dirty the document or add undo entries.
Physical ruler units, guide locking/layout presets and equal-gap smart guides
remain outstanding.

The lower Levels dock sets input black/white, midtone gamma and output black/white.
Curves offers RGB master and separate red/green/blue channels. Click its graph to
add a point, drag to move it, or enter normalized `[input,output]` pairs. Endpoints
stay at input 0/1; interior points cannot cross their neighbors. Each curve uses
2–32 points with selectable Linear or Smooth interpolation. Smooth uses a
shape-preserving cubic curve, and its graph samples the rendering kernel. Channel changes keep drafts, Escape
cancels an active graph drag, and Reset/Remove edit the draft. Add creates an
editable effect; Apply updates the selected effect with one undo item. Drafts
leave the image untouched until applied. Effects preserve source pixels and alpha,
can be reordered/toggled, and survive projects and recorded actions
(**S9.1.2 / S9.2.2**). New Levels layer / New Curves layer inserts an independent
adjustment above the selected sibling. New adjustment layer starts with an
identity effect. Edit its effect with the same controls, or click its M thumbnail
to paint/fill/invert its mask. A current selection initializes mask coverage;
otherwise the mask starts white. Opacity fades the grade while retaining alpha.
Adjustments affect lower siblings inside an isolated group, or the parent
backdrop inside a pass-through group. Source pixels remain editable and unchanged.

The first adjustment-layer profile supports pointwise color controls; blur,
sharpen, vignette, styles, affine transforms and clipping require later support.
Select the mask and use Move to move its coverage. Merge, standalone rasterization
and Apply mask reject backdrop-dependent layers; Flatten image bakes the full
composition. Ungrouping an isolated adjustment group rejects a scope change.
Adjustment layers and smooth curve settings survive `.photos` save/reopen;
export PNG/JPEG to bake pixels. Current PSD export rejects these editable layers.

Save defaults to `.photos`, preserving original samples and editable settings.
A `.psd` destination remains supported for the qualified PSD subset; Export PSD
is explicit. PSD refuses retained transforms/blends/clipping/pass-through and
editable effects, including disabled ones, rather than discarding them. Project data is parsed without
execution, capped at 128 MiB and currently uncompressed. Pixel tools require
rasterization of a transformed source. Full PhotoCraft parity and high-depth/ICC
editing are still pending.

The lower Text, Shapes & Paths dock adds and edits multiline text, rounded
rectangles, ellipses and M/L/Q/C/Z paths. Set local dimensions, fill/stroke and
font size/alignment, then Add or Apply. Text embeds the selected font file
(`.woff2` Computer Modern Sans by default) as a project asset. Missing glyphs
and unsupported complex-script shaping fail explicitly. Layer styles add editable
shadow, glow and color overlay. These features share the image compositor and
survive `.photos` reopening; use explicit rasterization for the current PSD profile.

Channels offers RGB/alpha component previews and saved selection channels.
Save/load a selection, capture a composite component, rename/delete it, or
replace a selected pixel layer component. Channel previews affect only the
workspace. Saved coverage follows canvas/image resizing and cropping, and is
preserved in projects.

Clone (S), Heal (J), Dodge (D), Burn (K), Sponge (R), Blur (U) and Sharpen (A)
share the brush controls. Alt-click Clone/Heal to freeze a merged source, then
paint on a pixel layer. The lower Retouch dock controls alignment, local strength
and neighborhood radius. Healing is neighborhood color matching, not a Poisson
or content-aware solver.

The Actions dock records edits on the selected layer. Stop stores the action;
Replay applies it to the current layer with one undo entry. Brush settings are
captured, and image/history/target switches are guarded during recording. Save
and load `.laction` data files to reuse actions. Batch Action runs the last action
on an explicit Mark job list, for example:

```lambda
[{source:"photo.png",target:"temp/photos/graded.png",format:"png"}]
```

Each job selects its first leaf layer, or the explicit `layer` ID. Outcomes report
successes and failures; stop/continue policy is selectable. Outputs publish
atomically per file, and failed jobs leave existing outputs intact. Execution is
sequential and bounded to 32 jobs; the UI waits for completion.

New Tab and Open in Tab retain the current document. Tabs keep independent undo,
selection, tool/camera and saved checkpoints. Close Tab uses Save/Discard/Cancel;
closing the window protects every dirty tab, including inactive documents.
The existing New/Open commands continue to replace the active document.

Remaining larger features include high-depth/ICC editing, complex text shaping
and rich runs, canvas pen handles, the wider layer-style catalogue, native PSD
artwork/style interchange and asynchronous batch/large-scene qualification.

Retouch source selection must be recorded as a step or supplied by the replay
session. Actions operate with that session's current selection coverage.

Brush and eraser previews process new samples incrementally and recomposite only
the changed region when the scene has no effects/styles. Histograms and inspector
panels refresh when the gesture completes or is cancelled. Undo remains one edit
per stroke. Effect/style scenes retain full-composition previews; canvas upload,
final validation and retouch kernels still have larger-scene costs.

Measure native preview-handler editing/publication on an optimized host with:

```sh
python3 test/demo/photos/benchmark_brush.py --paint --repetitions 3
```

This writes disposable instrumented app copies and reports under
`temp/photos/performance/brush/`. Pair `--reference-dir` and `--reference-home`
to compare saved demo/package sources on the same host. Reports separate edit
and publication times. `--paint` verifies each preview and adds native frame,
layout and CPU paint timings. Display refresh and hardware input queuing remain
outside these measurements. See the implementation record for measured cases.
