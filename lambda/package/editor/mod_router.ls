// mod_router.ls — connector routing for the drawing layer
// (Radiant Rich Editor, Stage 4 — §9, §13.1).
//
// Lambda port of test/editor-js/src/drawing/router.ts. `compute_route(connector,
// doc)` returns the polyline ([Vec2]) the connector should draw. Strategies:
//
//   'straight'   — [from, to]
//   'orthogonal' — right-angled L-route (single bend); falls back to a Z-route
//                  (two bends) when the L-corner lands inside an endpoint bbox.
//   'curved'     — same polyline as orthogonal; the renderer emits arc fillets
//                  (route_to_curved_svg_path).
//
// User waypoints (the connector's `waypoints` attr) are pinned through-points;
// each consecutive pair is routed independently.
//
// Focused port of the parts of maxGraph's mxEdgeStyle needed for the Stage-4 MVP.

import .mod_doc
import .mod_step
import .mod_geom

// ---------------------------------------------------------------------------
// Endpoint resolution
//
// An EndpointAnchor is {point: Vec2, bbox: Rect|null, side: symbol|null}.
// ---------------------------------------------------------------------------

fn free_anchor(connector, x_key, y_key) {
  let x = get_shape_attr_number(connector, x_key, 0.0)
  let y = get_shape_attr_number(connector, y_key, 0.0)
  {point: v2(x, y), bbox: null, side: null}
}

fn find_port_at(ports, port_id, bbox, i, n) {
  if (i >= n) { null }
  else {
    let p = ports[i]
    if (p.id == port_id) {
      // port coords are normalized 0..1 in shape-local space
      {x: bbox.x + p.x * bbox.width, y: bbox.y + p.y * bbox.height}
    } else { find_port_at(ports, port_id, bbox, i + 1, n) }
  }
}

fn find_shape_port(shape, port_id) {
  let ports = attrs_get(shape.attrs, 'ports')
  let bbox = get_shape_bbox(shape)
  if (type(ports) != array or bbox == null) { null }
  else { find_port_at(ports, port_id, bbox, 0, len(ports)) }
}

fn resolve_anchor(doc, connector, shape_key, port_key, x_key, y_key) {
  let shape_id = get_shape_attr_string(connector, shape_key, "")
  if (shape_id == "") { free_anchor(connector, x_key, y_key) }
  else {
    let found = find_shape_by_id(doc, shape_id)
    if (found == null) { free_anchor(connector, x_key, y_key) }
    else {
      let shape = found.shape
      let port_id = get_shape_attr_string(connector, port_key, "")
      let port = if (port_id == "") { null } else { find_shape_port(shape, port_id) }
      if (port != null) { {point: port, bbox: get_shape_bbox(shape), side: null} }
      else {
        let bbox = get_shape_bbox(shape)
        if (bbox == null) { free_anchor(connector, x_key, y_key) }
        else {
          // closest-edge anchor (fallback): use bbox center for now
          {point: {x: bbox.x + bbox.width / 2.0, y: bbox.y + bbox.height / 2.0},
           bbox: bbox, side: null}
        }
      }
    }
  }
}

pub fn resolve_endpoint_from(doc, connector) =>
  resolve_anchor(doc, connector, 'from-shape', 'from-port', 'from-x', 'from-y')

pub fn resolve_endpoint_to(doc, connector) =>
  resolve_anchor(doc, connector, 'to-shape', 'to-port', 'to-x', 'to-y')

// ---------------------------------------------------------------------------
// Routing strategies
// ---------------------------------------------------------------------------

pub fn route_straight(a, b) => [a, b]

// Single-bend L-route; falls back to a Z-route if the L-corner crosses either
// endpoint's bbox.
pub fn route_orthogonal(a, b, a_bbox, b_bbox) {
  if (a.x == b.x or a.y == b.y) { [a, b] }
  else {
    let horiz_first = abs(b.x - a.x) >= abs(b.y - a.y)
    let l_corner = if (horiz_first) { {x: b.x, y: a.y} } else { {x: a.x, y: b.y} }
    let crosses = (a_bbox != null and rect_contains(a_bbox, l_corner)) or
                  (b_bbox != null and rect_contains(b_bbox, l_corner))
    if (not crosses) { [a, l_corner, b] }
    else if (horiz_first) {
      let mx = (a.x + b.x) / 2.0
      [a, {x: mx, y: a.y}, {x: mx, y: b.y}, b]
    } else {
      let my = (a.y + b.y) / 2.0
      [a, {x: a.x, y: my}, {x: b.x, y: my}, b]
    }
  }
}

// Curved route shares the orthogonal polyline; arc fillets are applied at render.
pub fn route_curved(a, b, a_bbox, b_bbox) => route_orthogonal(a, b, a_bbox, b_bbox)

// ---------------------------------------------------------------------------
// Top-level compute_route
// ---------------------------------------------------------------------------

fn read_waypoints(connector) {
  let ws = attrs_get(connector.attrs, 'waypoints')
  if (type(ws) != array) { [] }
  else { [for (w in ws) {x: w.x, y: w.y}] }
}

fn route_one(routing, seg_a, seg_b, a_box, b_box) {
  if (routing == 'straight') { route_straight(seg_a, seg_b) }
  else if (routing == 'curved') { route_curved(seg_a, seg_b, a_box, b_box) }
  else { route_orthogonal(seg_a, seg_b, a_box, b_box) }
}

fn route_segments_at(points, routing, from_bbox, to_bbox, i, n, acc) {
  if (i + 1 >= n) { acc }
  else {
    let a_box = if (i == 0) { from_bbox } else { null }
    let b_box = if (i == n - 2) { to_bbox } else { null }
    let seg = route_one(routing, points[i], points[i + 1], a_box, b_box)
    // skip the duplicate join point for all but the first segment
    let added = if (i == 0) { seg } else { list_drop(seg, 1) }
    route_segments_at(points, routing, from_bbox, to_bbox, i + 1, n, list_concat(acc, added))
  }
}

pub fn compute_route(connector, doc) {
  let from = resolve_endpoint_from(doc, connector)
  let to   = resolve_endpoint_to(doc, connector)
  let routing = attrs_get(connector.attrs, 'routing')
  let r = if (routing == null) { 'orthogonal' } else { routing }
  let waypoints = read_waypoints(connector)
  let points = [from.point, *waypoints, to.point]
  route_segments_at(points, r, from.bbox, to.bbox, 0, len(points), [])
}

// ---------------------------------------------------------------------------
// Render helpers
// ---------------------------------------------------------------------------

pub fn route_to_svg_path(points) => polyline_to_svg_d(points)

fn curved_at(points, radius, i, last_i, acc) {
  if (i >= last_i) { acc }
  else {
    let prev = points[i - 1]
    let cur  = points[i]
    let next = points[i + 1]
    let in_dx  = sign(cur.x - prev.x)
    let in_dy  = sign(cur.y - prev.y)
    let out_dx = sign(next.x - cur.x)
    let out_dy = sign(next.y - cur.y)
    let in_len  = min(radius, hypot(cur.x - prev.x, cur.y - prev.y) / 2.0)
    let out_len = min(radius, hypot(next.x - cur.x, next.y - cur.y) / 2.0)
    let p1 = {x: cur.x - in_dx * in_len,  y: cur.y - in_dy * in_len}
    let p2 = {x: cur.x + out_dx * out_len, y: cur.y + out_dy * out_len}
    let seg = " L " ++ string(p1.x) ++ " " ++ string(p1.y) ++
              " Q " ++ string(cur.x) ++ " " ++ string(cur.y) ++
              " " ++ string(p2.x) ++ " " ++ string(p2.y)
    curved_at(points, radius, i + 1, last_i, acc ++ seg)
  }
}

// Curved SVG `d` with quarter-arc corners at each bend.
pub fn route_to_curved_svg_path(points, radius) {
  if (len(points) < 2) { "" }
  else if (len(points) == 2) { polyline_to_svg_d(points) }
  else {
    let head = points[0]
    let body = curved_at(points, radius, 1, len(points) - 1,
                         "M " ++ string(head.x) ++ " " ++ string(head.y))
    let last = points[len(points) - 1]
    body ++ " L " ++ string(last.x) ++ " " ++ string(last.y)
  }
}

// Tolerance-aware point-on-route check (reuses the polyline distance test).
pub fn is_point_on_route(p, route, tolerance) => point_near_polyline(p, route, tolerance)
