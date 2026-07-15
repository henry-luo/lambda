// Pure routed-edge geometry to generated SVG paint layers.

fn path_tail(points, index, result) {
  if (index >= len(points)) result
  else path_tail(points, index + 1,
    result ++ " L " ++ string(points[index].x) ++ " " ++ string(points[index].y))
}

fn line_path_data(points) {
  if (len(points) == 0) ""
  else path_tail(points, 1,
    "M " ++ string(points[0].x) ++ " " ++ string(points[0].y))
}

fn curve_tail(points, index, result) {
  if (index >= len(points) - 1) {
    let control = points[index];
    let finish = points[len(points) - 1];
    // The preceding waypoint remains the control for the terminal curve segment.
    result ++ " Q " ++ string(control.x) ++ " " ++ string(control.y) ++
      " " ++ string(finish.x) ++ " " ++ string(finish.y)
  }
  else {
    let control = points[index];
    let next = points[index + 1];
    let mid_x = (control.x + next.x) / 2.0;
    let mid_y = (control.y + next.y) / 2.0;
    curve_tail(points, index + 1,
      result ++ " Q " ++ string(control.x) ++ " " ++ string(control.y) ++
        " " ++ string(mid_x) ++ " " ++ string(mid_y))
  }
}

fn curve_path_data(points) {
  if (len(points) < 2) line_path_data(points)
  else if (len(points) == 2) {
    let start = points[0];
    let finish = points[1];
    let dx = finish.x - start.x;
    let dy = finish.y - start.y;
    "M " ++ string(start.x) ++ " " ++ string(start.y) ++
      " C " ++ string(start.x + dx / 3.0) ++ " " ++ string(start.y + dy / 3.0) ++
      " " ++ string(start.x + dx * 2.0 / 3.0) ++ " " ++
        string(start.y + dy * 2.0 / 3.0) ++
      " " ++ string(finish.x) ++ " " ++ string(finish.y)
  }
  else curve_tail(points, 1,
    "M " ++ string(points[0].x) ++ " " ++ string(points[0].y))
}

fn path_data(edge) =>
  if (edge.route_mode == "curved") curve_path_data(edge.points)
  else line_path_data(edge.points)

fn route_tail(points, index, result) {
  if (index >= len(points)) result
  else route_tail(points, index + 1,
    result ++ " " ++ string(points[index].x) ++ "," ++ string(points[index].y))
}

fn route_data(points) {
  if (len(points) == 0) ""
  else route_tail(points, 1, string(points[0].x) ++ "," ++ string(points[0].y))
}

fn route_kind(edge) {
  if (edge.route_mode == "none") "none"
  else if (edge.from == edge.to) "self-loop"
  else if (edge.is_bezier == true) "curved"
  else if (edge.route_mode == "polyline") "polyline"
  else if (len(edge.points) <= 2) "straight"
  else "orthogonal"
}

fn dash_array(style) {
  if (style == "dashed" or style == "dash") "7 5"
  else if (style == "dotted" or style == "dot") "2 4"
  else "none"
}

fn marker_id(edge, end_name) => "lambda-graph-arrow-" ++ string(edge.index) ++ "-" ++ end_name

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
  [for (token in split(trim(marker_type), " ") where token != "") token]

fn marker_component(id, token, index, count, color) {
  let spec = marker_spec(token);
  let clip_id = id ++ "-clip-" ++ string(index);
  let shape = if (spec.base == "none") <g 'data-marker-empty': true>
    else marker_shape(token, color);
  let clipped = if (spec.side == null or spec.base == "none") shape
    else <g;
      <clipPath id: clip_id;
        <rect x: 0, y: if (spec.side == "left") 0 else 4,
          width: 8, height: 4>
      >
      <g 'clip-path': "url(#" ++ clip_id ++ ")"; shape>
    >;
  <g transform: "translate(" ++ string((count - index - 1) * 8) ++ " 0)",
      'data-marker-component': token, 'data-marker-side': spec.side;
    clipped
  >
}

fn marker_content(id, marker_type, color) {
  let tokens = marker_tokens(marker_type);
  if (len(tokens) == 1 and marker_spec(tokens[0]).side == null)
    marker_shape(tokens[0], color)
  else <g;
    for (i, token in tokens) marker_component(id, token, i, len(tokens), color)
  >
}

fn marker_ref_x(tokens) {
  let nearest = marker_spec(tokens[0]).base;
  len(tokens) * 8 - (if (nearest == "normal") 1 else 4)
}

fn edge_marker(id, marker_type, color, opacity, size) {
  let tokens = marker_tokens(marker_type);
  let width = max([1, len(tokens)]) * 8;
  <marker id: id, markerWidth: width * size, markerHeight: 8 * size,
      viewBox: "0 0 " ++ string(width) ++ " 8",
      refX: marker_ref_x(tokens), refY: 4,
      orient: "auto-start-reverse", markerUnits: "strokeWidth",
      opacity: opacity, 'data-marker-type': marker_type;
    marker_content(id, marker_type, color)
  >
}

fn edge_defs(edge, color, opacity) {
  let size = max([0.1, float(if (edge.arrow_size != null) edge.arrow_size else 1.0)]);
  [<defs;
    edge_marker(marker_id(edge, "start"), edge.marker_start, color, opacity, size)
    edge_marker(marker_id(edge, "end"), edge.marker_end, color, opacity, size)
  >]
}

fn edge_path(edge, color, stroke_width, opacity) {
  let marker_start = if (edge.marker_start != "none")
    "url(#" ++ marker_id(edge, "start") ++ ")" else null;
  let marker_end = if (edge.marker_end != "none")
    "url(#" ++ marker_id(edge, "end") ++ ")" else null;
  <path d: path_data(edge), fill: "none", stroke: color,
      'stroke-width': stroke_width,
      'stroke-dasharray': if (edge.dash_array != null) edge.dash_array else dash_array(edge.style),
      opacity: opacity,
      'marker-start': marker_start, 'marker-end': marker_end,
      'data-graph-role': "edge", 'data-edge-id': edge.id,
      'data-from': edge.from, 'data-to': edge.to,
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
  let stroke_width = if (edge.stroke_width != null) edge.stroke_width
    else if (opts != null and opts.edge_width != null) opts.edge_width else 1.5;
  let opacity = if (edge.opacity != null) edge.opacity else null;
  let defs = edge_defs(edge, color, opacity);
  <svg xmlns: "http://www.w3.org/2000/svg", width: width, height: height,
      viewBox: "0 0 " ++ string(width) ++ " " ++ string(height),
      style: "overflow:visible;pointer-events:none;";
    for (item in defs) item
    edge_path(edge, color, stroke_width, opacity)
  >
}

fn cluster_svg(cluster, width, height) {
  <svg xmlns: "http://www.w3.org/2000/svg", width: width, height: height,
      viewBox: "0 0 " ++ string(width) ++ " " ++ string(height),
      style: "overflow:visible;pointer-events:none;";
    <rect x: cluster.x, y: cluster.y, width: cluster.width, height: cluster.height,
        rx: cluster.radius, ry: cluster.radius,
        fill: cluster.fill, stroke: cluster.stroke,
        'stroke-width': cluster.stroke_width,
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
    for (edge in result.edges where len(edge.points) > 1) {
      z: if (edge.z != null) int(edge.z) else -1,
      content: edge_svg(edge, result.width, result.height, opts),
      edge_id: edge.id
    }
  ]
}
