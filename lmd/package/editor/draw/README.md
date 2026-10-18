# `lambda.editor.draw`

A native Lambda drawing package for identified vertices and edges. The pure
facade builds and edits drawing values, queries topology, validates archives,
and generates SVG. `lambda.editor.draw.view` adds a retained interactive canvas.
Package naming follows **D7.2.4v3**; model transitions are pure and DOM effects
stay in handlers under **S1.4, S12.1.1v3, S12.1.3**.

```lambda
import draw: lambda.editor.draw

let a = draw.vertex("a", "rect",
    {x: 60.0, y: 100.0, width: 140.0, height: 80.0}, {label: "Input"})^
let b = draw.vertex("b", "diamond",
    {x: 320.0, y: 100.0, width: 140.0, height: 100.0}, {label: "Ready?"})^
let e = draw.edge("ab",
    {kind: 'boundary', vertex: "a", side: 'east'},
    {kind: 'boundary', vertex: "b", side: 'west'},
    {routing: 'orthogonal', 'end-arrow': "triangle"})^
let session = draw.open_session(draw.new_document({width: 640.0, height: 400.0}))^
let next = draw.exec(session, {kind: 'insert', objects: [a, b, e]})^;
draw.to_svg(next.doc)
```

`open_session` is the actual API name: `open` is a reserved language keyword.

## Vertices, ports, and paths

The built-in descriptors are `rect`, `ellipse`, `diamond`, `rounded`,
`parallelogram`, `trapezoid`, `hexagon`, `triangle`, `cylinder`, `document`,
`note`, and `text`. A custom descriptor is serializable data with an `id`, a
normalized polygon `outline`, and optional named ports:

```lambda
let service = {
    id: "service",
    outline: [[0.2, 0], [1, 0], [0.8, 1], [0, 1]],
    ports: [{id: "input", x: 0.0, y: 0.5, direction: 'in'},
            {id: "output", x: 1.0, y: 0.5, direction: 'out'}]
}
```

Pass this value as `vertex`'s descriptor argument. Local coordinates range
from 0 to 1; the same outline drives painting, picking, transformed bounds,
and boundary intersection. Ports follow resizing, rotation, and flips.
`rotate` is the normalized orientation attribute; the legacy demo's `rotation`
remains supported by its compatibility profile.

Endpoints have one of three forms:

- `{kind: 'boundary', vertex: "a", side: 'auto'}`; sides are `auto`, `north`,
  `east`, `south`, or `west`.
- `{kind: 'port', vertex: "a", port: "output"}`; named ports cannot use those
  reserved side names. Optional port direction is `'in'`, `'out'`, or `'both'`.
- `{kind: 'point', x: 200.0, y: 100.0}` for a free endpoint.

Edges support `straight`, `orthogonal`, and `curved` routing; optional
`waypoints` are drawing-space `{x, y}` values. Optional `segments` describe a
custom path as joined line arrays `[x0,y0,x1,y1]` or cubic arrays
`[x0,y0,c1x,c1y,c2x,c2y,x1,y1]`. Endpoint-adjacent controls follow attached
vertices. Path emission, label placement, picking, bounds, and handles use
that geometry; sampling reuses `lambda.slide.motion`'s bounded curve table.
Independent start/end arrowheads are `none`, `triangle`, `open`, `diamond`,
`circle`, or `bar`.

Enable self-loops with `new_document({'allow-loops': true})`. Automatic
parallel-edge routes use stable document order to select distinct lanes.
The endpoint-box router does not avoid unrelated vertices.

## Queries, commands, and embedding

`vertices`, `edges`, and `lookup` read the drawing; `incident_edges`,
`incoming_edges`, `outgoing_edges`, and `neighbors` read its topology.
`resolve_endpoint(document, edge, "from" | "to")`, `route`, and `hit` expose
resolved geometry. `route` returns sampled `points`, SVG `path`, `bounds`, and
`handles`. `validate` returns diagnostics; commands reject invalid results
before changing state or history.

`exec(session, command)` accepts these command maps:

| `kind` | Other fields |
|---|---|
| `'insert'` | `objects` |
| `'set'` | `id` or `ids`, `properties` |
| `'move'` | `ids`, `dx`, `dy` |
| `'delete'`, `'duplicate'` | `ids` |
| `'reconnect'` | `id`, `side: "from" | "to"`, `endpoint` |
| `'reverse'`, `'front'`, `'back'` | `id` |
| `'align'` | `ids`, `mode: "left" | "center" | "right" | "top" | "middle" | "bottom" | "distribute-x" | "distribute-y"` |
| `'undo'`, `'redo'` | None |

Deleting a vertex removes its incident edges. Duplicating a selection includes
its internal edges and remaps their IDs. An object's identity is immutable
through `'set'`. A document's default object limit is 200; configure it with
`'max-objects'`.

An embedding editor calls `transaction(root_document,
{drawing_path: [...], selection: ...}, command)` and applies that returned
shared editor transaction to its own history. A no-op returns `null`. The
package adds no private history to the embedding editor.

`update(session, input)` supplies pure begin/preview/commit/cancel transitions.
Input fields are `kind`, client `x/y`, drawing-space `point`, `pointer`, and
optional `shift`/`mode`. Previews leave history unchanged; a completed gesture
commits once, and cancellation restores its snapshot.
With `session.tool == "connect"`, begin starts a connector draft from a shape
or free page point. Preview attaches to valid target shapes and keeps history
unchanged; commit creates one edge. Four screen pixels distinguish a drag from
a click so the existing two-shape-click workflow remains available. The draft
ID becomes permanent only on commit; cancellation restores selection and IDs.

## Canvas and persistence

```lambda
import canvas: lambda.editor.draw.view

// in an HTML element body; document is a validated drawing value.
canvas.surface(document, {id: "flow-editor", width: 640, height: 400})^
```

Each surface requires a unique `id`. SVG paint servers, object presentation
IDs, controls, and native capture are scoped to that instance. The canvas
supports selection/marquee, movement, resizing, rotation, endpoint/bend/segment handles,
shape placement, connecting, label/orientation editing, undo/redo, duplicate,
delete, zoom buttons, and native scrolling. Keyboard actions leave input
controls alone. It emits `draw_change` with `{id, document}` for host handlers.
The demo keeps its richer application shell, style inspector, file dialogs,
and wheel/pan controls while sharing the package model and gesture helpers.

Polyline segment gestures keep both endpoints fixed and store their intermediate
points as `waypoints`; terminal segment drags insert connecting stubs. Curved
routing uses the same editable orthogonal skeleton. Green × overlays confirm
attached endpoints. Custom cubic control-point editing remains separate.
The demo adds a floating start-arrow/end-arrow/routing toolbar and retains
unaffected SVG nodes during connector previews (**D4.5.1v4–D4.5.3v2**).

`encode(document)` produces version-4 archive data; `decode` accepts that data
or parsed JSON and also reads existing demo versions 1–3. Version 4 preserves
page dimensions, custom descriptors, ports, segments, waypoints, and metadata.
Use JSON strings, numbers, booleans, arrays, maps, and null for metadata.
`encode(document, {version: 3})` refuses content the legacy format cannot
represent. `to_svg(document, {prefix: "export-"})` excludes editor overlays;
the prefix is optional. File reads, atomic writes, and saved checkpoints belong
to the host.

The initial editor supports one page and one editable layer, declarative
polygon descriptors, plain multiline labels, and line/cubic custom edges.
Callback registries, richer vertex content, persistent groups, obstacle
avoidance, automatic graph layout, and mxGraph/draw.io XML interchange remain
extensions. Arbitrary SVG source editing stays with `lambda.edit.svg` and
`lambda.edit.drawing`; it retains its source-preserving contract.

## Verification

Package goldens are `test/lambda/editor/draw_*.ls` with sibling `.txt` files.
Legacy drawing fixtures and demo replays remain in place. Run focused package
and legacy contracts with:

```sh
./test/test_lambda_gtest.exe --gtest_filter='*editor_draw_*:*tests_draw_*:*editing_drawing_*:*editor_drawing_block*'
./test/test_ui_automation_gtest.exe --suite draw --jobs 1
./test/test_ui_automation_gtest.exe --suite edit --test 'edit_edit_svg_*' --jobs 1
```

Use `LAMBDA_EXEC_BACKEND=interp`, `auto`, and `jit` for tier qualification
(**D8.1.1v18**). The two-canvas native fixture is
`test/ui/draw/package_canvases.json`; its source is `_canvases.ls`. Screenshots
and file outputs stay under `./temp/`.
