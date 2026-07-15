// Graphviz JSON maintenance references to renderer-neutral Graph Scene Mark.

import model: lambda.package.graph.model

fn first(values) => if (len(values) > 0) values[0] else null

fn values(value, key) => if (value[key] != null) value[key] else []

fn text(value, key, fallback = null) =>
  if (value[key] != null and value[key] != "") string(value[key]) else fallback

fn numbers(raw) => [for (part in split(replace(trim(string(raw)), ",", " "), " ")
  where part != "") float(part)]

fn graph_height(value) {
  let box = numbers(text(value, "bb", "0 0 0 0"));
  if (len(box) >= 4) box[3] - box[1] else 0.0
}

fn css_point(x, y, height) =>
  {x: float(x) * 96.0 / 72.0, y: (height - float(y)) * 96.0 / 72.0}

fn box_geometry(raw, height) {
  let value = numbers(raw);
  if (len(value) < 4) null
  else {
    let top_left = css_point(value[0], value[3], height);
    {x: top_left.x, y: top_left.y,
      width: (value[2] - value[0]) * 96.0 / 72.0,
      height: (value[3] - value[1]) * 96.0 / 72.0}
  }
}

fn node_geometry(value, height) {
  let position = numbers(text(value, "pos", ""));
  if (len(position) < 2 or value.width == null or value.height == null) null
  else {
    let center = css_point(position[0], position[1], height);
    let width = float(value.width) * 96.0;
    let node_height = float(value.height) * 96.0;
    {x: center.x - width / 2.0, y: center.y - node_height / 2.0,
      width: width, height: node_height}
  }
}

fn route_token(raw, height) {
  let parts = split(string(raw), ",");
  let tagged = len(parts) > 0 and (parts[0] == "e" or parts[0] == "s");
  let offset = if (tagged) 1 else 0;
  if (len(parts) < offset + 2) null
  else {
    tag: if (tagged) parts[0] else null,
    point: css_point(float(parts[offset]), float(parts[offset + 1]), height)
  }
}

fn route_points(raw, height) {
  let tokens = [for (part in split(trim(string(raw)), " "),
    let value = route_token(part, height) where value != null) value];
  let regular = [for (value in tokens where value.tag == null) value.point];
  let starts = [for (value in tokens where value.tag == "s") value.point];
  let ends = [for (value in tokens where value.tag == "e") value.point];
  [*starts, *regular, *ends]
}

fn object_by_gvid(objects, id) => first([
  for (value in objects where value["_gvid"] == id) value
])

fn label_text(raw, fallback, graph_name, edge_name = null) {
  if (raw == null or raw == "") fallback
  else replace(replace(replace(replace(string(raw), "\\N", fallback),
    "\\G", graph_name), "\\E", if (edge_name != null) edge_name else ""),
    "\\L", fallback)
}

fn label_element(value) => if (value == null or value == "") null else <label; value>

fn graphviz_style(value) {
  let style = lower(text(value, "style", ""));
  if (contains(style, "dashed")) "6 4"
  else if (contains(style, "dotted")) "2 3"
  else null
}

fn stroke_width(value) =>
  if (value.penwidth != null) float(value.penwidth) * 96.0 / 72.0 else null

fn cluster_parent(clusters, id) => first([
  for (value in clusters where contains(values(value, "subgraphs"), id)) value.name
])

fn node_group(clusters, id) {
  let matches = [for (value in clusters where contains(values(value, "nodes"), id)) value.name];
  if (len(matches) > 0) matches[len(matches) - 1] else null
}

fn direction(value) {
  let rankdir = upper(text(value, "rankdir", "TB"));
  if (contains(["TB", "BT", "LR", "RL"], rankdir)) rankdir else "TB"
}

fn edge_direction(edge, directed) => lower(text(edge, "dir",
  if (directed) "forward" else "none"))

fn marker(edge, directed, start) {
  let dir = edge_direction(edge, directed);
  let enabled = if (start) dir == "back" or dir == "both"
    else dir == "forward" or dir == "both";
  if (not enabled) "none"
  else text(edge, if (start) "arrowtail" else "arrowhead", "normal")
}

fn graphviz_cluster(value, clusters, graph_name, height, geometry) {
  let label = label_element(label_text(value.label, null, graph_name));
  let box = if (geometry) box_geometry(text(value, "bb", ""), height) else null;
  <cluster id: string(value.name), parent: cluster_parent(clusters, value["_gvid"]),
      fill: value.fillcolor, stroke: value.color,
      x: if (box != null) box.x else null, y: if (box != null) box.y else null,
      width: if (box != null) box.width else null,
      height: if (box != null) box.height else null,
      'stroke-width': stroke_width(value), 'dash-array': graphviz_style(value);
    if (label != null) { label }
  >
}

fn graphviz_node(value, clusters, graph_name, height, geometry) {
  let id = string(value.name);
  let label = label_element(label_text(value.label, id, graph_name));
  let box = if (geometry) node_geometry(value, height) else null;
  <node id: id, shape: text(value, "shape", "ellipse"),
      group: node_group(clusters, value["_gvid"]), fill: value.fillcolor,
      stroke: value.color, 'stroke-width': stroke_width(value),
      x: if (box != null) box.x else null, y: if (box != null) box.y else null,
      width: if (box != null) box.width else null,
      height: if (box != null) box.height else null,
      'dash-array': graphviz_style(value);
    if (label != null) { label }
  >
}

fn graphviz_edge(value, index, objects, directed, graph_name, height, geometry, edge_ids) {
  let tail = object_by_gvid(objects, value.tail);
  let head = object_by_gvid(objects, value.head);
  let from = if (tail != null) string(tail.name) else "";
  let to = if (head != null) string(head.name) else "";
  let edge_name = from ++ (if (directed) "->" else "--") ++ to;
  let label = label_element(label_text(value.label, null, graph_name, edge_name));
  <edge id: if (edge_ids != null and index < len(edge_ids)) edge_ids[index]
      else "edge-" ++ string(index), from: from, to: to,
      'marker-start': marker(value, directed, true),
      'marker-end': marker(value, directed, false), stroke: value.color,
      'stroke-width': stroke_width(value), 'dash-array': graphviz_style(value);
    if (label != null) { label }
    if (geometry and value.pos != null) {
      <route; for (point in route_points(value.pos, height))
        <point x: point.x, y: point.y>
      >
    }
  >
}

pub fn from_dot_json(value, geometry = false, edge_ids = null) {
  let objects = values(value, "objects");
  let count = if (value["_subgraph_cnt"] != null) int(value["_subgraph_cnt"]) else 0;
  let clusters = if (count > 0) slice(objects, 0, count) else [];
  let nodes = if (count < len(objects)) slice(objects, count, len(objects)) else [];
  let graph_name = text(value, "name", "");
  let directed = value.directed == true;
  let height = graph_height(value);
  <'graph-scene' direction: direction(value);
    for (entry in clusters) graphviz_cluster(entry, clusters, graph_name, height, geometry)
    for (entry in nodes) graphviz_node(entry, clusters, graph_name, height, geometry)
    for (i, entry in values(value, "edges"))
      graphviz_edge(entry, i, objects, directed, graph_name, height, geometry, edge_ids)
  >
}

fn canonical_label(value) => label_element(model.label_source(value))

fn canonical_cluster(entry) {
  let value = entry.value;
  let label = canonical_label(value);
  <cluster id: string(value.id), parent: entry.parent, fill: value.fill,
      stroke: value.stroke, 'stroke-width': value["stroke-width"],
      'dash-array': value["stroke-dasharray"];
    if (label != null) { label }
  >
}

fn canonical_node(entry) {
  let value = entry.value;
  let label = canonical_label(value);
  <node id: string(value.id), shape: if (value.shape != null) value.shape else "ellipse",
      group: entry.group, fill: value.fill, stroke: value.stroke,
      'stroke-width': value["stroke-width"], 'dash-array': value["stroke-dasharray"];
    if (label != null) { label }
  >
}

fn canonical_edge(value, index, directed) {
  let label = canonical_label(value);
  <edge id: "edge-" ++ string(index), from: string(value.from), to: string(value.to),
      'marker-start': if (value["arrow-tail"] != null) value["arrow-tail"] else "none",
      'marker-end': if (value["arrow-head"] != null) value["arrow-head"]
        else if (directed) "normal" else "none",
      stroke: value.stroke, 'stroke-width': value["stroke-width"],
      'dash-array': value["stroke-dasharray"];
    if (label != null) { label }
  >
}

pub fn from_canonical(graph) {
  let directed = graph.directed == true;
  <'graph-scene' direction: model.direction(graph);
    for (entry in model.visual_subgraph_entries(graph)) canonical_cluster(entry)
    for (entry in model.node_entries(graph)) canonical_node(entry)
    for (i, value in model.edges(graph)) canonical_edge(value, i, directed)
  >
}
