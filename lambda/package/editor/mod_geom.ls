// mod_geom.ls — 2D geometry, shape attribute helpers, and geometric hit-test
// for the drawing layer (Radiant Rich Editor, Stage 4 — §7, §13.1).
//
// Lambda port of the JS reference modules:
//   test/editor-js/src/drawing/geom.ts        (pure geometry primitives)
//   test/editor-js/src/drawing/shape-utils.ts (shape attribute readers / bbox)
//   test/editor-js/src/drawing/hit-test.ts    (per-kind geometric hit-test)
//
// Geometry types are plain maps:
//   Vec2 = {x: float, y: float}
//   Rect = {x: float, y: float, width: float, height: float}
//
// All operations are pure functional and never mutate.

import .mod_doc
import .mod_step

// ---------------------------------------------------------------------------
// Constructors / accessors  (geom.ts)
// ---------------------------------------------------------------------------

pub fn v2(x, y) => {x: x, y: y}

pub fn rect(x, y, width, height) => {x: x, y: y, width: width, height: height}

pub fn rect_from_points(a, b) {
  let x = min(a.x, b.x)
  let y = min(a.y, b.y)
  {x: x, y: y, width: abs(b.x - a.x), height: abs(b.y - a.y)}
}

pub fn rect_center(r) => {x: r.x + r.width / 2.0, y: r.y + r.height / 2.0}

pub fn rect_right(r) => r.x + r.width
pub fn rect_bottom(r) => r.y + r.height

// ---------------------------------------------------------------------------
// Vec2 arithmetic  (geom.ts)
// ---------------------------------------------------------------------------

pub fn v2_add(a, b) => {x: a.x + b.x, y: a.y + b.y}
pub fn v2_sub(a, b) => {x: a.x - b.x, y: a.y - b.y}
pub fn v2_scale(a, k) => {x: a.x * k, y: a.y * k}
pub fn v2_dot(a, b) => a.x * b.x + a.y * b.y

pub fn hypot(dx, dy) => math.sqrt(dx * dx + dy * dy)

pub fn v2_len(a) => hypot(a.x, a.y)
pub fn v2_dist(a, b) => hypot(a.x - b.x, a.y - b.y)

// ---------------------------------------------------------------------------
// Rect predicates  (geom.ts)
// ---------------------------------------------------------------------------

pub fn rect_contains(r, p) =>
  p.x >= r.x and p.x <= r.x + r.width and p.y >= r.y and p.y <= r.y + r.height

pub fn rect_intersects(a, b) =>
  not (a.x + a.width < b.x or b.x + b.width < a.x or
       a.y + a.height < b.y or b.y + b.height < a.y)

pub fn rect_union(a, b) {
  let x = min(a.x, b.x)
  let y = min(a.y, b.y)
  let x2 = max(a.x + a.width, b.x + b.width)
  let y2 = max(a.y + a.height, b.y + b.height)
  {x: x, y: y, width: x2 - x, height: y2 - y}
}

// ---------------------------------------------------------------------------
// Rotation around a center  (geom.ts)
// ---------------------------------------------------------------------------

pub fn rotate_point(p, center, angle_deg) {
  if (angle_deg == 0.0) { p }
  else {
    let rad = angle_deg * math.pi / 180.0
    let c = math.cos(rad)
    let s = math.sin(rad)
    let dx = p.x - center.x
    let dy = p.y - center.y
    {x: center.x + dx * c - dy * s, y: center.y + dx * s + dy * c}
  }
}

// ---------------------------------------------------------------------------
// Line / segment math  (geom.ts)
// ---------------------------------------------------------------------------

// Squared distance from point p to the line segment a->b.
pub fn point_to_segment_dist_sq(p, a, b) {
  let dx = b.x - a.x
  let dy = b.y - a.y
  let len_sq = dx * dx + dy * dy
  if (len_sq == 0.0) {
    let ex = p.x - a.x
    let ey = p.y - a.y
    ex * ex + ey * ey
  } else {
    let t0 = ((p.x - a.x) * dx + (p.y - a.y) * dy) / len_sq
    let t = max(0.0, min(1.0, t0))
    let px = a.x + t * dx
    let py = a.y + t * dy
    let ex = p.x - px
    let ey = p.y - py
    ex * ex + ey * ey
  }
}

pub fn point_to_segment_dist(p, a, b) => math.sqrt(point_to_segment_dist_sq(p, a, b))

fn near_polyline_at(p, poly, tol_sq, i, n) {
  if (i + 1 >= n) { false }
  else if (point_to_segment_dist_sq(p, poly[i], poly[i + 1]) <= tol_sq) { true }
  else { near_polyline_at(p, poly, tol_sq, i + 1, n) }
}

// True iff p is within `tolerance` of the polyline.
pub fn point_near_polyline(p, poly, tolerance) =>
  near_polyline_at(p, poly, tolerance * tolerance, 0, len(poly))

// ---------------------------------------------------------------------------
// Polygon (closed): even-odd point-in-polygon  (geom.ts)
// ---------------------------------------------------------------------------

fn pip_at(poly, p, i, j, n, inside) {
  if (i >= n) { inside }
  else {
    let a = poly[i]
    let b = poly[j]
    let cond1 = (a.y > p.y) != (b.y > p.y)
    let denom = if ((b.y - a.y) == 0.0) { 0.000000001 } else { b.y - a.y }
    let xcross = ((b.x - a.x) * (p.y - a.y)) / denom + a.x
    let intersect = cond1 and (p.x < xcross)
    let inside2 = if (intersect) { not inside } else { inside }
    pip_at(poly, p, i + 1, i, n, inside2)
  }
}

pub fn point_in_polygon(p, poly) =>
  if (len(poly) < 3) { false } else { pip_at(poly, p, 0, len(poly) - 1, len(poly), false) }

// ---------------------------------------------------------------------------
// Ellipse (axis-aligned) point-in test  (geom.ts)
// ---------------------------------------------------------------------------

pub fn point_in_ellipse(p, bbox) {
  let c = rect_center(bbox)
  let rx = bbox.width / 2.0
  let ry = bbox.height / 2.0
  if (rx == 0.0 or ry == 0.0) { false }
  else {
    let dx = (p.x - c.x) / rx
    let dy = (p.y - c.y) / ry
    ((dx * dx + dy * dy) <= 1.0)
  }
}

// ---------------------------------------------------------------------------
// SVG path d-string from polyline  (geom.ts)
// ---------------------------------------------------------------------------

fn pld_at(points, i, n, acc) {
  if (i >= n) { acc }
  else { pld_at(points, i + 1, n, acc ++ " L " ++ (points[i].x) ++ " " ++ (points[i].y)) }
}

pub fn polyline_to_svg_d(points) {
  if (len(points) == 0) { "" }
  else {
    let head = points[0]
    pld_at(points, 1, len(points), "M " ++ (head.x) ++ " " ++ (head.y))
  }
}

// ===========================================================================
// Shape attribute helpers  (shape-utils.ts)
// ===========================================================================

fn is_number(v) => type(v) == int or type(v) == float

// Shape kind symbol ('rect | 'ellipse | ...), or null for non-shape nodes.
pub fn get_shape_kind(shape) {
  if (not is_node(shape) or shape.tag != 'shape') { null }
  else { attrs_get(shape.attrs, 'kind') }
}

pub fn get_shape_attr_number(shape, name, def) {
  let v = attrs_get(shape.attrs, name)
  if (is_number(v)) { v } else { def }
}

pub fn get_shape_attr_string(shape, name, def) {
  let v = attrs_get(shape.attrs, name)
  if (type(v) == string) { v } else { def }
}

pub fn get_shape_id(shape) {
  let v = attrs_get(shape.attrs, 'id')
  if (type(v) == string and len(v) > 0) { v } else { null }
}

// Axis-aligned bounding box of a shape (rotation NOT applied).
pub fn get_shape_bbox(shape) {
  let kind = get_shape_kind(shape)
  let tag = if (is_node(shape)) { shape.tag } else { null }
  if (kind == null and tag != 'text-frame' and tag != 'image') { null }
  else {
    let x = get_shape_attr_number(shape, 'x', 0.0)
    let y = get_shape_attr_number(shape, 'y', 0.0)
    let w = get_shape_attr_number(shape, 'width', 0.0)
    let h = get_shape_attr_number(shape, 'height', 0.0)
    rect(x, y, w, h)
  }
}

// Parse polyline "x1,y1 x2,y2 ..." into [Vec2].
fn parse_points_at(parts, i, n, acc) {
  if (i >= n) { acc }
  else {
    let pair = split(trim(parts[i]), ",")
    if (len(pair) < 2) { parse_points_at(parts, i + 1, n, acc) }
    else {
      let x = float(pair[0])
      let y = float(pair[1])
      parse_points_at(parts, i + 1, n, [*acc, {x: x, y: y}])
    }
  }
}

pub fn parse_points(s) {
  let t = trim(s)
  if (len(t) == 0) { [] }
  else {
    let parts = split(t, " ")
    parse_points_at(parts, 0, len(parts), [])
  }
}

// Line endpoints: bbox top-left -> bottom-right.
pub fn get_line_endpoints(shape) {
  let x = get_shape_attr_number(shape, 'x', 0.0)
  let y = get_shape_attr_number(shape, 'y', 0.0)
  let w = get_shape_attr_number(shape, 'width', 0.0)
  let h = get_shape_attr_number(shape, 'height', 0.0)
  {a: v2(x, y), b: v2(x + w, y + h)}
}

fn is_drawing_object(n) =>
  is_node(n) and (n.tag == 'shape' or n.tag == 'group' or
                  n.tag == 'connector' or n.tag == 'text-frame')

// Depth-first search for a drawing-object by id. Returns {path, shape} or null.
fn find_in_children(content, target, path, i, n) {
  if (i >= n) { null }
  else {
    let c = content[i]
    let r = find_shape_node(c, target, [*path, i])
    if (r != null) { r } else { find_in_children(content, target, path, i + 1, n) }
  }
}

fn find_shape_node(n, target, path) {
  if (not is_node(n)) { null }
  else if (is_drawing_object(n) and get_shape_attr_string(n, 'id', "") == target) {
    {path: path, shape: n}
  }
  else { find_in_children(n.content, target, path, 0, len(n.content)) }
}

pub fn find_shape_by_id(doc, id) => find_shape_node(doc, id, [])

// ===========================================================================
// Geometric hit-test  (hit-test.ts)
// ===========================================================================

pub HIT_TOLERANCE = 4.0   // px

fn segment_hit_at(points, pp, i, n) {
  if (i + 1 >= n) { false }
  else if (point_to_segment_dist(pp, points[i], points[i + 1]) <= HIT_TOLERANCE) { true }
  else { segment_hit_at(points, pp, i + 1, n) }
}

// Hit-test a single shape against a point in drawing-local coords.
pub fn hit_test_shape(shape, p) {
  let kind = get_shape_kind(shape)
  let bbox = get_shape_bbox(shape)
  if (kind == null or bbox == null) { false }
  else {
    let rotate_angle = get_shape_attr_number(shape, 'rotate', 0.0)
    let pp = if (rotate_angle == 0.0) { p } else { rotate_point(p, rect_center(bbox), -rotate_angle) }
    if (kind == 'rect') { rect_contains(bbox, pp) }
    else if (kind == 'ellipse') { point_in_ellipse(pp, bbox) }
    else if (kind == 'line') {
      let ep = get_line_endpoints(shape)
      (point_to_segment_dist(pp, ep.a, ep.b) <= HIT_TOLERANCE)
    }
    else if (kind == 'polyline' or kind == 'path' or kind == 'freehand') {
      let points = parse_points(get_shape_attr_string(shape, 'points', ""))
      if (len(points) == 0) { false } else { segment_hit_at(points, pp, 0, len(points)) }
    }
    else if (kind == 'polygon') {
      let points = parse_points(get_shape_attr_string(shape, 'points', ""))
      point_in_polygon(pp, points)
    }
    else if (kind == 'image') { rect_contains(bbox, pp) }
    else { false }
  }
}

fn empty_hit() => {kind: 'empty', path: [], shape_id: null}

// Reverse-z walk over a container's children; first hit wins (topmost).
fn hit_container_at(content, base_path, p, i) {
  if (i < 0) { null }
  else {
    let c = content[i]
    let child_path = [*base_path, i]
    if (not is_node(c)) { hit_container_at(content, base_path, p, i - 1) }
    else if (c.tag == 'shape') {
      if (hit_test_shape(c, p)) {
        {kind: 'shape', path: child_path, shape_id: get_shape_id(c)}
      } else { hit_container_at(content, base_path, p, i - 1) }
    }
    else if (c.tag == 'group') {
      let r = hit_container_at(c.content, child_path, p, len(c.content) - 1)
      if (r != null) { r } else { hit_container_at(content, base_path, p, i - 1) }
    }
    else if (c.tag == 'text-frame') {
      let bbox = get_shape_bbox(c)
      if (bbox != null and rect_contains(bbox, p)) {
        {kind: 'shape', path: child_path, shape_id: get_shape_id(c)}
      } else { hit_container_at(content, base_path, p, i - 1) }
    }
    else { hit_container_at(content, base_path, p, i - 1) }
  }
}

// Iterate layers from last (top of z-stack) to first.
fn hit_layers_at(drawing, p, li) {
  if (li < 0) { empty_hit() }
  else {
    let layer = drawing.content[li]
    if (not is_node(layer) or layer.tag != 'layer') { hit_layers_at(drawing, p, li - 1) }
    else if (attrs_get(layer.attrs, 'visible') == false) { hit_layers_at(drawing, p, li - 1) }
    else {
      let r = hit_container_at(layer.content, [li], p, len(layer.content) - 1)
      if (r != null) { r } else { hit_layers_at(drawing, p, li - 1) }
    }
  }
}

// Hit-test a drawing tree against a point. Returns
//   {kind: 'shape'|'empty', path: SourcePath (relative to drawing), shape_id}
pub fn hit_test_drawing(drawing, p) =>
  hit_layers_at(drawing, p, len(drawing.content) - 1)
