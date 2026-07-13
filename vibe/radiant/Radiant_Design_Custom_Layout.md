# Radiant Custom Layout - Lambda/Houdini Proposal

**Status:** in progress; MVP hook, Lambda registration, map-backed Velmt snapshots, parent sizing, BFC behavior, and positive custom `z` stacking are implemented, while full Velmt/VMap host handles remain future work.
**Primary goal:** add a small, engine-supported custom layout surface for Lambda, enough to port Radiant graph layout out of C+ and into Lambda.
**Secondary goal:** keep the shape compatible with a future LambdaJS-facing API, without making JS/reflow the first implementation path.

## 1. Problem

Radiant has several layout domains that are not a perfect fit for built-in CSS layout modes:

- graph and diagram layout
- timeline / swimlane / dependency visualization
- rich labels inside graph nodes
- domain-specific document widgets
- small custom layout experiments that should not require a new C+ layout file

Today graph layout lives in C+ under `radiant/layout_graph.cpp`, `graph_dagre.cpp`, `graph_edge_utils.cpp`, and `graph_to_svg.cpp`. The current path is already more like a pure transform than a browser DOM workflow:

```
graph Element -> GraphLayout -> SVG Element
```

That makes it a good first candidate for a custom layout surface. The challenge is choosing the right boundary.

## 2. Why not JS DOM measurement

A browser-style procedural layout loop is the wrong foundation:

```js
node.style.width = "200px";
container.appendChild(node);
const box = node.getBoundingClientRect();
```

In browsers, `getBoundingClientRect()` and `offsetWidth` often force pending style/layout work. That read-after-write pattern is convenient, but it is a performance trap: code repeatedly mutates DOM, forces layout, mutates again, forces layout again. The result is layout thrashing.

In Radiant this is even less suitable as a base design:

- geometry reads currently return stored view fields; they do not guarantee a fresh layout flush
- a forced-flush API would couple custom layout to the global document layout pipeline
- layout scripts would become procedural and order-dependent
- every custom algorithm would need to manually avoid read/write interleaving
- graph layout wants to compute a whole geometry plan, not nudge DOM nodes one by one

This does not mean geometry reads are useless. They are needed for library compatibility and interactive JS. But they should not be the primitive for engine-level custom layout.

## 3. Why text-only measurement is not enough

Pretext is the right inspiration for one important lesson: avoid DOM reflow by providing a dedicated text layout path. Its split is attractive:

```
prepare(text, font, options) -> prepared
layout(prepared, maxWidth, lineHeight) -> height / lines
```

That shape is useful because `prepare()` performs expensive analysis and measurement once, while `layout()` is cheap arithmetic over cached widths. It can support paragraph height prediction, virtualized lists, manual line rendering, and text animation.

However, a graph node is not always just inline text. A useful diagram node may contain:

- multiline rich text
- inline code, icons, badges, and chips
- nested block content
- markdown-ish paragraphs
- tables or small property lists
- images or SVG fragments
- CSS padding, border, min/max constraints

So an inline text subset is valuable, but insufficient as the custom layout foundation. The core custom layout API should not ask Lambda to measure arbitrary rich content. Radiant already has layout engines for block, inline, flex, grid, table, SVG, replaced elements, and text. The better boundary is:

```
Radiant lays out each child with existing layout -> Lambda receives sized child views -> Lambda returns placements
```

Pretext still demonstrates that an inline subset can be useful and interesting, especially for labels and text animation, but it should be an optional helper surface. It is not the primitive for graph node sizing.

## 4. Design direction: custom layout as a Radiant sub-layout

Custom layout should hook into the current Radiant layout pipeline as another sub-layout mode, like table, flex, and grid.

The model is:

1. CSS selects a custom layout container, for example `display: layout(graph)`.
2. Radiant resolves styles for the container and its children.
3. Radiant lays out all in-flow children with the existing layout machinery.
4. Each child view now has a local border-box shape, initially normalized as `(x=0, y=0, width, height)`.
5. Radiant wraps those laid-out child views as `Velmt` handles.
6. Radiant calls the Lambda custom layout `fn`.
7. The `fn` returns `x, y` placements for child Velmts, plus optional parent sizing metadata.
8. Radiant writes those `x, y` values back to the child views.
9. Radiant computes the parent graph/container size from the containing box of the placed children, unless CSS width/height properties explicitly set or constrain it.

This is Houdini-inspired but simpler. Houdini lets author code measure child fragments during layout. Radiant's first design should avoid that callback-driven measurement step. Child sizing remains owned by Radiant; Lambda only computes arrangement.

Future JS support can use the same host objects and callback contract, but Lambda should come first because:

- graph layout can be ported as pure Lambda data/code
- Lambda already owns the document/data transform story
- the engine can keep callbacks deterministic and non-mutating
- the model avoids browser-JS layout thrashing by construction

## 5. Prerequisite: Velmt

Before custom layout, design and implement **Velmt**: a virtual element wrapper over Radiant's already-laid-out view tree.

Velmt is the Lambda-facing equivalent of a VMap host object: a lightweight handle to a `DomNode` / `View` that exposes safe layout-time reads without exposing raw C+ pointers or mutable internals.

Name rationale:

- `Element` is already the Lambda data model type.
- `DomElement` is the C+ DOM/layout node.
- `Velmt` means "virtual element": an engine-backed element handle for Lambda custom layout.

### 5.1 Velmt goals

Velmt should:

- wrap a laid-out `DomNode*` or layout child handle with stable identity for the current layout pass
- expose read-only structural, attribute, style, and geometry data needed by custom layout
- expose the child view's current border-box size
- expose no arbitrary DOM mutation during layout
- be cheap to pass through Lambda values
- be valid only inside the owning document/layout session
- support future JS host-object wrappers through the same native protocol

### 5.2 Velmt non-goals

Velmt is not:

- a general DOM API
- a replacement for Lambda `Element`
- a mutable MarkEditor surface
- a retained user object that survives arbitrary document mutations
- a browser `HTMLElement`
- a child measurement API

During layout, mutation is forbidden. The layout callback computes geometry; it does not edit the tree and does not ask Radiant to relayout children.

### 5.3 Velmt minimal API

For the graph-layout MVP:

```lambda
velmt.tag(v) -> symbol|string
velmt.id(v) -> string|null
velmt.attr(v, name, default = null) -> value
velmt.style(v, property, default = null) -> value
velmt.children(v) -> array<Velmt>
velmt.text(v) -> string
velmt.width(v) -> float
velmt.height(v) -> float
velmt.margin(v) -> Edges
velmt.border(v) -> Edges
velmt.padding(v) -> Edges
velmt.box(v) -> Box
```

`box(v)` returns the prelayout box for the child. For custom layout input, the child position is normalized:

```lambda
{
  x: 0.0,
  y: 0.0,
  width: 120.0,
  height: 48.0
}
```

The custom layout function returns where that already-sized child should be placed.

### 5.4 Safety and lifecycle

Velmt handles should be scoped:

- valid only during the current custom layout call
- invalidated after the layout pass
- non-owning over Radiant nodes
- protected from use-after-free by a document/layout generation stamp

If a stale Velmt is used, the API should return an error value and log a distinct prefix such as `VELMT_STALE_HANDLE`.

Current MVP note: the first implementation uses map-backed Velmt snapshots built per callback invocation. This avoids stale native pointer exposure while the full VMap/host-object Velmt design is deferred. The snapshot already exposes `tag`, `id`, `attrs`, `style`, bounded `children`, descendant `text`, box metrics, and edge metrics. A future Velmt host object should preserve the same public shape while adding document/layout generation checks.

## 6. Custom layout API

The custom layout API is a pure placement callback.

### 6.1 Registration

There are two possible registration models.

**Static package registration:**

```lambda
layout.register("graph", graph_layout)
```

**Document/import registration:**

```lambda
import radiant/layout

layout.register("graph", fn(parent, children, ctx) {
  ...
})
```

For the MVP, a simple function table is enough. Syntax can come later.

The design contract is that registered callbacks are pure Lambda `fn`s, not `pn`s. However, first-class function values do not currently carry reliable `fn`/`pn` metadata at runtime, so the MVP should not pretend to enforce this at registration. Runtime enforcement is deferred until function values expose a stable procedure/purity bit. Until then, the API documentation and examples should require `fn`, and the layout bridge should still forbid layout-time DOM mutation, measurement flushes, I/O, timers, and other side-effecting host APIs.

Current MVP API:

```lambda
import radiant;

radiant.register_layout("graph", (parent, children, ctx) => {
  placements: [for (child in children) {
    child: child,
    x: 0,
    y: child.index * (radiant.velmt_height(child) + 24)
  }]
})
```

The helper functions currently exported by the `radiant` module are:

```lambda
radiant.velmt_tag(v)
radiant.velmt_id(v)
radiant.velmt_attr(v, name)
radiant.velmt_attr_or(v, name, default)
radiant.velmt_style(v, name)
radiant.velmt_style_or(v, name, default)
radiant.velmt_children(v)
radiant.velmt_text(v)
radiant.velmt_width(v)
radiant.velmt_height(v)
radiant.velmt_box(v)
radiant.velmt_margin(v)
radiant.velmt_border(v)
radiant.velmt_padding(v)
```

MVP note: the ideal public shape remains `attr(v, name, default = null)` and
`style(v, name, default = null)`. Jube top-level native functions are currently
resolved by fixed arity, so the MVP exposes `velmt_attr_or` and
`velmt_style_or` for explicit default fallbacks until optional native module
arguments are supported.

### 6.2 CSS entry

Use a Radiant extension property/value inspired by Houdini:

```css
.diagram {
  display: layout(graph);
}
```

Fallback behavior:

- if `layout(graph)` is unknown, treat as block layout and log once per document
- `@supports (display: layout(graph))` can be added later
- custom layout containers establish a formatting context like block/flex/grid

### 6.3 Callback shape

```lambda
fn graph_layout(parent: Velmt, children: [Velmt], ctx: CustomLayoutContext) -> LayoutResult
```

The callback receives children after Radiant has already laid them out. It must not call back into layout measurement. It computes only placement and optional parent sizing.

Input context:

```lambda
type CustomLayoutContext = {
  layout_name: string,
  available_width: float|null,
  available_height: float|null,
  css_width: float|null,
  css_height: float|null,
  direction: string,
  writing_mode: string
}
```

Return types:

```lambda
type LayoutResult = {
  width?: float,
  height?: float,
  baseline?: float,
  placements: [PlacedChild]
}

type PlacedChild = {
  child: Velmt,
  x: float,
  y: float,
  z?: int
}
```

Child width and height are not returned because they were already resolved by Radiant. A later extension could allow explicit child resizing, but that would reintroduce a measurement loop and should stay out of the graph-layout MVP.

### 6.4 Parent sizing

If CSS explicitly sets width/height, CSS wins subject to the normal min/max constraints.

If width/height are auto, Radiant computes the custom layout container's border-box size from the containing box of placed children:

```
parent_width  = max(child.x + child.width)  - min(child.x)
parent_height = max(child.y + child.height) - min(child.y)
```

If placements include negative coordinates, Radiant should preserve them and create normal CSS overflow. This follows the existing Radiant/CSS model: positioned child boxes may extend outside the parent content box, and `overflow` decides whether the extra area is visible, clipped, or scrollable.

Auto parent sizing still uses the containing box of the placed children. If the minimum child x/y is negative, the containing box expands accordingly, but child coordinates are not rewritten just to avoid negative values.

## 7. Graph layout MVP

The first custom layout target is the existing graph layout pipeline.

### 7.1 Current C+ responsibilities

Current graph layout does:

- extract nodes, edges, and subgraphs from a graph `Element`
- assign ranks and order
- compute node coordinates
- route edges
- post-process edge paths
- generate SVG elements

### 7.2 Lambda port boundary

The Lambda port should own:

- graph extraction
- ranking / crossing reduction / coordinate assignment
- edge routing
- theme selection and graph options
- node placement decisions

Radiant should still own:

- parsing Mermaid/D2/DOT into graph elements
- laying out rich child node views with existing layout engines
- exposing already-sized children as Velmts
- rendering the resulting view tree or generated SVG
- integrating the custom layout result into the document pipeline

### 7.3 Minimal graph layout function

```lambda
fn graph_layout(parent: Velmt, children: [Velmt], ctx: CustomLayoutContext) -> LayoutResult {
  ...
}
```

The graph layout function reads each child Velmt's width/height and any graph attributes needed for node ids, edge endpoints, shape, rank hints, labels, and grouping.

For graph nodes with rich nested content, Radiant has already computed the node's `width` and `height`. For simple label nodes, that can still be represented as ordinary Radiant inline/block content inside the node, so the same prelayout flow applies.

This means the Lambda algorithm no longer needs `measure_text()` or `measure(child, constraints)` to place nodes. It receives concrete child boxes and returns positions.

## 8. Engine integration

### 8.1 Layout driver

Radiant layout should dispatch `display: layout(name)` similarly to flex/grid/table:

```
layout_flow_node
  -> resolve style
  -> if display is custom layout
       layout_custom_content(lycon, view, layout_name)
```

`layout_custom_content` should:

1. collect in-flow children
2. lay out each child with the existing Radiant layout machinery
3. normalize each child view to local `(0, 0, width, height)` for the Velmt input
4. build scoped Velmt handles
5. call the registered Lambda `fn`
6. validate returned placements
7. write final `x, y` back to child views
8. compute the parent size from CSS explicit sizes or the placed-child containing box
9. set container width/height/baseline

### 8.2 Child prelayout constraints

This is the main issue to define carefully.

Radiant must choose constraints for laying out children before the custom layout function runs. The default should be:

- use the custom container's resolved content width when definite
- otherwise use the parent layout context's available width
- honor each child's own explicit width/height/min/max properties
- honor custom-layout-specific CSS variables such as `--node-max-width` if we add them

This avoids a circular dependency where:

```
parent width depends on graph placement
graph placement depends on child size
child size depends on parent width
```

For the MVP, custom layout should document a simple invariant:

> Child sizes are computed before placement. Custom layout may position children but may not change their size.

If an algorithm needs different child wrapping, it must express that through ordinary CSS on the child before prelayout, for example a node `max-width`.

### 8.3 CSS box semantics

Define all placement coordinates as child border-box top-left positions in the custom container's content box. This follows Radiant's existing view geometry convention: `x` and `y` on a child view identify its border-box top-left relative to the parent layout coordinate space.

Open details:

- Whether margins are included in graph spacing or simply exposed to the Lambda function.
- Whether custom layout containers allow margin collapse. Recommendation: no; establish a formatting context like flex/grid.
- How absolutely positioned children are handled. Recommendation: exclude them from custom layout's child array and lay them out through the normal abs-position path.

### 8.4 Purity and re-entry

The custom layout callback is specified as a pure Lambda `fn`, not a `pn`.

Requirements:

- no state
- no side effects
- no DOM mutation
- no I/O
- no timers
- deterministic result for the same inputs

This matters because Radiant may need to run the callback more than once:

- during parent intrinsic sizing
- during a real layout pass
- after style invalidation
- under cache verification

The API must treat repeated execution as normal.

Implementation note: because runtime `Function` values currently do not reliably expose whether they originated from `fn` or `pn`, the first implementation should defer a hard registration-time `fn`/`pn` check. Add that check after first-class function values carry stable metadata. The important MVP invariant is that the host surface available during layout remains side-effect-free.

### 8.5 Caching

Custom layout needs three separate caches. They should not be collapsed into one concept.

**Runtime cache.** The Lambda script runtime that owns custom layout packages must be retained across layout passes. A page can relayout many times during load, resize, animation, editing, DOM mutation, and interactive state changes. Recreating the runtime for every layout pass would make custom layout unusable.

The runtime cache should be scoped at least per `DomDocument`, with module/package entries keyed by source identity:

- resolved source path or package id
- source content hash or mtime/size stamp
- Lambda compiler/runtime version
- ABI version of the Velmt host surface

If any key changes, the cached runtime entry is invalidated and rebuilt.

**Compiled function cache.** Registered custom layout `fn`s must be JIT compiled once and retained. `layout_custom_content` should call an already-compiled function handle, not parse/transpile/JIT the function body during layout. The cache entry should hold:

- layout name, for example `graph`
- compiled function item/handle
- owning runtime context
- source generation
- declared API version

This is similar in spirit to Radiant's retained page-script runtime and inline event-handler reuse: code is compiled once, then invoked repeatedly by the engine.

**Layout result cache.** The result of invoking the pure custom layout `fn` can be cached separately, keyed by:

- container id
- layout name
- available constraints
- parent style generation
- child ids
- child prelayout sizes
- child style generations
- graph-relevant attributes

The first implementation can use conservative invalidation for layout results. Correctness beats cleverness. Runtime/function caching, however, is not optional; it is part of the minimum viable performance story.

### 8.6 Invocation fast path

The hot path should look like:

```
layout_custom_content
  -> prelayout children with existing Radiant layout
  -> build temporary Velmt handles
  -> lookup retained CustomLayoutRuntime
  -> lookup compiled layout fn by layout name
  -> check layout result cache
  -> call compiled fn only on cache miss
  -> apply returned placements
```

No source loading, parsing, transpilation, MIR generation, or package initialization should happen on the hot layout path after warm-up.

The callback is still pure. Retaining the runtime does not mean layout code may keep mutable layout state. It only means the compiled code, immutable module constants, interned names, and runtime support objects survive across calls.

## 9. Potential issues with this flow

The design is sound, but a few rules need to be nailed down before implementation.

### 9.1 Sizing cycles

The only serious issue is child sizing dependency. If a child has `width: 50%`, what is the percentage relative to when the custom parent itself is auto-sized from child placements?

MVP rule:

- percentage child sizes resolve against the custom container's available width if definite
- otherwise use the outer parent layout context's available width
- if neither is definite, treat percentage widths as auto and log a custom-layout diagnostic

Current implementation note: custom layout now logs
`CUSTOM_LAYOUT_PERCENT_CHILD_AUTO_WIDTH` and
`CUSTOM_LAYOUT_PERCENT_CHILD_AUTO_HEIGHT` when an auto-size custom layout
container receives child percentage sizes on that axis. This makes the
prelayout fallback visible while preserving the MVP invariant that callbacks
only receive already-sized children. The callback context exposes
`child_available_width`, `child_available_height`, matching `*_definite`
booleans, and `*_source` strings (`css`, `available`, `intrinsic`,
`fallback`) so Lambda code can tell how child prelayout was constrained.

### 9.2 Child size is fixed for the callback

Since the callback cannot measure or relayout children, it cannot say "wrap this node at 160px, that node at 240px" unless those constraints were already encoded in CSS before child prelayout.

This is acceptable for the graph MVP. Add a later multi-pass extension only if a real layout needs it.

### 9.3 Edges are not ordinary child boxes

Graph edges are relationships, not child views with width/height. There are two viable designs:

- edges are graph data attributes read by the Lambda function, while only nodes are child Velmts
- edges are generated SVG/path content in a separate graph-to-SVG transform

The MVP should keep edge rendering separate from the custom placement API. Custom layout places nodes; graph rendering can consume the same layout result to draw edges.

### 9.4 Parent size vs overflow

If CSS explicitly sets the parent size smaller than the placed children, the result should overflow according to normal CSS overflow rules. Negative child placements create overflow too; they are not normalized away. If CSS size is auto, the parent grows to the containing box of placed children, including negative extents.

### 9.5 Ordering

Placement order and paint order should be separate:

- Lambda may return optional `z`
- default paint order remains child tree order
- positive `z` values are implemented as a pass-scoped custom-layout stacking overlay, wired into Radiant's shared render and hit-test stacking helper
- absent `z` clears any previous custom-layout stacking overlay on the next layout pass
- negative `z` and richer paint-layer ordering remain future design work

## 10. API discipline

Custom layout callbacks must be declarative:

- no DOM mutation
- no network
- no timers
- no global document layout flush
- no child measurement call
- no reading stale arbitrary geometry
- no storing non-regeneratable state in Velmt

Allowed:

- read attributes / selected computed style properties
- enumerate already-laid-out children
- read child width/height/box/margins
- allocate local Lambda data
- return placements and optional parent size

This keeps the engine free to cache, replay, and run layout multiple times.

## 11. Future JS support

After Lambda custom layout works, expose the same model to LambdaJS:

```js
RadiantLayout.register("graph", function(parent, children, ctx) {
  return {
    placements: children.map(child => ({ child, x: 0, y: 0 }))
  };
});
```

The important part is that JS receives Velmt-like layout children, not live mutable DOM elements. That follows Houdini's spirit and avoids procedural reflow loops.

A closer Houdini-compatible API can be evaluated later, but it should not pull in callback-driven child measurement unless the Lambda design proves we need it.

## 12. Implementation phases

### Phase 1: Velmt design and host representation

- define Velmt handle type - partial: map-backed snapshots exist
- define lifetime/generation checks
- expose `tag`, `id`, `attr`, `style`, `children`, `text` - initial helper API exists
- expose explicit default fallback helpers for `attr`/`style` - implemented as `_or` helpers
- expose `width`, `height`, `box`, `margin`, `border`, `padding` - initial helper API exists
- add C+ tests for stale handle behavior
- keep mutation and child measurement out of scope

### Phase 2: CSS and layout dispatch

- parse/represent `display: layout(name)` - implemented
- add `layout_custom_content` - implemented as `layout_custom_apply`
- establish custom layout as a formatting context
- define child prelayout constraints - implemented as a read-only per-axis child constraint snapshot with percent-size diagnostics
- lay out children through existing block/inline/flex/grid/table paths - implemented by running after child layout

### Phase 3: Lambda callback invocation

- implement custom layout registration - implemented as `radiant.register_layout`
- document registered callbacks as Lambda `fn` - implemented in this proposal; enforcement deferred
- defer hard `fn`/`pn` runtime validation until first-class function values expose stable procedure metadata
- add retained custom-layout runtime cache - partial: callback `Item`s are rooted and reused
- add JIT compiled layout `fn` cache - partial: registered function handles are retained
- build scoped Velmt arrays for laid-out children - implemented with map snapshots
- invoke the callback from `layout_custom_content` - implemented
- validate returned placements - implemented
- write child `x, y` back to the view tree - implemented
- compute parent size from CSS or containing box - implemented
- apply positive custom `z` through shared paint/hit-test stacking order - implemented

### Phase 4: Port graph layout to Lambda

- port graph extraction
- read node sizes from Velmt width/height
- port ranking/order/coordinate assignment
- port edge routing utilities
- keep C+ graph implementation behind a temporary flag until parity is proven

### Phase 5: SVG or graph rendering integration

- decide whether graph rendering remains graph-to-SVG or paints through child views plus generated edges
- add golden tests for Mermaid/D2/DOT fixtures
- compare against current C+ output before deleting old code

### Phase 6: JS-facing API

- wrap Velmt in JS host objects
- expose custom layout registration to LambdaJS
- evaluate Houdini-compatible names only after the Lambda API stabilizes

## 13. Open questions

1. Should Velmt wrap `DomNode*` directly, or an intermediate layout-child object?
2. Are graph edges represented as data only, generated SVG children, or a custom paint layer?
3. How much computed style should be visible in `style()` initially?
4. Should `display: layout(graph)` be accepted in CSS before custom layout registration exists?
5. What exact constraints should child prelayout use when the parent graph width is auto?
6. Should custom layout be allowed to return parent width/height when CSS explicitly sets them, or should CSS always override?
7. Should a future extension allow callback-driven child resizing, or is CSS-driven prelayout enough?

## 14. Recommendation

Implement **Velmt first**. Without a safe virtual element abstraction, every custom layout path will be tempted to grow its own ad-hoc bridge into Radiant's view tree.

Then implement a minimal Houdini-inspired Lambda custom layout API:

```
Radiant prelayouts children -> Velmt sized child views -> pure fn returns x/y placements
```

Use graph layout as the proving ground. It is complex enough to validate the API, but contained enough to avoid designing a whole browser extension platform in one pass.
