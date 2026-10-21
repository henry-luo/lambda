# Raster editor package

`import editor: lambda.editor.raster` exposes the value/command core used by
the [Photos demo](../../../../test/demo/photos/README.md). The image tree is the
shared `lambda.image` representation (**D7.2.4v3 / S9.1.2**); the editor owns
targets, gestures and history policy, rather than a second pixel store.

```lambda
import editor: lambda.editor.raster
import img: lambda.image

let session = editor.open_session(editor.new_document({width:64,height:48})^ )^
let pixels = img.blank({width:16,height:16,color:[1.0,0.0,0.0,1.0]})^
let placed = editor.exec(session,{id:"layer.insert",raster:pixels,name:"Accent",position:[8,8]})^
let moved = editor.exec(placed,{id:"layer.move",dx:3,dy:2})^
let restored = editor.exec(moved,{id:"history.undo"})^
restored.doc == placed.doc
```

`commands()` lists command IDs, labels and accepted parameter names.
`command_state(session,id)` reports basic enablement and a reason; `exec` performs
full validation and publishes one replacement. Unknown commands/parameters and
unsupported pixels fail explicitly. `lookup` resolves a stable layer ID to its
node and actual content path; `layers` returns foreground-first tree rows.

Supported commands cover layer insertion/new/delete/duplication, selection,
translation, ordering, grouping/ungrouping/adjacent merge, names/visibility/opacity,
locks, raster masks, fill/clear/baked adjustments, canvas/image resizing, flattening
and history. Rectangle, ellipse, lasso and color-wand selections combine in
replace/add/subtract/intersect modes and support invert, feather and mask creation.
Brush/eraser, selection-aware fill, flood fill, linear/radial gradients, mask
painting, copy/cut/paste and selection cropping use the same command path.
Duplication assigns fresh subtree identities. Group movement updates
descendants once. Mask positions remain local to their layer.

`layer.align` accepts `mode` (`left`, `center`, `right`, `top`, `middle`,
`bottom`) and `relative` (`selection`, the default, or `canvas`).
`layer.distribute` accepts `axis` (`x` or `y`), the same reference and at least
three selected roots. Geometry uses transformed/style bounds and equal gaps;
displacements round to integral pixels. Selected parents own descendant movement,
and inherited/descendant locks reject the whole operation. Arrangement shares
geometry math with Draw and records one transaction (**S9.2.2**).

`tool.configure` validates tool IDs, normalized foreground/background colors,
brush size/hardness/spacing/flow/opacity, selection mode and color tolerance.
`interaction.begin/sample/render/preview/finish/cancel` implements pointer transactions.
Drafts retain the original image and history; a completed move records one edit.
Stroke sampling carries distance across event segments, and pointer drafts render
at the host's frame rate. Brush/eraser previews resume cumulative native coverage
and apply only new dabs against the frozen source. Source expansion or changed
settings restart the exact stroke; completion still records its complete point
sequence as one transaction. Selection coverage has undo history without dirtying
document pixels. Panning has no image history. `mark_saved(session,snapshot)` tracks a content
checkpoint independently of selection/camera state. History reuses
`lambda.editor.mod_history`, including target restoration (**S9.2.2**).

The optional `lambda.editor.raster.view` module copies rendered pixels into a
retained native canvas and computes camera/target presentation (**D4.5.2**).
`lambda.editor.raster.render.frame` reuses a preceding composite and recomposites
the changed brush region. It falls back to full composition for stale caches,
effects/styles or other gestures. These caches are derived preview data; exports
and validation use the authored document. The demo updates the stage during a
gesture and refreshes histograms/inspectors on completion or cancellation.
Its host supplies `.raster-stage`, `.raster-surface`, `.raster-canvas`,
`.raster-selection` and `.raster-target` nodes. The selection overlay is separate
from saved/exported pixels. The facade and command tests need no native DOM.

The Crop tool drafts `session.crop` in document coordinates, clipped to the
canvas. `lambda.editor.raster.crop.rectangle` computes an outward-rounded pixel
rectangle; clicks and zero-area lines produce no draft. `crop.apply_draft`
submits the existing `document.crop` command, including layer translation and
saved-channel cropping. Pointer completion leaves a preview without dirtying or
history; cancellation restores the preceding draft. Changing tools, canvas extent
or undo/redo clears obsolete drafts. Hosts may provide `.raster-crop` for its
separate overlay (**S9.1.2 / S12.1.3**).

Storage uses shared `u8` tiles for region and paint edits; existing dense inputs
are retained as shared backing until a touched tile detaches (**S9.2.2 /
D4.1.4v5**). Layer affine transforms, eight blend modes, clipping chains and
pass-through groups use the shared renderer. `layer.transform` accepts an absolute
local `matrix` for one leaf, or relative `angle`/`scale_x`/`scale_y` around the
combined bounds of selected roots. `delta` is an explicit document-space affine;
it composes onto leaf geometry and also supports groups/multiselection.
Do not combine these forms. `transform.handle_gesture`
accepts an eight-way resize handle or `"rotate"`, returning the same command.
Opposite anchors stay fixed in document space; Shift constrains corner proportions
or snaps rotation to 15°, and Alt scales about the center. Signed scales allow
mirroring; their absolute magnitude is bounded to 0.001–4096. Hosts can provide
`.raster-transform` controls with `data-raster-handle` values; interaction inputs
carry `handle`, `shift` and `alt`. Nested groups and multiple selected roots are
supported, including existing affines and leaf masks. Selected ancestors suppress
duplicate child edits. Locks anywhere in a target subtree reject atomically;
group masks/effects/styles are explicitly outside this descendant-affine profile.
The image gesture also scales radially and Shift-drag rotates,
with shared cancel/commit history (**S9.1.2 / S9.2.2**). Pixel editing on a
transformed source requires explicit `layer.rasterize`.

`layer.straighten` accepts `{start:{x,y},end:{x,y},axis:"horizontal"|"vertical"}`.
It aligns the reference by the shortest rotation about the selected bounds center,
without changing canvas extent. The `straighten` tool uses the same command,
gesture draft and undo path; `tool.configure.straighten_axis` selects the reference
axis. Actions can capture/replay it (**S9.1.2 / S9.2.2 / S12.1.3**).

`guide.add/update/remove/clear` edits `photos_guides` metadata. Add takes `axis`
(`"x"` vertical or `"y"` horizontal) and integer `position`; update/remove takes
the stable `guide` ID. Positions lie inside the canvas, with at most 64 guides.
Guide edits dirty the project and share undo; `.photos` preserves them, while
other image formats do not retain this editor metadata. Canvas resize drops
outside guides, image resize scales them, and crop translates/clips them.
`guide.configure` changes `visible` and `snap` booleans without authored history.
Move inputs can carry a `guide` ID for a captured guide gesture. With snapping
enabled, `guides.snap_move` checks selected edges/centers against canvas/visible
guide coordinates within six screen pixels (`scale` maps document to screen).
Alt bypasses snapping; moves retain the integral placement contract. Optional
`.raster-guides` and `.raster-reference` hosts show guides, snap targets and the
straighten reference. Guide-only metadata changes reuse cached composition;
histograms follow the immutable raster rather than metadata edits.

`effect.add/update/toggle/remove/reorder` retain ordered adjustments/filters.
The demo provides Levels and RGB/channel Curves controls plus a histogram.
`lambda.editor.raster.tone` validates and edits normalized curve drafts without
changing documents. Curves have 2–32 strictly increasing input positions from
0 to 1, with outputs in [0,1]. `insert/move/remove/with_points` retain other
channels; endpoint inputs stay fixed and moves cannot cross neighbors. Apply
through `effect.add/update` with `curves:{rgb:[[0,0],[1,1]],r:...,g:...,b:...}`.
The shared image processor applies exact linear segments, then `output_black` /
`output_white` endpoints; alpha is unchanged. `lambda.image.project` preserves
these parameters, original pixel bytes, masks, locks and tree identities in a
versioned `.photos` data file. High depth and color conversion remain later work. The shared image
package's strict codec/render limits apply. PSD saving currently refuses editor
locks, which its writer cannot preserve; flat export intentionally renders pixels.

`lambda.editor.raster.export.prepare(image,format,options={})` derives a
PNG/JPEG composition without changing the input. `width` or `height` alone keeps
the aspect ratio; both specify an explicit size, within the existing limit of 16 million
pixels. PNG defaults to `alpha:"preserve"`. `alpha:"flatten"` requires an
explicit opaque `background`; JPEG always requires flattening and accepts integer
`quality` from 0 to 100 (default 90). Flattening uses the shared linear-light
compositor. Unknown or incompatible settings fail explicitly. The result contains
`image` and normalized `settings`; `destination(path,format)` derives a matching
`.png`/`.jpg` path. `files.write_document(session,path,format,options={})` encodes
and atomically publishes these pixels, returns the original snapshot and an
export report, and leaves history/checkpoints to the caller
(**S9.1.2 / S12.1.1v3**). PSD and `.photos` do not accept flat-export options.

Retouch tools share stroke sampling, selections, history and target mapping.
`retouch.source` freezes the current merged image at a chosen point; Clone/Heal
use Alt-click in the demo. `retouch.configure` selects aligned/reset-per-stroke
sampling, strength and radius. `retouch.stroke` supports clone, neighborhood
color-matched healing, dodge/burn, sponge, blur and sharpen. Aligned source offset
and target state restore through undo/cancel. Sampling is from immutable inputs,
including when painting onto a new transparent layer.

`action.start/stop/cancel/load/replay` record and replay plain command data on one
declared active layer. Recording guards target switches, history operations and
nonportable commands. Pointer strokes capture their resolved brush settings;
replay validates every intermediate replacement and records one atomic undo.
Selection context is supplied by the target session. The action header retains
its initial pixel/mask facet, and recorded same-layer target steps rebind to the
selected replay layer. `.laction` files are versioned Mark data; the demo caps
files at 8 MiB. Actions are session/library state, separate from image dirty state,
and persist across document replacement and tab activation in the demo.

`text.insert/update`, `vector.insert/update` and `font.embed` retain editable
primary content. Insert takes `options`, optional `name`/`position`/`parent`;
update patches authored options on the selected matching layer. Font embedding
takes an asset ID (`font`), captured `bytes` and optional `family`. It does not
read a path inside a pure command (**S12.1.1v3**). Text supports multiline,
horizontal kerning/alignment with embedded outlines; complex shaping, rich runs,
automatic wrapping and font substitution are outside this first profile.
Shapes include rectangles, rounded corners, ellipses and absolute M/L/Q/C/Z
paths with fill/stroke and optional linear gradients. The shared image renderer
owns rasterization. Pixel painting requires explicit artwork rasterization;
mask painting remains separate. Image resizing composes an affine so authored
geometry and its local masks retain their units.

`style.add/update/toggle/remove` manage ordered shadow/glow/color-overlay layer
styles. Their explicit local `outset` contributes to bounds and survives project
persistence; masks apply before styles, and transforms after styles. Native PSD
export refuses this metadata, including disabled styles.

`channel.view` selects composite, r/g/b/a or saved-channel previews without
changing image content/history. `channel.save` captures active selection;
`channel.capture` snapshots a composite component. `channel.load`, `rename` and
`delete` manage up to 64 saved coverage channels. `channel.replace` writes a saved
channel into a selected pixel/mask component through checked native channel
replacement. Saved coverage lives in `metadata role:photos_channels` and follows
crop/image/canvas resizing. Projects retain it; PSD refuses silent omission.
Undo restores coverage and falls back to composite view if a viewed channel no
longer exists. Component views and selection overlays never alter exports.

`lambda.editor.raster.workspace` owns up to 16 independent session snapshots.
`create/add/capture/activate/close/rows/any_dirty` provide stable tab identities,
independent history/checkpoints/cameras and guarded dirty closes. Active gestures
and action recording must finish before changing tabs. The demo's New/Open still
replace the active document through their existing save/discard guard; New Tab
and Open in Tab retain it. Closing the window visits every dirty tab and requests
Save/Discard/Cancel individually. Action libraries remain available across tabs.

`lambda.editor.raster.files` is the reusable procedural acquisition/publication
boundary. `lambda.editor.raster.batch.run(action,jobs,options={})` uses it to
process 1–32 jobs sequentially. Each job specifies `source`, `target`, `format`
(`photos`, `png`, `psd`) and optional target-layer ID (`layer`); otherwise the
first leaf layer is selected. Every action starts in a fresh session, with no
external selection/retouch source. Declare needed context in recorded commands.
Preflight normalizes path URLs, rejects repeated destinations and destinations
matching any input, before the first job. These are lexical URL checks; filesystem
link identity is not resolved. A failed job publishes no partial output; earlier
successful files remain. `continue_on_error:false` stops at the first error.
The result contains requested/completed/succeeded/failed counts and per-job
outcomes. The demo's Batch Action panel parses the explicit job list as Mark data
and reports the same outcomes. Batch execution currently blocks the UI until the
bounded list completes; asynchronous scheduling remains later work.

Retouch source selection must be recorded as a step or supplied by the replay
session. Actions operate with that session's current selection coverage.

Image resizing resamples active selection coverage; canvas resizing reframes it
with zero coverage outside the old extent. Undo restores its original dimensions
and samples with the document (**S9.2.2**).
