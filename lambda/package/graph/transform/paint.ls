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

fn arrow_marker(id, color) {
  <marker id: id, markerWidth: 8, markerHeight: 8,
      refX: 7, refY: 4, orient: "auto-start-reverse", markerUnits: "strokeWidth";
    <path d: "M 0 0 L 8 4 L 0 8 z", fill: color>
  >
}

fn edge_defs(edge, color) {
  [<defs;
    arrow_marker(marker_id(edge, "start"), color)
    arrow_marker(marker_id(edge, "end"), color)
  >]
}

fn edge_path(edge, color, stroke_width) {
  let marker_start = if (edge.arrow_start == true)
    "url(#" ++ marker_id(edge, "start") ++ ")" else null;
  let marker_end = if (edge.arrow_end == true)
    "url(#" ++ marker_id(edge, "end") ++ ")" else null;
  <path d: path_data(edge.points), fill: "none", stroke: color,
      'stroke-width': stroke_width, 'stroke-dasharray': dash_array(edge.style),
      'marker-start': marker_start, 'marker-end': marker_end>
}

fn edge_svg(edge, width, height, opts) {
  let color = if (opts != null and opts.edge_color != null) string(opts.edge_color) else "#59636e";
  let stroke_width = if (opts != null and opts.edge_width != null) opts.edge_width else 1.5;
  let defs = edge_defs(edge, color);
  <svg xmlns: "http://www.w3.org/2000/svg", width: width, height: height,
      viewBox: "0 0 " ++ string(width) ++ " " ++ string(height),
      style: "overflow:visible;pointer-events:none;";
    for (item in defs) item
    edge_path(edge, color, stroke_width)
  >
}

pub fn layers(result, opts = null) {
  [for (edge in result.edges where len(edge.points) > 1) {
    z: if (edge.z != null) int(edge.z) else -1,
    content: edge_svg(edge, result.width, result.height, opts),
    edge_id: edge.id
  }]
}
