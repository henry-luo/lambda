# Native map acceptance and supported style manifest

The public package is `lambda.map` (D7.2.4). Its pure validation, evaluation,
queries and export use declared Radiant functions (S12.1.1v2, D7.5.3).
Displayed frames own an Input arena, compiled expressions, evaluated paths and
a balanced bounding-box index (D4.5.2). The document registry retains those
native owners; public snapshots copy their feature metadata to precisely rooted
Lambda values (D5.3.3). No native frame retains GC pointers.

## Implemented properties

| Layer | Paint properties |
|---|---|
| `background` | `background-color`, `background-opacity` |
| `fill` | `fill-color`, `fill-opacity`, `fill-outline-color` |
| `line` | `line-color`, `line-opacity`, `line-width` |
| `circle` | `circle-color`, `circle-opacity`, `circle-radius`, `circle-stroke-color`, `circle-stroke-width`, `circle-stroke-opacity` |

Each layer accepts `minzoom`, `maxzoom`, and `layout.visibility`. Feature
filters apply to fill, line and circle; background paint cannot depend on
features. Colors are hex strings or `transparent`. Opacity is within 0–1;
size is within 0–4096 CSS pixels. Radius defaults to 5, width to 1, opacity to
1 and color to black. Unsupported properties and source kinds return ordinary
errors (S7.4.1). There are no transitions, feature state or legacy filters.

Lines also accept static `layout.line-cap` (`butt`, `round`, `square`) and
`layout.line-join` (`miter`, `round`, `bevel`). Defaults remain butt/round for
this bounded profile; miter limit is 4. Fill outlines are optional centered
1 CSS pixel strokes and share fill opacity. Circle radius is the inner fill
radius; a stroke extends outward by its width. Circle stroke defaults are
black, width 0 and opacity 1, independent of fill opacity. Every new paint
property accepts the same typed expression subset below.

## Implemented expressions

Argument counts below exclude the operator. Scalar values are literal;
arrays/objects used as data require `literal`.

| Operators | Arguments and result |
|---|---|
| `literal` | 1 arbitrary supported data value |
| `get`, `has` | 1 string key, optionally an explicit object; value / boolean |
| `id`, `geometry-type`, `zoom` | 0; feature identity, geometry family, numeric zoom |
| `coalesce` | 1 or more; first non-null value |
| `==`, `!=`, `<`, `<=`, `>`, `>=` | 2 scalar operands; strict boolean comparison; ordered operands require numbers or strings |
| `all`, `any`, `!` | 0 or more boolean operands / exactly 1; short circuit |
| `case` | Condition/output pairs followed by fallback; at least 3 arguments |
| `match` | Input, label/output pairs, fallback; at least 4 arguments; distinct labels of one string/integer type |
| `step` | Numeric input, default, stop/output pairs; at least 4 arguments |
| `interpolate` | `["linear"]`, numeric input, at least two stop/output pairs; numeric outputs |
| `number`, `string`, `boolean` | 1 or more; first value of the requested type; assertions perform no conversion |

Stops increase strictly. Output branches have compatible types. Compilation
checks all branches and constant failures. `get` returns null for missing
properties; `has` distinguishes absence from a present null. Paint zoom is
the direct input of the outermost `step`/`interpolate`. Filter zoom is floored;
paint zoom remains fractional. Runtime errors use the property's default,
while invalid filters exclude the feature. Native painting logs each fallback
kind once per layer; internal evaluation records expose warning bits
1=color, 2=opacity, 4=size, 8=filter, 16=outline color, 32=circle stroke width,
64=circle stroke color, 128=circle stroke opacity.

The compiler allows depth 32 and a shared budget of 1,024 expression visits
and match labels per layer. Frame planning compiles each layer once. Unchanged
displayed frames reuse those programs and paths. Camera/source/style or
document invalidation rebuilds the frame; sharing compiled styles between
different cameras is still open.

`map.plan(model, viewport)` returns an ordinary immutable frame value with
camera, viewport, revision, SVG, evaluated paths, owned feature records and an
index. `map.query_rendered(frame, point_or_box, options)` traverses its index in
reverse paint order, then uses the shared native path hit walker. Points use
`[x,y]`; rectangles use `[[left,top],[right,bottom]]`, clipped to the viewport.
Options are `layers` and `radius` (0–4096 CSS pixels). Geometry queries cover
fill holes, outlines, circle strokes, caps and joins and deduplicate paths of
the same feature/layer. `query_rendered(model, ...)` plans that model first.
`snapshot(node)` and `query_displayed(node, ...)` are procedures that read the
last painted frame, without advancing layout. An old frame remains usable
after camera changes, resize or viewport removal. `render_frame(frame)`
returns its SVG element.

Native planning limits children to 1,024, visited vertices and emitted paths to
262,144, feature visits to 16,384 and geometry depth to 32. Snapshot metadata
must be JSON-shaped, depth ≤64 and at most 262,144 nodes per feature. Packed
queries additionally bound path parsing to 64 MiB and validate index traversal.
Viewport dimensions and the absolute magnitude of query coordinates are
bounded to 1,048,576 CSS pixels.

`interactive(spec, options)` supports a 3 CSS pixel drag threshold, captured
dragging, click suppression after dragging, hover/selection, anchored wheel
and double-click zoom (Shift reverses), arrows/plus/minus, Home reset and
Escape selection clear. The shared `radiant.local_point` unprojects the painted
CSS plane, including rotation, skew and perspective. Optional `controls:true`
adds ordinary HTML zoom/reset buttons and a fit button when `bounds` is supplied
as `[west,south,east,north]`; `padding` controls fit inset. `feature_list:true`
adds a live selected-feature list. `label` sets the map's accessible label.
Procedural `on_select`/`on_hover` receive `{node,features,point,lnglat}`;
`on_camera` receives `{node,camera}` after an input-driven camera change.
Autonomous resize notifications and camera animation remain open.

## Frozen reference and execution

`package-lock.json` pins `@maplibre/maplibre-gl-style-spec@26.4.4` (ISC),
[release](https://github.com/maplibre/maplibre-style-spec/releases/tag/v26.4.4),
[commit](https://github.com/maplibre/maplibre-style-spec/commit/1033d48ba10b991d6d9f60afbed94c81662ef038).
No upstream code is copied into the engine. The original
[`expression_cases.json`](expression_cases.json) compares native results with
that evaluator, including compile failures, null/missing values, lazy branches,
zoom, defaults, labels and alpha. This is expression acceptance, not full
MapLibre compatibility or an independent geographic render reference.

```bash
npm ci --prefix test/map --ignore-scripts
make test-map-reference
make test-map
make test-map-export
```

`LAMBDA_BIN=/absolute/path/lambda.exe node test/map/check_expressions.cjs`
pins a preserved host. Its report records the binary and corpus SHA-256 plus
each result under `temp/map-expression-reference/report.json`. Native renderer
fixtures are `.ls` documents in this directory; functional scripts/goldens live
under `test/lambda/map/`. `expressions.ls` paints filtered features, a matched
color, an interpolated radius and ID-dependent alpha. `check_exports.cjs`
checks its pixels through native PNG and independent librsvg/Poppler rendering
of vector SVG/PDF, alongside geometry, stroke and rotated-viewport fixtures.
