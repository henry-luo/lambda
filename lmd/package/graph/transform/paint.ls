// Pure routed-edge geometry to generated SVG paint layers.

import route_geometry: lambda.graph.route_geometry

fn route_tail(points, index, result) any^ {
  if (index >= len(points)) result
  else route_tail(points, index + 1,
    result ++ " " ++ string(points[index].x) ++ "," ++ string(points[index].y))
}

fn route_data(points) any^ {
  if (len(points) == 0) ""
  else route_tail(points, 1, string(points[0].x) ++ "," ++ string(points[0].y))
}

fn route_kind(edge) any^ {
  if (edge.route_mode == "none") "none"
  else if (edge.from == edge.to) "self-loop"
  // Basis interpolation of one segment is geometrically straight.
  else if (len(edge.points) <= 2) "straight"
  else if (edge.is_bezier == true) "curved"
  else if (edge.route_mode == "polyline") "polyline"
  else "orthogonal"
}

fn dash_array(style) {
  if (style == "dashed" or style == "dash") "7 5"
  else if (style == "dotted" or style == "dot") "2 4"
  else "none"
}

fn marker_id(edge, end_name) any^ => "lambda-graph-arrow-" ++ string(edge.index) ++ "-" ++ end_name

fn marker_spec(marker_type) {
  let parts = split(marker_type, ":");
  let named = parts[0];
  let open_alias = contains(["odot", "obox", "odiamond", "ediamond", "empty"], named);
  let base = if (named == "odot") "dot"
    else if (named == "obox") "box"
    else if (named == "odiamond" or named == "ediamond") "diamond"
    else if (named == "empty") "normal" else named;
  {base: base, open: open_alias or contains(parts, "open"),
    side: if (contains(parts, "left")) "left"
      else if (contains(parts, "right")) "right" else null}
}

fn marker_shape(marker_type, color) {
  let spec = marker_spec(marker_type);
  let fill = if (spec.open) "none" else color;
  let stroke = if (spec.open) color else null;
  let outline_width = if (spec.open) 1.2 else null;
  if (spec.base == "none") {
    <path d: "M 0 0 L 8 4 L 0 8 z", fill: color>
  }
  else if (spec.base == "circle" or spec.base == "dot") {
    <circle cx: 4, cy: 4, r: 3, fill: fill,
      stroke: stroke, 'stroke-width': outline_width>
  }
  else if (spec.base == "diamond") {
    <path d: "M 0 4 L 4 0 L 8 4 L 4 8 z", fill: fill,
      stroke: stroke, 'stroke-width': outline_width>
  }
  else if (spec.base == "box") {
    <rect x: 1, y: 1, width: 6, height: 6, fill: fill,
      stroke: stroke, 'stroke-width': outline_width>
  }
  else if (spec.base == "tee") {
    <path d: "M 6 0 L 6 8", fill: "none", stroke: color, 'stroke-width': 2>
  }
  else if (spec.base == "vee" or spec.base == "open" or spec.base == "halfopen") {
    <path d: "M 0 0 L 8 4 L 0 8", fill: "none", stroke: color,
        'stroke-width': 1.5>
  }
  else if (spec.base == "inv") {
    <path d: "M 8 0 L 0 4 L 8 8 z", fill: fill,
      stroke: stroke, 'stroke-width': outline_width>
  }
  else if (spec.base == "crow") {
    <path d: "M 8 0 L 2 4 L 8 8 M 2 0 L 2 8", fill: "none", stroke: color,
        'stroke-width': 1.2>
  }
  else if (spec.base == "cross") {
    <path d: "M 1 1 L 7 7 M 7 1 L 1 7", fill: "none", stroke: color,
        'stroke-width': 1.5>
  }
  else if (spec.base == "curve" or spec.base == "icurve") {
    <path d: if (spec.base == "icurve") "M 7 0 Q 1 4 7 8" else "M 1 0 Q 7 4 1 8",
      fill: "none", stroke: color, 'stroke-width': 1.2>
  }
  else {
    <path d: "M 0 0 L 8 4 L 0 8 z", fill: fill,
      stroke: stroke, 'stroke-width': outline_width>
  }
}

fn marker_tokens(marker_type) =>
  [for (token in ((split(trim(marker_type), " ")) ^ { [] }) where token != "") token]

fn marker_component(id, token, index, count, color) {
  let spec = marker_spec(token);
  let clip_id = id ++ "-clip-" ++ string(index);
  let shape = if (spec.base == "none") <g 'data-marker-empty': true>
    else marker_shape(token, color);
  let clipped = if (spec.side == null or spec.base == "none") shape
    else <g
      <clipPath id: clip_id,
        <rect x: 0, y: if (spec.side == "left") 0 else 4,
          width: 8, height: 4>
      >
      <g 'clip-path': "url(#" ++ clip_id ++ ")", shape>
    >;
  <g transform: "translate(" ++ string((count - index - 1) * 8) ++ " 0)",
      'data-marker-component': token, 'data-marker-side': spec.side,
    clipped
  >
}

fn marker_content(id, marker_type, color) any^ {
  let tokens = marker_tokens(marker_type);
  if (len(tokens) == 1 and marker_spec(tokens[0]).side == null)
    marker_shape(tokens[0], color)
  else <g
    for (i, token in tokens) marker_component(id, token, i, len(tokens), color)
  >
}

fn marker_ref_x(tokens) any^ {
  let nearest = marker_spec(tokens[0]).base;
  len(tokens) * 8 - (if (nearest == "normal") 1 else 4)
}

fn marker_dimensions(marker_type, size, stroke_width, flavor) {
  let width = max([1, len(marker_tokens(marker_type))]) * 8;
  let d2_arrow = flavor == "d2" and marker_type == "normal";
  {width: if (d2_arrow) (4.0 + 3.0 * stroke_width) * size else width * size,
    height: if (d2_arrow) (4.0 + 4.0 * stroke_width) * size
      else route_geometry.marker_height * size,
    scale: if (d2_arrow) 1.0 else stroke_width,
    tip_offset: if (d2_arrow) 0 else size * stroke_width}
}

fn edge_marker(id, marker_type, color, opacity, size, stroke_width, flavor) {
  let tokens = marker_tokens(marker_type);
  let width = max([1, len(tokens)]) * 8;
  // D2's default triangle grows from a fixed base. SVG strokeWidth units
  // scale the entire marker and can make thick arrows cover the label.
  let d2_arrow = flavor == "d2" and marker_type == "normal";
  let dims = marker_dimensions(marker_type, size, stroke_width, flavor);
  <marker id: id,
      markerWidth: dims.width, markerHeight: dims.height,
      viewBox: "0 0 " ++ string(width) ++ " 8",
      refX: if (d2_arrow) 8 else marker_ref_x(tokens), refY: 4,
      orient: "auto-start-reverse",
      markerUnits: if (d2_arrow) "userSpaceOnUse" else "strokeWidth",
      preserveAspectRatio: if (d2_arrow) "none" else null,
      opacity: opacity, 'data-marker-type': marker_type,
    marker_content(id, marker_type, color)
  >
}

fn clip_id(edge, end_name) any^ => marker_id(edge, end_name) ++ "-stroke-clip"

fn terminal_direction(points, endpoint, index, step) any^ {
  if (index < 0 or index >= len(points)) null
  else {
    let dx = points[index].x - endpoint.x;
    let dy = points[index].y - endpoint.y;
    let length = math.sqrt(dx * dx + dy * dy);
    if (length > 0.001) {x: dx / length, y: dy / length}
    else terminal_direction(points, endpoint, index + step, step)
  }
}

fn triangle_clip(edge, marker_type, end_name, size, stroke_width, flavor, width, height) any^ {
  let points = edge.points;
  let dims = marker_dimensions(marker_type, size, stroke_width, flavor);
  let marker_width = dims.width * dims.scale;
  let marker_height = dims.height * dims.scale;
  // The stroke must end where the triangle is at least as wide as the stroke.
  let cut = if (marker_type == "normal" and marker_height > 0)
    max([0.0, min([marker_width, stroke_width * marker_width / marker_height - dims.tip_offset])]) else 0;
  let start = end_name == "start";
  let endpoint = if (len(points) > 0) points[if (start) 0 else len(points) - 1] else null;
  let direction = if (endpoint != null) terminal_direction(points, endpoint,
    if (start) 1 else len(points) - 2, if (start) 1 else -1) else null;
  if (cut <= 0 or direction == null) null else {
    let outer = max([width + height, marker_width + marker_height, 1.0]);
    let reach = max([marker_width, stroke_width * 2.0]);
    let half = max([stroke_width * 2.0, marker_height]);
    let near = {x: endpoint.x + direction.x * cut, y: endpoint.y + direction.y * cut};
    let far = {x: endpoint.x - direction.x * reach, y: endpoint.y - direction.y * reach};
    let nx = -direction.y * half;
    let ny = direction.x * half;
    <clipPath id: clip_id(edge, end_name), 'clipPathUnits': "userSpaceOnUse",
      <path 'clip-rule': "evenodd", d:
        "M " ++ string(-outer) ++ " " ++ string(-outer) ++
        " H " ++ string(width + outer) ++ " V " ++ string(height + outer) ++
        " H " ++ string(-outer) ++ " Z M " ++
        string(near.x + nx) ++ " " ++ string(near.y + ny) ++ " L " ++
        string(far.x + nx) ++ " " ++ string(far.y + ny) ++ " L " ++
        string(far.x - nx) ++ " " ++ string(far.y - ny) ++ " L " ++
        string(near.x - nx) ++ " " ++ string(near.y - ny) ++ " Z">
    >
  }
}

fn edge_defs(edge, color, opacity, stroke_width, flavor) {
  let size = max([0.1, float(if (edge.arrow_size != null) edge.arrow_size else 1.0)]);
  [<defs
    edge_marker(marker_id(edge, "start"), edge.marker_start, color, opacity, size, stroke_width, flavor)
    edge_marker(marker_id(edge, "end"), edge.marker_end, color, opacity, size, stroke_width, flavor)
  >]
}

fn edge_path(edge, color, stroke_width, opacity, markers = true) {
  let marker_start = if (edge.marker_start != "none")
    "url(#" ++ marker_id(edge, "start") ++ ")" else null;
  let marker_end = if (edge.marker_end != "none")
    "url(#" ++ marker_id(edge, "end") ++ ")" else null;
  <path d: route_geometry.path_data(edge), fill: "none", stroke: color,
      'stroke-width': stroke_width,
      'stroke-dasharray': if (edge.dash_array != null) edge.dash_array else dash_array(edge.style),
      opacity: opacity,
      'marker-start': if (markers) marker_start else null,
      'marker-end': if (markers) marker_end else null,
      'data-graph-role': "edge", 'data-edge-id': edge.id,
      'data-from': edge.from, 'data-to': edge.to,
      'data-from-port': edge.from_port, 'data-to-port': edge.to_port,
      'data-from-compass': edge.from_compass, 'data-to-compass': edge.to_compass,
      'data-tail-cluster': edge.tail_cluster, 'data-head-cluster': edge.head_cluster,
      'data-route': route_data(edge.points), 'data-route-kind': route_kind(edge),
      'data-route-mode': edge.route_mode,
      'data-marker-start': edge.marker_start, 'data-marker-end': edge.marker_end,
      'data-stroke': color, 'data-stroke-width': stroke_width,
      'data-opacity': opacity,
      'data-dash-array': (if (edge.dash_array != null) edge.dash_array else dash_array(edge.style))>
}

fn edge_svg(edge, width, height, opts) {
  let color = if (edge.stroke != null) string(edge.stroke)
    else if (opts != null and opts.edge_color != null) string(opts.edge_color) else "#59636e";
  let base_width = if (edge.stroke_width != null) edge.stroke_width
    else if (opts != null and opts.edge_width != null) opts.edge_width else route_geometry.default_edge_width;
  let stroke_width = if (opts != null and opts.edge_width_scale != null)
    float(base_width) * float(opts.edge_width_scale) else base_width;
  let opacity = if (edge.opacity != null) edge.opacity else null;
  let flavor = if (opts == null) null else opts.flavor;
  let defs = edge_defs(edge, color, opacity, stroke_width, flavor);
  let size = max([0.1, float(if (edge.arrow_size != null) edge.arrow_size else 1.0)]);
  let start_clip = triangle_clip(edge, edge.marker_start, "start", size, stroke_width, flavor, width, height);
  let end_clip = triangle_clip(edge, edge.marker_end, "end", size, stroke_width, flavor, width, height);
  <svg xmlns: "http://www.w3.org/2000/svg", width: width, height: height,
      viewBox: "0 0 " ++ string(width) ++ " " ++ string(height),
      style: "overflow:visible;pointer-events:none;",
    for (item in defs) item
    if (start_clip == null and end_clip == null) edge_path(edge, color, stroke_width, opacity)
    else <g
      <defs start_clip; end_clip>
      <g 'clip-path': if (start_clip != null) "url(#" ++ clip_id(edge, "start") ++ ")" else null,
        <g 'clip-path': if (end_clip != null) "url(#" ++ clip_id(edge, "end") ++ ")" else null,
          edge_path(edge, color, stroke_width, opacity, false) >>
      // Keep the marker attached to the original endpoint and tangent.
      <path d: route_geometry.path_data(edge), fill: "none", stroke: "none",
        'stroke-width': stroke_width, opacity: opacity,
        'marker-start': if (edge.marker_start != "none") "url(#" ++ marker_id(edge, "start") ++ ")" else null,
        'marker-end': if (edge.marker_end != "none") "url(#" ++ marker_id(edge, "end") ++ ")" else null>
    >
  >
}

fn cluster_svg(cluster, width, height) {
  <svg xmlns: "http://www.w3.org/2000/svg", width: width, height: height,
      viewBox: "0 0 " ++ string(width) ++ " " ++ string(height),
      style: "overflow:visible;pointer-events:none;",
    <rect x: cluster.x, y: cluster.y, width: cluster.width, height: cluster.height,
        rx: cluster.radius, ry: cluster.radius,
        fill: cluster.fill, stroke: cluster.stroke,
        'stroke-width': cluster.stroke_width,
        'stroke-dasharray': if (cluster.dash_array == null) null else string(cluster.dash_array),
        'data-graph-role': "cluster", 'data-cluster-id': cluster.id,
        'data-parent-cluster-id': cluster.parent,
        'data-fill': cluster.fill, 'data-stroke': cluster.stroke,
        'data-stroke-width': cluster.stroke_width>
  >
}

pub fn layers(result, opts = null) {
  [
    for (cluster in if (result.clusters != null) result.clusters else []) {
      z: if (cluster.z != null) int(cluster.z) else -2,
      content: cluster_svg(cluster, result.width, result.height),
      cluster_id: cluster.id
    },
    for (edge in result.edges where ((len(edge.points) > 1) ^ { false })) {
      z: if (edge.z != null) int(edge.z) else -1,
      content: edge_svg(edge, result.width, result.height, opts),
      edge_id: edge.id
    }
  ]
}
