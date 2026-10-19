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

`tool.configure` validates tool IDs, normalized foreground/background colors,
brush size/hardness/spacing/flow/opacity, selection mode and color tolerance.
`interaction.begin/sample/render/preview/finish/cancel` implements pointer transactions.
Drafts retain the original image and history; a completed move records one edit.
Stroke sampling carries distance across event segments, and pointer drafts render
at the host's frame rate. Selection coverage has undo history without dirtying
document pixels. Panning has no image history. `mark_saved(session,snapshot)` tracks a content
checkpoint independently of selection/camera state. History reuses
`lambda.editor.mod_history`, including target restoration (**S9.2.2**).

The optional `lambda.editor.raster.view` module copies rendered pixels into a
retained native canvas and computes camera/target presentation (**D4.5.2**).
Its host supplies `.raster-stage`, `.raster-surface`, `.raster-canvas`,
`.raster-selection` and `.raster-target` nodes. The selection overlay is separate
from saved/exported pixels. The facade and command tests need no native DOM.

Storage uses shared `u8` tiles for region and paint edits; existing dense inputs
are retained as shared backing until a touched tile detaches (**S9.2.2 /
D4.1.4v5**). Reversible effects, native rich projects, high depth and
color conversion remain subsequent proposal milestones. The shared image
package's strict codec/render limits apply. PSD saving currently refuses editor
locks, which its writer cannot preserve; flat export intentionally renders pixels.
