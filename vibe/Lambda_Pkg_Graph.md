# Lambda Graph Package - Design Proposal

**Status:** implemented for the initial rich-node release. The package split,
semantic HTML transform, Velmt callback, generated SVG paint, signed stacking,
runtime-scoped registration, CLI bridges, and C graph ABI removal are complete.
Advanced routing parity remains future work as described in Section 17.

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

## 3. Non-goals

- `transform.to_svg()` is not part of the first implementation phase.
- The custom layout callback does not mutate the DOM, perform I/O, start timers,
  or request child measurement or relayout.
- The graph package does not replace the Mermaid, D2, or DOT parsers. Their C
  parsers may continue producing Lambda `<graph>` data elements.
- The first version does not need full Graphviz/Dagre parity. Advanced spline
  routing, compound graphs, ports, and rich edge labels can be added in later
  phases.
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

## 5. Canonical Graph Model

The layout package should consume one normalized model regardless of whether
the source was a Lambda map, Mermaid, D2, DOT, or author-created elements:

```lambda
{
  nodes: [{
    id: "a",
    index: 0,
    width: 120.0,
    height: 48.0,
    shape: "box",
    z: 0
  }],
  edges: [{
    id: "e0",
    from: "a",
    to: "b",
    directed: true,
    arrow_start: false,
    arrow_end: true,
    style: "solid",
    z: -1
  }],
  direction: "TB"
}
```

Node `width` and `height` are border-box dimensions read from Velmt for deferred
HTML layout. The input parser or transform may supply shape and visual metadata,
but Radiant remains the authority for resolved node size.

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

### Phase 1 - Package split (complete)

- add `graph/layout.ls` and move the current public geometry API there;
- keep `dagre.ls` as the initial private implementation used by `layout.ls`;
- add `geometry.ls` and `routing.ls` as complexity warrants;
- retain `graph/graph.ls` as a compatibility facade;
- preserve and update current pure Lambda layout tests.

### Phase 2 - HTML transform (complete)

- add `graph/transform.ls` and `graph/transform/html.ls`;
- normalize `<graph>` parser output and map-shaped graph input;
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

### Phase 5 - Routing parity (four directions complete; advanced routing deferred)

- honor all four layout directions in rank and coordinate assignment;
- run the requested number of crossing-reduction iterations;
- add shape-specific clipping;
- add parallel-edge and self-loop routing;
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
