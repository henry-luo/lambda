// Pure routed-edge geometry to generated SVG paint layers.

fn path_tail(points, index, result) {
  if (index >= len(points)) result
  else path_tail(points, index + 1,
    result ++ " L " ++ string(points[index].x) ++ " " ++ string(points[index].y))
}

fn path_data(points) {
  if (len(points) == 0) ""
  else path_tail(points, 1,
    "M " ++ string(points[0].x) ++ " " ++ string(points[0].y))
}

fn dash_array(style) {
  if (style == "dashed" or style == "dash") "7 5"
  else if (style == "dotted" or style == "dot") "2 4"
  else "none"
}

fn marker_id(edge, end_name) => "lambda-graph-arrow-" ++ string(edge.index) ++ "-" ++ end_name

fn marker_shape(marker_type, color) {
  if (marker_type == "circle") {
    <circle cx: 4, cy: 4, r: 3, fill: color>
  }
  else if (marker_type == "cross") {
    <path d: "M 1 1 L 7 7 M 7 1 L 1 7", fill: "none", stroke: color,
        'stroke-width': 1.5>
  }
  else {
    <path d: "M 0 0 L 8 4 L 0 8 z", fill: color>
  }
}

fn edge_marker(id, marker_type, color, opacity) {
  let ref_x = if (marker_type == "normal") 7 else 4;
  <marker id: id, markerWidth: 8, markerHeight: 8,
      refX: ref_x, refY: 4, orient: "auto-start-reverse", markerUnits: "strokeWidth",
      opacity: opacity, 'data-marker-type': marker_type;
    marker_shape(marker_type, color)
  >
}

fn edge_defs(edge, color, opacity) {
  [<defs;
    edge_marker(marker_id(edge, "start"), edge.marker_start, color, opacity)
    edge_marker(marker_id(edge, "end"), edge.marker_end, color, opacity)
  >]
}

fn edge_path(edge, color, stroke_width, opacity) {
  let marker_start = if (edge.marker_start != "none")
    "url(#" ++ marker_id(edge, "start") ++ ")" else null;
  let marker_end = if (edge.marker_end != "none")
    "url(#" ++ marker_id(edge, "end") ++ ")" else null;
  <path d: path_data(edge.points), fill: "none", stroke: color,
      'stroke-width': stroke_width,
      'stroke-dasharray': if (edge.dash_array != null) edge.dash_array else dash_array(edge.style),
      opacity: opacity,
      'marker-start': marker_start, 'marker-end': marker_end,
      'data-graph-role': "edge", 'data-edge-id': edge.id,
      'data-from': edge.from, 'data-to': edge.to,
      'data-marker-start': edge.marker_start, 'data-marker-end': edge.marker_end>
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
        'data-parent-cluster-id': cluster.parent>
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
