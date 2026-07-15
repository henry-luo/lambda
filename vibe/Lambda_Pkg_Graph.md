# Lambda Graph Package - Design Proposal

**Status:** Stage 1 is implemented for the initial rich-node release. The package
split, semantic HTML transform, Velmt callback, generated SVG paint, signed
stacking, runtime-scoped registration, CLI bridges, and C graph ABI removal are
complete. Stage 2 flowchart support is implemented: the common Mark IR contract,
declarative schema validation, recursive model queries, Mermaid normalization
fixes, measured edge and cluster labels, visual recursive clusters, named
ports, compound and parallel-edge routing, broader shape clipping, safe style
cascade, final-SVG graph metadata, Graph Scene adaptation/comparison, in-process
Radiant scene rendering, relational scene validation, and the pinned semantic
corpus runner are implemented. Section 18.12 records the current boundary;
additional graph-oriented Mermaid families remain a subsequent phase.
Stage 3, proposed in Section 19, adds source-faithful Graphviz DOT support on
the same canonical Graph IR and retained Radiant layout path.

## 1. Summary

The Lambda graph package is split into two public modules with separate
responsibilities:

- `lambda.package.graph.layout` computes graph geometry from node sizes and
  relationships. It owns ranking, ordering, node coordinates, edge routing,
  clipping, and graph bounds.
- `lambda.package.graph.transform` converts graph data into a semantic HTML
  element tree, installs the Radiant custom layout callback, and converts routed
  edge geometry into generated SVG paint layers.

The primary output is HTML, not a pre-laid-out SVG. Radiant first lays out the
rich contents of every graph node using its existing block, inline, flex, grid,
table, replaced-element, text, and SVG layout paths. The graph custom layout
then receives those resolved node border boxes as Velmts, computes node and edge
geometry in Lambda, and returns placements plus generated paint layers.

`transform.to_svg()` is deferred. Static SVG generation cannot measure rich
nested HTML content and is not required to retire the C graph layout path. Final
SVG, PDF, PNG, and JPEG output should initially be produced by rendering the
transformed HTML through Radiant.

The C `layout_graph()` and `graph_to_svg()` pipeline has been retired. Mermaid,
D2, DOT, and GV syntax parsing remains in C, while every layout and rendering
path now enters the Lambda HTML transform and normal Radiant renderer.

## 2. Goals

1. Support graph nodes containing arbitrary rich content, not only text labels.
2. Reuse Radiant's existing child layout engines for all node measurement.
3. Keep graph ranking, positioning, and link routing in pure Lambda functions.
4. Represent input graphs as semantic HTML containing `<graph>`, `<node>`, and
   `<edge>` elements.
5. Route and paint links as part of the same pure custom layout result that
   positions nodes.
6. Allow edges and nodes to interleave through one explicit custom stacking
   order rather than a fixed underlay/overlay pair.
7. Retain and reuse the JIT-compiled layout function and its Lambda runtime
   across repeated layout passes.
8. Remove the old C graph layout and SVG generation pipeline after CLI and
   rendering parity is established.
9. Normalize graph-oriented source formats through one public Mark Graph IR.
10. Support the graph-oriented Mermaid families without moving chart-oriented
    Mermaid diagrams into the graph package.

## 3. Non-goals

- `transform.to_svg()` is not part of the first implementation phase.
- The custom layout callback does not mutate the DOM, perform I/O, start timers,
  or request child measurement or relayout.
- The graph package does not replace the Mermaid, D2, or DOT parsers. Their C
  parsers may continue producing Lambda `<graph>` data elements.
- Full Graphviz/Dagre parity is not required. The package implements the
  deterministic cluster-aware ordering, orthogonal obstacle routing, and label
  collision handling needed by the current Mermaid flowchart contract, without
  claiming identical geometry to either renderer.
- SVG primitives do not become individually measured CSS layout children.

## 4. Module Structure

```text
lambda/package/graph/
  graph.ls                 compatibility facade
  layout.ls                public graph geometry API
  dagre.ls                 initial layered algorithm and orthogonal routing
  transform.ls             public HTML transform and installation API
  transform/
    html.ls                semantic graph HTML construction
    paint.ls               routed geometry to SVG paint layers
    theme.ls               graph CSS and visual defaults
```

The existing `graph.ls` should remain temporarily as a compatibility facade. It
can forward the current `make_options()`, `layout()`, and `layout_custom()` APIs
to the new layout module while callers migrate.

### 4.1 Layout package

The public geometry API is pure:

```lambda
layout.make_options() -> GraphLayoutOptions

layout.compute(model, opts = null) -> GraphLayoutResult

layout.from_velmts(parent, children, ctx, opts = null)
    -> GraphLayoutResult
```

`layout.compute()` accepts a canonical graph model with explicit node sizes.
`layout.from_velmts()` builds that model from the already-laid-out `<node>` and
`<edge>` children supplied by Radiant.

The layout package must not generate SVG or depend on Radiant rendering types.
Its edge output is geometry and styling metadata, not markup.

### 4.2 Transform package

The public transform API is:

```lambda
transform.install() -> bool

transform.to_html(graph, opts = null) -> element
```

`install()` explicitly registers the `lambda-graph` custom layout function with
Radiant. Registration is an orchestration side effect performed before layout;
the registered callback itself remains a pure `fn`.

`to_html()` is a pure graph-data transformation. It returns a Lambda HTML
element tree, not a serialized HTML string. Keeping the element tree in the
same Lambda document runtime preserves structured values, the compiled callback,
and future reactive updates.

## 5. Canonical Mark Graph IR

The public representation between graph parsers, format adapters, transforms,
and formatters is a Lambda Mark element tree. A JSON-shaped map is not the graph
interchange format. Mermaid, D2, DOT, GV, and author-created graph data normalize
to the same recursive Mark structure:

```mark
<graph version:1 flavor:"mermaid" kind:"flowchart"
       directed:true rank-dir:"TB">
  <meta title:"Example" acc-title:"Example flow">

  <styles>
    <style-rule target:"node" class:"warning"
                fill:"#fff4cc" stroke:"#8a6500">
  >

  <subgraph id:"backend" label:"Backend" direction:"LR">
    <node id:"a" shape:"rounded" classes:["service"];
      <label format:"text"; "Start">
    >
    <node id:"b" shape:"diamond";
      <label format:"text"; "Valid?">
    >
  >

  <edge id:"e0" from:"a" to:"b"
        arrow-head:"normal" arrow-tail:"none"
        line-style:"solid" min-length:1;
    <label position:"center" format:"text"; "continue">
  >
>
```

The Mark IR preserves recursive subgraphs, rich labels, style rules, endpoint
and marker semantics, source metadata, and stable identities. A node may contain
arbitrary Mark content in addition to or instead of a simple `<label>`.

The layout implementation may derive compact immutable records from this tree
after normalization and Velmt measurement. Those records are an internal
calculation form, not a second public graph format. Node `width` and `height` in
that internal form are border-box dimensions read from Velmt; Radiant remains
the authority for resolved rich-content size.

The layout result contains both nodes and routed edges:

```lambda
{
  width: 420.0,
  height: 280.0,
  nodes: [{id, index, x, y, width, height, rank, order, z}],
  edges: [{id, from, to, segments, arrow_start, arrow_end, style}],
  placements: [{index, x, y, z}]
}
```

Node `x, y` in `placements` are border-box top-left coordinates in the custom
container's local coordinate system. Routed edge points use the same system.

## 6. HTML Transformation

`transform.to_html()` should produce this semantic structure:

```html
<graph class="lambda-graph" data-radiant-layout="lambda-graph">
  <node class="graph-node" data-node-id="a">...</node>
  <node class="graph-node" data-node-id="b">...</node>
  <edge data-from="a" data-to="b" data-directed="true"></edge>
</graph>
```

The transform preserves rich node content. If a source node already has child
elements, those elements become the `<node>` contents. A source node containing
only a label can use that label as text content.

Graph options become stable attributes on the graph root, for example:

```html
<graph class="lambda-graph"
       data-radiant-layout="lambda-graph"
       data-direction="TB"
       data-node-sep="60"
       data-rank-sep="80">
```

Edge options remain on their semantic `<edge>` elements rather than being
serialized into one opaque graph attribute. This keeps the transformed tree
inspectable and allows the callback to read edge identity, endpoints, direction,
style, arrows, and future routing hints through Velmt attributes.

### 6.1 Layout participation

Both `<node>` and `<edge>` are direct custom-layout children:

- `<node>` is a normal measured block whose border box participates in graph
  geometry and parent bounds.
- `<edge>` is a non-visual, zero-size metadata box. It participates in the
  callback so its relationship attributes are available, but it does not draw
  itself or contribute positive size.

The package stylesheet should establish that contract:

```css
graph.lambda-graph {
  display: layout(lambda-graph);
  position: relative;
}

graph.lambda-graph > node.graph-node {
  display: inline-block;
}

graph.lambda-graph > edge {
  display: block;
  width: 0;
  height: 0;
  overflow: hidden;
  visibility: hidden;
  pointer-events: none;
}
```

The callback returns `(0, 0)` placements for metadata edges so every in-flow
custom-layout child has an explicit result. Edge visuals are emitted separately
as generated paint layers.

Rich edge labels may later be represented by measured visual children associated
with an edge id. They should not overload the zero-size metadata edge box.

## 7. Custom Layout Call Flow

The implemented and proposed flow is:

```text
graph source
  -> C input parser or Lambda graph value
  -> transform.to_html()
  -> semantic <graph>/<node>/<edge> tree
  -> Radiant resolves data-radiant-layout="lambda-graph"
  -> Radiant prelays out every direct child
  -> node and edge Velmts passed to the retained Lambda fn
  -> layout.from_velmts() builds the canonical graph model
  -> layout.compute() ranks, orders, positions, and routes
  -> transform.paint builds generated SVG paint layers
  -> callback returns placements, parent size, and paint layers
  -> Radiant writes node x/y/z to child views
  -> Radiant stores generated paint layers on the graph view
  -> renderer interleaves generated layers and node child views by z
```

The callback should have this shape:

```lambda
fn lambda_graph_layout(parent, children, ctx) {
  let result = layout.from_velmts(parent, children, ctx)
  {
    width: result.width,
    height: result.height,
    placements: result.placements,
    paint_layers: paint.edge_layers(result)
  }
}
```

The function allocates and returns immutable data. It does not mutate the
semantic graph tree or force another layout pass.

## 8. Edge Routing

Graph layout is responsible for links as well as nodes. Routing occurs after
node positions are known and should produce renderer-independent geometry.

The initial router may retain the current orthogonal route shape, but its output
should be normalized into segments:

```lambda
{
  id: "e0",
  from: "a",
  to: "b",
  segments: [{
    points: [{x: 120.0, y: 40.0}, {x: 120.0, y: 80.0}],
    z: -1
  }],
  arrow_end: true,
  style: "solid"
}
```

Geometry work belongs in `graph.layout`:

- clip endpoints against the actual node shape;
- route orthogonal or curved paths;
- separate parallel edges;
- route self-loops;
- detect or honor crossing information;
- choose label and arrowhead anchors;
- split a route when different portions require different stacking.

SVG path construction, marker definitions, colors, dash patterns, and theme
mapping belong in `graph.transform.paint`.

## 9. Unified Node and Edge Stacking

A single `underlay` plus a single `overlay` is not sufficient. It supports only
"all edges behind all nodes" or "all edges above all nodes." Graphs may require
selected edges above nodes, ordinary edges below nodes, or one routed edge that
passes behind one node and above another.

Custom layout should therefore expose generated paint layers with signed `z`
values, and child placements should use the same `z` coordinate:

```lambda
{
  placements: [
    {index: 0, x: 40.0, y: 20.0, z: 0},
    {index: 1, x: 240.0, y: 120.0, z: 0}
  ],
  paint_layers: [
    {z: -1, content: <svg; ...ordinary edge segments...>},
    {z: 1, content: <svg; ...raised or selected segments...>}
  ]
}
```

The graph custom-layout container owns one local stacking sequence:

1. Paint the container background and border.
2. Merge generated `paint_layers` and normal child views.
3. Sort by signed `z`, lowest first.
4. Preserve result/tree order for equal `z`; at an equal value, generated paint
   is before child content so a node covers an edge unless the edge requests a
   greater `z`.
5. Paint outlines and other parent-level effects according to the normal
   Radiant boundary rules.

Each node subtree remains atomic relative to graph-generated paint. Its own CSS
stacking contexts continue to order descendants internally.

Typical defaults are:

- ordinary edge segments: `z = -1`;
- graph nodes: `z = 0`;
- selected edges, handles, and interaction highlights: `z = 1`.

For a genuinely weaving edge, the router splits the route into segments. The
segments below nodes are grouped into a negative-z SVG layer, while raised
segments are grouped into a positive-z layer. Crossing gaps, masks, or bridge
arcs can be represented in those generated SVG fragments without changing node
layout.

Radiant now stores signed custom child `z`, generated paint entries, and uses a
shared stable stacking helper for SVG, PDF, raster, and reverse-order hit
testing. Generated edge paint is non-interactive initially; edge hit testing
can later reuse routed segment geometry.

## 10. Generated Paint Layer Contract

The minimal generic custom-layout extension is:

```lambda
type CustomPaintLayer = {
  z: int,
  content: element
}

type CustomLayoutResult = {
  width: float?,
  height: float?,
  baseline: float?,
  placements: [Placement],
  paint_layers: [CustomPaintLayer]?
}
```

For the graph package, `content` is an `<svg>` element whose viewport matches
the graph content box. Paint layers:

- use the custom container's local coordinate system;
- do not affect parent sizing;
- obey the parent's CSS overflow and clipping;
- are immutable layout output, not DOM children;
- are replaced on the next successful custom layout pass;
- must be copied or lowered into document-owned PaintIR before the callback's
  temporary result becomes unreachable;
- may be grouped by `z` so many edges share one SVG subscene.

This extension is generic enough for connectors, annotations, diagrams, and
other custom layouts without adding graph-specific fields to Radiant.

## 11. SVG Scope

Radiant currently treats the root `<svg>` as a replaced CSS box and renders its
native SVG element tree separately. Therefore:

- an entire SVG can be rich content inside one `<node>` and can be measured and
  positioned as that node's child content;
- `<path>`, `<g>`, `<rect>`, and other SVG primitives are not individual custom
  layout children;
- generated SVG paint layers are the intended bridge for edge paths.

This design does not require making SVG graphics elements participate in CSS
custom layout. The graph callback returns a finished SVG fragment in graph-local
coordinates, and Radiant lowers that fragment through its existing SVG renderer.

`transform.to_svg()` may be added later as an eager convenience API for graphs
whose node dimensions are already known. It is not the rich-content path and is
not required for the first release.

## 12. Registration, Runtime, and Caching

The graph package is installed explicitly:

```lambda
import graph_transform: lambda.package.graph.transform

let installed = graph_transform.install()
graph_transform.to_html(graph_data)
```

The normal Lambda document loader already retains the Lambda runtime, GC heap,
JIT code, and registered function while the resulting `DomDocument` is alive.
Repeated resize, interaction, animation, edit, and mutation layouts must call
the retained compiled function rather than reparsing or recompiling the package.

Registration is runtime/document scoped. The native custom-layout registry keeps
one stable bridge per name, while the Lambda callback is resolved by:

```text
(document.lambda_runtime, layout_name)
```

rather than by `layout_name` alone. The implementation keys registrations by the
retained runtime heap reached from the parent view's `DomDocument`, preventing
one live document from replacing another document's `lambda-graph` function.

The callback and its JIT code remain rooted for that heap across every reflow.
The Jube module receives a per-heap cleanup notification immediately before
runtime heap destruction, unregisters the callback root, and makes the stable
registry slot reusable. Generated SVG layer roots are likewise registered and
unregistered directly against the document runtime's owning heap, including
when document teardown occurs outside the callback's thread-local context.

The low-level native registry may keep one stable bridge for the name, but that
bridge must select the Lambda callback using the parent view's owning document
and retained runtime.

The pure graph result may later be cached using node ids, node border-box sizes,
edge data, options, writing direction, and the graph package/version identity.
The cache entry must include both placements and generated paint layers.

## 13. Standalone HTML

`to_html()` initially returns an in-memory Lambda element tree. This is enough
for Lambda scripts, `render`, `view`, and `layout`, because their document loader
retains the runtime that installed the callback.

Serializing that tree to a standalone `.html` file loses the retained Lambda
runtime. CSS such as `display: layout(lambda-graph)` names a callback but does
not load its package. Portable deferred-layout HTML therefore requires a future
declarative and security-scoped module loader, for example document metadata
that opts into a trusted Lambda layout package.

Until that loader exists, standalone output should be one of:

- a Lambda script/document that imports and installs the graph transform;
- final SVG/PDF/PNG/JPEG rendered from the in-memory HTML document;
- a future eager static SVG produced after explicit node sizes are known.

## 14. Retiring the C `layout_graph()` Pipeline

The direct graph branches in `render`, `view`, and `layout` have been replaced
with the same Lambda bridge pattern used for package-driven PDF conversion.

The bridge source is conceptually:

```lambda
import graph_transform: lambda.package.graph.transform

let graph^err = input("diagram.mmd", {type: "mermaid"})
let installed = graph_transform.install()
graph_transform.to_html(graph, {theme: "zinc-dark"})
```

### 14.1 CLI migration

1. Add a shared graph bridge-script builder for Mermaid, D2, DOT, and GV input.
2. Route `render` graph files through the normal Lambda-script HTML loader and
   `render_html_to_output_target()`.
3. Route `view` graph files through the in-memory Lambda script viewer, avoiding
   a temporary SVG file.
4. Route `layout` graph files through the same transformed document path so its
   result is a normal Radiant view tree.
5. Produce requested `.svg` output through Radiant's HTML-to-SVG renderer. Do
   not keep a separate direct graph-to-SVG branch.
6. Keep the graph document runtime alive until layout, rendering, interaction,
   and window teardown are complete.

### 14.2 C removal

After CLI and golden tests pass, remove:

- `radiant/layout_graph.cpp` and `radiant/layout_graph.hpp`;
- `radiant/graph_to_svg.cpp` and `radiant/graph_to_svg.hpp`;
- `radiant/graph_layout_types.hpp`;
- C graph theme files if no other caller remains;
- graph layout and graph-to-SVG stubs from `lambda/headless_stubs.cpp`;
- corresponding includes, forward declarations, build entries, analysis rules,
  and lint allow-list entries.

The C Mermaid, D2, and DOT input parsers and graph formatters remain. They deal
with graph syntax and Lambda data, not layout or painting.

The C coordinate algorithm, graph SVG generator, exported `layout_graph()` ABI,
headless stubs, build references, and lint/analysis mappings have all been
removed. Only graph syntax parsing and data formatting remain native.

## 15. Implementation Plan

The phases below constitute Stage 1.

### Phase 1 - Package split (complete)

- add `graph/layout.ls` and move the current public geometry API there;
- keep `dagre.ls` as the initial private implementation used by `layout.ls`;
- add `geometry.ls` and `routing.ls` as complexity warrants;
- retain `graph/graph.ls` as a compatibility facade;
- preserve and update current pure Lambda layout tests.

### Phase 2 - HTML transform (complete)

- add `graph/transform.ls` and `graph/transform/html.ls`;
- normalize canonical `<graph>` Mark input and retain map input only as a
  compatibility adapter;
- produce the specified `<graph>/<node>/<edge>` semantic tree;
- add package CSS for measured nodes and zero-size metadata edges;
- add exact Lambda `*.ls` and expected `*.txt` transform tests.

### Phase 3 - Graph custom callback (complete)

- classify node and edge Velmts by tag and attributes;
- construct the canonical model using resolved node border-box sizes;
- return placements for nodes and metadata edges;
- preserve full routed edge geometry in the custom layout result;
- make registration runtime/document scoped.

### Phase 4 - Generated paint and stacking (complete for non-interactive paint)

- extend the custom layout result parser with `paint_layers`;
- retain generated SVG element content as document resources and lower it
  through each existing renderer;
- implement signed custom child `z`;
- merge child views and generated paint layers in render and hit-test order;
- test the shared signed ordering of generated layers and child views,
  including generated-before-child ordering at equal `z`.

### Phase 5 - Routing parity (parallel and self-loop paths complete; compound routing deferred)

- honor all four layout directions in rank and coordinate assignment (complete);
- run the requested number of crossing-reduction iterations (complete);
- add shape-specific clipping (complete for the initial shape set);
- add parallel-edge and self-loop routing (complete for non-compound edges);
- add subgraph/cluster geometry and edge-label placement as required;
- compare Mermaid, D2, and DOT fixtures against accepted goldens.

### Phase 6 - CLI migration and C removal (complete)

- add the graph Lambda bridge to `render`, `view`, and `layout`;
- add SVG, PDF, PNG, and headless view integration tests;
- remove the old C graph layout/render ABI and files;
- run Lambda and Radiant baseline suites and the Radiant layout int-cast lint.

## 16. Acceptance Criteria

The first graph package release is complete when:

1. `transform.to_html()` emits the required semantic graph tree.
2. Nodes containing nested block, inline, flex/grid, text, image, and whole-SVG
   content receive their dimensions from Radiant and are positioned correctly.
3. Links are routed from measured node boxes and rendered from generated SVG
   paint layers.
4. Signed `z` ordering can place ordinary edges below nodes, selected edges
   above nodes, and split segments on both sides of node content.
5. Repeated relayout reuses the retained runtime and compiled callback.
6. Two simultaneously live graph documents cannot overwrite one another's
   layout callback.
7. Negative placements and explicit parent sizes follow the established custom
   layout overflow rules.
8. Mermaid, D2, DOT, and GV files render through the Lambda package in `render`,
   `view`, and `layout`.
9. No active caller or build target references `layout_graph()`,
   `graph_to_svg()`, or the deleted C graph layout types.
10. Focused tests, Lambda baseline tests, Radiant baseline tests, and layout lint
    all pass.

## 17. Future Work

- eager `transform.to_svg()` for graph models with explicit node sizes;
- portable standalone HTML with an opt-in trusted layout-module loader;
- interactive edge hit testing using routed segment geometry;
- rich measured edge labels and editable routing handles;
- graph animation using stable node, edge, and segment identities;
- LambdaJS registration over the same Velmt and paint-layer contracts;
- additional layout algorithms behind the same canonical model.

## 18. Stage 2 - Rich Mermaid and Graph Conformance

Stage 2 evolves the working rich-node path into a format-independent graph
pipeline with broad Mermaid graph support. It does not put every Mermaid diagram
inside `lambda.package.graph`; package ownership follows the required layout
model rather than the source language name.

### 18.1 Package ownership

`lambda.package.graph` owns diagrams whose defining problem is graph topology:
node ranking or free placement, recursive grouping, relationship routing, ports,
markers, and overlap avoidance. Mermaid flowcharts are the first conformance
target. Class, state, entity-relationship, requirement, architecture, block,
mindmap, and other topology-oriented Mermaid families may reuse the graph IR and
graph layout with family-specific transforms.

`lambda.package.chart` owns Sequence, Gantt, pie, Sankey, timeline, XY, and
other data-, axis-, or time-oriented Mermaid families. The existing chart
package already accepts `<chart>` Mark and renders SVG. Those families require
new chart adapters and specialized chart layouts; they must not be represented
as fake Dagre graphs merely because their source syntax is Mermaid.

The Mermaid parser therefore belongs to the input layer and dispatches by
diagram family:

```text
Mermaid source
  -> source-stage Mark with source-order declarations
  -> graph-oriented family -> canonical Mark Graph IR -> graph package
  -> chart-oriented family -> canonical Mark Chart IR -> chart package
```

The graph proposal specifies only the graph-oriented branch and the boundary by
which chart-oriented source-stage Mark is handed to `lambda.package.chart`.

### 18.2 Stage 2 pipeline

The complete graph path is:

```text
Mermaid, D2, DOT, GV, or Mark graph source
  -> format-specific parser
  -> source-stage Mark with source spans and format-specific declarations
  -> pure normalization and semantic resolution
  -> canonical Mark Graph IR
  -> graph.transform.to_html()
  -> semantic HTML <graph>/<node>/<edge> tree
  -> Radiant lays out each rich node subtree
  -> measured node and edge-label Velmts
  -> retained JIT-compiled graph layout fn
  -> node/group placements and routed edge geometry
  -> generated SVG paint layers
  -> final SVG, PDF, raster, or interactive view
```

The source and canonical stages use the same Mark representation and are
distinguished by the root `ir-stage` attribute. Source-stage Mark preserves
declarations in source order, exact source spans, and format-specific constructs;
it may therefore contain repeated node ids. The normalizer applies
source-language semantics, including Mermaid node redeclaration, chained-edge
expansion, class assignment, defaults, subgraph membership, and generated
identity. It then emits canonical Mark Graph IR with unique node identities.
There is no separate Mermaid AST data model.

The Mermaid parser must not merge explicit node declarations. For example:

```text
A[Initial]
A{Final}
```

produces two source-stage `<node>` values, each with its own label, shape, and
source span. Mermaid normalization folds them into one canonical node whose
effective label is `Final`, shape is `diamond`, and provenance points to the
final effective declaration. Bare endpoint references synthesized while parsing
edges remain deduplicated because they carry no later declaration value that
would otherwise be lost.

A parser may build the Mark IR directly when the source grammar is already
canonical, but it must still pass through the same validation and normalization
contract. Unsupported syntax must produce a diagnostic; it must never be
silently discarded or accepted as a different diagram family.

### 18.3 Mark Graph IR contract

The Stage 2 IR extends the base schema in
`vibe/Mark_Graph_Schema.md`. Its stable top-level elements are:

- `<graph>` for identity, flavor, graph kind, direction, defaults, and options;
- `<meta>` for title, accessibility, provenance, and source information;
- `<styles>` containing normalized `<style-rule>` elements;
- recursive `<subgraph>` elements for clusters and local layout direction;
- `<node>` elements with stable ids, shape roles, classes, ports, and content;
- `<edge>` elements with stable ids, endpoints, constraints, markers, and labels;
- `<constraint>` elements for same-rank, ordering, minimum length, or fixed
  placement requirements that do not naturally belong to one edge.

Node labels, edge labels, and subgraph labels are content elements rather than
opaque serialized attributes when they contain formatting:

```mark
<node id:"client";
  <port id:"request" side:"east" offset:0.5>
>
<node id:"api" shape:"rounded" source-start:42 source-end:71;
  <label format:"markdown"; "The **API** service">
  <content; <strong; "API"> " service">
  <port id:"request" side:"west" offset:0.5>
>
```

`<label>` preserves the source-level label and format. `<content>` contains the
safe normalized Mark subtree that `to_html()` places in the measured graph node.
This separation allows formatting and text semantics to be tested without
depending on final HTML wrapper elements.

Edges use explicit endpoint and marker semantics:

```mark
<edge id:"request" from:"client" to:"api"
      from-port:"request" to-port:"request"
      arrow-tail:"none" arrow-head:"normal"
      line-style:"dashed" stroke-width:2
      constraint:true min-length:2;
  <label position:"center"; "HTTP">
>
```

Normalization invariants are:

1. Node, edge, and subgraph identities are stable and unique within a graph.
2. References resolve to canonical identities or produce structured diagnostics.
3. Node redeclarations merge according to source-format semantics.
4. Chained and multi-node links expand into explicit edge elements.
5. Subgraph containment remains recursive and deterministic.
6. Defaults, classes, and inline styles remain distinguishable until the style
   cascade is resolved; resolved values may be attached for layout and paint.
7. Source spans survive normalization for diagnostics and editor integration.
8. Unknown source extensions are either preserved in a namespaced metadata
   element or diagnosed; they are not silently thrown away.
9. Labeled nodes, edges, and subgraphs carry source semantics in `<label>` and
   measured Mark in `<content>`; a second normalization pass is idempotent.

### 18.4 Stage 2 module structure

The package should grow along established boundaries rather than enlarge
`dagre.ls` indefinitely:

```text
lambda/package/graph/
  graph.ls                     compatibility facade
  model.ls                     Mark Graph IR queries and constructors
  normalize.ls                 validation and canonicalization
  schema.ls                    declarative element, attribute, and child contract
  diagnostics.ls               structured graph diagnostics
  style.ls                     graph style cascade and safe properties
  layout.ls                    public measured-layout adapter
  scene.ls                     Graph Scene Mark SVG adapter and comparator
  layout/
    layered.ls                 ranking and rank constraints
    ordering.ls                crossing reduction and stable ordering
    positioning.ls             node and cluster coordinates
    routing.ls                 edge routing coordinator
    shapes.ls                  shape bounds and endpoint clipping
    clusters.ls                recursive subgraph geometry
    labels.ls                  edge and cluster label placement
  transform.ls                 public installation and HTML API
  transform/
    html.ls                    Mark Graph IR to semantic HTML
    content.ls                 safe rich-label/content lowering
    paint.ls                   routed geometry to SVG paint layers
    theme.ls                   graph visual defaults
  mermaid/
    flowchart.ls               flowchart-specific normalization
    class.ls                   class-node and relation semantics
    state.ls                   state-node and transition semantics
    er.ls                      entity and cardinality semantics
```

`dagre.ls` may remain as the Stage 1 implementation while the shared layered
components are extracted. The extraction should happen when the richer cases
require it, without duplicating ranking or routing logic among Mermaid families.

### 18.5 Mermaid flowchart coverage

Flowchart support is developed as feature groups so the conformance matrix can
show useful partial progress without claiming support based only on parsing the
header. The Stage 2 target includes:

- all directions (`TB`, `TD`, `BT`, `LR`, and `RL`) at graph and subgraph level;
- stable node identity, later declarations, quoted ids, Unicode, and escapes;
- legacy delimiter shapes and the general `@{ shape: ..., label: ... }` form;
- plain, quoted, HTML, and Markdown labels with wrapping and line breaks;
- solid, dotted, thick, open, directed, bidirectional, circle, and cross links;
- edge ids, edge labels, minimum lengths, chained links, and multi-node links;
- recursive subgraphs, explicit subgraph ids, local direction, and links to
  subgraphs;
- `classDef`, `class`, `:::`, `style`, and `linkStyle` semantics;
- graph defaults, front matter, supported initialization options, title,
  accessibility title and description, comments, and statement separators;
- links, tooltips, and safe interaction metadata without executing callbacks
  during parse, transform, or layout;
- self-loops, parallel edges, cluster crossing, ports, edge-label placement,
  shape-specific clipping, and deterministic routing.

Each feature is recorded as `baseline`, `extended`, `unsupported`, or
`invalid` in the test manifest. Merely producing an SVG is not a passing test.

### 18.6 Semantic HTML and render metadata

`transform.to_html()` continues to produce measured HTML rather than eager SVG.
Stage 2 adds recursive cluster metadata, named ports, and separately measured
edge and cluster label children. Zero-size `<edge>`, `<cluster>`, and `<port>`
metadata remains available to the callback, while visual labels are measured
children associated by stable identities.

Final SVG output preserves renderer-neutral identity metadata:

```html
<g data-graph-role="node" data-node-id="api">...</g>
<rect data-graph-role="cluster" data-cluster-id="backend">...</rect>
<path data-graph-role="edge" data-edge-id="request"
      data-from="client" data-to="api">...</path>
<g data-graph-role="edge-label" data-edge-id="request">...</g>
```

These attributes support accessibility, hit testing, editor integration, and
semantic conformance tests. They are not styling hooks and do not determine
paint order. Renderer-generated wrapper tags, classes, marker ids, and grouping
remain implementation details.

### 18.7 Semantic Graph Scene Mark

Raw SVG strings and raster screenshots are not conformance fixtures. Mermaid and
Radiant legitimately use different SVG tags, wrappers, classes, transforms,
marker definitions, and floating-point formatting. Both outputs are therefore
adapted into a smaller renderer-neutral Graph Scene Mark:

```mark
<graph-scene direction:"LR" width:286 height:104>
  <node id:"a" shape:"rounded" x:0 y:30 width:86 height:44;
    <label; "Start">
  >
  <node id:"b" shape:"diamond" x:180 y:10 width:106 height:84;
    <label; "Valid?">
  >
  <edge id:"e0" from:"a" to:"b"
        marker-start:"none" marker-end:"normal"
        start-side:"right" end-side:"left" route-kind:"orthogonal";
    <route;
      <point x:86 y:52>
      <point x:180 y:52>
    >
  >
>
```

The Mermaid SVG adapter and Lambda HTML/SVG adapter must perform equivalent
normalization:

- flatten nested SVG transforms into graph-local coordinates;
- map `rect`, `path`, `polygon`, `ellipse`, and renderer-specific wrappers to
  graph shape roles;
- collapse `<text>`, `<tspan>`, HTML labels, and `<foreignObject>` into normalized
  label content and whitespace;
- resolve CSS and presentation attributes into optional semantic paint values;
- replace generated DOM ids and marker URLs with source-level node, edge, and
  subgraph identities;
- remove wrapper groups, raw classes, implementation tag names, definition
  order, and insignificant numeric precision;
- parse path geometry, normalize its origin, and classify straight,
  orthogonal, curved, parallel, and self-loop routes;
- represent only meaningful bends or sampled curve structure rather than the
  original path command spelling.

Raw classes and element names may be used by a version-specific adapter to find
objects, but they are not emitted into Graph Scene Mark and are never compared.

The comparator has three levels:

1. **Exact semantics:** identities, topology, group membership, labels, shape
   roles, endpoint markers, directions, and declared constraints must match.
2. **Relational geometry:** ranks, ordering, containment, non-overlap, endpoint
   attachment, attachment side, and route kind must match.
3. **Tolerant geometry and paint:** dimensions, points, colors, stroke widths,
   and dash patterns use explicit per-category tolerances and comparison flags.

This contract intentionally ignores DOM construction while still detecting a
node attached to the wrong edge, a reversed rank, a lost label, an incorrect
shape, overlap, containment failure, or materially different route.

### 18.8 Adapted Mermaid conformance corpus

All Stage 2 Mermaid graph tests live under:

```text
test/lambda/graph/mermaid/
  README.md
  LICENSE.mermaid
  UPSTREAM_COMMIT
  manifest.mark
  cases/
    flowchart/
      basic/
      nodes/
      edges/
      labels/
      subgraphs/
      styles/
      interactions/
      invalid/
  expected/
    ir/
    html/
    scene/
  reference/
    package.json
    package-lock.json
    extract_cases.mjs
    generate_scene_refs.mjs
    mermaid_svg_adapter.mjs
```

The corpus adapts Mermaid's source fixtures and rendering-test diagram strings,
not its screenshots. `UPSTREAM_COMMIT` pins the exact source revision, and each
manifest case records the upstream file, upstream test name, Mermaid options,
feature tags, expected status, and reference version. The Mermaid license and
provenance remain beside the adapted corpus.

`expected/ir` contains canonical Mark Graph IR for parser and normalization
tests. `expected/html` contains selected semantic HTML structure expectations;
it excludes unstable CSS serialization. `expected/scene` contains reviewed
Graph Scene Mark. Geometry references may be generated from the pinned Mermaid
SVG DOM, while semantic-only references may deliberately omit geometry. No PNG,
JPEG, PDF, or screenshot goldens are part of this suite.

The reference tools are maintenance tools, not production dependencies and not
part of ordinary test execution. Reference generation runs pinned Mermaid in a
browser DOM because Mermaid uses browser text measurement. The adapter reads the
live SVG DOM, computed styles, and `getBBox()` results and writes canonical Mark
scene files. Updating references is an explicit reviewed operation; normal test
runs never rewrite expected output.

### 18.9 Dedicated test runner

Stage 2 adds a dedicated native test runner:

```text
source: test/test_graph_mermaid_gtest.cpp
binary: test/test_graph_mermaid_gtest.exe
cases:  test/lambda/graph/mermaid/manifest.mark
```

The runner is registered through `build_lambda_config.json` and the normal test
orchestrator. A focused Make target should be provided:

```text
make test-graph-mermaid
./test/test_graph_mermaid_gtest.exe
./test/test_graph_mermaid_gtest.exe --gtest_filter='*subgraph*'
```

The native runner uses project `lib/` containers and strings rather than `std::`
types. It owns the parsed manifest and Mark input state for the suite. The
in-process transform/layout/render stages run in a paired Lambda integration
fixture, which installs the compiled callback once and renders multiple cases in
one Lambda runtime. The focused Make target runs both layers; it does not start a
new `lambda.exe` process for every manifest entry.

Across the native corpus runner and retained render fixture, each manifest entry
performs the applicable stages:

1. Parse source into source-stage Mark and collect structured diagnostics.
2. Normalize to Mark Graph IR and compare with `expected/ir`.
3. Transform to semantic HTML and compare selected normalized structure with
   `expected/html`.
4. Run Radiant measurement, custom graph layout, and SVG rendering in-process.
5. Adapt Lambda HTML/SVG to Graph Scene Mark.
6. Compare the actual scene with `expected/scene` using the case policy.
7. Write actual IR, HTML, scene, and mismatch reports only under
   `temp/graph_mermaid/<case-id>/` when a case fails or diagnostic output is
   explicitly requested.

Every manifest case becomes a named parameterized GTest result. Baseline cases
must pass. Extended and explicitly unsupported cases remain visible and report
their expected status; they must not be silently omitted from discovery.
Invalid cases pass only when the expected diagnostic category and source span
are produced without a crash or partial graph masquerading as success.

The runner supports case and feature filtering, deterministic fixed font and
viewport configuration, concise semantic diffs, and a verbose mode that retains
all normalized artifacts. It never generates image fixtures and never updates
checked-in references during a normal test invocation.

### 18.10 Stage 2 implementation sequence

#### Stage 2A - IR and diagnostics

- finalize the Mark Graph IR schema and validator;
- emit source-order Mark separately from graph normalization;
- distinguish source and canonical contracts with `ir-stage`;
- preserve graph direction, source spans, diagnostics, and stable identities;
- add common IR queries and constructors in `graph/model.ls`;
- add exact IR fixtures and the initial dedicated runner.

#### Stage 2B - Mermaid flowchart semantics

- implement node redeclaration, chained and multi-node edge expansion;
- preserve recursive subgraphs and local direction;
- implement full label forms, general shape syntax, classes, styles, and
  accessibility metadata;
- diagnose chart-oriented and unsupported Mermaid families rather than parsing
  them as flowcharts;
- import and classify the pinned upstream flowchart corpus.

#### Stage 2C - Rich HTML transform

- lower safe Markdown and HTML label content into Mark/HTML children;
- emit recursive groups and separately measured edge and cluster labels;
- preserve stable graph-role metadata through final SVG rendering;
- compare normalized semantic HTML for selected cases.

#### Stage 2D - Layout and routing parity

- extract ranking, ordering, positioning, shapes, clusters, labels, and routing
  from the Stage 1 implementation into shared modules;
- add shape-specific bounds and clipping, self-loops, parallel edges, ports,
  compound routing, cluster crossing, and deterministic label placement;
- compare Graph Scene Mark semantics and tolerant geometry against the pinned
  Mermaid references.

#### Stage 2E - Additional graph-oriented Mermaid families

- add family adapters only when their semantics fit or deliberately extend the
  common Mark Graph IR;
- reuse rich HTML nodes, generated paint, stacking, routing, and the semantic
  runner;
- route chart-oriented Mermaid ASTs to `lambda.package.chart` and test that
  dispatch independently in the chart suite.

### 18.11 Stage 2 acceptance criteria

Stage 2 rich Mermaid graph support is complete when:

1. The public parser-to-transform boundary is canonical Mark Graph IR.
2. Mermaid graph and chart families dispatch to the correct package.
3. Unsupported syntax yields structured diagnostics rather than silent loss.
4. Recursive groups, rich node labels, edge labels, classes, styles, markers,
   accessibility metadata, and stable ids survive normalization and transform.
5. Flowchart directions, shapes, edge forms, subgraphs, self-loops, parallel
   edges, and compound routes satisfy the declared conformance matrix.
6. Final SVG retains renderer-neutral graph identity metadata.
7. The dedicated runner discovers all manifest cases and reuses one retained
   compiled runtime across the suite.
8. Parser IR, semantic HTML, and Graph Scene Mark comparisons pass for all
   baseline cases.
9. No image fixture or raw SVG string equality is required.
10. Every adapted Mermaid case records upstream provenance, pinned revision,
    feature tags, status, and comparison policy.

### 18.12 Current implementation status

The initial Stage 2 tranche is implemented as follows:

- the Mermaid parser emits `version`, `kind`, `diagram-type`, `directed`,
  `direction`, and `rank-dir` on the root Mark `<graph>`;
- `TD` is normalized to `TB`, and all five flowchart directions are preserved;
- every edge materializes both endpoint nodes, and subgraph references no longer
  create duplicate implicit nodes;
- the Mermaid parser emits `ir-stage: "source"` and preserves repeated explicit
  node declarations with independent values and source spans; normalization
  applies Mermaid's graph-global last-declaration semantics and emits
  `ir-stage: "canonical"` with unique node ids;
- recursive `<subgraph>` structure and local direction are retained, including
  quoted subgraph labels and ids;
- `classDef` and `class` statements survive as `<style-rule>` and
  `<class-assignment>` metadata instead of being discarded;
- quoted node labels lose only their Mermaid delimiter quotes;
- Mermaid Markdown-string delimiters and HTML-like label content now normalize to
  explicit `label-format` metadata on nodes and edges while preserving the
  source-level Markdown or HTML text in `label`;
- chained flowchart links normalize each operator into an explicit edge whose
  source is the preceding target, including independently styled operators;
- multi-node source and target sets use the same node-reference parser and
  normalize to the Cartesian set of explicit edges; chained operators consume
  the preceding target set as their next source set;
- extra solid, dotted, and thick link units normalize to `min-length`; the
  constraint survives semantic HTML and Velmt adaptation and is enforced by
  layered rank assignment rather than retained as inert parser metadata;
- Mermaid `edgeId@operator` syntax preserves stable edge identity, including
  deterministic suffixed identities when one multi-node declaration expands
  into several canonical edges;
- normal, circle, and cross endpoint markers normalize independently as
  `arrow-tail` and `arrow-head`, survive HTML and Velmt adaptation, and render
  as distinct SVG marker geometry;
- Mermaid graph, node, edge, and recursive subgraph values now retain numeric
  `source-start`, `source-end`, `source-line`, and `source-column` attributes;
  repeated node references preserve the first declaration span rather than
  replacing provenance during semantic upsert;
- each edge expanded from a chained or multi-node statement records the
  statement span, segment index, expansion index/count, and stable `source-id`,
  so normalization can relate generated edges back to one declaration;
- YAML front matter and `%%{init: ...}%%` directives survive as source metadata
  and are parsed structurally by `graph/mermaid/config.ls`; supported title,
  node/rank spacing, curve, and HTML-label settings lower into the transform;
- safe `click` link and callback declarations survive as inert `<interaction>`
  metadata, including tooltip, target, and balanced callback arguments;
  edge-ID class assignments and `@{ animate, animation, curve }` properties
  resolve through generated `source-id` values;
- parser errors and warnings are retained as structured `<diagnostic>` Mark
  values with stable code, severity, message, and source location; chart-family
  dispatch uses the explicit `mermaid.chart-family` code;
- `graph/diagnostics.ls` and `graph/normalize.ls` establish the pure public
  validation result `{graph, diagnostics, valid}`; the initial invariant pass
  diagnoses invalid roots and directions, missing or duplicate identities, and
  missing or unresolved edge endpoints, while preserving parser diagnostics;
- `transform.to_html()` passes Mark through this normalization boundary before
  semantic lowering, without adding state;
- Mermaid `accTitle` and line or block `accDescr` directives survive under
  source-spanned `<meta>` children; model queries apply last-declaration
  semantics and semantic HTML exposes them through `aria-label`,
  `aria-description`, and graph title metadata;
- `style` and `linkStyle` statements survive as source-spanned
  `<style-assignment>` values with node/edge target kind, canonical target text,
  and raw declarations; model queries resolve node ids, edge indices, explicit
  edge ids, and `default`, while HTML retains the original declarations in
  `data-style-declarations` for provenance;
- `graph/style.ls` tokenizes comma- or semicolon-separated declarations without
  splitting functional colors, rejects properties and values outside the safe
  graph allowlist, and emits typed fill, stroke, stroke width, text color,
  opacity, and dash-array values rather than appending authored CSS verbatim;
- class-definition declarations cascade before node-specific `style`
  assignments; safe node paint becomes reconstructed inline CSS, while safe
  `linkStyle` stroke, width, opacity, and dash metadata survives Velmt
  adaptation, Dagre normalization and routing, and generated SVG paint;
- node-local `:::` classes normalize to ordinary `<class-assignment>` metadata
  and therefore share the common model and HTML class-lowering path;
- Mermaid's general `id@{ shape: ..., label: ... }` node form is parsed, with
  `rect`, `diam`, `dbl-circ`, and `cyl` aliases canonicalized to the existing
  graph shape vocabulary while other shape names remain available to later
  renderers;
- chart-oriented Mermaid headers produce an `unsupported` graph result and a
  diagnostic identifying `lambda.package.chart` as their owner, rather than
  being parsed as fake flowcharts;
- `graph/model.ls` provides recursive node, edge, subgraph, style, class, and
  direction queries over Mark without replacing the public IR with maps;
- `graph/normalize.ls` recursively rebuilds `<graph>`, `<subgraph>`, `<node>`,
  and `<edge>` values into canonical Mark. It preserves arbitrary source
  attributes, materializes one `<label>` source element and one measured
  `<content>` element for labeled values, wraps authored rich node children in
  `<content>`, and returns an already-canonical recursive tree unchanged;
- `graph/schema.ls` defines the Graph IR element hierarchy, known attribute
  types and enums, required identities and endpoints, and canonical
  `<label>/<content>` cardinality. Normalization validates the authored tree
  before rebuilding it, so malformed canonical children cannot be silently
  collapsed;
- canonical `<label>` and `<content>` children are authoritative. Legacy
  `label` and `label-format` attributes remain as compatibility/provenance
  attributes, but model and transform consumers prefer the canonical children;
- `transform.to_html()` recursively lowers nested graph nodes and edges, carries
  subgraph membership as stable metadata, applies class assignments, places
  canonical `<content>` in measured nodes, and emits separately measured
  `<edge-label>` children from canonical edge content;
- `graph/transform/content.ls` reuses Lambda's Markdown and HTML-fragment parsers
  to lower rich node and edge labels into measured semantic Mark; its sanitizer
  reconstructs only safe inline formatting (`strong`, `em`, `code`, `u`, `sub`,
  `sup`, and `br`), strips authored attributes and unknown wrappers, and removes
  script, style, and template subtrees;
- the Velmt adapter places measured edge labels at routed anchors, paints start
  and end endpoint markers, clips rectangle, ellipse/circle, diamond, hexagon,
  trapezoid, inverse-trapezoid, and asymmetric endpoints, routes self-loops,
  and includes route extents in graph bounds;
- canonical nodes may contain scoped `<port>` children with a side and
  normalized offset; normalization diagnoses duplicate node-local port ids and
  unresolved edge port references, while semantic HTML emits zero-size port
  metadata for the Velmt callback;
- `transform.to_html()` emits zero-size recursive cluster metadata and
  separately measured `<cluster-label>` children. The layout callback computes
  nested containing boxes from measured member nodes and child clusters,
  reserves the label band, and returns full-viewport SVG cluster paint layers
  with independent signed `z` values;
- compound routes cross each exclusive source and target cluster boundary in
  containment order. Named ports are retained for ordinary, parallel,
  compound, and self-loop routes, and parallel compound edges receive
  deterministic sibling-local lanes;
- semantic node HTML now renders the legacy Mermaid rounded, stadium,
  cylinder, subroutine, hexagon, trapezoid, inverse-trapezoid, and asymmetric
  families in addition to rectangle, ellipse/circle, diamond, and
  double-circle shapes;
- semantic HTML and clipping also cover the general Mermaid shapes exercised by
  the pinned corpus, including card/notch, cloud, hourglass, bolt, lean,
  triangle, tag/document, delay, cylinder, brace, fork, divided, and lined
  variants;
- repeated edges with the same ordered endpoints receive deterministic,
  sibling-local lanes separated by `edge_sep`; semantic HTML carries this
  option as `data-edge-sep`, and route-bound normalization shifts nodes and
  edges together when an outer lane extends before the original graph origin;
- self-loop spacing also uses sibling-local order rather than global source
  index, so unrelated preceding edges cannot change loop geometry; measured
  edge labels interpolate the half-length point of the complete polyline rather
  than selecting a point-count midpoint on bent routes;
- generated edge paths retain `data-graph-role`, stable edge id, endpoints, and
  normalized marker names for semantic SVG adaptation and future hit testing;
- semantic HTML emits graph, node, edge, edge-label, cluster, and cluster-label
  roles with stable source identities, direction, shape, marker, label, and
  route metadata; Radiant preserves these attributes on final SVG wrappers and
  records measured absolute border-box geometry;
- `graph/scene.ls` adapts final Radiant SVG into renderer-neutral Graph Scene
  Mark, discarding wrapper tags and classes while retaining normalized nodes,
  clusters, edges, labels, markers, measured boxes, route classes, and points;
  its comparator performs exact textual semantics plus configurable tolerant
  box and route-point comparison with structured mismatch values. Canonical
  paint values, endpoint sides, node non-overlap, recursive cluster containment,
  endpoint attachment, and optional rank order are checked as renderer-neutral
  relations instead of wrapper- or class-specific SVG structure;
- Dagre ordering keeps direct cluster members contiguous, reserves inter-cluster
  padding, and iterates crossing reduction deterministically. Orthogonal routes
  avoid unrelated node and cluster boxes, and edge-label placement avoids nodes,
  cluster labels, and prior edge labels while expanding graph bounds when a
  collision-free label lies outside the original geometry;
- `graph.transform.render_svg()` and `render_scene()` run HTML parsing, Radiant
  measurement, the cached custom layout callback, generated paint, SVG lowering,
  and scene adaptation in one process. The paired `scene_render.ls` fixture
  renders multiple Mermaid cases after one callback installation and exercises
  both accepted and rejected geometry tolerances;
- `reference/mermaid_svg_adapter.mjs` adapts a live Mermaid browser SVG DOM to
  the same scene vocabulary, flattening transforms and normalizing labels,
  source identities, shapes, endpoint markers, bounds, and sampled routes. It is
  a pinned maintenance tool and is not an ordinary test dependency;
- the adapted Mermaid corpus pins commit
  `f3dea58385fd5c7dd1f4e9c9c1876751ae6943cc` (Mermaid 11.16.0), carries the
  upstream MIT license, and records source file, upstream test name, features,
  status, policy, and reference version for every adapted case;
- `reference/extract_cases.mjs` uses Acorn to verify or regenerate all 18
  adapted source strings from their exact upstream tests. Pinned Mermaid and
  Puppeteer maintenance tooling renders selected references and adapts their
  live DOM into Graph Scene Mark; neither dependency is used by ordinary tests;
- `test/test_graph_mermaid_gtest.cpp` reads
  `test/lambda/graph/mermaid/manifest.mark` once, retains one input runtime,
  dynamically registers every case as a named/filterable GTest, and compares
  recursive Mark semantics without raw SVG or image fixtures;
- the normal retained Lambda batch runner discovers every paired `.ls`/`.txt`
  fixture under `test/lambda/graph/mermaid`, so package transform, layout, and
  paint behavior is part of baseline test discovery;
- `make test-graph-mermaid` builds and runs both the focused native Mark runner
  and the filtered Lambda package integration batch; the build generator now
  applies a target's platform library exclusions consistently to consuming
  tests.
- the retained `scene_render.ls` fixture is manifest-driven: it discovers every
  `scene-semantic` case, installs the compiled layout callback once, renders all
  selected cases in one runtime, and compares five reviewed semantic scene
  fixtures without per-case process startup.

The checked-in manifest is a bootstrap corpus covering implicit endpoints,
directions, shapes and labels, recursive subgraphs, class and style metadata,
accessibility directives, and chart family diagnostics. Lambda integration
fixtures additionally cover recursive HTML membership, rich Markdown/HTML node
and edge labels, hostile-tag removal, class lowering, accessibility lowering,
measured edge-label placement, bidirectional markers,
shape clipping, self-loop bounds, parallel-edge separation and label anchors,
safe style rejection and precedence, and styled edge paint propagation. They
also cover recursive canonical rebuilding, arbitrary-attribute retention,
authored block content, canonical HTML consumption, normalization idempotency,
recursive visual clusters, measured cluster labels, named ports, compound and
parallel boundary routes, broader shape roles, cluster paint metadata, and
port-reference diagnostics, plus schema type, enum, child-placement,
cardinality, and required-attribute diagnostics.

The Stage 2 flowchart items covered by this proposal are complete: source
fidelity and safe metadata, cluster-aware layout quality, Graph Scene paint and
relational conformance, a broader reproducible Mermaid corpus, and a
manifest-driven retained end-to-end runner. The package deliberately compares
semantic and tolerant geometric relations rather than promising pixel parity
with Mermaid.

Graph-oriented Mermaid family adapters beyond flowcharts remain a subsequent
phase. Chart-oriented family dispatch is already rejected with structured
ownership diagnostics, but detailed sequence, Gantt, pie, Sankey, timeline, and
XY support belongs to `lambda.package.chart` and its independent test suites.

## 19. Stage 3 - Graphviz DOT Support

### 19.1 Scope and principles

Stage 3 gives DOT input the same end-to-end status as Mermaid flowcharts:

```text
DOT source
  -> source-stage Mark Graph IR
  -> DOT semantic normalization
  -> canonical Mark Graph IR
  -> graph.transform.to_html()
  -> Radiant child measurement
  -> retained Lambda graph layout callback
  -> generated graph paint
  -> final HTML/SVG/PDF/PNG
```

The implementation supports the Graphviz `dot` layered-layout model first.
DOT is the graph description language; `dot`, `neato`, `fdp`, `sfdp`, `circo`,
`twopi`, `osage`, and `patchwork` are distinct layout engines. Parsing the DOT
language must not imply that every Graphviz engine has been ported.

Stage 3 follows these principles:

1. Graphviz is not a production dependency. Lambda parses, normalizes, lays
   out, and renders graphs without invoking a Graphviz executable or library.
2. The parser preserves source structure and raw attributes. It does not apply
   defaults, merge nodes, expand edge products, convert units, or rewrite
   Graphviz names into CSS names.
3. DOT semantics are implemented by pure Lambda normalization functions.
4. Known Graphviz attributes lower into renderer-neutral Graph IR. Unknown
   attributes remain available as namespaced properties because DOT permits
   applications to define arbitrary attributes.
5. Graphviz maintenance tools provide semantic and geometric references, never
   raw SVG equality or image fixtures.
6. Rich node content continues to be measured by Radiant before graph layout.
   Graphviz geometry is a conformance reference, not a browser-layout oracle.

Stage 3 does not initially promise pixel parity with Graphviz, support every
Graphviz plugin, execute URLs or callbacks from authored DOT, or preserve source
whitespace and comments through formatting.

### 19.2 Pre-Stage 3 DOT baseline and required replacement

Before Stage 3A, `lambda/input/input-graph-dot.cpp` was a useful bootstrap
parser, but it was not a complete DOT parser. It handled simple identifiers,
single-hop node-to-node edges, one attribute list, basic assignments/defaults,
and named subgraph blocks. It did not preserve enough information to implement
DOT semantics correctly, including:

- numeric and HTML-string IDs;
- quoted-string concatenation and escaped physical newlines;
- case-insensitive keywords;
- node ports and compass points;
- chained edge right-hand sides;
- subgraphs as edge endpoints and their Cartesian edge expansion;
- anonymous subgraphs and brace-only endpoint sets;
- repeated attribute lists and separator-free attribute assignments;
- temporal, nested default-attribute scopes;
- strict graph edge identity and update semantics;
- source-order node redeclarations and implicit node creation;
- Graphviz HTML-like labels, record labels, and label escape substitution;
- structured diagnostics and complete source spans.

The pre-Stage 3 parser also eagerly renamed attributes such as `rankdir`,
`fontcolor`, `arrowhead`, `width`, and edge `style`. This loses the distinction
between authored DOT values and resolved rendering values. Stage 3 moves all
such interpretation into the DOT normalizer.

The current DOT formatter is similarly a basic graph serializer. It emits only
simple nodes, edges, labels, and clusters, always uses a directed edge spelling,
and does not round-trip arbitrary attributes, ports, defaults, strictness, or
source-stage statements. It will be revised after the source and canonical
contracts are stable.

### 19.3 Parser implementation options and size gate

Mermaid and DOT may use either manual C/C+ parsers or generated Tree-sitter
parsers. They do not have to make the same choice. Both implementations must
emit the source-stage Mark contract in Section 19.4, so parser technology does
not leak into normalization, transform, layout, or tests.

#### 19.3.1 Option A - manual C/C+

The manual option extends the current `SourceTracker` and `InputContext` style.
Mermaid and DOT can share:

- whitespace, line/block comment, source-location, and recovery helpers;
- quoted string and escape handling where the languages agree;
- bounded identifier/value readers;
- source-spanned property and diagnostic constructors;
- graph, statement, endpoint, and Mark child builders;
- parser limits and progress/infinite-loop guards.

The state machines cannot be usefully shared wholesale. DOT is a token grammar
with nested brace scopes, arbitrary attributes, endpoint subgraphs, and
case-insensitive keywords. Mermaid flowcharts are line-oriented and have
different edge, shape, directive, label, class, and interaction forms. A shared
"generic graph parser" would hide two grammars behind conditionals and would be
harder to verify than two small parsers using common lexical/Mark helpers.

Advantages:

- smallest expected code and read-only table footprint;
- direct source-to-Mark construction without a CST traversal layer;
- no generated parse tables and no per-parse Tree-sitter tree allocation;
- Mermaid already has broad tested flowchart coverage in this form;
- precise control over recovery, limits, and source-stage edge expansion data.

Costs:

- the DOT grammar and recovery behavior must be maintained manually;
- nested/ambiguous syntax requires more lookahead and progress invariants;
- diagnostics and malformed-input recovery tend to become ad hoc unless they
  are designed as first-class parser behavior;
- grammar changes are distributed across parser code rather than reviewed as a
  declarative grammar diff.

If selected, shared code is extracted only after identifying actual duplicate
logic in the Mermaid and DOT parsers. Parser state machines are not unified for
the sake of nominal reuse.

#### 19.3.2 Option B - Tree-sitter

The Tree-sitter option vendors a pinned grammar per language:

```text
lambda/tree-sitter-dot/
  grammar.js
  src/parser.c                 generated, never edited manually
  LICENSE
```

`input-graph-dot.cpp` then becomes a CST-to-Mark builder. It owns source
locations, semantic diagnostics, allocation limits, and construction of
source-stage Mark, while lexical recognition and syntax recovery come from the
generated parser.

Advantages:

- grammar rules are declarative and easier to audit against language specs;
- robust CST production and error recovery are supplied by a proven parser
  runtime;
- editor/incremental parsing can reuse the same grammar in the future;
- generated parsers are easy to fuzz independently of Mark construction;
- Lambda already links Tree-sitter, so its runtime has no incremental cost in
  the main `lambda.exe` binary.

Costs:

- every grammar contributes its own parse tables even though the runtime is
  shared;
- Mermaid and DOT cannot share generated tables, only runtime and CST-to-Mark
  helper code;
- a CST-to-Mark adapter remains necessary and contributes code not represented
  by generated-parser measurements;
- available third-party grammars may lag the source language and require
  substantial local extensions;
- the CST and Tree-sitter parser have a higher transient memory cost than direct
  Mark construction.

The experiment used `rydesun/tree-sitter-dot`; its pinned revision and license
are retained with the adapted fixture tranche. If the decision is revisited,
Lambda would own compatibility extensions in `grammar.js` and regenerate
`parser.c` through the build, following the same generated-source rule as the
Lambda grammar.

#### 19.3.3 Binary-size experiment

An initial size experiment was run on 2026-07-15 on arm64 macOS with Apple
Clang 17.0.0. All parser objects used:

```text
-O3 -DNDEBUG -ffunction-sections -fdata-sections -fvisibility=hidden
```

ThinLTO was disabled for object-level comparison because LTO objects contain
LLVM bitcode rather than final Mach-O sections. Generated parsers were also
linked into minimal `-dead_strip` probes to verify that the measured tables stay
live when the language entry point is referenced. The production release still
uses ThinLTO and dead stripping; a final implementation must be measured as a
full release binary delta.

Pinned experiment inputs:

- DOT Tree-sitter commit
  `80327abbba6f47530edeb0df9f11bd5d5c93c14d`;
- Mermaid Tree-sitter commit
  `90ae195b31933ceb9d079abfa8a3ad0a36fee4cc`;
- pre-Stage 3 manual parsers in `input-graph-dot.cpp` and
  `input-graph-mermaid.cpp`;
- pre-Stage 3 release `lambda.exe`: 17,261,320 bytes.

The Mermaid upstream grammar covers class, ER, flowchart, Gantt, mindmap, pie,
sequence, and state diagrams. A second generated candidate restricted the same
grammar entry point and supertypes to flowcharts. This flow-only build is a
parse-table lower bound, not a production-ready Lambda grammar.

`size` reports the following optimized object footprint (`dec`, including text,
data, unwind, and other object sections):

| Implementation | Scope | Object footprint | Difference from manual baseline |
|---|---:|---:|---:|
| pre-Stage 3 manual DOT | incomplete DOT subset plus Mark building | 8,436 B | baseline |
| Tree-sitter DOT | generated DOT grammar only | 44,821 B | +36,385 B |
| pre-Stage 3 manual Mermaid | Stage 2 flowchart parser plus Mark building | 28,439 B | baseline |
| Tree-sitter Mermaid, flow-only | generated grammar only | 21,213 B | -7,226 B before adapter |
| Tree-sitter Mermaid, all supported families | generated grammar only | 207,009 B | +178,570 B before adapter |

The dead-stripped probe file deltas over an empty executable were 50,240 bytes
for DOT, 33,896 bytes for flow-only Mermaid, and 215,360 bytes for full Mermaid.
Mach-O page alignment makes those file deltas coarser than object sections, but
they confirm that dead stripping does not remove the active parse tables.

The existing Tree-sitter runtime object is approximately 171,731 bytes. This is
not an incremental graph-parser cost in `lambda.exe`, which already needs the
runtime for Lambda, JavaScript/TypeScript, LaTeX, and other parsing paths. It
would matter for a future standalone graph-only binary.

Grammar validation results:

- the DOT candidate passed all 11 upstream corpus cases and the checked-in
  `test/input/test_graph.dot` fixture;
- the full Mermaid candidate passed its own 45 cases;
- the flow-only Mermaid candidate passed its eight upstream flowchart cases;
- both Mermaid candidates parsed only 8 of 34 current Lambda Stage 2 Mermaid
  fixtures without an error node (23.5%). The candidate grammar therefore does
  not currently cover Lambda's Mermaid v11.16-oriented feature surface.

After the manual DOT implementation and shared-helper refactor, the same
non-LTO object measurement was repeated. These numbers include direct Mark
construction, unlike the generated-parser objects:

| Manual unit | Before Stage 3A | Completed Stage 3A | Difference |
|---|---:|---:|---:|
| DOT parser and Mark builder | 8,436 B | 11,782 B | +3,346 B |
| Mermaid parser and Mark builder | 28,439 B | 26,300 B | -2,139 B |
| shared graph parser/Mark helpers | 3,056 B | 4,509 B | +1,453 B |
| combined | 39,931 B | 42,591 B | +2,660 B |

The production ThinLTO/dead-stripped `lambda.exe` grew from 17,261,320 to
17,261,336 bytes, a 16-byte file-size delta. Across the three implementation
units and their shared header, source size fell from 2,428 to 2,357 lines. The
completed parser therefore adds the missing DOT grammar and source fidelity
while using 71 fewer lines than the pre-Stage 3 implementation.

The initial generated-versus-manual rows were not a complete apples-to-apples
comparison: manual objects included Mark construction while generated objects
excluded CST-to-Mark adapters, and the original DOT parser was incomplete. The
post-implementation rows close the manual side of that comparison. A generated
replacement would still need an adapter and equivalent corpus/source-Mark
coverage, so the recorded Tree-sitter sizes remain conservative lower bounds.

#### 19.3.4 Final decision from the experiment

Stage 3 uses manual C/C+ parsers for both Mermaid and DOT:

- keep the existing manual Mermaid flowchart parser. The full generated parser
  costs about 179 KiB more before its required CST-to-Mark adapter and accepts
  only 8 of the 34 existing Lambda Mermaid fixtures without an error node;
- replace the incomplete manual DOT parser with a compact recursive-descent DOT
  parser. The generated DOT grammar alone costs about 36 KiB more than the
  current parser before adding its CST-to-Mark adapter, while DOT's grammar is
  small enough to express directly;
- share lexical, source-span, diagnostic, and Mark-construction helpers between
  graph parsers, but keep the Mermaid and DOT grammar state machines separate;
- measure the completed manual parser objects and release binary after corpus
  coverage is complete. LOC and binary size are implementation constraints, not
  permission to weaken source fidelity or diagnostics.

Tree-sitter remains a useful experiment and an independent grammar reference,
not a production dependency for either graph parser. The pinned commits,
commands, and measurements above make the decision reproducible. The release
measurement should still be repeated on Linux and Windows after Stage 3A, but
there is no longer a 64 KiB technology gate.

#### 19.3.5 Parser footprint ledger

Parser LOC is an actively maintained constraint. The metric is physical source
lines as reported by `wc -l`; it includes the two grammar implementations and
their shared graph parser source/header, but excludes tests, schemas, generated
files, and downstream normalization. The Stage 3A ledger is:

| Source unit | Before Stage 3A | Current ceiling | Difference |
|---|---:|---:|---:|
| `input-graph-dot.cpp` | 546 | 471 | -75 |
| `input-graph-mermaid.cpp` | 1,578 | 1,534 | -44 |
| `input-graph.cpp` | 206 | 247 | +41 |
| `input-graph.h` | 98 | 105 | +7 |
| total | 2,428 | 2,357 | -71 |

Reproduce the source measurement from the repository root with:

```sh
wc -l lambda/input/input-graph-dot.cpp \
  lambda/input/input-graph-mermaid.cpp \
  lambda/input/input-graph.cpp lambda/input/input-graph.h
```

`GraphParserTest.ParserLocBudget` enforces every current per-file ceiling and
the combined ceiling during the native graph parser suite. A parser change that
increases a ceiling must reduce duplication first, then update this ledger and
the test with a short rationale in the change description. A reduction should
lower both values in the same change. The ceilings are review gates, not spare
LOC that a later feature may consume without justification.

Optimized object and packaged-release measurements are repeated at each parser
milestone using the flags and method in Section 19.3.3. They are tracked beside
LOC because source reduction alone does not guarantee a smaller binary.

Parser limits must be explicit and diagnostic-producing:

- maximum statement and subgraph nesting depth;
- maximum HTML-label nesting and table-cell count;
- maximum chained endpoint count;
- maximum edge count after endpoint-set expansion;
- maximum identifier, string, and attribute value length.

Invalid input returns the recoverable source graph plus structured diagnostics
where possible. It must never silently skip an unsupported statement or emit a
partially canonical graph that appears valid.

### 19.4 Source-stage DOT Mark

DOT uses the existing public Mark Graph IR with `ir-stage: "source"` and
`flavor: "dot"`. It does not introduce a private C AST or a second public data
model. Source-only elements preserve the statement forms needed by the pure
normalizer:

```mark
<graph ir-stage: "source", flavor: "dot", id: "G",
    directed: true, strict: false;
  <dot-attr-statement target-kind: "graph";
    <property name: "rankdir", value: "LR">
  >
  <dot-attr-statement target-kind: "node";
    <property name: "shape", value: "box">
    <property name: "color", value: "steelblue">
  >
  <node id: "a";
    <properties namespace: "graphviz";
      <property name: "label", value: "Start">
      <property name: "shape", value: "ellipse">
    >
  >
  <dot-edge-statement id: "dot-stmt-4";
    <dot-endpoint kind: "node", id: "a", port: "out", compass: "e">
    <dot-endpoint kind: "node", id: "b">
    <dot-endpoint kind: "node", id: "c">
    <properties namespace: "graphviz";
      <property name: "label", value: "next">
    >
  >
>
```

The exact schema may use the ordinary `subgraph` element inside a
`dot-endpoint` for an inline endpoint set. The required invariants are more
important than the spelling:

- statement order is exact;
- every statement, endpoint, attribute list, and property has a source span;
- repeated declarations remain repeated;
- multiple attribute lists and repeated property names remain ordered;
- quoted, numeric, ordinary, and HTML IDs normalize to one string identity but
  retain source-kind metadata;
- raw DOT names and values are not converted during parsing;
- edge chains remain one source statement until normalization;
- anonymous subgraphs receive stable generated source identities without being
  misclassified as visual clusters.

`<properties namespace: "graphviz">` and `<property>` are generic Graph IR
extensions rather than DOT-specific maps. A property records at least `name`,
`value`, source span, and, after normalization, `origin` (`direct`, `default`,
or `inherited`) plus its defining scope/statement identity. This representation
preserves arbitrary DOT attributes while keeping known canonical attributes
directly queryable on graph objects.

### 19.5 DOT semantic normalization

Add the following package modules:

```text
lambda/package/graph/graphviz/
  normalize.ls              source order, identity, defaults, edge expansion
  attributes.ls             typed attribute interpretation and unit conversion
  labels.ls                 plain, record, and Graphviz HTML-like labels
  markers.ls                Graphviz arrow-shape grammar
  shapes.ls                 shape aliases and geometry families
  engine.ls                 layout-engine selection and diagnostics
```

`graph.normalize` dispatches source-stage DOT values to
`graphviz.normalize`. The normalizer performs these operations in source order:

1. Establish graph kind, strictness, graph identity, and lexical scopes.
2. Maintain independent graph, node, and edge default environments per
   subgraph scope.
3. Materialize implicit nodes at first reference and apply the defaults active
   at creation time.
4. Merge later declarations into the same node identity without retroactively
   applying newer defaults.
5. Resolve node ports and compass points.
6. Resolve subgraph endpoint node sets and expand chained edge right-hand sides
   into explicit pairwise canonical edges.
7. Preserve source-statement, segment, and expansion provenance on every
   generated edge.
8. In strict graphs, canonicalize directed or unordered endpoint identity,
   merge repeated edges, and apply later explicit attributes to the existing
   edge.
9. Classify only subgraphs whose names begin with `cluster` as visual clusters;
   ordinary and anonymous subgraphs remain semantic/default/rank scopes.
10. Resolve supported attributes into canonical values while retaining the
    complete namespaced property set.
11. Emit `ir-stage: "canonical"` and run the common graph schema and relational
    validators.

DOT graph-global assignment statements such as `rankdir=LR` and subgraph
constraints such as `rank=same` are not ordinary node declarations. They remain
scope properties and lower to canonical graph constraints where supported.

Canonical `<subgraph>` gains a `role` of `cluster` or `scope`. The transform and
layout paint and contain only `role: "cluster"` values. Scope-only subgraphs are
flattened into their nearest visual container for node/edge ownership while
their source identity, properties, memberships, and lowered constraints remain
available as metadata. This permits anonymous endpoint sets and `rank=same`
blocks without drawing a box around them.

The normalizer emits structured diagnostics for semantic conflicts, including
the wrong edge operator for graph kind, invalid compass points, duplicate ports,
unresolved compound cluster references, expansion limits, malformed record or
HTML labels, and known layout-affecting attributes that are not implemented.
Unknown application attributes are preserved without an error.

### 19.6 Attributes, units, and styles

Graphviz attributes are strings at the language level and acquire meaning from
the selected engine and object kind. `graphviz/attributes.ls` uses declarative
tables keyed by object kind and attribute name. Each entry defines:

- accepted value grammar;
- default value when Lambda intentionally matches it;
- canonical Graph IR target;
- unit conversion;
- inheritance/default behavior;
- layout, content, paint, interaction, or metadata ownership;
- supported, preserved-only, or unsupported status.

The first conformance matrix includes:

- graph: `rankdir`, `nodesep`, `ranksep`, `splines`, `compound`, `newrank`,
  `ordering`, `outputorder`, `bgcolor`, `font*`, `label`, `labelloc`,
  `labeljust`, `margin`, `pad`, `dpi`, and `layout`;
- node: `label`, `xlabel`, `shape`, `width`, `height`, `fixedsize`, `margin`,
  `style`, `color`, `fillcolor`, `font*`, `penwidth`, `peripheries`, `group`,
  `orientation`, `regular`, `sides`, `skew`, `distortion`, `image`, `URL`,
  `tooltip`, and `target`;
- edge: `label`, `xlabel`, `headlabel`, `taillabel`, `dir`, `arrowhead`,
  `arrowtail`, `arrowsize`, `headport`, `tailport`, `minlen`, `weight`,
  `constraint`, `samehead`, `sametail`, `lhead`, `ltail`, `decorate`, `style`,
  `color`, `font*`, `penwidth`, `URL`, `tooltip`, and `target`;
- cluster: graph paint/label attributes plus `cluster`, `margin`, `pencolor`,
  `peripheries`, and compound-edge participation.

Input dimensions are converted only during normalization. Canonical geometry
uses CSS pixels because Radiant measures border boxes in CSS pixels. Graphviz
reference geometry, which is generally expressed in points, is converted by
the reference adapter using `96 / 72`. Authored inch-valued node dimensions and
point-valued font/pen dimensions use explicit attribute-specific conversions;
they are never parsed as interchangeable unitless CSS numbers.

Graphviz `style` is tokenized as a Graphviz style list, not treated as authored
CSS. Supported styles lower to canonical fill, stroke, dash, radius, visibility,
and emphasis values. Unsupported style tokens remain in Graphviz properties and
produce a warning only when they affect declared conformance behavior.

Authored `URL`, `href`, `tooltip`, and `target` values remain inert metadata.
They may lower to safe link semantics in HTML, but no callback, command, image
fetch, or external navigation occurs during parse, normalization, or layout.

### 19.7 Labels, records, ports, and annotations

DOT has three materially different label forms:

1. Plain strings with object substitutions such as `\\N`, `\\G`, `\\E`,
   `\\T`, `\\H`, and `\\L`, plus line-alignment escapes.
2. Record labels with nested fields and `<port>` declarations.
3. Graphviz HTML-like labels, including tables, cells, font styles, images, and
   cell ports.

The parser preserves the authored form. `graphviz/labels.ls` applies object
substitutions only after identity resolution, parses record fields into nested
content and canonical ports, and parses Graphviz HTML-like syntax with a
dedicated allowlist. Graphviz HTML-like labels are not passed to the browser as
raw HTML.

Safe content lowers to ordinary Mark/HTML elements that Radiant can measure:
text runs, line breaks, inline emphasis, tables, rows, cells, and explicit cell
alignment. Unsupported tags or attributes produce diagnostics and safe fallback
text. Image labels use Radiant's normal resource policy and are disabled in
deterministic conformance tests unless the image is a checked-in fixture.

The existing canonical main `<label>/<content>` pair remains. Stage 3 adds a
generic measured annotation element for Graphviz's additional labels:

```mark
<annotation id: "edge-1-head-label", role: "head-label";
  <label format: "text"; "accepted">
  <content; "accepted">
>
```

Roles initially include `external-label`, `head-label`, and `tail-label`.
Annotations receive stable IDs, measured Velmts, placements, paint order, and
Graph Scene roles. This avoids forcing several semantic labels into one string
or adding Graphviz-specific fields to the layout callback.

Graphviz node ports lower to the existing canonical `<port>` model. Port IDs,
record/table geometry, and compass points determine attachment positions after
Radiant has measured the complete node content.

### 19.8 Shapes and arrow markers

`graphviz/shapes.ls` maps Graphviz shape names and aliases into geometry
families rather than implementing every name as unrelated code:

- rectangle/polygon: `box`, `rect`, `rectangle`, `square`, `polygon`;
- rounded or specialized polygon: `diamond`, `triangle`, `hexagon`, `octagon`,
  `parallelogram`, `trapezium`, house variants, stars, and biological symbols;
- ellipse: `ellipse`, `circle`, `doublecircle`, point variants;
- record/content: `record`, `Mrecord`, `plain`, `plaintext`, `none`;
- storage/document: cylinder, note, folder, component, tab, box3d, and related
  families.

Polygon parameters (`sides`, `regular`, `orientation`, `skew`, `distortion`),
`peripheries`, fixed sizing, and content margins become explicit geometry data.
Clipping and paint consume the shared family representation.

Graphviz arrow specifications are a small compositional grammar, not a single
marker enum. `graphviz/markers.ls` parses modifier and shape sequences into
canonical marker components, including open/filled, left/right clipping,
inversion, and multiple shapes. Paint places components in order at the routed
endpoint. Unsupported marker components retain their raw specification and use
a documented fallback marker with a warning.

### 19.9 Layout-engine contract

Stage 3 supports these engine modes:

- `dot`: supported by the Lambda layered layout engine;
- absent layout: select the Lambda `dot` layered engine for both directed and
  undirected DOT graphs, while recording the choice;
- `neato`, `fdp`, `sfdp`, `circo`, `twopi`, `osage`, `patchwork`: parsed and
  preserved, but return `graph.graphviz.unsupported-engine` until a dedicated
  engine is implemented;
- fixed `pos` input and Graphviz `-n` semantics: deferred with a distinct
  diagnostic rather than being mistaken for ordinary `dot` layout.

The `dot` mapping extends the shared Lambda graph layout with:

- `rankdir` and rank separation;
- temporal defaults already resolved by normalization;
- `rank=same|min|max|source|sink` subgraph constraints;
- `minlen`, `weight`, and `constraint=false` edges;
- stable source order, `ordering`, and node `group` hints;
- nested visual clusters and `newrank` behavior;
- record/HTML ports and compass attachment;
- compound edges through `lhead` and `ltail` clusters;
- parallel edges, self-loops, and label/annotation collision handling;
- `splines=ortho`, `polyline`, `line`, and a deterministic curved/spline mode.

Exact Graphviz ranking and crossing-minimization choices are not a requirement.
Declared semantic constraints and Graph Scene relations are. Geometry policies
may use tolerances for ranks, containment, attachment, route class, bends, and
relative ordering without comparing every control point.

### 19.10 Semantic HTML and rendering

`graph.transform.to_html()` receives only canonical Graph IR. It does not know
whether a node came from Mermaid, DOT, D2, or authored Mark except when a
namespaced property is intentionally exposed for provenance.

Stage 3 extends the existing transform with:

- Graphviz shape-family and marker metadata;
- safe plain, record, and HTML-like label content;
- measured external/head/tail annotations;
- periphery and gradient paint metadata;
- compound cluster endpoints;
- inert links and tooltips;
- renderer-neutral source IDs and Graphviz property provenance.

The same cached custom-layout registration and retained Lambda runtime are used
for repeated layout. Parsing and normalization happen once per source graph;
interactive relayout reuses canonical topology, resolved style data, measured
Velmts, and the compiled callback. Cache keys include layout-affecting Graphviz
properties and measured child dimensions.

### 19.11 Formatter behavior

The revised DOT formatter has two explicit modes:

- canonical DOT: serialize canonical graph semantics and retained Graphviz
  properties into stable DOT;
- source-semantic DOT: serialize source-stage statements in preserved order,
  without promising original whitespace or comments.

The formatter supports graph kind and strictness, correct `->`/`--` operators,
all DOT ID forms, safe quoting/escaping, ports and compass points, chained edge
statements where provenance permits reconstruction, nested/anonymous subgraphs,
defaults, assignments, repeated attribute lists, and HTML-string labels.

Round-trip conformance is semantic:

```text
DOT -> source Mark -> DOT -> source Mark
```

The two source Mark values must normalize equivalently. Byte-for-byte source
round trips are outside Stage 3.

### 19.12 Graphviz conformance corpus

All Stage 3 tests live under:

```text
test/lambda/graph/graphviz/
  README.md
  LICENSE.graphviz
  UPSTREAM_COMMIT
  manifest.mark
  cases/dot/
    language/
    defaults/
    edges/
    subgraphs/
    labels/
    records/
    html-labels/
    shapes/
    markers/
    styles/
    layout/
    invalid/
  expected/
    source/
    canonical/
    html/
    scene/
  reference/
    corpus.mark
    sync_cases.mjs
    generate_refs.mjs
    graphviz_json_adapter.ls
```

The corpus pins one official Graphviz source commit and carries the applicable
Graphviz license beside adapted fixtures. Cases are selected from the official
language examples, `tests`, `tests/graphs`, `rtest/graphs`, and focused Lambda
regressions. Every adapted case records upstream path, test name or fixture,
commit, engine, options, features, status, and comparison policy.

Reference generation uses pinned Graphviz only as an explicit maintenance tool:

```text
dot -Tdot_json case.dot      parser/cgraph semantic reference without layout
dot -Tjson case.dot          laid-out semantic and geometry reference
dot -Txdot_json case.dot     optional paint-operation reference
```

`dot_json` validates canonical topology, resolved object identities,
subgraphs, and attributes. Laid-out `json` supplies node boxes, cluster boxes,
edge splines, labels, and draw metadata. `xdot_json` is used only where paint
semantics cannot be recovered from ordinary JSON attributes.

`graphviz_json_adapter.ls` converts Graphviz JSON into the existing Graph Scene
Mark vocabulary. It removes Graphviz object indices, draw-operation ordering,
font-backend details, and engine-specific serialization while retaining stable
identities, topology, labels, shapes, markers, paint, boxes, endpoint sides,
routes, cluster containment, and rank relations. Point coordinates are converted
to CSS pixels and the bottom-up Graphviz coordinate system is converted to
Radiant's top-down coordinate system.

No Graphviz binary, browser, network access, raw SVG fixture, or image comparison
is part of an ordinary test run. Checked-in source, canonical Mark, HTML
semantics, and Graph Scene Mark are the test inputs.

### 19.13 Manifest-driven runners

Before adding a second format-specific runner, extract the reusable Mermaid
manifest discovery, status handling, semantic comparison, failure artifact, and
retained-runtime logic into a shared graph conformance harness. Mermaid and
Graphviz keep separate manifests and format-specific expectations but do not
duplicate runner mechanics.

Stage 3 provides:

```text
source: test/test_graph_graphviz_gtest.cpp
binary: test/test_graph_graphviz_gtest.exe
target: make test-graph-graphviz
```

The native runner dynamically registers every manifest case and checks parser
diagnostics, source spans, source-stage structure, and canonical expectations.
The Lambda fixture filters all render policies, installs the compiled layout
callback once, and renders every selected case in one retained runtime.

Policies are shared with Mermaid and extended where necessary:

- `source`: exact source-stage Mark semantics;
- `canonical`: normalized topology, defaults, properties, and diagnostics;
- `html-semantic`: renderer-neutral transformed HTML roles/content;
- `scene-semantic`: final topology, labels, shapes, paint, and relations;
- `scene-geometry`: semantic comparison plus declared tolerant geometry.

Failures write actual source IR, canonical IR, HTML, scene, and structured diffs
under `temp/graph_graphviz/<case-id>/`. Normal test runs never update checked-in
references.

### 19.14 Implementation sequence

#### Stage 3A - Parser and source contract (implemented 2026-07-15)

Stage 3A is complete:

- the manual-versus-Tree-sitter size and corpus experiment is recorded above;
- the old DOT subset is replaced by a 471-line recursive-descent parser behind
  the existing public parser entry point;
- Mermaid remains a compact manual flowchart parser, covering Lambda's existing
  Mermaid corpus; non-graph Mermaid families remain owned by `lambda.chart`;
- both parsers reuse shared source-span and diagnostic-to-Mark helpers without
  merging their distinct grammar state machines;
- DOT source Mark preserves statement order, raw attributes, repeated lists,
  assignments, ports, chained endpoints, and named or anonymous subgraphs;
- quoted, numeric, Unicode, concatenated, multiline, and nested HTML-like IDs,
  comments, case-insensitive keywords, limits, recovery, and diagnostics are
  covered by parser fixtures;
- source-only DOT schema and provenance expectations pass;
- the initial adapted `tree-sitter-dot` lexical corpus tranche is checked in at
  `test/lambda/graph/graphviz`, with its pinned revision and license.

Expansion of edge endpoint products, scoped defaults, node merging, and strict
edge identity remains intentionally in Stage 3B normalization. Keeping those
operations out of the parser preserves exact source declarations and avoids a
second source AST.

#### Stage 3B - DOT semantics and canonical IR

- implement temporal scoped defaults and node identity merging;
- expand edge chains and subgraph endpoint products with provenance;
- implement strict graph edge updates;
- distinguish ordinary subgraphs, rank scopes, and visual clusters;
- retain arbitrary Graphviz properties and lower the first typed attribute set;
- compare canonical output against pinned `dot_json` references.

#### Stage 3C - Content, shapes, markers, and HTML

- implement plain-label substitutions, record labels, ports, and safe Graphviz
  HTML-like labels;
- add generic measured annotations;
- implement Graphviz shape families, polygon parameters, peripheries, and arrow
  composition;
- lower safe styles, paint, links, and tooltips to semantic HTML;
- add selected normalized HTML expectations.

#### Stage 3D - Layered layout parity

- implement rank constraints, ordering/group hints, spacing, and edge weights;
- complete compound cluster and port behavior;
- implement declared line/polyline/orthogonal/curved route classes;
- place all labels and annotations without incoherent overlap;
- compare Graph Scene semantics and tolerant geometry against pinned Graphviz
  JSON references.

#### Stage 3E - Formatter, runner, and integration

- rewrite canonical and source-semantic DOT formatting;
- extract and reuse the graph conformance runner infrastructure;
- add `make test-graph-graphviz` and baseline discovery;
- verify `.dot` and `.gv` CLI render/view paths through the Lambda package;
- document the supported attribute/engine matrix and remaining diagnostics.

### 19.15 Acceptance criteria

Stage 3 basic Graphviz support is complete when:

1. The DOT parser covers the official grammar forms listed in Section 19.3 and
   emits structured source-spanned Mark without eager semantic rewriting.
2. Source-stage declarations, defaults, assignments, endpoint sets, and raw
   attributes survive parsing in exact statement order.
3. Pure Lambda normalization implements temporal defaults, implicit/redeclared
   nodes, edge expansion, strict graphs, and subgraph/cluster semantics.
4. Canonical IR preserves arbitrary Graphviz properties while exposing known
   layout, content, paint, marker, interaction, and metadata values.
5. Plain, record, and the declared safe Graphviz HTML-like label subset render
   as measured content, including ports.
6. Baseline Graphviz shapes, polygon parameters, peripheries, and compositional
   arrow markers survive final rendering and Graph Scene adaptation.
7. The Lambda `dot` engine honors declared rank, spacing, constraint, port,
   compound-cluster, and route-class behavior.
8. Unsupported Graphviz engines and known unsupported layout attributes produce
   stable diagnostics rather than silent fallback.
9. Canonical DOT and source-semantic DOT formatting round-trip semantically.
10. The pinned official corpus records provenance and passes source, canonical,
    selected HTML, and Graph Scene policies without image fixtures.
11. Reference regeneration is reproducible from a pinned Graphviz commit or
    binary/container digest and is not required by ordinary tests.
12. The manifest-driven native and retained-runtime runners discover every case
    and do not duplicate Mermaid runner infrastructure.
13. `.dot` and `.gv` render/view commands use the Lambda graph package without
    Graphviz installed.
14. Repeated Radiant layout reuses the retained JIT-compiled callback and does
    not reparse or renormalize unchanged DOT source.
15. Lambda and Radiant baseline tests remain green for the implemented surface.

### 19.16 Deferred Graphviz work

The following work is explicitly outside the initial Stage 3 acceptance gate:

- force-directed `neato`, `fdp`, and `sfdp` layout engines;
- radial/circular `twopi` and `circo` engines;
- packing engines such as `osage` and `patchwork`;
- exact Graphviz B-spline control-point parity;
- PostScript shape files and Graphviz renderer/plugin loading;
- unrestricted external images, URLs, scripts, or callbacks;
- byte-identical DOT formatting and comment preservation;
- pixel-identical output across Graphviz and Radiant font backends.

These features can be added behind explicit engine or capability contracts.
They must not weaken the source-faithful DOT parser, canonical Graph IR, or
renderer-neutral conformance model established by Stage 3.

### 19.17 Normative and reference sources

Stage 3 implementation and corpus selection use these upstream sources:

- official DOT language grammar: <https://graphviz.org/doc/info/lang.html>;
- Graphviz attributes and value types: <https://graphviz.org/docs/attrs/>;
- Graphviz JSON output contract: <https://graphviz.org/docs/outputs/json/>;
- official Graphviz repository and tests:
  <https://gitlab.com/graphviz/graphviz/-/tree/main/tests>;
- rejected Tree-sitter DOT experiment grammar reference:
  <https://github.com/rydesun/tree-sitter-dot>;
- Graphviz license: <https://graphviz.org/license/>.

The checked-in `UPSTREAM_COMMIT`, grammar revision, reference-tool version, and
license files become the reproducibility authority once Stage 3 implementation
starts. Moving upstream documentation does not silently change conformance.
