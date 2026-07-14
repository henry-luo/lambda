# Lambda Graph Package - Design Proposal

**Status:** Stage 1 is implemented for the initial rich-node release. The package
split, semantic HTML transform, Velmt callback, generated SVG paint, signed
stacking, runtime-scoped registration, CLI bridges, and C graph ABI removal are
complete. Stage 2 is in progress: the first common Mark IR contract, declarative
schema validation, recursive model queries, Mermaid flowchart normalization
fixes, measured edge and cluster labels, visual recursive clusters, named
ports, compound and parallel-edge routing, broader shape clipping, safe style
cascade, and the semantic corpus runner are implemented. Section 18.12 records
the current boundary and remaining work.

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
- Full Graphviz/Dagre parity is not required. Spline routing, obstacle
  avoidance, cluster-aware ranking, and collision-aware label placement remain
  later work.
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
  -> Mermaid source AST
  -> graph-oriented family -> Mark Graph IR -> graph package
  -> chart-oriented family -> Mark Chart IR -> chart package
```

The graph proposal specifies only the graph-oriented branch and the boundary by
which chart-oriented ASTs are handed to `lambda.package.chart`.

### 18.2 Stage 2 pipeline

The complete graph path is:

```text
Mermaid, D2, DOT, GV, or Mark graph source
  -> format-specific parser
  -> source AST with source spans and format-specific statements
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

The source AST and common IR are distinct. The AST preserves declarations in
source order, comments or directives where needed, exact source spans, and
format-specific constructs. The normalizer applies source-language semantics,
including Mermaid node redeclaration, chained-edge expansion, class assignment,
defaults, subgraph membership, and generated identity. Only then does it emit
the canonical Mark Graph IR.

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

Final SVG output should preserve renderer-neutral identity metadata:

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
it excludes unstable CSS serialization. `expected/scene` contains Graph Scene
Mark generated from the pinned Mermaid SVG DOM. No PNG, JPEG, PDF, or screenshot
goldens are part of this suite.

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

The runner must use project `lib/` containers and strings rather than `std::`
types. It owns one retained Lambda runtime and compiled graph package for the
suite, so cases exercise the same module and JIT caching behavior as repeated
document layout. It must not start a new `lambda.exe` process for every fixture.

For each manifest entry the runner performs the applicable stages:

1. Parse source into the source AST and collect structured diagnostics.
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
- separate Mermaid source AST construction from graph normalization;
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
- every edge materializes both endpoint nodes, repeated declarations update one
  graph-global node identity, and subgraph references no longer create duplicate
  nodes;
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
- `test/test_graph_mermaid_gtest.cpp` reads
  `test/lambda/graph/mermaid/manifest.mark` once, retains one input runtime, and
  compares recursive Mark semantics without raw SVG or image fixtures;
- the normal retained Lambda batch runner discovers every paired `.ls`/`.txt`
  fixture under `test/lambda/graph/mermaid`, so package transform, layout, and
  paint behavior is part of baseline test discovery;
- `make test-graph-mermaid` builds and runs both the focused native Mark runner
  and the filtered Lambda package integration batch; the build generator now
  applies a target's platform library exclusions consistently to consuming
  tests.

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

The following Stage 2 work remains open:

- a distinct Mermaid source AST and declaration-list provenance for merged
  values; the current boundary performs declarative Graph IR validation and
  retains first-declaration spans but does not yet separate parsing from
  normalization;
- interaction metadata, edge-ID property/class statements, the remaining
  Mermaid style properties beyond the initial safe paint allowlist, and
  rendering semantics for general shape names beyond the implemented legacy
  and polygon families;
- cluster-aware ranking/ordering, obstacle avoidance between independently
  placed clusters and nonmembers, and collision-aware route-label placement;
- renderer-neutral graph-role metadata in final SVG, Graph Scene Mark adapters,
  tolerant geometry comparison, and in-process Radiant render stages in the
  native runner;
- the pinned and licensed adapted upstream Mermaid corpus, provenance files,
  feature/status policies, and named per-case GTest registration;
- graph-oriented Mermaid family adapters beyond flowcharts and independent
  chart-package dispatch tests for chart-oriented families.

Accordingly, the current runner proves the parser-to-Mark semantic baseline. It
does not yet claim full Mermaid compatibility or satisfy the final Stage 2
acceptance criteria in Section 18.11.
