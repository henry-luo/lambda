# Radiant Editable Drawing Compatibility — Implemented SVG DOM Contract

**Date:** 2026-08-01
**Status:** Implemented; focused gate and MIR ratchet verified
**Scope:** real SVG-only Raphaël, maxGraph, and JointJS core drawing probes in
one Radiant `DomDocument`.

**Builds on:** [Radiant Editable Support](Radiant_Design_Editable.md),
[Radiant Native DOM API](Radiant_Design_DOM_API.md), and
[LambdaJS Web DOM](../../doc/dev/js/JS_13_Web_DOM.md).

---

## 0. Decision

Radiant hosts third-party drawing editors through its existing DOM, SVG
renderer, layout, event, focus, mutation, and lifecycle machinery. The
libraries retain ownership of graph/document models, commands, history, and
their SVG projection. This is a compatibility track; it does not replace the
Stage 4/5 native drawing-block architecture.

~~~
local pinned drawing bundle
        |
        | standard DOM, SVG DOM, CSSOM, events
        v
LambdaJS native DOM wrappers  <---->  one Radiant DomDocument
        |                                  |
        | mutation / geometry / lifecycle  | CSS cascade -> layout -> SVG render
        v                                  v
library-owned drawing state          visible editable SVG drawing
~~~

The resulting probes use unmodified public-library APIs. Page adapters publish
state and expose explicit controls for deterministic assertions; they never
patch a library, polyfill SVG in JavaScript, or select a Radiant-specific code
path.

### Compatibility rules retained

1. **SVG only.** Every fixture asserts one SVG host and zero canvas nodes.
2. **No React.** The covered packages are Raphaël, `@maxgraph/core`, and
   JointJS core; JSX, hooks, and virtual-DOM adapters are excluded.
3. **No legacy editing commands.** Drawing pages neither call nor expose
   `execCommand` or `queryCommand*` as an accidental compatibility surface.
4. **One retained DOM.** SVG identity and geometry are native interfaces over
   existing `DomElement` nodes, not a JavaScript shadow tree.
5. **No fake geometry.** Bounds and matrices come from Radiant SVG, layout,
   and font data. Zero boxes, identity-only matrices, and page shims are not
   accepted.
6. **No source-of-truth change.** Graph models remain library-owned; they are
   not converted to Mark records or a native graph model.

---

## 1. Delivered probe portfolio

| Probe | Pinned package | Actual coverage | Focused fixture |
|---|---:|---|---|
| Raphaël | `raphael@2.3.0` | SVG Paper, rect/circle/path/text/image/gradient, native hit-tested drag, destroy/recreate | `test/ui/editable-drawing-raphael.json` |
| maxGraph | `@maxgraph/core@0.16.0` | two vertices and an edge, pointer select/drag, wheel zoom, focused keyboard move, add/resize/reconnect, pan/zoom, undo/redo, destroy/recreate | `test/ui/editable-drawing-maxgraph.json` |
| JointJS | `jointjs@3.7.7` | SVG Paper, linked labelled elements, pointer drag, wheel zoom, focused keyboard move, link reconnect, Paper scale/translate, tool add/remove, native text bounds, destroy/recreate | `test/ui/editable-drawing-jointjs.json` |

The local package at `test/drawing-editors/` pins the public packages and
`esbuild@0.25.9`. `tools/build.mjs` bundles them offline; the test target never
installs packages. Package versions, supported configurations, exclusions, and
fixture evidence are frozen in
[`capability-manifest.json`](../../test/drawing-editors/capability-manifest.json).

The pages under `test/html/editable-{raphael,maxgraph,jointjs}.html` are also
manually usable. An external Chrome 150 render check on 2026-08-01 confirmed
that all three pages create their upstream SVG projections, including the
JointJS connector/tool affordance and maxGraph edge/marker output.

---

## 2. Native SVG DOM contract

`lambda/js/js_dom.cpp` now exposes this SVG surface on the retained DOM tree.
It is intentionally narrow but real.

| API family | Delivered contract | Evidence |
|---|---|---|
| SVG identity | `SVGElement`, `SVGSVGElement`, `SVGGraphicsElement`, `SVGPathElement`, `SVGTextContentElement`, and `SVGAngle` prototypes; correct `instanceof` and `ownerSVGElement` behavior | direct fixture and all three bundles |
| Namespace attributes | `setAttributeNS`, `getAttributeNS`, and `removeAttributeNS`; XLink `href` is preserved for renderer lookup and XML serialization | direct fixture and Raphaël image smoke case |
| Affine objects | native `SVGMatrix`/`DOMMatrix` and `SVGPoint`/`DOMPoint` coefficients plus multiply, inverse, translate, scale, rotate, flip, and point transformation | direct fixture; Raphaël and JointJS startup |
| Coordinate transforms | `createSVGMatrix`, `createSVGPoint`, `getCTM`, and `getScreenCTM` compose SVG transforms with committed layout coordinates | direct hit-test contract; JointJS and Raphaël operations |
| Geometry | `getBBox` for rect, circle, ellipse, line, polyline, polygon, path, text, image, and group; group bounds union descendants in group coordinates; text uses Radiant font measurement | direct rect/group/path/text assertions and JointJS text assertion |
| Hit testing | `document.elementFromPoint` and event-sim `assert_hit_test` use the same exact native target; rect, circle, ellipse, line, polyline, polygon, and path share fill and flattened stroke geometry rather than bounding boxes, including dashes, caps, joins, miter limits, and a 3 CSS-pixel stroke aiming allowance | direct fixture, WPT-derived hit-test suite, and drag assertions |
| XML | `DOMParser` parses SVG XML and `XMLSerializer` serializes the native SVG subtree, reconstructing XLink-qualified attributes | direct fixture |

Geometry follows Radiant's committed-layout checkpoint. It does not introduce a
second synchronous SVG renderer. A group's local box includes every child
contribution; therefore the direct test deliberately asserts that its
zero-position image contributes the origin to the union.

The direct contract lives in `test/html/svg-dom-contract.html` with
`test/ui/svg-dom-contract.json`. It creates SVG nodes without any library,
checks SVG-specific `instanceof` identity, rect/group/path/text boxes, CTM
point conversion and hit testing, XLink lookup, SVG XML parsing and
serialization, and absence of legacy editing commands.

`test/ui/hit-test/` is the dedicated coordinate-conformance suite. Its paired
pages under `test/html/hit-test/` adapt the relevant WPT SVG geometry,
pointer-events, and dashed-stroke cases to deterministic Radiant event replay.
They cover every basic SVG geometry element, fill rules, transforms and paint
order, all SVG `pointer-events` geometry modes, CSS cascade overrides, dash
phase/caps/joins/miter limits, `<use>` instances, text/image/`foreignObject`
viewport boxes, and nested SVG clipping. `assert_hit_test` intentionally
checks the one exact target returned by `document.elementFromPoint`; legacy
`assert_element_at` retains its ancestor-friendly layout-box behavior.

---

## 3. Runtime and lifecycle integration

The implementation keeps library behavior on normal DOM and LambdaJS paths:

- A document owns storage for its DOM nodes, so removing a library Paper cannot
  leave wrappers or listeners tied to a retired document allocation.
- `innerHTML`/node removal and event dispatch follow the retained lifecycle;
  JointJS may remove its configured Paper root and the page-owned host is
  restored before a new documented lifecycle begins.
- Event-simulation selector counting flushes the existing reflow checkpoint,
  so SVG host counts observe the committed DOM rather than a stale projection.
- The LambdaJS direct-class lowering preserves default-derived-constructor
  semantics through the class object's construct path. This retains the
  body-owning `HomeObject` for `super()` in bundled maxGraph classes instead of
  borrowing an ancestor body.
- Loop closure capture and callee rooting cover the bundled listener paths;
  direct and bundled closure fixtures lock the shared outer-state behavior.

These are general runtime/DOM fixes. There is no `if (library == ...)` branch
in the DOM, SVG, event, or compiler implementation.

---

## 4. Completed phase ledger

### D0 — Shared harness and inventory — complete

`test/drawing-editors/` contains the pinned package definition, README,
capability manifest, offline build script, and adapters. Each adapter publishes
library-owned state in `#drawing-state` and reports the exposed SVG interfaces
in `#drawing-capabilities`. The common harness enforces SVG-only output and
destroy/recreate behavior without changing upstream prototypes.

### D1 — Raphaël minimum — complete

The real SVG Paper creates all required primitive/image/gradient cases. Its
native drag begins only after the actual SVG hit target receives the pointer
press; the fixture observes that event and verifies teardown.

### D2 — maxGraph SVG interaction — complete

The real model serializes through `ModelXmlSerializer`, not a replacement
model. The fixture verifies selection, physical drag, wheel and focused key
delivery, geometry-changing controls, edge reconnect, view transform,
undo/redo, and destruction/recreation against the graph's own model state.

### D3 — JointJS core geometry — complete

The real `Paper` exercises SVG geometry under link and tool changes. Its
element labels are native SVG text in the selected upstream configuration, so
the fixture checks their nonzero native `getBBox` result. Link-label internal
markup is deliberately not treated as a public JointJS selector contract.

### D4 — Contract freeze — complete

The manifest records exact direct dependencies, configurations, exclusions,
implemented capability status, fixture locations, and the external-browser
initial-render check. Upgrades must refresh this evidence before they change
the supported surface.

---

## 5. Acceptance gates and regression coverage

`make test-drawing` is the focused compatibility gate. It builds the offline
bundles and runs, in order:

1. the direct SVG DOM geometry/serialization/hit-test contract;
2. the dedicated WPT-derived SVG hit-test suite (`make hit-test-ui`);
3. direct and minified-bundle loop-listener closure regressions;
4. Raphaël UI replay;
5. maxGraph UI replay; and
6. JointJS UI replay.

The UI scenarios drive regular pointer, wheel, focus, keyboard, button, and
lifecycle events through Radiant. They assert library-owned serialized state
as well as DOM state; a library merely loading is not sufficient.

For native SVG or LambdaJS changes, retain these gates:

~~~
make build
make hit-test-ui
make test-drawing
make test-radiant-baseline
make test-lambda-baseline
make test262-baseline
git diff --check
~~~

The browser oracle is an additional upgrade check, not a runtime fallback:
render the local built pages in a supported browser and compare their actual
upstream SVG projection before changing a capability entry or geometry
tolerance. The committed UI fixtures remain the deterministic event replay.

### Verification snapshot — 2026-08-01

- `make hit-test-ui` passes all six fixtures (66 exact-target assertions). The
  suite is a dependency of `make test-drawing`, so library compatibility and
  native SVG geometry cannot drift apart. It verifies that a thin path only
  wins within its stroked contour and aim allowance; empty path boxes, dash
  gaps, bounded viewport overflow, and `pointer-events: none` never steal a
  neighboring SVG target.
- `make build` and `make test-drawing` pass. The focused gate covers all three
  real libraries, the direct SVG contract, bundled listener closures, and the
  dedicated exact-target SVG geometry suite.
- The direct SVG contract also places a stroked path above a circle inside the
  path's bounding box. The circle remains pickable away from the actual path,
  while a point 3 CSS pixels from the thin stroke picks the path. This prevents
  a broad path box from stealing circle drags without making thin paths hard to
  aim at.
- The same contract verifies filled and stroke-only rounded rects, circles,
  ellipses, polylines, polygons, and paths, plus a line. Fill selects only the
  actual interior; `fill="none"` selects only the stroked contour and its
  aiming allowance. Bounding-box corners, shape interiors without fill, and
  empty container-union gaps select the backing surface instead.
- The external Chrome 150 oracle completed real pointer drag, focused keyboard,
  control, and destroy/recreate sequences for all three pages, as well as
  maxGraph's native wheel path. JointJS's page-owned host wheel listener passed
  its standard browser `WheelEvent` path; Chrome's DevTools input injection did
  not acknowledge a hardware-wheel command for that Paper. The deterministic
  Radiant fixture remains the hardware-event acceptance gate.
- `make test-radiant-baseline` passes the 4,378-case core layout suite, 23
  view-command tests, 105 page-load tests, and all 211 expected visual cases.
  The aggregate target still returns nonzero only for the known DOM/UI cases
  `babel_class_capture`, `bootstrap_data_api_alert`, and `sortable_drag`
  (60/63), plus the four unchanged WPT input-event failures (16/20). This
  record does not rebaseline or mask them.
- `make test-lambda-baseline` passes all 2,104 input cases and the 651-case
  Lambda runtime suite. Its aggregate remains nonzero for three existing MIR
  size-ratchet entries and 201 broad Node/system JS cases (204 total), none of
  which exercise the SVG hit-test path.
- `make test262-baseline` builds its runner and release executable, then stops
  before executing tests because `ref/test262` is at
  `2b2ecead6e828dd9af13a9ec72065e645724a50f` while the checked-in baseline
  records `673e9bacbe28590f501e2dcd817aadcc31899191`. Updating either snapshot
  is a separate conformance-inventory decision.

---

## 6. Deliberate exclusions and upgrade policy

- Canvas, WebGL, VML, React, JSX, virtual-DOM adapters, WebViews, iframes,
  workers, and `foreignObject` are excluded.
- The packages use only their selected SVG configurations. No claim is made
  for JointJS+, arbitrary browser SVG APIs, arbitrary SVG filters/animation,
  or every possible library configuration.
- Detailed SVG path-length APIs and SVG 2 `getBBox` options are not promised.
  Add them only after a direct contract and a real-library trace demonstrate
  the need.
- Radiant's future drawing blocks still own product storage, transactions,
  geometric hit testing, and routing. These library fixtures are compatibility
  probes, not product persistence.

When upgrading a package, rebuild the local bundle, run the external-browser
render check and `make test-drawing`, then update the manifest only with a
demonstrated standard DOM/SVG requirement. A failure is not grounds for a
page shim or a library-specific native branch.

---

## 7. Outcome

Radiant can now host real Raphaël, maxGraph, and JointJS SVG editors in one
native document with accurate SVG identity, namespace handling, affine
geometry, bounds, text measurement, hit testing, XML handling, ordinary
events, and clean lifecycle teardown. The boundary is explicit, portable, and
covered by a focused compatibility gate; the verification snapshot above keeps
the separate workspace-baseline blockers visible rather than treating them as
part of the drawing contract.
