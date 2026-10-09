# Native map acceptance and supported style manifest

The public package is `lambda.map` (D7.2.4). Its pure validation, evaluation,
queries and export use declared Radiant functions (S12.1.1v2, D7.5.3).
Compiled expressions borrow precisely rooted input only during one call
(D4.5.2, D5.3.3); no map registry retains Lambda values.

## Implemented properties

| Layer | Paint properties |
|---|---|
| `background` | `background-color`, `background-opacity` |
| `fill` | `fill-color`, `fill-opacity` |
| `line` | `line-color`, `line-opacity`, `line-width` |
| `circle` | `circle-color`, `circle-opacity`, `circle-radius` |

Each layer accepts `minzoom`, `maxzoom`, and `layout.visibility`. Feature
filters apply to fill, line and circle; background paint cannot depend on
features. Colors are hex strings or `transparent`. Opacity is within 0–1;
size is within 0–4096 CSS pixels. Radius defaults to 5, width to 1, opacity to
1 and color to black. Unsupported properties and source kinds return ordinary
errors (S7.4.1). There are no transitions, feature state or legacy filters.

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
1=color, 2=opacity, 4=size, 8=filter.

The compiler allows depth 32 and a shared budget of 1,024 expression visits
and match labels per layer. Painting compiles each layer once. Rendered queries compile one layer
for a batch of up to 16,384 features, then use its evaluated radius/width,
visibility and alpha. Queries still scan geometry linearly and describe the
supplied model, without a retained frame index.

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
of vector SVG/PDF, alongside four geometry fixtures.
