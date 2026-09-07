# Radiant Design: Geometry API

**Status:** implemented; proposed working design, 2026-09-07
**Scope:** logical view-tree coordinates, document and viewport mapping, text
geometry, rectangular bounds, and 2D matrix operations
**Implementation:** `radiant/view_geometry.cpp`, declarations in
`radiant/view.hpp`, coordinate-domain types in `radiant/scale.hpp`
**Formal-spec linkage:** no current `S#` or `D#` ruling covers Radiant geometry
ownership. This document extends the coordinate-domain decisions in
[RSC1–RSC12](Radiant_Scale.md), the coherent-header boundary in
`DD4` ([Radiant_Imp_Code_Dedup (done)](Radiant_Imp_Code_Dedup%20(done).md)),
and SVG transform hit-testing semantics in `ESO47`
([DOM state design](../Lambda_Design_DOM_State.md)).
**Related detailed design:** [RAD_04 — Box Model & Containing Blocks](../../doc/dev/radiant/RAD_04_Box_Model_Containing_Blocks.md),
[RAD_15 — Events & Input](../../doc/dev/radiant/RAD_15_Events_Input.md), and
[RAD_18 — Editing & Selection](../../doc/dev/radiant/RAD_18_Editing_Selection_Ranges.md)

---

## 1. Purpose

Radiant has one logical geometry model shared by layout, DOM geometry,
hit-testing, editing, interaction state, and semantic painting. Before this
API, the same parent-walk, scroll subtraction, text-rectangle lookup, and
matrix projection were repeated in DOM, editing, event, render, and SVG code.
Those copies drifted in small but observable ways: nested documents used
different origins, state-backed scrolling was bypassed, and a transformed SVG
edge could produce a different target depending on the caller.

The Geometry API provides the common mathematical and view-coordinate layer.
It does not make geometry policy common. Event target selection, paint order,
editing boundary rules, CSS layout, and SVG shape semantics remain owned by
their respective modules.

The design goals are:

1. keep all document and interaction geometry in CSS logical pixels;
2. make every origin and scroll direction explicit in the function name;
3. use one view-tree walk for DOM, editing, overlay, and state-store callers;
4. centralize matrix operations without changing numerical edge contracts; and
5. make the common surface discoverable from `view.hpp`, consistent with DD4.

## 2. Ownership and coordinate layers

| Layer | Owner | Contract |
|---|---|---|
| Coordinate domains | `radiant/scale.hpp` | `RdtLogical*` values remain fractional and CSS-logical; device and host conversions happen only at adapters. |
| View-tree mapping | `view_geometry.cpp` | Maps local, block-document, document, nested-viewport, and top-level-window origins through view parents and scroll containers. |
| Geometric primitives | `view_geometry.cpp` | Contains, intersects, expands, converts, and measures `Rect`/`Bound`; no DOM or event policy. |
| Text geometry | `view_geometry.cpp` | Resolves `TextRect` ownership, line/offset lookup, interpolation, PDF selection metrics, and text-at-point queries. |
| Matrix primitives | inline helpers in `view.hpp` | Identity, multiplication, projection, inversion, affine unprojection, and rectangle bounds. |
| Transform composition | `view_transform.cpp` | Builds a view's CSS transform from transform functions and parent perspective; consumers use the shared matrix primitives. |
| Layout | `layout_*.cpp` | Produces `View` positions, dimensions, `TextRect` chains, and scroll state. It does not duplicate viewport mapping. |
| Rendering | `render_*.cpp` | Consumes logical geometry and lowers to paint or device coordinates at the rendering boundary. It retains paint-specific overflow/filter/shadow policy. |
| Events and editing | `event.cpp`, `editing_*.cpp`, `dom_range_resolver.cpp` | Own target order, default actions, caret/selection policy, and state mutation; they call Geometry API queries. |

`view.hpp` is the shared declaration surface because `View`/`DomNode`, `Rect`,
`Bound`, `TextRect`, and `RdtMatrix` are view-model types. The implementation
remains in `view_geometry.cpp`; callers must not add a second private copy of
one of these operations.

## 3. Coordinate contract

### 3.1 Units and origins

All functions in this document use `float` logical coordinates unless their
name or type explicitly says otherwise. `RdtLogicalPoint` and
`RdtLogicalRect` are the canonical point and rectangle types. `Rect` and
`Bound` are view-pool geometry records; they also contain logical values, but
use different origin conventions:

- a `Rect` is an `(x, y, width, height)` box;
- a `Bound` is `(left, top, right, bottom)` edges;
- a node-local point is relative to the node's own local box;
- a block-document point is relative to the nearest block/document flow
  origin;
- a document point is relative to the document view-tree root and excludes
  scrolling;
- a viewport point is relative to the visible viewport and subtracts the
  applicable scroll offsets; and
- a window point is a top-level logical point after nested-document placement
  and the external viewport offset have been applied.

Two values being CSS-logical is not sufficient to compare them: their origins
must also match. A caller must select the mapping function that names the
required destination origin instead of adding `x`/`y` fields by hand.

### 3.2 Scroll resolution

View mapping accepts an optional `ViewGeometryScrollResolver`:

```cpp
typedef void (*ViewGeometryScrollResolver)(
    ViewBlock* block, float* out_x, float* out_y, void* context);
```

When supplied, the resolver reads canonical state-store scroll positions. When
omitted, Geometry API code reads the view block's pane mirror. This keeps the
coordinate algorithm identical for stateful interaction and dependency-light
DOM callers. A resolver returns logical scroll offsets; it never performs
device-scale conversion.

### 3.3 Nested documents

`view_geometry_document_viewport_offset(root_document, target_document, ...)`
walks embedded documents from the root view tree to the target document and
returns the target document's logical viewport offset. It includes each
embedded element's node origin and the resolver-provided scroll state. A
missing document, missing view tree, or root-to-root query returns `(0, 0)`.

The result is a logical placement offset, not a host-window or device-pixel
coordinate. Native overlays must continue through the scale/host adapter after
this function returns.

## 4. View-coordinate API

The following functions are the stable view-tree mapping surface.

| Function | Meaning |
|---|---|
| `view_geometry_node_document_origin(view)` | Origin of `view` in document coordinates; adds every ancestor/node position and does not subtract scroll. |
| `view_geometry_local_to_block_document(view, local)` | Maps a local point through block ancestors into block-document coordinates. |
| `view_geometry_node_viewport_origin(view, resolver, context)` | Node origin in viewport coordinates; includes the node position and subtracts ancestor scroll. |
| `view_geometry_local_to_block_viewport(view, local, resolver, context)` | Maps a local point through block ancestors and subtracts their scroll offsets. |
| `view_geometry_block_viewport_to_local(view, point, resolver, context)` | Inverse of the block-viewport walk for the same resolver and view chain. |
| `view_geometry_child_content_origin(view, parent_origin, resolver, context)` | Applies a child block's position and content scroll to a parent's origin. |
| `view_geometry_child_node_origin(view, parent_origin, resolver, context)` | Applies a child node's position and scroll without restricting the walk to blocks. |
| `view_geometry_apply_external_viewport(point, viewport_root, document_offset, resolver, context)` | Joins a nested-document point to the external top-level viewport. |
| `view_geometry_local_to_window(view, local, viewport_root, document_offset, resolver, context)` | Complete local-to-top-level logical mapping, including the nested viewport seam. |
| `view_geometry_document_viewport_offset(root, target, resolver, context)` | Finds a nested document's logical offset from the root document. |

The mapping functions are deliberately typed with `RdtLogicalPoint`. This
prevents a physical `RdtDevicePoint` or host-native coordinate from entering a
layout or event calculation without an explicit conversion.

Example: an overlay anchored to a text or control view should remain logical
until it is painted or handed to a native window:

```cpp
RdtLogicalPoint anchor = view_geometry_node_viewport_origin(view);
RdtLogicalRect popup = { anchor.x, anchor.y, width, height };
// Convert popup only in the render or host adapter.
```

## 5. Rectangular and text primitives

### 5.1 Rectangles and bounds

`view_geometry_rect_contains_point(rect, point)` uses a positive-size,
half-open rectangle: left/top are inclusive and right/bottom are exclusive.
This matches layout tile and hit-test iteration and avoids double ownership at
adjacent edges.

The remaining rectangle helpers have these contracts:

| Function | Contract |
|---|---|
| `view_geometry_rect_contains_rect(outer, inner, epsilon)` | Tests containment with an explicit logical tolerance. |
| `view_geometry_point_rect_distance(rect, point)` | Returns the outside Manhattan distance; an interior point has distance zero. |
| `view_geometry_intersect_bound_rect(bound, rect)` | Clamps a `Bound` to the rectangle's edge representation. |
| `view_geometry_expand_rect(rect, expand)` | Expands all sides by a non-negative logical amount and clamps dimensions at zero. |
| `view_geometry_rect_to_bound(rect)` | Converts `(x, y, width, height)` to edge form. |
| `view_geometry_bounds_intersect(first, second)` | Tests strict overlap; touching edges do not overlap. |

These helpers are mathematical utilities. They do not decide whether a CSS
box is painted, whether `pointer-events` permits targeting, or whether an
overflow clip is active.

### 5.2 Text rectangles and offsets

`ViewGeometryTextHit` is the result of a text query:

```cpp
typedef struct ViewGeometryTextHit {
    DomText* text;
    TextRect* rect;
    float local_x;
} ViewGeometryTextHit;
```

`view_geometry_find_text_at(view, point, origin, include_trailing_edges,
out_hit)` traverses placed descendants in DOM order. It applies child content
origins, checks each `TextRect`, and returns the matching text node, text
fragment, and x-coordinate relative to that fragment. The trailing-edge flag
allows callers to choose whether the final right/bottom edge belongs to the
fragment. It skips nodes without a placed `view_type`.

This query is intentionally narrower than `elementFromPoint`: it answers
“which text fragment contains this point?” and does not perform paint-order
selection, SVG stroke testing, `pointer-events` filtering, or event dispatch.
DOM Range resolution and caret placement use it; event target traversal keeps
its event-specific ordering and side effects.

Text offset helpers are:

- `view_geometry_text_rect_for_offset` — finds the fragment containing a byte
  offset, with optional line output and explicit last-fragment clamping;
- `view_geometry_interpolate_text_x` — maps a byte offset into a fragment's
  logical x-coordinate;
- `view_geometry_text_rect_width` — returns the effective fragment width;
- `view_geometry_text_rect_document_origin` — maps a fragment origin into
  document coordinates; and
- `view_geometry_pdf_text_metrics` / `view_geometry_pdf_visible_end_offset` —
  preserve the PDF selection-overlay width and trailing-copy-space contract.

The PDF helpers are compatibility metadata for text geometry. They do not
move PDF rendering policy into the geometry module.

## 6. Matrix API

`RdtMatrix` is the 3×3 CSS/SVG matrix used for affine transforms and projected
points. The shared inline helpers in `view.hpp` are:

| Helper | Contract |
|---|---|
| `rdt_matrix_identity()` | Returns the 3×3 identity matrix. |
| `rdt_matrix_is_identity(matrix, epsilon)` | Tests all coefficients against identity using a logical tolerance. |
| `rdt_matrix_multiply(a, b)` | Returns `a * b` using the existing row-major transform convention. |
| `rdt_matrix_transform_point(matrix, x, y, out_x, out_y)` | Applies an affine matrix without homogeneous division. |
| `rdt_matrix_inverse(matrix, out_inverse)` | Computes a full 3×3 inverse; returns false for a null or near-singular matrix. |
| `rdt_matrix_project_point(matrix, x, y, out_x, out_y)` | Applies the matrix and divides by homogeneous `w`; near-zero `w` uses the existing bounded affine fallback for visual bounds. |
| `rdt_matrix_unproject_affine_point(matrix, x, y, out_x, out_y)` | Inverts the affine 2D portion with offset-first arithmetic for SVG hit-test compatibility. |
| `rdt_matrix_project_rect_bounds(matrix, left, top, right, bottom, ...)` | Projects all four corners and returns their enclosing bounds. |
| `rdt_matrix_transform_rect_bounds(matrix, left, top, right, bottom, ...)` | Transforms an affine rectangle and returns its enclosing bounds without homogeneous division. |
| `rdt_matrix_translate(tx, ty)` | Returns a 2D translation matrix. |

The distinction between `inverse` and `unproject_affine_point` is deliberate.
SVG shape hit tests use the latter because reassociating the subtraction and
matrix products changes exact boundary classification for some float32
dashed-stroke points. Generic callers that need a complete 3×3 inverse use
`rdt_matrix_inverse`.

3D CSS transform composition remains in `view_transform.cpp`:
`compute_transform_matrix_3d` composes `RdtMatrix4` functions,
`matrix4_parent_perspective` applies parent perspective around its origin, and
`compute_transform_matrix` projects the result to `RdtMatrix`. The old
approximate 2D perspective branch is retired. `transform_point` delegates to
`rdt_matrix_project_point` so paint and hit-test consumers share projection
behavior.

## 7. Consumer rules

New code follows these rules:

1. Choose the function whose name states the destination origin. Do not add
   `view->x`, `view->y`, or scroll fields directly in a consumer.
2. Pass a `ViewGeometryScrollResolver` when the caller has canonical state
   rather than relying on a stale pane mirror.
3. Keep logical points and rectangles logical through event dispatch, DOM
   geometry, editing, and semantic paint construction.
4. Convert to `RdtDevice*`, pixel indices, or host-native coordinates only at
   `scale.hpp`/surface/platform boundaries.
5. Use the matrix helper matching the operation: projected bounds for visual
   bounds, affine unprojection for SVG shape hit tests, and full inversion for
   general matrix APIs.
6. Keep policy in the owning module. Geometry answers coordinate or primitive
   questions; it does not dispatch events, mutate selection, apply CSS
   visibility, or choose paint order.
7. Before adding a helper, search `view.hpp` and `view_geometry.cpp`. A third
   variant of a coordinate walk or matrix operation is an extraction point,
   not a reason to copy the existing code.

## 8. Retired duplication and deliberate boundaries

The consolidation retired the following legacy shapes:

- the document/iframe viewport-offset walk formerly embedded in editing and
  event code;
- the recursive text hit walker duplicated by DOM Range and DOM text queries;
- handwritten transformed-rectangle corner projection in the view pool;
- repeated identity checks and affine point projection in render/SVG callers;
- the bespoke 2D/perspective transform branch in `view_transform.cpp`; and
- low-level clip-shape declarations that were exposed as cross-module API even
  though they are private implementation details.

`render_geometry.cpp` remains renderer-owned. Its visual-overflow, filter,
shadow, and paint-box adjustments carry rendering policy and are not generic
view geometry. Likewise, the event target walk is not replaced by
`view_geometry_find_text_at`: target selection depends on paint order,
pointer-events, iframe boundaries, and event-side effects.

## 9. Verification and migration status

The implemented API was verified with:

- `make build`;
- `make lint ARGS='--rule ^no-int-cast-radiant$'`;
- `make check-radiant-dup` (zero substantive duplicate families);
- UI hit-test fixtures: 7/7;
- transformed-hit fixtures: 2/2, 11 assertions; and
- DOM Range fixtures: 2/2, 37 assertions.

The full Radiant baseline remains a separate aggregate gate and is not claimed
green by these focused checks. Its current aggregate result is recorded with
the implementation handoff rather than used as evidence that the geometry API
itself is incomplete.

Future work may add typed affine/viewport wrappers or a shared shape-hit-test
result, but those changes require an explicit policy contract. In particular,
the current API must not be widened by merging event target traversal,
renderer overflow policy, and text-fragment queries into one ambiguous
“hit-test” function.
