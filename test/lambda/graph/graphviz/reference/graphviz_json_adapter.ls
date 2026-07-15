// Graphviz JSON maintenance references to renderer-neutral Graph Scene Mark.

import model: lambda.package.graph.model

fn first(values) => if (len(values) > 0) values[0] else null

fn values(value, key) => if (value[key] != null) value[key] else []

fn text(value, key, fallback = null) =>
  if (value[key] != null and value[key] != "") string(value[key]) else fallback

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

fn graphviz_cluster(value, clusters, graph_name) {
  let label = label_element(label_text(value.label, null, graph_name));
  <cluster id: string(value.name), parent: cluster_parent(clusters, value["_gvid"]),
      fill: value.fillcolor, stroke: value.color,
      'stroke-width': stroke_width(value), 'dash-array': graphviz_style(value);
    if (label != null) { label }
  >
}

fn graphviz_node(value, clusters, graph_name) {
  let id = string(value.name);
  let label = label_element(label_text(value.label, id, graph_name));
  <node id: id, shape: text(value, "shape", "ellipse"),
      group: node_group(clusters, value["_gvid"]), fill: value.fillcolor,
      stroke: value.color, 'stroke-width': stroke_width(value),
      'dash-array': graphviz_style(value);
    if (label != null) { label }
  >
}

fn graphviz_edge(value, index, objects, directed, graph_name) {
  let tail = object_by_gvid(objects, value.tail);
  let head = object_by_gvid(objects, value.head);
  let from = if (tail != null) string(tail.name) else "";
  let to = if (head != null) string(head.name) else "";
  let edge_name = from ++ (if (directed) "->" else "--") ++ to;
  let label = label_element(label_text(value.label, null, graph_name, edge_name));
  <edge id: "edge-" ++ string(index), from: from, to: to,
      'marker-start': marker(value, directed, true),
      'marker-end': marker(value, directed, false), stroke: value.color,
      'stroke-width': stroke_width(value), 'dash-array': graphviz_style(value);
    if (label != null) { label }
  >
}

pub fn from_dot_json(value) {
  let objects = values(value, "objects");
  let count = if (value["_subgraph_cnt"] != null) int(value["_subgraph_cnt"]) else 0;
  let clusters = if (count > 0) slice(objects, 0, count) else [];
  let nodes = if (count < len(objects)) slice(objects, count, len(objects)) else [];
  let graph_name = text(value, "name", "");
  let directed = value.directed == true;
  <'graph-scene' direction: direction(value);
    for (entry in clusters) graphviz_cluster(entry, clusters, graph_name)
    for (entry in nodes) graphviz_node(entry, clusters, graph_name)
    for (i, entry in values(value, "edges"))
      graphviz_edge(entry, i, objects, directed, graph_name)
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
