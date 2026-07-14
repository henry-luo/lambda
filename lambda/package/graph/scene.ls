// renderer-neutral Graph Scene Mark adaptation and comparison.

import model: .model

fn attr(value, key, fallback = null) {
  let found = if (value == null) null else value[key];
  if (found != null) found else fallback
}

fn number_attr(value, key, fallback = 0.0) {
  let found = attr(value, key, null);
  if (found == null or found == "") float(fallback) else float(found)
}

fn descendants(value) => [
  for (child in model.element_children(value), nested in [child, *descendants(child)]) nested
]

fn first_or(values, fallback = null) => if (len(values) > 0) values[0] else fallback

fn text_parts(value) => [
  for (child in model.child_items(value), part in
    if (child is element) text_parts(child)
    else if (child == null) []
    else [string(child)]) part
]

fn normalized_text(value) {
  let flattened = replace(replace(replace(join(text_parts(value), " "),
    "\n", " "), "\r", " "), "\t", " ");
  join([for (part in split(trim(flattened), " ") where part != "") part], " ")
}

fn graph_bounds(graph) => {
  x: number_attr(graph, "data-x"),
  y: number_attr(graph, "data-y"),
  width: number_attr(graph, "data-width"),
  height: number_attr(graph, "data-height")
}

fn local_bounds(entry, graph_box) {
  let has_measured_box = attr(entry, "data-x", null) != null;
  {
    x: number_attr(entry, if (has_measured_box) "data-x" else "x") -
      (if (has_measured_box) graph_box.x else 0.0),
    y: number_attr(entry, if (has_measured_box) "data-y" else "y") -
      (if (has_measured_box) graph_box.y else 0.0),
    width: number_attr(entry, if (has_measured_box) "data-width" else "width"),
    height: number_attr(entry, if (has_measured_box) "data-height" else "height")
  }
}

fn label_for(elements, role, id_key, id) => first_or([
  // the `element` type literal must not be shadowed by a loop binding here.
  for (entry in elements
    where attr(entry, "data-graph-role", "") == role and
      string(attr(entry, id_key, "")) == string(id)) entry
])

fn scene_label(label) {
  let rendered = if (label == null) "" else normalized_text(label);
  let text = if (rendered != "") rendered else string(attr(label, "data-label", ""));
  if (text == "") null else <label; text>
}

fn route_point(token) {
  let coordinates = split(token, ",");
  if (len(coordinates) == 2) {
    <point x: float(coordinates[0]), y: float(coordinates[1])>
  } else null
}

fn route_points(edge) => [
  for (token in split(trim(string(attr(edge, "data-route", ""))), " "),
       let point = route_point(token)
    where point != null) point
]

fn scene_node(node, graph_box) {
  let box = local_bounds(node, graph_box);
  let label = scene_label(node);
  <node id: string(attr(node, "data-node-id", "")),
      shape: string(attr(node, "data-shape", "box")),
      group: attr(node, "data-subgraph-id", null),
      x: box.x, y: box.y, width: box.width, height: box.height;
    if (label != null) { label }
  >
}

fn scene_cluster(cluster, labels, graph_box) {
  let id = string(attr(cluster, "data-cluster-id", ""));
  let box = local_bounds(cluster, graph_box);
  let label = scene_label(label_for(labels, "cluster-label", "data-cluster-id", id));
  <cluster id: id, parent: attr(cluster, "data-parent-cluster-id", null),
      x: box.x, y: box.y, width: box.width, height: box.height;
    if (label != null) { label }
  >
}

fn scene_edge(edge, labels) {
  let id = string(attr(edge, "data-edge-id", ""));
  let points = route_points(edge);
  let label = scene_label(label_for(labels, "edge-label", "data-edge-id", id));
  <edge id: id, 'from': string(attr(edge, "data-from", "")),
      'to': string(attr(edge, "data-to", "")),
      'marker-start': string(attr(edge, "data-marker-start", "none")),
      'marker-end': string(attr(edge, "data-marker-end", "none")),
      'route-kind': string(attr(edge, "data-route-kind", "straight"));
    if (label != null) { label }
    <route;
      for (point in points) point
    >
  >
}

pub fn from_svg(source) {
  let parsed_document = if (source is element) source
    else parse(string(source), {type: "xml"});
  // svg wrappers are renderer details; the retained role is the stable root.
  let graph = first_or([for (entry in descendants(parsed_document)
    where attr(entry, "data-graph-role", "") == "graph") entry]);
  if (graph == null) {
    <'graph-scene' status: "invalid";
      <diagnostic code: "graph-scene.missing-root", severity: "error">
    >
  } else {
    let box = graph_bounds(graph);
    let elements = descendants(graph);
    let labels = [for (entry in elements
      where attr(entry, "data-graph-role", "") == "edge-label" or
        attr(entry, "data-graph-role", "") == "cluster-label") entry];
    let nodes = [for (entry in elements
      where attr(entry, "data-graph-role", "") == "node")
      scene_node(entry, box)];
    let clusters = [for (entry in elements
      where attr(entry, "data-graph-role", "") == "cluster")
      scene_cluster(entry, labels, box)];
    let edges = [for (entry in elements
      where attr(entry, "data-graph-role", "") == "edge")
      scene_edge(entry, labels)];
    <'graph-scene' direction: string(attr(graph, "data-direction", "TB")),
        width: box.width, height: box.height;
      for (cluster in clusters) cluster
      for (node in nodes) node
      for (edge in edges) edge
    >
  }
}

fn children_by_tag(scene, wanted_tag) => [
  for (child in model.element_children(scene) where model.tag(child) == wanted_tag) child
]

fn child_by_id(scene, wanted_tag, id) => first_or([
  for (child in children_by_tag(scene, wanted_tag)
    where string(attr(child, "id", "")) == string(id)) child
])

fn mismatch(field, expected, actual, id = null) =>
  <mismatch field: field, id: id, expected: expected, actual: actual>

fn exact_attr_mismatches(actual, expected, keys) => [
  for (key in keys, let expected_value = attr(expected, key, null)
    where expected_value != null and
      string(attr(actual, key, null)) != string(expected_value))
    // mark fixtures use symbols while SVG parsing yields strings; graph
    // identity compares their textual value rather than storage type.
    mismatch(key, string(expected_value), string(attr(actual, key, null)),
      attr(expected, "id", null))
]

fn numeric_attr_mismatches(actual, expected, keys, tolerance) => [
  for (key in keys, let expected_value = attr(expected, key, null)
    where expected_value != null and
      abs(number_attr(actual, key) - float(expected_value)) > tolerance)
    mismatch(key, float(expected_value), number_attr(actual, key), attr(expected, "id", null))
]

fn label_text(value) {
  let labels = children_by_tag(value, "label");
  if (len(labels) > 0) normalized_text(labels[0]) else null
}

fn entity_mismatches(actual_scene, expected, tag, exact_keys, numeric_keys, tolerance) {
  let actual = child_by_id(actual_scene, tag, attr(expected, "id", ""));
  if (actual == null) { [mismatch(tag, attr(expected, "id", null), null)] }
  else { [
    *exact_attr_mismatches(actual, expected, exact_keys),
    *numeric_attr_mismatches(actual, expected, numeric_keys, tolerance),
    for (issue in if (label_text(expected) != null and label_text(actual) != label_text(expected))
      [mismatch("label", label_text(expected), label_text(actual), attr(expected, "id", null))]
      else []) issue
  ] }
}

fn point_mismatches(actual, expected, edge_id, tolerance) {
  let actual_points = if (actual == null) [] else children_by_tag(first_or(
    children_by_tag(actual, "route"), <route>), "point");
  let expected_points = children_by_tag(first_or(
    children_by_tag(expected, "route"), <route>), "point");
  if (len(expected_points) == 0) { [] }
  else if (len(actual_points) != len(expected_points)) { [
    mismatch("route.point-count", len(expected_points), len(actual_points), edge_id)
  ] }
  else { [for (i, point in expected_points,
    axis in ["x", "y"],
    let actual_value = number_attr(actual_points[i], axis),
    let expected_value = number_attr(point, axis)
    where abs(actual_value - expected_value) > tolerance)
    mismatch("route." ++ string(i) ++ "." ++ axis,
      expected_value, actual_value, edge_id)] }
}

fn edge_mismatches(actual_scene, expected, geometry_tolerance, route_tolerance) {
  let actual = child_by_id(actual_scene, "edge", attr(expected, "id", ""));
  [
    *entity_mismatches(actual_scene, expected, "edge",
      ["id", "from", "to", "marker-start", "marker-end", "route-kind"],
      [], geometry_tolerance),
    *point_mismatches(actual, expected, attr(expected, "id", null), route_tolerance)
  ]
}

pub fn compare_scenes(actual, expected, policy = null) {
  let geometry_tolerance = if (policy != null and policy["geometry-tolerance"] != null)
    float(policy["geometry-tolerance"]) else 1.5;
  let route_tolerance = if (policy != null and policy["route-tolerance"] != null)
    float(policy["route-tolerance"]) else 2.0;
  let expected_nodes = children_by_tag(expected, "node");
  let expected_clusters = children_by_tag(expected, "cluster");
  let expected_edges = children_by_tag(expected, "edge");
  let diagnostics = [
    *exact_attr_mismatches(actual, expected, ["direction"]),
    for (issue in if (len(children_by_tag(actual, "node")) != len(expected_nodes))
      [mismatch("node-count", len(expected_nodes), len(children_by_tag(actual, "node")))] else []) issue,
    for (issue in if (len(children_by_tag(actual, "cluster")) != len(expected_clusters))
      [mismatch("cluster-count", len(expected_clusters), len(children_by_tag(actual, "cluster")))] else []) issue,
    for (issue in if (len(children_by_tag(actual, "edge")) != len(expected_edges))
      [mismatch("edge-count", len(expected_edges), len(children_by_tag(actual, "edge")))] else []) issue,
    for (node in expected_nodes, issue in entity_mismatches(actual, node, "node",
      ["id", "shape", "group"], ["x", "y", "width", "height"], geometry_tolerance)) issue,
    for (cluster in expected_clusters, issue in entity_mismatches(actual, cluster, "cluster",
      ["id", "parent"], ["x", "y", "width", "height"], geometry_tolerance)) issue,
    for (edge in expected_edges, issue in edge_mismatches(
      actual, edge, geometry_tolerance, route_tolerance)) issue
  ];
  {'equal': len(diagnostics) == 0, diagnostics: diagnostics}
}
