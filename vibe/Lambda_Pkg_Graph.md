# Lambda Graph Package — Design

**Status:** Consolidated working design, 2026-10-23. Rich-node layout, the semantic HTML transform, generated SVG paint with signed stacking, automatic layout connection, Mermaid flowchart/class/ER/flat-state support, Graphviz DOT with the layered `dot` engine, and Structurizr DSL with static, dynamic, and deployment C4 views are implemented. The C `layout_graph()`/`graph_to_svg()` pipeline is retired. §15 records open items and deferred work.

**Scope:** Topology-oriented diagrams — node ranking or placement, recursive grouping, relationship routing, ports, markers, and overlap avoidance — from Mermaid, Graphviz DOT/GV, D2, Structurizr DSL, and authored Mark sources. This document replaces the former Stage 1–3 graph proposal and the separate Structurizr/C4 proposal (`Lambda_Pkg_Graph2.md`). Implementation phase plans, module inventories, parser LOC and binary-size ledgers, test-runner mechanics, and status reports are omitted.

**Specification linkage:** [D7.5.3v2](../doc/Lambda_Formal_Design.md#d75-jube-modules-trust-and-io) covers the automatic Lambda↔Radiant layout connection (§7.2); [D5.3.3](../doc/Lambda_Formal_Design.md#d53-gc-rooting) covers the explicit rooting of generated paint (§7.2); [D7.2.4v4](../doc/Lambda_Formal_Design.md#d72-script-packages) covers the shipped `lambda.*` namespace and the graph/chart package split (§2); [S12.1.1v3](../doc/Lambda_Formal_Semantics.md#s121-the-one-bit-effect-system) covers the purity of the layout export and every normalizer (§1); [S7.6.2v3/S7.6.3v2](../doc/Lambda_Formal_Semantics.md#s76-discharge-the-handler-and-postfix-) cover the postfix error handler used by the CLI bridge (§7.4). Graph-specific choices below are working design, not formal-spec rulings.

## 1. Purpose and design principles

The graph package turns graph data from any supported source language into a semantic HTML document whose rich node content is laid out by Radiant and whose graph geometry — ranks, coordinates, routed edges, clusters, labels — is computed by pure Lambda functions.

1. **Rich nodes.** Graph nodes contain arbitrary rich content, not only text labels. Radiant's existing block, inline, flex, grid, table, replaced-element, text, and whole-SVG layout paths measure every node; the graph layout never approximates text size.
2. **Pure geometry.** Ranking, ordering, positioning, routing, clipping, and label placement are pure `fn` code (S12.1.1v3). The layout callback does not mutate the DOM, perform I/O, start timers, or request relayout.
3. **One interchange format.** Every source language normalizes to one public Mark Graph IR (§4). Parsers record syntax; pure Lambda normalizers apply source-language semantics. There is no private per-language AST data model.
4. **HTML first.** The primary output is semantic HTML, not a pre-laid-out SVG. Final SVG, PDF, PNG, and JPEG are produced by rendering that HTML through Radiant.
5. **Nodes and links in one result.** One custom layout pass positions nodes and routes links, returning placements plus generated paint layers that interleave with nodes through one signed `z` order.
6. **Retained runtime.** The JIT-compiled layout export and its module state are reused across repeated layout passes; parsing and normalization happen once per source graph.
7. **Source fidelity and diagnostics.** Unsupported or malformed syntax produces structured, source-spanned diagnostics. Nothing is silently discarded, silently reinterpreted as a different diagram family, or presented as a partially canonical graph that appears valid.
8. **Safe by construction.** Authored callbacks, URLs, scripts, plugins, includes, and images are inert metadata during parse, normalization, transform, and layout. No external process, network fetch, or executable extension is ever invoked.
9. **Semantic conformance.** Conformance compares renderer-neutral semantics and tolerant geometric relations (§13), never raw SVG strings, screenshots, or pixel parity with Mermaid, Graphviz, or Structurizr.
10. **No external engines.** Graphviz, Mermaid, Node.js, Java, the Structurizr CLI, and browsers are never production dependencies; they appear only as pinned reference-generation maintenance tools.

## 2. Package boundaries

### 2.1 Graph versus chart

Package ownership follows the required layout model, not the source language name.

- **`lambda.graph`** owns diagrams whose defining problem is graph topology: Mermaid flowchart, class, state, and entity-relationship diagrams (and, later, requirement, architecture, block, and mindmap families); Graphviz DOT/GV; D2; and Structurizr/C4 static, dynamic-collaboration, and deployment views.
- **`lambda.chart`** owns data-, axis-, and time-oriented diagrams: Mermaid sequence, Gantt, pie, Sankey, timeline, and XY families. They are never represented as fake layered graphs merely because their source syntax is Mermaid.

The Mermaid input parser dispatches by diagram family:

```text
Mermaid source
  -> source-stage Mark with source-order declarations
  -> graph-oriented family -> canonical Mark Graph IR -> lambda.graph
  -> chart-oriented family -> canonical Mark Chart IR -> lambda.chart
```

Chart-oriented families that `lambda.chart` does not yet support produce an `unsupported` result with a `mermaid.chart-family` diagnostic naming `lambda.chart` as the owner. The supported `sequenceDiagram` header enters the chart sequence adapter, which lays out participant columns, lifelines, messages, loop/alt/par blocks, notes, and activation bars without graph ranking or routing (D7.2.4v4).

### 2.2 Why C4 belongs in the graph package

Static C4 diagrams are topology-oriented: their defining operations are model projection, visual containment, relationship routing, boundary layout, and overlap avoidance. People, systems, containers, components, and instances become nodes; groups, system/container boundaries, and deployment nodes become clusters; model relationships become edges; view rules select the projection; `autoLayout` maps to layered direction and separation. A Structurizr dynamic view renders as a collaboration graph here; its sequence rendering is an ordered-axis layout that belongs to `lambda.chart` (§11.6).

### 2.3 Non-goals

- Pixel parity with Mermaid, Graphviz, Dagre, or Structurizr. The package implements deterministic cluster-aware ordering, obstacle-aware routing, and label collision handling sufficient for the declared conformance surface.
- An eager `transform.to_svg()` that bypasses Radiant measurement (deferred, §15).
- SVG primitives as individually measured CSS layout children (§9.3).
- Byte-identical source formatting or comment preservation.
- Execution of any authored callback, script, plugin, or JVM extension.

## 3. Pipeline

```text
Mermaid / DOT / GV / D2 / Structurizr / authored Mark
  -> manual C/C+ format parser                     (syntax only)
  -> source-stage Mark  (ir-stage: "source")       (source order, spans, raw values)
  -> pure Lambda semantic normalization            (per-language semantics)
  -> [Structurizr only] canonical C4 Workspace Mark -> selected view projection
  -> canonical Mark Graph IR (ir-stage: "canonical")
  -> graph.transform.to_html()
  -> semantic HTML <graph>/<node>/<edge>/<cluster>/... tree
  -> Radiant lays out each rich node and label subtree
  -> measured Velmts passed to the retained Lambda layout export
  -> ranks, orders, positions, clusters, routes, places labels
  -> placements + generated SVG paint layers
  -> Radiant interleaves paint and node views by signed z
  -> final SVG / PDF / raster / interactive view
```

The package is organized around these stages:

| Area | Responsibility |
|---|---|
| `lambda.graph.layout` | Pure geometry: `compute(model, opts)` over explicit node sizes and `from_velmts(parent, children, ctx, opts)` over measured Radiant children. Emits geometry and styling metadata, never markup, and does not depend on Radiant rendering types. |
| `lambda.graph.transform` | `to_html(graph, opts)` semantic transform; the automatically resolved `layout(parent, children, ctx)` export; routed geometry to generated SVG paint; in-process `render_svg`/`render_scene` conveniences. |
| normalization, schema, model, style, diagnostics | Common Graph IR validation and canonicalization, recursive model queries, the safe style cascade, and structured diagnostics. |
| `mermaid`, `graphviz`, `structurizr` adapters | Pure per-language normalization and family-specific content lowering. |
| `scene`, `conformance` | Graph Scene Mark adaptation, comparison, and the shared manifest-driven conformance harness. |
| `graph.ls` | Compatibility facade forwarding `make_options()`, `layout()`, and `layout_custom()`. |

Shared layered-layout concerns (ranking, ordering, positioning, shapes, clusters, labels, routing) are factored so that no Mermaid family or source language duplicates ranking or routing logic.

## 4. Mark Graph IR

The public representation between parsers, normalizers, transforms, and formatters is a Lambda Mark element tree; a JSON-shaped map is not the interchange format. The base schema is `vibe/Mark_Graph_Schema.md`.

### 4.1 Source and canonical stages

Source and canonical graphs use the same Mark vocabulary and are distinguished by the root `ir-stage` attribute.

- **Source stage** preserves declarations in source order, exact source spans (`source-start`, `source-end`, `source-line`, `source-column`), raw attribute values, repeated declarations, and format-specific statement elements. It may contain repeated node ids.
- **Canonical stage** applies source-language semantics — redeclaration merging, chained/multi-node edge expansion, defaults, class assignment, subgraph membership, generated identity — and contains unique identities.

A parser may build canonical-shaped Mark directly when its grammar is already canonical, but it still passes through the same validation and normalization contract. Normalization of an already-canonical tree is idempotent and returns it unchanged.

### 4.2 Canonical elements

```mark
<graph version:1 ir-stage:"canonical" flavor:"mermaid" kind:"flowchart"
       directed:true rank-dir:"TB">
  <meta title:"Example" acc-title:"Example flow">
  <styles>
    <style-rule target:"node" class:"warning" fill:"#fff4cc" stroke:"#8a6500">
  >
  <subgraph id:"backend" role:"cluster" label:"Backend" direction:"LR">
    <node id:"api" shape:"rounded" classes:["service"] source-start:42 source-end:71;
      <label format:"markdown"; "The **API** service">
      <content; <strong; "API"> " service">
      <port id:"request" side:"west" offset:0.5>
    >
  >
  <node id:"client"; <port id:"request" side:"east" offset:0.5> >
  <edge id:"request" from:"client" to:"api" from-port:"request" to-port:"request"
        arrow-tail:"none" arrow-head:"normal" line-style:"dashed" stroke-width:2
        constraint:true min-length:2;
    <label position:"center"; "HTTP">
    <content; "HTTP">
  >
>
```

| Element | Role |
|---|---|
| `<graph>` | Identity, flavor, kind, direction, defaults, and options. |
| `<meta>` | Title, accessibility title/description, provenance, source information. |
| `<styles>` / `<style-rule>` / `<style-assignment>` / `<class-assignment>` | Class definitions, direct node/edge styles, and class membership, kept distinguishable until the cascade is resolved (§6.5). |
| `<subgraph>` | Recursive grouping with local direction. `role: "cluster"` is a visual container; `role: "scope"` is a semantic/default/rank scope that is never painted (§11.4). |
| `<node>` | Stable id, shape role, classes, ports, and content. May contain arbitrary Mark in addition to or instead of a label. |
| `<edge>` | Stable id, endpoints and ports, constraints, compositional markers, route mode, labels. |
| `<port>` | Node-local attachment: id, side, normalized offset; ids are unique within the node. |
| `<constraint>` | Same-rank, min/max/source/sink, ordering, minimum length, or fixed placement requirements not owned by one edge. |
| `<annotation>` | Additional measured labels with an owner identity and role: `external-label`, `head-label`, `tail-label`, and notes. |
| `<properties namespace:…>` / `<property>` | Arbitrary source-language attributes (e.g. Graphviz) with `name`, `value`, source span, and, after normalization, `origin` (`direct`, `default`, `inherited`) and defining scope/statement. |
| `<interaction>` | Inert link, tooltip, target, and callback metadata. |
| `<diagnostic>` | Stable code, severity, message, and source location. |

`<label>` preserves the source-level label text and format (`text`, `markdown`, `html`, `record`, `graphviz-html`). `<content>` holds the safe normalized Mark subtree that `to_html()` places in the measured box. Canonical `<label>`/`<content>` children are authoritative; legacy `label`/`label-format` attributes remain only as provenance. This separation lets text semantics be tested without depending on final HTML wrappers.

### 4.3 Normalization invariants

1. Node, edge, subgraph, and annotation identities are stable and unique within a graph.
2. References resolve to canonical identities or produce structured diagnostics.
3. Node redeclarations merge according to source-language semantics.
4. Chained, multi-node, and endpoint-set links expand into explicit edges that record their source statement, segment index, expansion index/count, and a stable `source-id`.
5. Subgraph containment remains recursive and deterministic.
6. Defaults, classes, and inline styles stay distinguishable until the style cascade is resolved; resolved values may be attached for layout and paint.
7. Source spans survive normalization for diagnostics and editor integration. Repeated references keep the first declaration span rather than overwriting provenance.
8. Unknown source extensions are preserved in namespaced metadata or diagnosed — never dropped.
9. Labeled values carry source semantics in `<label>` and measured Mark in `<content>`; a second normalization pass is idempotent.
10. The declarative schema validates element hierarchy, attribute types and enums, required identities and endpoints, and `<label>`/`<content>` cardinality *before* rebuilding, so malformed canonical children cannot be silently collapsed. The public validation result is `{graph, diagnostics, valid}`.

### 4.4 Internal layout records

After normalization and Velmt measurement, the layout may derive compact immutable records. They are an internal calculation form, not a second public graph format. Node `width`/`height` are border-box dimensions read from Velmts; Radiant remains the authority for rich-content size.

```lambda
{
  width: 420.0, height: 280.0,
  nodes: [{id, index, x, y, width, height, rank, order, z}],
  edges: [{id, from, to, segments, arrow_start, arrow_end, style}],
  placements: [{index, x, y, z}]
}
```

Placement `x, y` are border-box top-left coordinates in the custom container's local coordinate system; routed points use the same system.

## 5. Public interface

```lambda
layout.make_options() -> GraphLayoutOptions
layout.compute(model, opts = null) -> GraphLayoutResult
layout.from_velmts(parent, children, ctx, opts = null) -> GraphLayoutResult

transform.to_html(graph, opts = null) -> element
transform.layout(parent, children, ctx)          // automatically resolved export (§7.2)
transform.render_svg(graph, width, height, opts = null)
transform.render_scene(graph, width, height, opts = null)
transform.compare_scenes(actual, expected, policy = null)

structurizr.normalize(source_workspace)
structurizr.view_keys(workspace)
structurizr.project(workspace, view_key = null) -> Graph IR | diagnostics
structurizr.project_all(workspace)
structurizr.to_html(workspace, view_key = null, opts = null)
```

`to_html()` is a pure transformation returning an in-memory Lambda element tree, not a serialized string, so structured values, the compiled callback, and future reactive updates stay in one document runtime. It passes Mark through the normalization boundary before semantic lowering and adds no state. `render_svg()`/`render_scene()` run HTML parsing, Radiant measurement, the retained callback, paint, SVG lowering, and scene adaptation in one process. `structurizr.project()` never installs the callback or renders; HTML and rendering stay with the common transform.

## 6. Semantic HTML transform

### 6.1 Structure

```html
<graph class="lambda-graph" data-radiant-layout="lambda-graph"
       data-direction="TB" data-node-sep="60" data-rank-sep="80" data-edge-sep="10">
  <node class="graph-node" data-node-id="a">...rich content...</node>
  <node class="graph-node" data-node-id="b">...</node>
  <edge data-from="a" data-to="b" data-directed="true"></edge>
  <edge-label data-edge-id="e0">continue</edge-label>
  <cluster data-cluster-id="backend"></cluster>
  <cluster-label data-cluster-id="backend">Backend</cluster-label>
  <port data-node-id="a" data-port-id="out"></port>
</graph>
```

The transform receives only canonical Graph IR; it does not know whether a node came from Mermaid, DOT, D2, Structurizr, or authored Mark except where a namespaced property is intentionally exposed for provenance. Graph options become stable root attributes. Edge options stay on their semantic `<edge>` elements rather than one opaque graph attribute, so the callback reads identity, endpoints, direction, style, markers, ports, constraints, and route hints through Velmt attributes. Authored rich node children become the `<node>` contents; recursive subgraph membership is carried as stable metadata.

### 6.2 Layout participation

All semantic children are direct custom-layout children.

- `<node>`, `<edge-label>`, `<cluster-label>`, and annotation elements are measured boxes whose border boxes participate in geometry and bounds.
- `<edge>`, `<cluster>`, `<port>`, and `<constraint>` are zero-size, non-visual metadata boxes that expose relationship attributes to the callback but neither draw nor contribute size.

```css
graph.lambda-graph { display: layout(lambda-graph); position: relative; }
graph.lambda-graph > node.graph-node { display: inline-block; }
graph.lambda-graph > edge {
  display: block; width: 0; height: 0; overflow: hidden;
  visibility: hidden; pointer-events: none;
}
```

The callback returns `(0, 0)` placements for metadata boxes so every in-flow custom-layout child has an explicit result. Edge and cluster visuals are generated paint (§9), never the metadata boxes themselves.

### 6.3 Rich content and sanitization

Markdown and HTML labels are lowered by reusing Lambda's Markdown and HTML-fragment parsers. The sanitizer reconstructs only safe inline formatting (`strong`, `em`, `code`, `u`, `sub`, `sup`, `br`), plus safe tables/rows/cells for Graphviz record and HTML-like labels (§11.5). It strips authored attributes and unknown wrappers and removes `script`, `style`, and `template` subtrees. Raw authored HTML never reaches the document.

### 6.4 Shapes

Node shape roles render through HTML geometry and matching layout clipping: rectangle, rounded, stadium, ellipse/circle, double-circle, diamond, hexagon, trapezoid and inverse trapezoid, asymmetric, cylinder, subroutine, parallelogram, triangle, octagon, house, card/notch, cloud, hourglass, bolt, lean, tag/document, delay, brace, fork, divided, lined, and general polygons. Source shape names map to shared *geometry families* rather than unrelated code paths; the raw source name stays as canonical provenance. One pure polygon helper generates both normalized CSS clip polygons and the vertices used for routed-edge clipping, so paint and layout never carry independent polygon formulas.

### 6.5 Safe style cascade

Styles are parsed into typed values, never appended as authored CSS. Declarations are tokenized on commas or semicolons without splitting functional colors; properties and values outside the graph allowlist are rejected. The allowlist yields fill, stroke, stroke width, text color, opacity, dash array, and (for formats that define them) font family/size, node size, shape, and route mode. Class definitions cascade before node-specific direct styles. Safe node paint becomes reconstructed inline CSS; safe edge stroke, width, opacity, and dash metadata survives Velmt adaptation, routing, and generated paint. Font families pass through an allowlist with fallback stacks; unsafe values never enter CSS declarations.

## 7. Custom layout callback and runtime

### 7.1 Call flow

```lambda
fn layout(parent, children, ctx) {
  let result = layout.from_velmts(parent, children, ctx)
  {
    width: result.width,
    height: result.height,
    placements: result.placements,
    paint_layers: paint.edge_layers(result)
  }
}
```

Radiant prelays out every direct child, passes the node, label, and metadata Velmts to the export, writes the returned `x/y/z` to child views, stores the generated paint layers on the graph view, and renders paint and child views in one signed order. The function allocates and returns immutable data; it neither mutates the semantic tree nor forces another pass. Negative placements and explicit parent sizes follow the established custom-layout overflow rules.

Port attachment uses measured geometry: the Velmt adapter walks the already-laid-out node subtree, accumulates each port cell's local border box, and retains normalized horizontal and vertical centers. Direct non-Radiant callers fall back to canonical source-order offsets. Velmt descendant exposure stays lazy and bounded.

### 7.2 Automatic connection and lifetime

Under **D7.5.3v2**, graph HTML carries `data-radiant-layout="lambda-graph"` and Radiant automatically invokes `lambda.graph.transform.layout` after measuring the children. This works for package rendering, native document loading, and serialized HTML opened in a fresh process. There is no public registration or `install()` API.

The host resolves the export on the document's retained evaluator. A static HTML document acquires an evaluator lazily; a synchronous render from Lambda shares the caller's live evaluator; existing window-loader ownership is unchanged. Reflow reuses imported module state and compiled code. Module lifetime replaces the former separately rooted callback registry and per-heap cleanup slots, so two live graph documents cannot overwrite each other's callback.

Generated SVG layers keep explicit roots on the owning heap and replace their previous layer set on reflow (**D5.3.3**). Closing one document never disconnects another document's layout or releases its paint. Relayout reuses canonical topology, resolved style data, measured Velmts, and the compiled callback; layout cache keys include layout-affecting source properties and measured child dimensions.

~~The original API required `graph_transform.install()` calling `radiant.register_layout("lambda-graph", callback)`.~~ Superseded by automatic package-export connection (D7.5.3v2, user ruling 2026-10-16).

### 7.3 Standalone HTML

Serializing the transformed tree to a standalone `.html` file loses the retained Lambda runtime: `display: layout(lambda-graph)` names a callback but does not load its package. Portable deferred-layout HTML requires a future declarative, security-scoped module loader (for example, document metadata that opts into a trusted Lambda layout package). Until then, standalone output is a Lambda script that imports the transform, a final SVG/PDF/raster render, or a future eager SVG with explicit node sizes.

### 7.4 CLI bridge

`render`, `view`, `layout`, and `convert -t html` route graph files (`.mmd`, `.d2`, `.dot`, `.gv`, `.structurizr`, and detected `.dsl`) through one Lambda bridge script and the normal HTML loader, with no separate graph-to-SVG branch and no temporary SVG file. Requested `.svg` output comes from Radiant's HTML-to-SVG renderer. The document runtime stays alive through layout, rendering, interaction, and window teardown. The bridge uses the postfix handler (**S7.6.2v3/S7.6.3v2**):

```lambda
import graph_transform: lambda.graph.transform

let graph = input("diagram.mmd", {type: "mermaid"}) ^ { null }
graph_transform.to_html(graph, {theme: "zinc-dark"})
```

Graph syntax parsing and data formatting remain native; all layout and painting is Lambda.

## 8. Layered layout and routing

The engine is a deterministic layered (Sugiyama-family) layout. Exact Graphviz or Dagre ranking and crossing choices are not a requirement; declared semantic constraints and Graph Scene relations (§13) are.

### 8.1 Ranking

- All four directions (`TB`/`TD`, `BT`, `LR`, `RL`) at graph and subgraph level; `TD` normalizes to `TB`.
- Edge `min-length` and `weight`; `constraint=false` edges are excluded from ranking.
- `rank=same|min|max|source|sink` constraints, with same-rank promotion propagated through successors so `min-length` stays valid; self-loops and cycle back-edges are ignored during promotion relaxation.
- Graphviz recursive-rank default versus `newrank=true`: the default excludes rank sets whose members span structural clusters; `newrank` applies them in one global rank space.
- Measured center labels raise the minimum rank gap when the declared gap would crowd the text.

### 8.2 Ordering

Crossing reduction runs a requested number of deterministic barycentric iterations, weighted by edge weight. Direct cluster members stay contiguous, with inter-cluster padding reserved. Nodes group by their complete outer-to-inner cluster ancestry and then by authored alignment `group`, keeping nested clusters and alignment groups contiguous without conflating them. After optimization, authored `ordering=in|out` sequences are reapplied so crossing reduction cannot reverse a declared order. Stable source order is the final tie breaker.

### 8.3 Clusters

Cluster boxes are computed from measured member nodes and child clusters, recursively, with a reserved label band and padding. Only `role: "cluster"` subgraphs are contained and painted; scope-only subgraphs are flattened into the nearest visual container for ownership while keeping their identity, properties, membership, and lowered constraints as metadata.

### 8.4 Routing

Routing runs after node positions are known and produces renderer-independent segment geometry:

```lambda
{id: "e0", from: "a", to: "b",
 segments: [{points: [{x: 120.0, y: 40.0}, {x: 120.0, y: 80.0}], z: -1}],
 arrow_end: true, style: "solid"}
```

Geometry concerns belong to layout; SVG path construction, marker definitions, colors, dashes, and theme mapping belong to paint.

- **Route modes.** Canonical `route-mode` is `none`, `line`, `polyline`, `orthogonal`, or `curved`. Paint omits `none` edges, keeps direct `line` segments, emits waypoint paths for `polyline`/`orthogonal`, and produces deterministic quadratic/cubic paths for `curved`; terminal curve segments keep a defined tangent for arrowheads.
- **Obstacles.** Orthogonal routes avoid unrelated node and cluster boxes.
- **Clipping.** Endpoints clip against the actual node shape, including polygon parameters and fixed inner shapes (§11.5).
- **Ports.** Named ports and compass points determine the attachment side and offset, using measured port-cell geometry when available.
- **Parallel edges.** Edges with the same ordered endpoints get deterministic sibling-local lanes separated by `edge-sep`; when an outer lane extends before the graph origin, nodes and edges shift together.
- **Self-loops.** Spacing uses sibling-local order so unrelated edges cannot change loop geometry; for LR/RL graphs unported loops use the upper cross-axis side.
- **Compound routes.** Routes cross each exclusive source and target cluster boundary in containment order. With Graphviz `compound=true`, an explicit `lhead`/`ltail` cluster becomes the endpoint and the route clips to that cluster's border; outer nested boundaries remain in the route. Parallel compound edges also receive lanes.
- **Feedback.** Singleton feedback ranks shift to separate short and long return paths; long returns route directly when they clear intermediate boxes, otherwise on lanes that clear the endpoint corridor, separate fanout, and avoid loop outlines.
- **Weaving.** A route may be split into segments with different `z` when part must pass behind one node and above another (§9.2).
- **Bounds.** Route extents and placed labels are included in graph bounds.

### 8.5 Label and annotation placement

Labels and annotations are placed against a shared occupied-geometry set containing node boxes, cluster labels, other routed edge segment boxes, and labels already placed in source order. Edge labels anchor at the arc-length midpoint of the complete route (head/tail labels at the ends), using sampled geometry of the same curve the paint layer emits. Candidates expand through bounded rings around the anchor or owner; when a dense graph exhausts them, a deterministic position beyond the occupied right boundary guarantees a non-overlapping result rather than accepting a collision. Graph bounds expand to contain it.

## 9. Generated paint and stacking

### 9.1 Paint-layer contract

The generic custom-layout result extension is:

```lambda
type CustomPaintLayer = { z: int, content: element }

type CustomLayoutResult = {
  width: float?, height: float?, baseline: float?,
  placements: [Placement],
  paint_layers: [CustomPaintLayer]?
}
```

For graphs, `content` is an `<svg>` whose viewport matches the graph content box. Paint layers use the container's local coordinates, do not affect parent sizing, obey the parent's overflow and clipping, are immutable layout output rather than DOM children, are replaced on the next successful pass, are lowered into document-owned paint before the callback's temporary result becomes unreachable, and may group many edges into one SVG subscene per `z`. The contract is generic enough for connectors, annotations, and other custom layouts without graph-specific Radiant fields.

### 9.2 Signed stacking

A single underlay plus overlay cannot express "selected edges above nodes, ordinary edges below" or one edge weaving behind one node and over another. Placements and paint layers therefore share one signed `z` coordinate within the container's local stacking sequence:

1. Paint the container background and border.
2. Merge generated paint layers and child views.
3. Sort by signed `z`, lowest first.
4. Preserve result/tree order at equal `z`; generated paint precedes child content, so a node covers an edge unless the edge requests a greater `z`.
5. Paint outlines and parent-level effects under normal Radiant boundary rules.

Each node subtree is atomic relative to generated paint; its own CSS stacking contexts order its descendants. Defaults: ordinary edge segments and cluster boxes `z = -1`, nodes `z = 0`, selected edges, handles, and highlights `z = 1`. Crossing gaps, masks, and bridge arcs live inside generated fragments without changing node layout. SVG, PDF, raster, and reverse-order hit testing share one stable stacking helper. Generated paint is currently non-interactive; edge hit testing can reuse routed segment geometry.

### 9.3 SVG scope

Radiant treats a root `<svg>` as a replaced box with its own native SVG renderer. A whole SVG may be rich content inside a `<node>` and is measured as that node's content, but `<path>`, `<g>`, `<rect>`, and other primitives are never individual custom-layout children. Generated SVG paint layers are the bridge for edges, markers, and cluster boxes.

### 9.4 Markers

Endpoint markers are compositional, not a single enum. A marker is a sequence of up to four components — `normal`, `inv`, `open`, `circle`/`dot`, `diamond`, `box`, `tee`, `vee`, `crow`, `none`, … — each with open/filled and left/right half-clip modifiers, painted in order at the routed endpoint and scaled by `arrowsize`. Mermaid circle/cross markers, UML class relationship ends, ER crow's-foot cardinalities, and Graphviz arrow grammar all lower to this one vocabulary, so the common paint path needs no family-specific painter. Unsupported components keep their raw specification and use a deterministic fallback marker with a non-fatal diagnostic.

### 9.5 Paint values

Paint supports fill, stroke, width, dash, opacity, peripheries, and weighted multi-stop linear/radial gradients plus hard-stop `striped`/`wedged` fills from color lists.

## 10. Final SVG metadata

Final SVG preserves renderer-neutral identity metadata for accessibility, hit testing, editor integration, and semantic conformance:

```html
<g data-graph-role="node" data-node-id="api">...</g>
<rect data-graph-role="cluster" data-cluster-id="backend">...</rect>
<path data-graph-role="edge" data-edge-id="request" data-from="client" data-to="api">...</path>
<g data-graph-role="edge-label" data-edge-id="request">...</g>
```

Roles cover graph, node, edge, edge-label, cluster, cluster-label, and annotation, with stable source identities, direction, shape, marker, label, and route metadata; Radiant records measured absolute border-box geometry. These attributes are not styling hooks and do not determine paint order. Wrapper tags, classes, marker ids, and grouping remain implementation details. Accessibility title and description surface as `aria-label`, `aria-description`, and graph title metadata.

## 11. Source formats

### 11.1 Parser principles

All graph parsers are compact manual C/C+ recursive-descent or line-oriented parsers that emit source-stage Mark directly.

- **Syntax only.** Parsers do not apply defaults, merge nodes, expand edge products, convert units, rename attributes into CSS names, resolve identifiers, evaluate view expressions, or apply style cascades. All interpretation lives in pure Lambda normalizers.
- **Shared lexical helpers, separate grammars.** Whitespace/comment skipping, quoted strings, bounded identifier/value readers, boundary-aware keyword matching, source spans, diagnostics, generated source ids, balanced-block recovery, and Mark builders are shared and format-neutral. Grammar state machines are not unified: a generic graph parser would hide distinct grammars behind conditionals. A new format searches existing helpers and promotes a narrowly parameterized one before adding a local copy.
- **Explicit limits.** Maximum statement/subgraph nesting depth, HTML-label nesting and table cells, chained endpoint count, edge count after expansion, identifier/string/value length, statement count, and input size each produce a diagnostic.
- **Recovery.** Invalid input returns the recoverable source graph plus diagnostics, recovering at the next newline or balanced closing brace.
- **Size is a constraint.** Parser physical LOC and release-binary contribution are actively budgeted and enforced by a test; a ceiling increase must first reduce duplication. LOC is never reduced by compressing statements or deleting comments.

**Why not Tree-sitter.** A 2026-07-15 experiment measured pinned Tree-sitter DOT and Mermaid grammars against the manual parsers. Generated DOT tables alone were ~36 KiB larger than the manual parser before a CST-to-Mark adapter; the full Mermaid grammar was ~179 KiB larger and parsed only 8 of 34 existing Lambda Mermaid fixtures without error nodes. The completed manual DOT parser added full DOT fidelity for a 16-byte release-binary delta. Tree-sitter remains an independent grammar reference, not a production dependency.

### 11.2 Mermaid flowchart

Feature groups are tracked in the conformance manifest as `baseline`, `extended`, `unsupported`, or `invalid`; producing an SVG is not a pass.

- **Identity and redeclaration.** The parser never merges explicit declarations: `A[Initial]` then `A{Final}` produce two source nodes. Normalization applies graph-global last-declaration semantics (label `Final`, shape `diamond`, provenance of the effective declaration). Bare endpoint references stay deduplicated because they carry no later value. Every edge materializes both endpoints; subgraph references do not create duplicate implicit nodes. Quoted ids, Unicode, and escapes are supported.
- **Shapes.** Legacy delimiter shapes and the general `id@{ shape: …, label: … }` form; aliases (`rect`, `diam`, `dbl-circ`, `cyl`, …) canonicalize to shared shape roles.
- **Labels.** Plain, quoted (delimiter quotes removed), Markdown-string, and HTML labels with wrapping and line breaks, lowered to `<label>`/`<content>`.
- **Links.** Solid, dotted, thick, open, directed, bidirectional, circle, and cross; extra link units normalize to `min-length` and are enforced by ranking. Chained links expand operator by operator with independent styling; multi-node `&` sets expand to their Cartesian product, and chained operators consume the preceding target set. `edgeId@operator` gives stable edge identity, with deterministic suffixes when one declaration expands to several edges. Edge `@{ animate, animation, curve }` properties and edge-id class assignments resolve through `source-id`.
- **Subgraphs.** Recursive, with explicit ids, quoted labels, local direction, and links to subgraphs.
- **Styles.** `classDef`, `class`, `:::` (normalized to ordinary class assignments), `style`, and `linkStyle` (node ids, edge indices, explicit edge ids, `default`), feeding the safe cascade (§6.5). Original declarations remain as provenance.
- **Configuration.** YAML front matter and `%%{init: …}%%` are preserved as source metadata and parsed structurally; supported title, node/rank spacing, curve, and HTML-label settings lower into the transform.
- **Accessibility.** `accTitle` and line or block `accDescr` survive under source-spanned `<meta>` with last-declaration semantics.
- **Interactions.** `click` link and callback declarations become inert `<interaction>` metadata (tooltip, target, balanced callback arguments); nothing executes.
- **Diagnostics.** Parser errors and warnings are structured `<diagnostic>` values; chart families use `mermaid.chart-family`.

### 11.3 Mermaid class, ER, and state diagrams

Family adapters are added only when their semantics fit, or deliberately extend, the common Graph IR. They reuse rich nodes, compositional markers, recursive clusters, annotations, routing, and the semantic runner. A shared compartment adapter rebuilds measured title/body content for class, ER, and state nodes.

- **`classDiagram`.** Single and comma-separated declarations, aliases and escaped names, nested generics, source-spanned fields and methods, inline and separate stereotypes, visibility and static/abstract classifiers, direction, global and class-owned notes, recursive labeled/dotted namespaces, relationship labels and cardinalities, and the inheritance, composition, aggregation, association, dependency, realization, lollipop, two-ended, and plain-link operators. `classDef`, `cssClass`, inline class suffixes, direct styles, and link/callback/click interactions share the common metadata path. Stereotypes, headers, fields, and methods become distinct measured UML compartments; `hideEmptyMembersBox` controls empty compartments. Namespaces are recursive clusters; notes are annotations; UML ends are compositional markers.
- **`erDiagram`.** Entity declarations and aliases, typed and nullable attributes, PK/FK/UK key and comment metadata, direction, labeled identifying and non-identifying relationships, and all four symbolic cardinality roles. Attributes become measured compartments; cardinalities are preserved semantically and lowered to `tee`, `crow`, and open-dot marker components.
- **`stateDiagram` / `stateDiagram-v2` (flat).** Direction, declarations, quoted aliases, repeated descriptions, labeled transitions, start/end pseudostates, choice/fork/join roles, left/right notes, class/style directives, and accessibility metadata. Marker states receive fixed measurable content so empty semantic labels do not collapse during child layout. Notes are annotations with ownership, kind, text format, and final bounds in Graph Scene Mark. Composite states produce `mermaid.state.composite-unsupported` instead of being flattened or misrepresented.

### 11.4 Graphviz DOT: source contract and semantics

DOT is the description language; `dot`, `neato`, `fdp`, `sfdp`, `circo`, `twopi`, `osage`, and `patchwork` are distinct engines. Parsing DOT does not imply every engine is ported (§11.5).

**Parser surface.** Quoted, numeric, ordinary, Unicode, and HTML-string ids (with source-kind metadata); quoted-string concatenation and escaped physical newlines; case-insensitive keywords; comments; node ports and compass points; chained edge right-hand sides; subgraphs, anonymous subgraphs, and brace-only sets as edge endpoints; repeated attribute lists and separator-free assignments; graph-level assignments; strict graphs.

**Source-stage Mark** uses `ir-stage: "source"`, `flavor: "dot"` and preserves exact statement order, a source span on every statement/endpoint/list/property, repeated declarations and property names in order, raw names and values, and edge chains as one statement until normalization. Anonymous subgraphs receive stable generated source identities without being classified as clusters.

```mark
<graph ir-stage: "source", flavor: "dot", id: "G", directed: true, strict: false;
  <dot-attr-statement target-kind: "node";
    <property name: "shape", value: "box">
  >
  <node id: "a"; <properties namespace: "graphviz"; <property name: "label", value: "Start"> > >
  <dot-edge-statement id: "dot-stmt-4";
    <dot-endpoint kind: "node", id: "a", port: "out", compass: "e">
    <dot-endpoint kind: "node", id: "b">
    <dot-endpoint kind: "node", id: "c">
    <properties namespace: "graphviz"; <property name: "label", value: "next"> >
  >
>
```

**Normalization**, in source order:

1. Establish graph kind, strictness, identity, and lexical scopes.
2. Keep independent graph, node, and edge default environments per subgraph scope (temporal defaults).
3. Materialize implicit nodes at first reference with the defaults active at creation.
4. Merge later declarations into the same identity without retroactively applying newer defaults.
5. Resolve ports and compass points. The ambiguous one-token endpoint suffix is resolved only after record/HTML content has generated its real ports: a matching name is a port, otherwise `n|ne|e|se|s|sw|w|nw|c|_` is a compass point.
6. Expand subgraph endpoint sets and chained right-hand sides into pairwise canonical edges with statement/segment/expansion provenance, bounded at 100,000 edges.
7. In strict graphs, canonicalize directed or unordered endpoint identity, merge repeated edges into the first canonical edge, and apply later explicit attributes, retaining per-property defining statements.
8. Classify only `cluster*` subgraphs as `role: "cluster"`; ordinary and anonymous subgraphs are `role: "scope"`.
9. Resolve known attributes into canonical values while keeping the complete namespaced property set with origin and defining scope.
10. Lower scope properties such as `rank=same` to `<constraint>` values; graph assignments such as `rankdir=LR` stay scope properties, never node declarations.
11. Emit `ir-stage: "canonical"` and run the common schema and relational validators.

**Attributes** are interpreted through declarative tables keyed by object kind and name, each defining value grammar, intentional default, canonical target, unit conversion, inheritance behavior, ownership (layout, content, paint, interaction, metadata), and status (supported, preserved-only, unsupported). Unknown application attributes are preserved without error.

**Units.** Canonical geometry is CSS pixels, because Radiant measures in CSS pixels. Inch-valued node dimensions and margins and point-valued font/pen sizes use explicit attribute-specific conversions; they are never parsed as unitless CSS numbers. Reference geometry in points converts at `96/72`, and Graphviz's bottom-up coordinates flip to top-down.

**Styles and color.** Graphviz `style` is tokenized as a Graphviz style list, not CSS, and lowers to fill, stroke, dash, radius, visibility, and emphasis. Color lists lower to gradients and `striped`/`wedged` paint. Qualified SVG/X11 names and a compact embedded set of qualitative palettes (`accent8`, `dark28`, `paired12`, `set19`, `set28`, `set312`) resolve before canonicalization; this bounded table is an intentional size choice, and other schemes fall back to the theme with a diagnostic. `fontname`/`fontsize`/`fontcolor` lower through the font allowlist; PostScript Times, Helvetica, and Courier names get fallback stacks and bold/italic variants. `URL`/`href`/`tooltip`/`target` become inert interactions.

### 11.5 Graphviz DOT: content, geometry, and engines

**Labels.** Three materially different forms:

1. *Plain* strings: `\N`, `\G`, `\E`, `\T`, `\H`, `\L` substitutions applied only after identity resolution; `\n`, `\l`, `\r` become measured line breaks with alignment; doubled backslashes stay literal.
2. *Record* labels: flat and nested fields lower to measured table content, alternating horizontal/vertical orientation per nesting level; named fields lower to canonical ports.
3. *HTML-like* labels: delimiters removed, then parsed with a dedicated allowlist — never passed through as raw HTML. Safe tables, rows, cells, inline emphasis, allowlisted alignment and vertical alignment, and bounded positive row/column spans survive; `<TD PORT>` uses the same measured-content port walker as records. Images follow Radiant's resource policy and are disabled in deterministic tests unless checked in.

Node and edge `xlabel` and edge `headlabel`/`taillabel` lower to graph-level `<annotation>` elements with explicit owner identities, so several semantic labels never share one string and the callback needs no Graphviz-specific fields.

**Shapes.** Graphviz names map into geometry families: rectangle/polygon; specialized polygons (diamond, triangle, hexagon, octagon, parallelogram, trapezium, house variants, stars); ellipse/circle/doublecircle/point; record/content (`record`, `Mrecord`, `plain`, `plaintext`, `none`); storage/document (cylinder, note, folder, component, tab, box3d). Biological and directional forms (`promoter`, `cds`, `terminator`, overhang/site/stability shapes, arrows) reuse box, ellipse, and polygon roles with the raw name as provenance. Nodes default to ellipse. Polygon parameters (`sides`, `regular`, `orientation`, `skew`, `distortion`) and `peripheries` become explicit geometry.

**Sizing.** `fixedsize=true` yields a definite border box; `regular=true` applies a square preferred ratio with equal minima (a `1in × 0.5in` regular fixed node measures `96px × 96px`). `fixedsize=shape` uses a fixed centered inner shape inside a naturally measured outer box: spacing and collision use the outer box while edge clipping uses the inner shape, so a long label never moves the authored shape boundary. Circular nodes use equal-axis sizing.

**Markers.** `dir=forward|back|both|none` decides presence; the arrow-name grammar parses up to four components with `o`, `l`, and `r` modifiers into the compositional vocabulary (§9.4).

**Splines.** `splines` lowers to canonical `route-mode`: `false`/empty → `none`, `line`, `polyline`, `ortho` → `orthogonal`, `spline`/`true` → `curved`.

**Engine contract.**

| DOT `layout` value | Status | Behavior |
|---|---|---|
| absent or `dot` | supported | Lambda layered layout, routing, and measured content, for both directed and undirected graphs; the choice is recorded |
| `neato`, `fdp`, `sfdp`, `twopi`, `circo`, `osage`, `patchwork`, other | deferred | preserved; `graph.graphviz.unsupported-engine` |
| fixed `pos` / `-n` semantics | deferred | distinct diagnostic, never treated as ordinary `dot` |

**Formatter.** Mode follows `ir-stage`. *Source-semantic DOT* walks declarations, repeated attribute lists, endpoint chains, assignments, and recursive subgraphs in source order. *Canonical DOT* emits explicit topology and retained Graphviz properties, omitting inherited subgraph properties that reparsing reconstructs. Both support graph kind and strictness, correct `->`/`--`, all id forms, safe quoting and escaping, ports and compass points, and HTML-string labels. Round trips are semantic — `DOT → source Mark → DOT → source Mark` must normalize equivalently — not byte-identical.

**Support matrix.**

| Area | Supported | Partial or deferred |
|---|---|---|
| structure | strict graphs, directed/undirected operators, temporal defaults, assignments, endpoint sets, recursive scopes and `cluster*` subgraphs | non-`dot` engine semantics |
| ranking/order | `rankdir`, `nodesep`, `ranksep`, `rank=same\|min\|max\|source\|sink`, `minlen`, `weight`, `constraint`, `ordering`, `group`, `newrank` | exact Graphviz crossing choices |
| routing | all `splines` modes, parallel edges, self-loops, compass points, record/HTML ports, `compound`, `lhead`, `ltail` | spline control-point parity |
| content | plain substitutions, multiline labels, flat/nested records, safe HTML tables and inline content, `xlabel`, `headlabel`, `taillabel` | full HTML-like grammar; backend text metrics |
| node geometry | shared shape families, polygon parameters, `regular`, peripheries, inch dimensions, `fixedsize=true\|shape` | backend-exact specialized silhouettes |
| markers | up to four components, inversion, side modifiers, `dir`, `arrowsize` | unknown components use fallback paint |
| paint/fonts | color/fill/stroke, width/dash/opacity, gradients, striped/wedged, SVG/X11 names, six embedded palettes, allowlisted fonts | full ColorBrewer table; backend-identical font metrics |
| interaction | inert `URL`/`href`, `tooltip`, `target` | script/callback execution |
| formatter | source-semantic and canonical DOT, semantic round trips | comments and whitespace |

**Diagnostics.**

| Code | Condition |
|---|---|
| `graph.graphviz.edge-operator` | `--` in a digraph or `->` in an undirected graph |
| `graph.graphviz.expansion-limit` | endpoint products exceed 100,000 edges |
| `graph.graphviz.invalid-compass` | unsupported compass point |
| `graph.graphviz.invalid-rank` | unsupported rank constraint value |
| `graph.graphviz.invalid-ordering` | ordering other than `in` or `out` |
| `graph.graphviz.invalid-splines` | unsupported spline mode |
| `graph.graphviz.unsupported-engine` | layout engine other than `dot` |
| `graph.graphviz.unsupported-colorscheme` | non-embedded colorscheme |
| `graph.graphviz.unsupported-arrow` | unsupported arrow component; warning with fallback paint |
| `graph.graphviz.unresolved-compound-cluster` | unknown `lhead`/`ltail` cluster |
| `graph.graphviz.compound-endpoint-outside-cluster` | compound endpoint is outside its named cluster |

Parser recovery diagnostics and the shared `graph.edge.unresolved-port` diagnostic remain additional source-spanned errors.

### 11.6 Structurizr DSL and C4

Structurizr is a *workspace* language: one architecture model defines many views, and each view projects a different diagram from the same elements and relationships. The workspace model is therefore preserved, not lowered immediately to one graph. The official language reference is the semantic authority.

#### 11.6.1 Three-level data model

1. **Source Structurizr Mark** (`<workspace flavor: "structurizr" ir-stage: "source">`) records syntax: `<model>`, `<views>`, `<declaration keyword identifier>`, `<relationship from to>`, `<view kind scope key>`, `<include expression>`, `<auto-layout>`, `!include` and directive nodes, and generic `<statement>` for unknown forms — all with spans and explicit statement order, quoted versus bare arguments distinguished where it matters.
2. **Canonical C4 Workspace Mark** (`<c4-workspace ir-stage: "canonical">`) holds `<c4-model>` (`<c4-element id identifier kind name parent>` with tags, description, technology, properties, perspectives; `<c4-relationship id source destination>`), `<c4-views>`, `<c4-styles>`, and diagnostics. Canonical ids are generated deterministically and kept separate from authored identifiers, which remain with spans for diagnostics, editor navigation, and round trips.
3. **Per-view canonical Graph IR** (`<graph flavor: "structurizr-c4" diagram-type source-view-key>`) is produced by projecting one view. It preserves C4 identity, kind, parent, tags, technology, description, relationship identity, view key, and workspace provenance; generic layout never needs to understand Structurizr syntax.

#### 11.6.2 Parser

Statements reduce to three shapes, so one generic statement reader (optional assignment, arguments, optional relationship operator, optional body) plus a compact keyword→kind/allowed-context table keeps parser size proportional to grammar shapes rather than vocabulary:

```text
[identifier =] keyword argument... [{ child statements }]
source -> destination argument... [{ relationship properties }]
!directive argument... [{ child statements }]
```

The parser handles comments, strings, bare tokens, assignments, arrows, braces, and newlines; preserves nested blocks and order; distinguishes declarations, relationships, directives, and view statements; retains optional arguments without guessing omitted roles; and preserves unknown statements. It performs no identifier lookup, containment validation, archetype expansion, default-view generation, expression evaluation, implication, style resolution, or file/network access.

Dispatch accepts the `structurizr` and `c4` flavors. `.structurizr` is unambiguous; `.dsl` is claimed only when its first significant token is the boundary-safe `workspace` keyword, because `.dsl` is not unique to Structurizr.

#### 11.6.3 Normalization

- **Identifiers.** Flat and hierarchical modes; each scope records authored identifiers, canonical ids, parent, and declaration source. `this` resolves to the current element, including as either relationship endpoint and in hierarchical forward references. Duplicate, unknown, ambiguous, and illegal cross-scope references diagnose with use and declaration provenance.
- **Hierarchy.** Person, software system, container, component, custom element, group, deployment environment and group, deployment node, infrastructure node, software-system instance, container instance. Legal containment is validated. Groups are separate from C4 containment. Instances reference their logical element rather than copying identity or metadata.
- **Relationships.** Source, destination, description, technology, tags, properties, URL, and perspectives (compact and expanded forms); explicit identifiers stay stable for dynamic views.
- **Implied relationships.** Boolean implication runs after explicit relationships resolve: on by default, can be disabled, stops at shared ancestors, suppresses existing endpoint pairs, and records provenance. Custom Java strategies are diagnosed and never loaded.
- **Archetypes.** Declarative element and relationship archetypes supply default description, technology, tags, properties, and perspectives through a pure merge in which explicit values win. Cycles, unknown bases, and kind mismatches are errors.
- **Tags and terminology.** Default and authored tags are canonical; terminology overrides type labels.

#### 11.6.4 Includes and unsafe directives

`!include` and `workspace extends` are always recorded by the parser; resolution is a separate host operation with an explicit policy. Local includes are enabled only for local-file input that explicitly selects Structurizr/C4 or passes `.dsl` detection; in-memory and network inputs stay inert. The containing workspace directory is the allowed root, and canonicalized paths or symlinks may not escape it. Limits are 32 nested includes, 256 files, and 16 MiB of included source. Directory includes accept only `.dsl`/`.structurizr` entries in sorted order. Source Mark keeps each authored `!include` and nests resolved statements beneath it with canonical `source-file` identity and file-local spans; normalization treats resolved children as source-order-transparent. Remote URLs are disabled by default. JSON workspace extension awaits a canonical Structurizr JSON adapter.

`!script`, `!plugin`, `!components`, custom-code `!impliedRelationships`, and other executable directives remain inert source nodes with `structurizr.unsafe-directive`.

#### 11.6.5 View projection

| View | Primary contents |
|---|---|
| system landscape | people and software systems |
| system context | scoped system, connected people, and connected systems |
| container | containers in the scoped system plus connected external elements |
| component | components in the scoped container plus connected external elements |
| filtered | tag-based projection over a resolved base static view |
| custom | explicitly selected custom elements and relationships |
| dynamic | ordered relationship instances as a collaboration diagram |
| deployment | nested deployment boundaries and instances in one environment |

All static kinds share one projection path following view semantics, not mere type filtering. `include *`, reluctant `*?`, explicit identifiers, and expressions are evaluated against the model and scope; relationships are included only when their endpoints are selected and view rules permit; exclusion runs after inclusion in source order. C4 elements become nodes with rich content; system, container, and (nested) group boundaries become clusters.

**Expressions** are evaluated by a small pure recognizer for a restricted grammar — never as Lambda source and without a second general-purpose AST. It supports identifiers, `*`, afferent/efferent coupling, element type, parent, group, tag, technology, and property predicates; relationship tag, property, source, destination, and endpoint predicates; `&&`, `||`, and precedence-aware parentheses. Results keep model source order. Unknown element predicates select nothing and unknown relationship predicates match nothing; malformed syntax is `structurizr.invalid-expression`, valid but unsupported predicates are `structurizr.unsupported-expression`. No invalid or unsupported expression ever falls back to including everything.

**Dynamic views** keep, per entry: sequence order, optional explicit order token, relationship identity (from endpoint or relationship-identifier syntax), per-instance description/technology override, parallel-sequence grouping (including anonymous parallel blocks), and source span. The graph projection renders a collaboration diagram whose edge labels carry stable ordinals; parallel occurrences stay separate edges. The same canonical entries can later feed a `lambda.chart` sequence projection.

**Deployment views** map nested deployment nodes to clusters and infrastructure, software-system, and container instances to nodes. Logical relationships lift to instances only when environment, deployment groups (inherited from nodes), and view scope allow. Instance multiplicity is preserved — two container instances never collapse into one node — and health checks are retained. Deployment include/exclude filtering keeps required ancestor boundaries and drops relationships whose endpoints leave the projection.

**View selection.** `--view-key <key>` selects the view for `view`, `render`, and `convert -t html`, and `structurizr.project()`/`to_html()` take an optional view key. When no key is given, the first declared view is selected, even if the workspace declares several (decided 2026-10-23; supersedes the original C4 proposal's diagnostic listing the view keys).

#### 11.6.6 C4 HTML, styles, and layout

```html
<graph class="lambda-graph c4-diagram" data-radiant-layout="lambda-graph" data-c4-view="Containers">
  <node class="graph-node c4-person" data-node-id="e1">...</node>
  <cluster class="graph-cluster c4-software-system-boundary">...</cluster>
  <edge data-from="e1" data-to="e3" data-directed="true"></edge>
</graph>
```

C4 node content is rich measured HTML — name, type or configured terminology, optional technology, optional description — never one flattened text string. Tags, properties, URLs, and perspectives are metadata unless an allowlisted transform displays them. Default roles: `c4-person`, `c4-software-system`, `c4-container`, `c4-component`, `c4-deployment-node`, `c4-infrastructure-node`, `c4-instance`, `c4-group-boundary`, `c4-software-system-boundary`, `c4-container-boundary`.

Element and relationship styles lower to canonical style assignments and pass through the common safe style parser. The allowlist covers background, text/line color, stroke, stroke width/thickness, node font size, node width/height, percent opacity, solid/dashed/dotted borders and relationships, the documented element shapes, and per-relationship direct/orthogonal/curved routing; shape and routing become geometry attributes, not CSS. Unsupported icons, fonts, or paint stay as canonical workspace properties with `structurizr.unsupported-style`. External icons and themes are never fetched.

`autoLayout [tb|bt|lr|rl] [rankSeparation] [nodeSeparation]` maps directly to Graph IR direction and separations (Structurizr specifies pixels, matching CSS-pixel geometry). Layout also respects nested boundaries, boundary labels and padding, fixed view membership, relationship labels, routing hints, multiple relationship instances, deployment instance identity, and source order as the final tie breaker. Views without `autoLayout` use a deterministic source-order layered fallback and emit `structurizr.implicit-layout`.

#### 11.6.7 Diagnostics

| Code | Condition |
|---|---|
| `structurizr.syntax` | malformed token, statement, or block |
| `structurizr.unknown-statement` | statement preserved but not understood |
| `structurizr.invalid-context` | known statement in an illegal parent |
| `structurizr.duplicate-identifier` | identifier conflicts in its effective scope |
| `structurizr.unresolved-identifier` | model or view reference cannot be resolved |
| `structurizr.invalid-containment` | C4 element under an illegal parent |
| `structurizr.invalid-relationship` | incompatible source and destination kinds |
| `structurizr.invalid-view-scope` | view scope does not match its kind |
| `structurizr.duplicate-view-key` | two canonical views share a key |
| `structurizr.invalid-expression` | malformed include/exclude expression |
| `structurizr.unsupported-expression` | valid expression outside the allowlist |
| `structurizr.unsupported-view` | image or other deferred view requested |
| `structurizr.unsafe-directive` | executable or external extension kept inert |
| `structurizr.include-cycle` | local include cycle |
| `structurizr.include-limit` | include depth, file count, or byte limit exceeded |
| `structurizr.include-missing` | local include path unreadable |
| `structurizr.include-policy` | include is remote or escapes the allowed root |
| `structurizr.unsupported-style` | style value cannot be represented safely |
| `structurizr.implicit-layout` | deterministic fallback layout selected |

### 11.7 D2

D2 input is parsed natively into Graph IR and rendered through the same transform, callback, and paint path. It has no dedicated conformance corpus yet.

## 12. Diagnostics model

Diagnostics are Mark values with a stable dotted code, severity, message, and source location, collected alongside — never instead of — the recoverable graph. Codes are namespaced by owner: `graph.*` for common invariants (invalid roots or directions, missing or duplicate identities, unresolved endpoints, duplicate ports, `graph.edge.unresolved-port`, schema type/enum/placement/cardinality/required-attribute failures), `mermaid.*`, `graph.graphviz.*`, and `structurizr.*`. Invalid conformance cases pass only when the expected code and source span are produced without a crash or a partial graph masquerading as success.

## 13. Conformance model

### 13.1 Graph Scene Mark

Raw SVG and screenshots are not fixtures: renderers legitimately differ in tags, wrappers, classes, transforms, marker definitions, and number formatting. Both reference and Lambda output are adapted into a smaller renderer-neutral Graph Scene Mark:

```mark
<graph-scene direction:"LR" width:286 height:104>
  <node id:"a" shape:"rounded" x:0 y:30 width:86 height:44; <label; "Start"> >
  <node id:"b" shape:"diamond" x:180 y:10 width:106 height:84; <label; "Valid?"> >
  <edge id:"e0" from:"a" to:"b" marker-start:"none" marker-end:"normal"
        start-side:"right" end-side:"left" route-kind:"orthogonal";
    <route; <point x:86 y:52> <point x:180 y:52> >
  >
>
```

Every adapter performs equivalent normalization: flatten nested transforms into graph-local coordinates; map renderer primitives and wrappers to shape roles; collapse text, tspans, HTML labels, and foreignObject into normalized label text; resolve CSS and presentation attributes into optional semantic paint; replace generated DOM ids and marker URLs with source identities; drop wrappers, raw classes, tag names, definition order, and insignificant precision; parse path geometry, normalize its origin, and classify straight, orthogonal, curved, parallel, and self-loop routes using meaningful bends or sampled curve structure. Raw classes may help an adapter *find* objects but are never emitted or compared. Scenes include nodes, clusters, edges, labels, annotations (with owner, kind, format, and bounds), markers, polygon/periphery metadata, measured boxes, route classes, and points.

### 13.2 Comparison levels

1. **Exact semantics:** identities, topology, membership, labels, shape roles, markers, directions, declared constraints.
2. **Relational geometry:** ranks, ordering, containment, node non-overlap, endpoint attachment and side, route kind.
3. **Tolerant geometry and paint:** dimensions, points, colors, stroke widths, dashes, under explicit per-category tolerances and flags.

Per-case policies are `source` (exact source-stage Mark), `canonical` (topology, defaults, properties, diagnostics), `html-semantic` (renderer-neutral transformed HTML roles and content, excluding unstable CSS serialization), `scene-semantic`, and `scene-geometry`. Where two renderers produce different valid route representations (Graphviz splines versus Lambda routes), control-point equality is disabled while endpoint relations stay checked.

### 13.3 Corpora and references

Each format keeps a manifest-driven corpus pinned to an exact upstream revision, with the upstream license beside adapted fixtures and per-case provenance (upstream file and test name, options, feature tags, expected status, comparison policy, reference version). One shared conformance harness owns manifest discovery, status handling, policy defaults, retained-runtime rendering, comparison, and failure artifacts; formats contribute only their source and reference adapters. Every case is discovered and reported, including `extended` and `unsupported` ones.

- **Mermaid:** cases adapted from Mermaid's source fixtures and rendering-test diagram strings (not screenshots), pinned to Mermaid 11.16.0. References are generated by running pinned Mermaid in a browser DOM — Mermaid needs browser text measurement — and adapting the live SVG DOM, computed styles, and `getBBox()` into Graph Scene Mark. Semantic-only references may omit geometry.
- **Graphviz:** cases from the official language examples, test graphs, and focused Lambda regressions. `dot -Tdot_json` provides an unlaid semantic reference, `dot -Tjson` laid-out boxes, splines, and labels, and `xdot_json` paint only where JSON cannot recover it. A Lambda adapter converts Graphviz JSON to Graph Scene Mark, discarding object indices, draw order, and font-backend detail. Generator version, command, and binary hashes are pinned.
- **Structurizr:** a pinned Structurizr CLI exports DSL workspaces to JSON; a Lambda adapter converts that JSON and Lambda's canonical workspace into one semantic comparison Mark covering element paths, kinds, parents, names, descriptions, technology, tags, explicit and implied relationships, deployment environments, nodes, and instances, view kind/scope/key and resolved/filtered membership, dynamic order and parallel groups, layout intent, resolved styles, and projected containment — discarding generated numeric ids and renderer detail. Reference provenance records source and artifact SHA-256 values.

Reference tools are explicit, reviewed maintenance operations. Ordinary test runs use only checked-in source, canonical Mark, HTML semantics, and scene/semantic Mark; they never invoke Mermaid, Graphviz, Java, a browser, or the network, never compare images or raw SVG, and never rewrite references. One retained Lambda runtime renders all selected cases without per-case process startup.

## 14. Acceptance criteria

1. `to_html()` emits the semantic graph tree; rich node content (block, inline, flex/grid, table, text, image, whole SVG) is measured by Radiant and positioned correctly.
2. Links are routed from measured boxes and painted as generated layers; signed `z` places ordinary edges below nodes, selected edges above, and split segments on both sides.
3. Repeated relayout reuses the retained runtime and compiled export without reparsing or renormalizing unchanged source; simultaneously live documents cannot disturb each other.
4. The parser-to-transform boundary is canonical Mark Graph IR for every source language; parsers perform no semantic rewriting, identifier resolution, file or network access, or extension loading.
5. Unsupported syntax, engines, views, and directives yield structured diagnostics rather than silent loss or execution.
6. Groups, rich labels, annotations, classes, styles, markers, accessibility metadata, ports, and stable ids survive normalization, transform, and final SVG.
7. Mermaid graph and chart families dispatch to the correct package; flowchart, class, ER, and flat-state surfaces satisfy their declared conformance matrices.
8. DOT source, semantics, content, shapes, markers, `dot`-engine constraints, ports, compound clusters, route classes, and formatter round trips satisfy §11.4–§11.5.
9. Structurizr workspaces normalize deterministically; landscape, context, container, component, filtered, custom, dynamic, and deployment views project correct model subsets; C4 content is measured and boundaries use cluster geometry.
10. Graph files render through the Lambda package in `render`, `view`, `layout`, and `convert` with no Graphviz, Mermaid, Java, or Structurizr installed; no caller references the retired C layout/SVG ABI.
11. Every corpus case records provenance and passes its policy without image fixtures or raw SVG equality; reference regeneration is reproducible from pinned versions and never required by ordinary tests.
12. Parser LOC and binary contribution stay within their reviewed budgets.

## 15. Open items and deferred work

**Open items.**

- **Structurizr diagnostics.** Some codes in §11.6.7 are not yet emitted; corpus expansion beyond the three pinned references (basic, rich static/filtered, dynamic/deployment) is outstanding.
- **Mermaid families.** ER relationship word aliases, exhaustive quoted/Unicode identifiers, Markdown-rich entity names, and multiple shorthand classes; composite states, nested transitions, concurrency regions, history states, richer state descriptions, and `hide empty description`; requirement, architecture, block, and mindmap adapters.
- **D2** conformance corpus.

**Deferred.**

- Eager `transform.to_svg()` for models with explicit node sizes.
- Portable standalone HTML through an opt-in trusted layout-module loader.
- Interactive edge hit testing from routed segments; rich editable edge labels and routing handles; animation through stable node, edge, and segment identities.
- LambdaJS access over the same Velmt and paint-layer contracts; additional layout algorithms behind the same canonical model.
- Graphviz `neato`, `fdp`, `sfdp`, `twopi`, `circo`, `osage`, `patchwork`, and fixed-position input; B-spline control-point parity; PostScript shape files and plugin loading; the full ColorBrewer table behind an explicit size budget; byte-identical formatting and comments; cross-backend pixel identity.
- Structurizr JSON import/export parity and manual coordinates (a separate fixed-layout mode); remote include/extend under an explicit network capability; image views and PlantUML/Mermaid/Kroki rendering; external themes and icons; documentation and ADR rendering; component discovery; scripts, plugins, and JVM extensions; the full expression language; dynamic sequence rendering through `lambda.chart`; workspace editing and a source-preserving formatter; pixel parity with Structurizr's renderer.

Deferred features may be added behind explicit engine or capability contracts but must not weaken source-faithful parsing, the canonical Graph IR, or the renderer-neutral conformance model.

## 16. References

- Mermaid: <https://mermaid.js.org/> (corpus pinned to 11.16.0).
- DOT language grammar: <https://graphviz.org/doc/info/lang.html>; attributes and value types: <https://graphviz.org/docs/attrs/>; JSON output: <https://graphviz.org/docs/outputs/json/>; tests: <https://gitlab.com/graphviz/graphviz/-/tree/main/tests>; license: <https://graphviz.org/license/>.
- Rejected Tree-sitter DOT experiment grammar: <https://github.com/rydesun/tree-sitter-dot>.
- Structurizr DSL: [overview](https://docs.structurizr.com/dsl), [language reference](https://docs.structurizr.com/dsl/language), [cookbook](https://docs.structurizr.com/dsl/cookbook/) (system context, container, component, dynamic, and deployment views), [exporters](https://docs.structurizr.com/export).
- Base Graph IR schema: [Mark_Graph_Schema.md](Mark_Graph_Schema.md); companion package: [Lambda_Pkg_Chart.md](Lambda_Pkg_Chart.md).

Checked-in upstream revision, reference-tool version, and license files are the reproducibility authority; moving upstream documentation does not silently change conformance.
