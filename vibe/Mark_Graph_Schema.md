# Mark Graph Notation (MGN) Schema

A comprehensive graph markup language based on **Mark Notation**, designed to express structure, layout, styling, and semantics across major graph formats — Graphviz, Mermaid, PlantUML, D2, Cytoscape.js, BPMN, UML, etc.

Reference:
- DOTML: https://www.martin-loetzsch.de/DOTML/
- the styling attributes are designed to align with CSS and SVG;

---

## 1. Top‑Level Structure

```mark
<graph
  id: myGraph
  ir-stage: "canonical"     // source or canonical
  directed: true
  layout: "dot"           // e.g. dot, neato, fdp, force, radial, grid, dagre
  rank-dir: "LR"          // LR, TB, RL, BT (for hierarchical layouts)
  background: "#fff"
  font-family: "Inter"    // CSS-aligned naming
  theme: "light"
  comment: "Enhanced demonstration graph"
>
  <meta>
    <author>Henry</author>
    <version>2.0</version>
    <description>"Enhanced graph with flattened structure and CSS-aligned attributes"</description>
  </meta>

  <style>
    <node fill:"#E3F2FD" stroke:"#1E88E5" font-family:"Inter" shape:"rect" />
    <edge color:"#555" stroke-width:2 stroke-dasharray:"solid" />
    <graph margin:20 padding:10 />
  </style>

  <defs>
    <node-style id:highlight fill:"#FFF59D" stroke:"#F9A825" />
    <edge-style id:dashed stroke-dasharray:"dashed" color:"#8E24AA" />
  </defs>

  <!-- Direct children - no <nodes> wrapper -->
  <node id:A label:"Start" tooltip:"Entry point" icon:"🚀" style:highlight />
  <node id:B label:"Process" shape:"ellipse" fill:"#C8E6C9" />
  <node id:C label:"Decision" shape:"diamond" fill:"#FFE0B2" />
  <node id:D label:"End" shape:"rect" fill:"#FFCDD2" />

  <!-- Direct children - no <edges> wrapper -->
  <edge from:A to:B label:"begin" />
  <edge from:B to:C label:"evaluate" />
  <edge from:C to:D label:"yes" style:dashed />
  <edge from:C to:B label:"no" color:"#F57C00" />

  <subgraph id:cluster1 label:"Loop section" rank:"same" color:"#90CAF9">
    <node id:E label:"Check" />
    <node id:F label:"Retry" />
    <edge from:E to:F label:"retry" />
    <edge from:F to:E label:"loop" />
  </subgraph>
>
```

---

## 2. Elements and Attributes

### 2.1 `<graph>`
| Attribute | Type | Description |
|------------|------|-------------|
| id | string | Unique identifier |
| ir-stage | string | `source` preserves parser declarations; `canonical` has resolved identities |
| directed | bool | `true` for digraphs |
| layout | string | Layout engine or algorithm |
| rank-dir | string | Direction for hierarchical layouts |
| background | color | Background color |
| font-family | string | Default font |
| theme | string | "light" or "dark" |
| margin, padding | number | Space around the graph |
| scale | number | Global zoom scaling |
| width, height | number | Fixed graph dimensions |

Source-stage and canonical graph values share this Mark representation. A
source-stage Mermaid graph may contain repeated node ids so each declaration
retains its own values and source span. Normalization resolves those declarations
before emitting a canonical graph, where node ids are unique.

### 2.2 `<node>`

Nodes may be direct children of `<graph>` or recursively contained by a
`<subgraph>`.

| Attribute                 | Type          | Description                        |
| ------------------------- | ------------- | ---------------------------------- |
| id                        | string        | Unique node identifier             |
| label                     | string        | Legacy input shorthand for `<label>` |
| shape                     | string        | `rect`, `ellipse`, `diamond`, etc. |
| fill, stroke              | color         | Colors                             |
| width, height             | number        | Size override                      |
| icon                      | string        | Emoji, Unicode, or icon name       |
| image                     | URL           | Embedded image                     |
| style                     | ref           | Reference to a style in `<defs>`   |
| tooltip                   | string        | Hover text                         |
| href                      | URL           | Hyperlink                          |
| group                     | string        | Logical grouping                   |
| pos                       | x,y           | Fixed position                     |
| font-family, font-size, color | string/number | Text styling (CSS-aligned)         |
| opacity                   | float         | Transparency                       |

A node may contain scoped `<port>` children in addition to its canonical
`<label>/<content>` pair.

### 2.3 `<edge>` (Direct children of `<graph>`)
| Attribute                 | Type          | Description                  |
| ------------------------- | ------------- | ---------------------------- |
| from, to                  | node id       | Endpoints                    |
| from-port, to-port        | port id       | Optional ports scoped to the endpoint nodes |
| label                     | string        | Legacy input shorthand for `<label>` |
| style                     | ref/string    | Reference to style in `<defs>` |
| stroke-dasharray          | string        | Line pattern: `solid`, `dashed`, etc. |
| color                     | color         | Line color                   |
| stroke-width              | number        | Line thickness               |
| arrow-head, arrow-tail    | string        | Arrow style                  |
| weight                    | number        | Layout ranking weight        |
| constraint                | bool          | Affects layout               |
| curve                     | string        | `smooth`, `poly`, etc.       |
| tooltip                   | string        | Hover text                   |
| href                      | URL           | Hyperlink                    |
| label-position            | string        | `mid`, `head`, `tail`        |
| font-family, font-size, color | string/number | Label styling (CSS-aligned)     |
| opacity                   | float         | Transparency                 |

### 2.4 `<subgraph>`
| Attribute | Type | Description |
|------------|------|-------------|
| id | string | Cluster name |
| label | string | Display title |
| rank | string | “same”, “min”, “max” |
| color, fill, stroke | color | Cluster border styling |
| style | string | `dashed`, `rounded`, etc. |
| padding | number | Space around member nodes and nested clusters |
| label-gap | number | Gap between the measured label band and content |
| radius | number | Visual cluster corner radius |
| z | integer | Signed cluster paint order |

### 2.5 `<port>`

```mark
<node id:"api";
  <port id:"request" side:"west" offset:0.5>
  <port id:"response" side:"east" offset:0.25>
  "API"
>
```

| Attribute | Type | Description |
|-----------|------|-------------|
| id | string | Stable identity, unique within the containing node |
| side | enum | `auto`, `north`, `east`, `south`, `west`, or the CSS aliases `top`, `right`, `bottom`, `left` |
| offset | number | Position along the selected border-box side, clamped to `[0, 1]` |
| z | integer | Signed metadata stacking value |

An edge names ports with `from-port` and `to-port`. Normalization reports a
duplicate port identity within one node or a reference that does not resolve on
the corresponding endpoint node. Port coordinates use the node border box;
`x` and `y` remain border-box top-left coordinates in Radiant placements.

### 2.6 Canonical `<label>` and `<content>`

Normalization rebuilds every labeled node, edge, and subgraph with separate
source and measured-content children:

```mark
<node id:"api" label:"The **API**" label-format:"markdown";
  <label format:"markdown"; "The **API**">
  <content; "The " <strong; "API">>
>
```

`<label>` preserves the source value and format. `<content>` is the Mark subtree
consumed by HTML transformation and measured by Radiant. Plain, Markdown, and
HTML label shorthands are lowered into this pair. Authored rich node children
are wrapped in `<content>` without reducing them to inline text, so normal
block, inline, table, flex, grid, replaced, and SVG descendants remain
available to Radiant layout.

Canonical consumers use these children as the authority. Legacy `label` and
`label-format` attributes may remain for source compatibility and provenance,
but do not override `<label>` or `<content>`. Normalization is recursive and
idempotent, and preserves unrelated attributes.

`lambda.package.graph.schema` enforces the structural Graph IR boundary before
normalization rebuilds the tree. It validates known attribute types and enum
values, required node/subgraph identities and edge endpoints, permitted child
placement, and the cardinality of canonical `<label>/<content>` pairs. Unknown
attributes remain available for source provenance and future extensions, while
unknown structural children produce diagnostics.

### 2.7 `<style>` and `<defs>`
Used for global and reusable style definitions.

```mark
<style>
  <node fill:"#f0f0f0" stroke:"#000" />
  <edge color:"#555" stroke-width:1.5 />
  <graph margin:10 />
</style>
<defs>
  <node-style id:warning fill:"#FFF59D" stroke:"#F9A825" />
  <edge-style id:highlight color:"#E91E63" stroke-dasharray:"dashed" />
</defs>
```

### 2.8 `<meta>`
Metadata for authorship, versioning, provenance, etc.

```mark
<meta>
  <author>Henry</author>
  <version>2.0</version>
  <license>MIT</license>
  <date>2025‑10‑25</date>
  <source>"https://example.com/data"</source>
>
```

---

## 3. Optional Extensions

### 3.1 Data attributes
```mark
<node id:X type:"function" data:"relu" />
<edge from:X to:Y weight:0.85 />
```

### 3.2 Clustering
```mark
<cluster id:GroupA color:"#E0F7FA">
  <members>[A,B,C]</members>
>
```

### 3.3 Events and animations
```mark
<edge from:A to:B on:hover color:"#FF0000" />
```

### 3.4 Layering and constraints
```mark
<node id:X layer:2 />
<constraints>
  <sameRank>[A,B,C]</sameRank>
  <order>[A,B,C,D]</order>
>
```

Measured labels that are not primary node, edge, or cluster content are
graph-level owner records. Keeping them beside interactions avoids embedding
cross-object metadata inside measured node content:

```mark
<annotation owner-kind:"node" owner-id:"api" kind:"external"
  label:"public endpoint" label-format:"text">
<annotation owner-kind:"edge" owner-id:"calls" kind:"head"
  label:"response" label-format:"text">
```

The initial layout contract recognizes `external`, `center`, `head`, and
`tail`; adapters may preserve additional kinds for future placement policies.

---

## 4. Example

```mark
<graph directed:true layout:"dot" theme:"light">
  <style>
    <node fill:"#E3F2FD" stroke:"#1E88E5" />
    <edge color:"#757575" />
  </style>
  <node id:Login label:"User Login" shape:"rect" icon:"🔐" tooltip:"Start of flow" />
  <node id:Validate label:"Validate Credentials" shape:"diamond" />
  <node id:Dashboard label:"Dashboard" shape:"ellipse" />
  <node id:Error label:"Error Page" shape:"rect" fill:"#FFCDD2" />
  <edge from:Login to:Validate label:"submit" />
  <edge from:Validate to:Dashboard label:"success" color:"#43A047" />
  <edge from:Validate to:Error label:"fail" color:"#E53935" stroke-dasharray:"dashed" />
  <subgraph id:authFlow label:"Authentication Flow" color:"#BBDEFB" />
</graph>
```

---

## 5. Compatibility Mappings

| Target | Mapping | Notes |
|---------|----------|-------|
| Graphviz DOT | `graph.directed` → digraph/graph; attributes map 1:1 | Full coverage |
| Mermaid | Layout → graph direction (`LR`, `TD`); `<subgraph>` → `subgraph` | Supported |
| PlantUML | `<edge>` → `A --> B : label`; styles → skinparam | Good coverage |
| D2 | Direct mapping to D2 syntax | |
| Cytoscape.js | Export to JSON `{ data, style }` | |
| BPMN/UML | Extend node `type` (e.g., “task”, “event”) | |

---

## 6. Implementation Roadmap

1. Parser → Reuse Mark parser (recursive descent).
2. Builder → Intermediate representation (nodes, edges, clusters, styles).
3. Exporters → DOT, Mermaid, PlantUML, D2, JSON.
4. Renderer → via D3.js, Dagre, or Graphviz.
5. Validator → declarative structure and attribute checks plus semantic
   identity, endpoint-reference, and style-inheritance checks.

---

© 2025 Mark‑Graph Notation Schema Specification v1.0
