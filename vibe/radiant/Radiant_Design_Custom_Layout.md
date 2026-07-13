# Radiant Custom Layout — Lambda/Houdini Proposal

**Status:** proposal
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

Pretext is the right inspiration for one important lesson: avoid DOM reflow by providing a dedicated measurement/layout API. Its split is attractive:

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

So an inline text subset is valuable, but insufficient as the custom layout foundation. The custom layout API needs two measurement levels:

1. **Inline text measurement** for labels and animation-friendly text effects.
2. **Child fragment measurement** for rich nested content, where the engine measures a child under constraints and returns a fragment size.

Pretext shows that a constrained subset can be genuinely powerful. Radiant should copy that lesson, not limit custom layout to text.

## 4. Design direction: Houdini-inspired, Lambda-first

CSS Houdini Layout API is the closest platform design to what we want. It proposes:

- a named custom layout mode
- an `intrinsicSizes()` callback
- a `layout()` callback
- child fragments measured under constraints
- declared input properties
- a worklet-like environment separated from normal DOM scripting

Radiant should implement the same idea in a simpler Lambda-native form.

The first version does not need the full CSS Houdini API. It needs only enough to port graph layout:

- register or resolve a custom layout by name
- expose the layout container and children as virtual elements
- let Lambda ask the engine to measure children
- let Lambda return container size and child placements
- keep mutation out of the layout callback
- allow the result to become normal Radiant view geometry

Future JS support can use the same host objects and callback contract, but Lambda should come first because:

- graph layout can be ported as pure Lambda data/code
- Lambda already owns the document/data transform story
- the engine can keep callbacks deterministic and non-mutating
- we avoid baking browser-JS layout thrashing into the design

## 5. Prerequisite: Velmt

Before custom layout, design and implement **Velmt**: a virtual element wrapper over Radiant's view/DOM tree.

Velmt is the Lambda-facing equivalent of a VMap host object: a lightweight handle to a `DomNode` / `View` that exposes safe layout-time operations without exposing raw C+ pointers or mutable internals.

Name rationale:

- `Element` is already the Lambda data model type.
- `DomElement` is the C+ DOM/layout node.
- `Velmt` means "virtual element": an engine-backed element handle for Lambda custom layout.

### 5.1 Velmt goals

Velmt should:

- wrap a `DomNode*` or layout child handle with stable identity for the current layout pass
- expose read-only structural and style data needed by layout
- expose measurement methods controlled by Radiant
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

During layout, mutation is forbidden. The layout callback computes geometry; it does not edit the tree.

### 5.3 Velmt minimal API

For the graph-layout MVP:

```lambda
velmt.tag(v) -> symbol|string
velmt.id(v) -> string|null
velmt.attr(v, name, default = null) -> value
velmt.style(v, property, default = null) -> value
velmt.children(v) -> array<Velmt>
velmt.text(v) -> string
velmt.measure(v, constraints) -> Fragment
velmt.measure_text(text, font, options = {}) -> TextMetrics
```

`measure()` delegates to Radiant layout in a constrained, side-effect-contained mode. It should return a fragment-like record:

```lambda
{
  width: 120.0,
  height: 48.0,
  baseline: 34.0,
  min_width: 80.0,
  max_width: 180.0
}
```

`measure_text()` is useful for labels, edge text, and animation. It should be implemented over Radiant's font engine, not through DOM layout.

### 5.4 Constraints

Use explicit constraints instead of implicit viewport state:

```lambda
{
  available_width: 320.0 | infinite,
  available_height: auto | 200.0,
  writing_mode: "horizontal-tb",
  direction: "ltr",
  mode: "measure" | "layout"
}
```

All dimensions are floats.

### 5.5 Safety and lifecycle

Velmt handles should be scoped:

- valid only during the current custom layout call
- invalidated after the layout pass
- non-owning over Radiant nodes
- protected from use-after-free by a document/layout generation stamp

If a stale Velmt is used, the API should return an error value and log a distinct prefix such as `VELMT_STALE_HANDLE`.

## 6. Custom layout API

The custom layout API should mirror Houdini's two callback phases, but in Lambda terms.

### 6.1 Registration

There are two possible registration models.

**Static package registration:**

```lambda
layout.register("graph", {
  intrinsic: graph_intrinsic,
  layout: graph_layout
})
```

**Document/import registration:**

```lambda
import radiant/layout

layout "graph" {
  intrinsic(container, children, style, ctx) { ... }
  layout(container, children, constraints, style, ctx) { ... }
}
```

For the MVP, a simple function table is enough. Syntax can come later.

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
fn intrinsic(container: Velmt, children: [Velmt], style: StyleMap, ctx: LayoutContext) -> IntrinsicSizes

fn layout(
  container: Velmt,
  children: [Velmt],
  constraints: LayoutConstraints,
  style: StyleMap,
  ctx: LayoutContext
) -> LayoutResult
```

Return types:

```lambda
type IntrinsicSizes = {
  min_width: float,
  max_width: float,
  min_height?: float,
  max_height?: float
}

type LayoutResult = {
  width: float,
  height: float,
  baseline?: float,
  fragments: [PlacedFragment]
}

type PlacedFragment = {
  child: Velmt,
  x: float,
  y: float,
  width: float,
  height: float,
  z?: int,
  transform?: value
}
```

For graph-to-SVG generation, a custom layout may also return generated content in a later phase, but the first version should keep layout and generation separate.

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
- SVG element generation

Radiant should still own:

- parsing Mermaid/D2/DOT into graph elements
- measuring rich node content through Velmt
- measuring text through the font engine
- rendering the resulting SVG
- integrating generated SVG into the document pipeline

### 7.3 Minimal graph layout functions

```lambda
fn graph_layout(graph: Element, opts: GraphOptions, ctx: LayoutContext) -> GraphLayout
fn graph_to_svg(graph: Element, layout: GraphLayout, opts: SvgOptions) -> Element
```

When graph nodes correspond to rich DOM/View children:

```lambda
let child_box = velmt.measure(node_view, {
  available_width: opts.max_node_width,
  available_height: auto
})
```

When graph nodes are simple labels:

```lambda
let text_box = velmt.measure_text(label, font, {
  max_width: opts.max_node_width,
  line_height: opts.line_height,
  white_space: "normal"
})
```

This gives the Pretext-like fast path for labels and the richer fragment path for nested content.

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
2. build scoped Velmt handles
3. call intrinsic callback when needed by parent measurement
4. call layout callback with explicit constraints
5. validate returned fragments
6. write final geometry back to child views
7. set container width/height/baseline

### 8.2 Measurement isolation

Child measurement must not commit final geometry unless explicitly requested by the custom layout result.

This follows Radiant's existing measure-then-place pattern:

- flex has a measurement pass
- grid has track sizing and item measurement
- intrinsic sizing has `RunMode::ComputeSize`

Custom layout should reuse that vocabulary instead of inventing a separate measurement stack.

### 8.3 Caching

Custom layout needs two cache layers:

- **Velmt child measurement cache** keyed by child id, constraints, style generation
- **custom layout result cache** keyed by container id, layout name, input properties, child style generation, constraints

The first implementation can use conservative invalidation. Correctness beats cleverness.

## 9. API discipline

Custom layout callbacks must be declarative:

- no DOM mutation
- no network
- no timers
- no global document layout flush
- no reading stale arbitrary geometry
- no storing non-regeneratable state in Velmt

Allowed:

- read attributes / selected computed style properties
- enumerate children
- measure child fragments under constraints
- measure text
- allocate local Lambda data
- return layout fragments

This keeps the engine free to cache, replay, and run measurement multiple times.

## 10. Future JS support

After Lambda custom layout works, expose the same model to LambdaJS:

```js
CSS.layoutWorklet.addModule("graph-layout.js")
```

or a Radiant-specific first step:

```js
RadiantLayout.register("graph", class {
  intrinsic(container, children, style, ctx) { ... }
  layout(container, children, constraints, style, ctx) { ... }
})
```

The important part is that JS receives Velmt-like layout children, not live mutable DOM elements. That follows Houdini's spirit and avoids procedural reflow loops.

## 11. Implementation phases

### Phase 1: Velmt design and host representation

- define Velmt handle type
- define lifetime/generation checks
- expose `tag`, `id`, `attr`, `style`, `children`, `text`
- add C+ tests for stale handle behavior
- keep mutation out of scope

### Phase 2: Measurement API

- expose `velmt.measure_text`
- expose constrained child `velmt.measure`
- route child measurement through existing Radiant measurement machinery
- add baseline and intrinsic size fields
- add cache keys and conservative invalidation

### Phase 3: Custom layout dispatch

- parse/represent `display: layout(name)`
- add `layout_custom_content`
- implement Lambda callback invocation
- validate returned fragments
- write child geometry back to the view tree

### Phase 4: Port graph layout to Lambda

- port graph extraction
- port ranking/order/coordinate assignment
- port edge routing utilities
- use `measure_text` for simple labels
- use `measure` for rich node content
- keep C+ graph implementation behind a temporary flag until parity is proven

### Phase 5: SVG generation and integration

- port `graph_to_svg` to Lambda
- render generated SVG through existing Radiant SVG pipeline
- add golden tests for Mermaid/D2/DOT fixtures
- compare against current C+ output before deleting old code

### Phase 6: JS-facing API

- wrap Velmt in JS host objects
- expose custom layout registration to LambdaJS
- evaluate Houdini-compatible names only after the Lambda API stabilizes

## 12. Open questions

1. Should Velmt wrap `DomNode*` directly, or an intermediate layout-child object?
2. Should custom layout return generated children, or should generation be a separate transform phase?
3. How much computed style should be visible in `style()` initially?
4. Should `display: layout(graph)` be accepted in CSS before custom layout registration exists?
5. Can Lambda callbacks be safely re-entered during intrinsic measurement from flex/grid parents?
6. What is the smallest useful rich-content measurement subset for graph nodes?
7. Should graph layout remain data-transform-only for SVG output, or also become a true CSS layout mode for DOM children?

## 13. Recommendation

Implement **Velmt first**. Without a safe virtual element abstraction, every custom layout path will be tempted to grow its own ad-hoc bridge into Radiant's view tree.

Then implement a minimal Houdini-inspired Lambda custom layout API:

```
Velmt + measure_text + measure(child, constraints) + layout callback -> fragments
```

Use graph layout as the proving ground. It is complex enough to validate the API, but contained enough to avoid designing a whole browser extension platform in one pass.

