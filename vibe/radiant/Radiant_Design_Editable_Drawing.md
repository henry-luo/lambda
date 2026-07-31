# Radiant Editable Drawing Compatibility — Minimal SVG DOM Plan

**Date:** 2026-07-31
**Status:** Draft
**Scope:** extend Radiant's browser-shaped DOM only far enough to run real,
SVG-based drawing libraries as editable drawing probes: Raphaël, maxGraph, and
JointJS core.

**Builds on:**

- [Radiant Editable Support](Radiant_Design_Editable.md) — common editable
  input architecture and the CodeMirror, ProseMirror, and Editor.js gates.
- [Radiant Native DOM API](Radiant_Design_DOM_API.md) — native DOM contract and
  capability inventory.
- [LambdaJS Web DOM](../../doc/dev/js/JS_13_Web_DOM.md) — bridge and event
  implementation map.
- [Radiant Rich Editor Stage 4](../editing/Radiant_Editor_Stage4.md) and
  [Stage 5](../editing/Radiant_Editor_Stage5.md) — the native drawing-block
  architecture.

---

## 0. Decision

Radiant will use three real JavaScript drawing libraries as DOM/SVG
compatibility probes, in exactly the role that CodeMirror 6, ProseMirror, and
Editor.js play for text editing. The libraries own their graph/document models,
commands, history, and DOM projection. Radiant owns the single DOM, SVG
rendering, layout, standard event dispatch, focus, selection, mutation
records, and geometry APIs that the libraries consume.

~~~
local pinned drawing bundle
        |
        | standard DOM, SVG DOM, CSSOM, events
        v
LambdaJS native DOM wrappers  <---->  one Radiant DomDocument
        |                                  |
        | mutation / observer / geometry   | CSS cascade -> layout -> SVG render
        v                                  v
library-owned drawing state          visible editable SVG drawing
~~~

This is an upstream-library compatibility track. It does not replace the
Stage 4/5 architecture: Radiant's future drawing block, Mark records,
transaction algebra, geometric hit testing, and router remain the product
architecture. The probes establish that the same Radiant DOM can host a
third-party SVG editor without a second DOM tree, a WebView, or
library-specific native code.

The arrangement is the same as the existing editor fixtures:

1. A pinned upstream package is bundled locally for offline execution.
2. A small page adapter configures the library through its documented API.
3. Radiant receives ordinary browser events and exposes ordinary DOM APIs.
4. The library mutates its own model and DOM.
5. The fixture asserts library-owned serialized state plus visible DOM/SVG
   state.

An adapter may publish state for assertions and add explicit test controls. It
must not patch missing DOM methods, alter a library prototype, emulate SVG
geometry in JavaScript, or special-case Radiant. A missing API must fail the
probe and become a narrowly justified native implementation task.

### 0.1 Non-negotiable compatibility rules

1. **No canvas.** Every probe renders through DOM SVG. Its fixture asserts that
   the host contains SVG and no canvas. Canvas APIs, including Radiant's
   measurement-only OffscreenCanvas support, are outside this plan.
2. **No React.** Use JointJS core, maxGraph core, and Raphaël directly. React,
   JSX, hooks, virtual DOM, and React-specific adapters are out of scope.
3. **No execCommand or queryCommand APIs.** These APIs remain absent. Rich
   labels use the standards-based editing path, as the ProseMirror light-DOM
   fixture does.
4. **No library-specific native branch.** A DOM API is implemented for the SVG
   DOM contract, not because a caller is one of these libraries.
5. **No fake geometry.** Zero rectangles, identity matrices, and test-only
   JavaScript polyfills would make a page appear to load while corrupting
   selection, connector routing, and label placement.
6. **No source-of-truth change.** A probe's graph model stays inside the
   library. It is not converted into Mark records, Lambda transactions, or a
   native graph model.

---

## 1. Goals and non-goals

### 1.1 Goals

1. Run pinned, unmodified SVG branches of Raphaël, maxGraph, and JointJS core
   in a normal Radiant document.
2. Support common drawing interactions: create a graph, select and drag a
   shape, connect nodes, pan/zoom where provided, and destroy/recreate an
   editor.
3. Add only the smallest real SVG DOM surface demonstrated by a direct
   capability probe or upstream-library trace.
4. Reuse Radiant's SVG renderer, transform parsing, geometry, font
   measurement, DOM mutation, event, and lifecycle paths. Do not construct a
   parallel SVG scene or geometry system in LambdaJS.
5. Keep ordinary text-label editing on the existing focus, Selection/Range,
   beforeinput/input, and contenteditable compatibility path.
6. Produce deterministic headless fixtures and manually usable pages under
   test/html.

### 1.2 Non-goals

- Implement the full browser SVG DOM or arbitrary SVG filter/animation APIs.
- Support JointJS+, React, Canvas, WebGL, a WebView, an iframe, or a Worker as
  a compatibility workaround.
- Implement foreignObject in the initial milestone. SVG/HTML embedding is a
  separate design and the selected baseline configurations must not require it.
- Add touch/stylus APIs before a selected desktop-pointer probe demonstrates a
  blocker.
- Introduce synchronous reflow merely to mimic a library. Geometry reads retain
  Radiant's committed-layout checkpoint contract unless a separate DOM-layout
  decision changes it.
- Reimplement library routing, command, history, selection, or graph models in
  Radiant. Stage 5 may adopt their ideas and tests, but not their runtime.
- Implement execCommand or queryCommand APIs.

---

## 2. Probe portfolio

The order is deliberate: each library raises the SVG DOM requirement in a
measurable way.

| Probe | Role | Baseline interaction | DOM/SVG pressure |
|---|---|---|---|
| **Raphaël 2.x** | Small legacy-compatible SVG surface | create shapes/text, transform, drag, hit-test, image/gradient smoke, destroy | SVG construction, namespaced attributes, getBBox, matrix creation, mouse events |
| **maxGraph core** | draw.io/mxGraph-style diagram interaction | two vertices and an edge, select/move/resize, connect, pan/zoom, keyboard delete, undo/redo | layered SVG, CSS positioning, document gestures, scrolling, hit testing, graph bounds |
| **JointJS core** | strict SVG DOM/transformation probe | Paper with elements/link, drag, link reconnect, tools, label measurement, scale/translate, destroy | SVG identity, bounding boxes, CTM/screen CTM, matrices/points, text measurement |

The required configurations are intentionally modest and public:

- Raphaël uses its SVG branch only; its legacy VML path is never a target.
- maxGraph uses its SVG dialect only; HTML-only, Canvas, and VML fallbacks are
  excluded.
- JointJS uses core with an SVG Paper only; no React package and no
  foreignObject label configuration in the baseline.

A Raphaël page can pass while JointJS still exposes incomplete coordinate
transforms; a maxGraph move can pass while SVG text measurement is wrong.
Compatibility is reported per library and fixture, never as an unqualified
claim that SVG is supported.

---

## 3. Current starting point

Radiant already supplies the ordinary DOM foundation used by the editor pages:
tree mutation, selectors, inline/computed style, event capture/target/bubble,
PointerEvent/WheelEvent, getBoundingClientRect, elementFromPoint, animation
frames, and mutation/resize observers.

The critical distinction is that current createElementNS creates the same
generic DomElement as createElement and records a namespace URI as an internal
attribute. That is useful namespace reporting, not an SVG DOM implementation.

| API family | Current position | Required direction |
|---|---|---|
| SVG element identity | SVG nodes are generic wrappers; SVGElement, SVGSVGElement, SVGPathElement, and ownerSVGElement are absent | expose real SVG-aware wrapper/prototype identity without a second DOM |
| Namespaced attributes | setAttributeNS, getAttributeNS, and removeAttributeNS are absent | preserve SVG/XLink attribute behavior, including images and patterns |
| SVG bounds | getBBox is absent | local SVG bounds for shape, group, path, and text geometry |
| SVG coordinate transforms | getCTM and getScreenCTM are absent | coordinate conversion for transformed and zoomed SVG papers |
| SVG matrices and points | createSVGMatrix, createSVGPoint, SVGMatrix, SVGPoint, DOMMatrix, and DOMPoint are absent | affine conversion for Raphaël and JointJS; expose only the proven subset |
| SVG path/text measurement | native path length and SVG text-metric APIs are absent | defer until a selected trace needs them; JointJS label measurement is an expected candidate |
| HTML inside SVG | renderer/tag support is distinct from an SVG DOM interface | foreignObject stays out of the first baseline |

The non-forcing layout-metric contract remains in force. SVG geometry APIs must
state their committed-geometry behavior and must not quietly introduce a
second synchronous layout/render path.

---

## 4. Native SVG DOM design

### 4.1 One DOM with typed SVG capabilities

DomDocument, DomNode, and DomElement remain the only retained document tree.
Namespace and SVG-kind metadata attach to those existing nodes; no
JavaScript-only shadow SVG tree is introduced.

createElementNS with the SVG namespace must create an SVG-aware element,
retain its namespace, and select the relevant DOM prototype/interface. Parsed
inline SVG follows the same representation. The minimum identity contract is:

- SVGElement for SVG nodes;
- SVGSVGElement for svg roots;
- SVGGraphicsElement for renderable geometry nodes;
- SVGPathElement and SVGTextContentElement only when their first required
  method is implemented; and
- ownerSVGElement for descendants, with null for an SVG root.

The initial implementation must support instanceof and ordinary property lookup
for exposed interfaces. It must not claim unsupported methods on every SVG node
just to satisfy feature detection.

### 4.2 Namespaced attributes

Add standard namespace-aware attribute methods to the existing native attribute
store. The first required namespace is XLink for legacy SVG image and pattern
references; SVG 2 href remains an ordinary attribute.

The contract is attribute identity, lookup, replacement, removal, mutation
records, serialization, and repaint/layout invalidation. It is not a special
Raphaël attribute map. Existing setAttribute behavior remains unchanged.

### 4.3 Affine transform objects

Provide a native SVG affine matrix object with a, b, c, d, e, and f
coefficients. Reuse transform math already used by the SVG renderer; a second
matrix parser or transform convention is forbidden.

The method set is driven by actual library calls:

- SVGSVGElement.createSVGMatrix;
- multiply, inverse, translate, scale, rotate, and flip only when a probe
  covers them;
- point transformation through SVGPoint.matrixTransform or the modern DOMPoint
  equivalent when JointJS needs it; and
- direct a..f reads for callers that need coefficients only.

getCTM composes local SVG user space through ancestor transforms and the root
viewBox. getScreenCTM additionally composes committed DOM layout position and
scroll into client coordinates. Results derive from Radiant's authoritative
transform and layout data, never from page-side string parsing.

### 4.4 Bounds and text measurement

SVGGraphicsElement.getBBox returns the element's bounds in local SVG user
coordinates. The first supported shapes are those emitted by the probes:
rect, circle, ellipse, line, polyline, polygon, path, text, image, and g.

The contract is explicit:

- an element's own transform is not applied to its local box;
- group bounds union transformed descendant bounds in group coordinates;
- text bounds use Radiant's real font/layout measurements;
- missing, hidden, and unrenderable behavior is selected by a browser
  comparison; and
- stroke, marker, clipping, and SVG-2 getBBox options are deferred until
  required.

Native getTotalLength, getPointAtLength, and detailed SVG text APIs such as
getComputedTextLength remain absent until a library trace proves they are
necessary. Raphaël's mathematical path fallback does not justify native
path-length APIs. If JointJS needs text measurement, implement it through the
shared font/geometry path, not Canvas.

### 4.5 Rendering, events, and invalidation

SVG tree, attribute, and style mutations continue through the common DOM
mutation path. New SVG methods must retain this rule:

- mutations dirty the existing SVG/layout paint path;
- geometry reads observe the most recently committed checkpoint;
- hit testing stays behind elementFromPoint and normal event dispatch; and
- removing a drawing host releases library nodes, wrappers, listeners,
  observer targets, and SVG host objects with the owning document.

No drawing library action may invoke a renderer directly, bypass invalidation,
or attach a second event dispatcher.

---

## 5. Phased implementation and proof

### Phase D0 — Shared harness and capability inventory

Create an offline package alongside test/editable-editors:

~~~
test/drawing-editors/
  package.json              # exact dependency versions
  tools/build.mjs
  src/raphael-entry.js
  src/maxgraph-entry.js
  src/jointjs-entry.js
  README.md

test/html/
  editable-raphael.html
  editable-maxgraph.html
  editable-jointjs.html

test/ui/
  editable-drawing-raphael.json
  editable-drawing-maxgraph.json
  editable-drawing-jointjs.json
~~~

Each page includes a diagnostic capability report, but never a fallback
implementation. Each library publishes explicit state:

- Raphaël: deterministic live-element type/attribute data;
- maxGraph: official model serialization plus selected-cell identity; and
- JointJS: graph.toJSON output plus selected cell/link state.

The first fixtures create the page, wait for library readiness and a committed
layout checkpoint, assert SVG-only output, then exercise lifecycle teardown.
They also assert that execCommand and queryCommandSupported are absent.

**Exit:** all three pages load their bundles in a browser oracle. Any Radiant
failure identifies the first absent or semantically incorrect public API. No
native compatibility code is added in this phase.

### Phase D1 — Raphaël minimum

Implement only the SVG layer identified by the Raphaël trace:

1. SVG element identity and createElementNS construction;
2. SVGSVGElement.createSVGMatrix and accurate getScreenCTM;
3. SVGGraphicsElement.getBBox for shapes and text;
4. namespace-aware attributes; and
5. existing mouse/pointer, document-level move/up, hit-test, and lifecycle
   behavior verified through the library.

The fixture creates rectangle, circle, path, text, gradient, and image
elements; transforms one around its bounds; performs a drag; checks a hit test;
then removes the Paper. Image/gradient cases justify namespaced attributes.

**Exit:** all actions produce expected Raphaël state and visible SVG, with no
Canvas, exception, page shim, or legacy command API.

### Phase D2 — maxGraph SVG interaction

Use real SVG primitives in a maxGraph SVG-only page. Add APIs only after
recording the first upstream call path that needs them. Expected pressure is
SVG groups/defs/markers, CSS/layout metrics, document gesture capture,
wheel/keyboard events, SVG hit testing, and graph bounds. Options such as
useSvgBoundingBox are coverage choices, not features disabled permanently to
hide a missing API.

The first graph has two labelled vertices, one edge, and controls for adding a
vertex and destroying/recreating the graph. The fixture selects and drags a
vertex, resizes it, creates or reconnects an edge, pans/zooms, performs model
undo/redo, and verifies recreation.

**Exit:** the graph model reports expected positions/terminals and the SVG
projection contains expected vertex, edge, and marker structure.

### Phase D3 — JointJS core geometry completion

JointJS is the strictest acceptance target. Its Vectorizer requires real
bounds and affine conversion; it must not pass on zero-size boxes or identity
transforms. Implement remaining traced APIs, expected to include SVG
matrices/points, getCTM, getScreenCTM, local getBBox, and font-backed SVG text
measurement.

The page creates a Paper, two elements, a link, and a label. The fixture drags
an element, reconnects a link end, performs Paper scale/translate, shows and
removes a tool, verifies label bounds, and destroys/recreates the Paper.

**Exit:** coordinates and bounds agree with the pinned browser oracle within
documented floating-point tolerances, and no JointJS API is polyfilled by the
page.

### Phase D4 — Contract freeze

Record exact package versions, supported configuration, API table, and
exclusions in test/drawing-editors/capability-manifest.json. A library upgrade
must refresh the browser oracle and manifest before it changes Radiant
behavior.

The SVG interfaces remain owned by the DOM/SVG bridge, not fixture code. A new
library first reuses the manifest; it adds a compatibility item only for a
demonstrated standard API gap.

---

## 6. Test policy and acceptance gates

Each library requires three evidence layers:

1. **Direct SVG DOM tests** validate the method and geometry contract without a
   library.
2. **Headless UI fixtures** replay pointer, wheel, keyboard, and lifecycle
   actions through Radiant's normal event system and assert library-owned
   state.
3. **Browser oracle runs** execute the identical locally built page and action
   sequence to establish expected state and geometry.

The combined fixture set covers:

- construction and destroy/recreate;
- SVG-only rendering;
- selection and pointer drag;
- mutation/observer reconciliation when used by a library;
- pan/zoom or equivalent coordinate transformation;
- text-label measurement when configured;
- a namespaced image/pattern smoke case for Raphaël;
- absence of execCommand/queryCommand APIs; and
- no uncaught exception or stale listener/document ownership after destroy.

For native changes, required verification is:

~~~
make build
focused SVG DOM and drawing-editor UI fixtures
make test-radiant-baseline
git diff --check
~~~

npm install and npm run build inside test/drawing-editors refresh local browser
bundles only; baseline targets never install packages. The Make target name is
chosen with the harness, but it must run all three drawing fixtures as one
compatibility gate.

---

## 7. Risks and decisions

| Risk | Consequence | Decision / mitigation |
|---|---|---|
| Treating inline SVG rendering as a complete SVG DOM | missing methods or silent geometry errors | test SVG DOM methods directly; report status per API |
| Faking getBBox or matrices | links, labels, selection, and routing drift | calculate from authoritative Radiant SVG/layout/font data; no shims |
| Stale same-task geometry reads | library sees a pre-layout box after mutation | expose checkpoint timing; no global synchronous reflow as an ad-hoc fix |
| foreignObject scope creep | SVG/HTML embedding becomes a second large editing project | exclude from baseline and design separately |
| Library-specific patches | upgrades hide platform gaps | adapters configure and observe only; no prototype patches or caller detection |
| Product architecture confusion | third-party models become product storage | Stage 4/5 retain Mark records and transaction ownership |
| Canvas or React transitive use | violates scope and hides DOM gaps | pin core packages and assert SVG-only DOM in every fixture |

---

## 8. Open questions

1. Does the selected maxGraph version require native SVG bounds in its default
   SVG configuration, or only under optional shape/label features? Resolve
   from the Phase D2 source trace, not by assuming a default.
2. Which exact SVG text metric does the selected JointJS core version require
   after bounds and matrices are correct? Add only the observed method with a
   browser-oracle test.
3. Can the SVG renderer expose path/group bounds directly, or is a reusable
   geometry helper needed at the Radiant boundary? Reuse existing helpers; do
   not duplicate SVG path math in js_dom.cpp.
4. Before the first layout checkpoint, should SVG geometry return an empty
   browser-shaped value, schedule a checkpoint, or use retained attribute
   geometry? Make one DOM-wide decision, not a library branch.
5. Should drawing compatibility extend make test-editable or become a sibling
   drawing target? Decide after D0 establishes run cost.

---

## 9. Summary

The editable drawing work stays on the same side of the architecture boundary
as existing editor compatibility work: libraries own editing state, Radiant
supplies a real browser-shaped surface, and tests prove behavior through normal
events and serialized library state. The first deliverable is not a native
draw.io clone; it is a minimal, accurate SVG DOM able to host Raphaël,
maxGraph, and JointJS core without Canvas, React, execCommand, polyfills, or
library-specific engine branches.

Stage 4/5 remains the route to a first-class Radiant drawing editor. These
probes make that work safer by turning the SVG DOM and drawing-interaction
boundary into a concrete, regression-tested compatibility contract.
