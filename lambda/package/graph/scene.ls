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

fn nullable_number_attr(value, key) {
  let found = attr(value, key, null);
  if (found == null or found == "") null else float(found)
}

fn nullable_bool_attr(value, key) {
  let found = attr(value, key, null);
  if (found == null or found == "") null
  else found == true or lower(string(found)) == "true"
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

fn scene_node_by_id(nodes, id) => first_or([
  for (node in nodes where string(attr(node, "id", "")) == string(id)) node
])

fn endpoint_side(point, node) {
  if (point == null or node == null) null
  else {
    let left = number_attr(node, "x");
    let top = number_attr(node, "y");
    let right = left + number_attr(node, "width");
    let bottom = top + number_attr(node, "height");
    let distances = [
      {side: "west", distance: abs(number_attr(point, "x") - left)},
      {side: "east", distance: abs(number_attr(point, "x") - right)},
      {side: "north", distance: abs(number_attr(point, "y") - top)},
      {side: "south", distance: abs(number_attr(point, "y") - bottom)}
    ];
    sort(distances, (entry) => entry.distance)[0].side
  }
}

fn scene_node(node, graph_box) {
  let box = local_bounds(node, graph_box);
  let label = scene_label(node);
  <node id: string(attr(node, "data-node-id", "")),
      shape: string(attr(node, "data-shape", "box")),
      'shape-family': attr(node, "data-shape-family", null),
      'polygon-sides': nullable_number_attr(node, "data-polygon-sides"),
      'polygon-orientation': nullable_number_attr(node, "data-polygon-orientation"),
      'polygon-skew': nullable_number_attr(node, "data-polygon-skew"),
      'polygon-distortion': nullable_number_attr(node, "data-polygon-distortion"),
      regular: nullable_bool_attr(node, "data-regular"),
      peripheries: nullable_number_attr(node, "data-peripheries"),
      group: attr(node, "data-subgraph-id", null),
      x: box.x, y: box.y, width: box.width, height: box.height,
      fill: attr(node, "data-fill", null), stroke: attr(node, "data-stroke", null),
      'stroke-width': nullable_number_attr(node, "data-stroke-width"),
      color: attr(node, "data-color", null), opacity: nullable_number_attr(node, "data-opacity"),
      'dash-array': attr(node, "data-dash-array", null);
    if (label != null) { label }
  >
}

fn scene_cluster(cluster, labels, graph_box) {
  let id = string(attr(cluster, "data-cluster-id", ""));
  let box = local_bounds(cluster, graph_box);
  let label = scene_label(label_for(labels, "cluster-label", "data-cluster-id", id));
  <cluster id: id, parent: attr(cluster, "data-parent-cluster-id", null),
      x: box.x, y: box.y, width: box.width, height: box.height,
      fill: attr(cluster, "data-fill", null), stroke: attr(cluster, "data-stroke", null),
      'stroke-width': nullable_number_attr(cluster, "data-stroke-width"),
      opacity: nullable_number_attr(cluster, "data-opacity");
    if (label != null) { label }
  >
}

fn scene_edge(edge, labels, nodes) {
  let id = string(attr(edge, "data-edge-id", ""));
  let points = route_points(edge);
  let label = scene_label(label_for(labels, "edge-label", "data-edge-id", id));
  let from_id = string(attr(edge, "data-from", ""));
  let to_id = string(attr(edge, "data-to", ""));
  let first_point = if (len(points) > 0) points[0] else null;
  let last_point = if (len(points) > 0) points[len(points) - 1] else null;
  <edge id: id, 'from': string(attr(edge, "data-from", "")),
      'to': string(attr(edge, "data-to", "")),
      'from-port': attr(edge, "data-from-port", null),
      'to-port': attr(edge, "data-to-port", null),
      'from-compass': attr(edge, "data-from-compass", null),
      'to-compass': attr(edge, "data-to-compass", null),
      'tail-cluster': attr(edge, "data-tail-cluster", null),
      'head-cluster': attr(edge, "data-head-cluster", null),
      'from-side': endpoint_side(first_point, scene_node_by_id(nodes, from_id)),
      'to-side': endpoint_side(last_point, scene_node_by_id(nodes, to_id)),
      'marker-start': string(attr(edge, "data-marker-start", "none")),
      'marker-end': string(attr(edge, "data-marker-end", "none")),
      'arrow-size': nullable_number_attr(edge, "data-arrow-size"),
      'route-kind': string(attr(edge, "data-route-kind", "straight")),
      'route-mode': attr(edge, "data-route-mode", null),
      stroke: attr(edge, "data-stroke", null),
      'stroke-width': nullable_number_attr(edge, "data-stroke-width"),
      opacity: nullable_number_attr(edge, "data-opacity"),
      'dash-array': attr(edge, "data-dash-array", null);
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
      scene_edge(entry, labels, nodes)];
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

fn hex_digit(ch) => index_of("0123456789abcdef", lower(ch))

fn hex_byte(text, index) =>
  hex_digit(slice(text, index, index + 1)) * 16 +
    hex_digit(slice(text, index + 1, index + 2))

fn canonical_color(value) {
  if (value == null) null
  else {
    let text = lower(trim(string(value)));
    if (starts_with(text, "#") and len(text) == 4) {
      let red = hex_digit(slice(text, 1, 2)) * 17;
      let green = hex_digit(slice(text, 2, 3)) * 17;
      let blue = hex_digit(slice(text, 3, 4)) * 17;
      "rgb(" ++ string(red) ++ "," ++ string(green) ++ "," ++ string(blue) ++ ")"
    }
    else if (starts_with(text, "#") and (len(text) == 7 or len(text) == 9)) {
      "rgb(" ++ string(hex_byte(text, 1)) ++ "," ++ string(hex_byte(text, 3)) ++
        "," ++ string(hex_byte(text, 5)) ++ ")"
    }
    else if (starts_with(text, "rgb(")) {
      let body = slice(text, 4, len(text) - 1);
      "rgb(" ++ join([for (part in split(body, ",")) trim(part)], ",") ++ ")"
    }
    else text
  }
}

fn canonical_dash(value) {
  if (value == null) null
  else join([for (part in split(replace(trim(string(value)), ",", " "), " ")
    where part != "") part], " ")
}

fn comparable_attr(value, key) {
  let found = attr(value, key, null);
  if (key == "fill" or key == "stroke" or key == "color") canonical_color(found)
  else if (key == "dash-array") canonical_dash(found)
  else if (found == null) null
  else string(found)
}

fn exact_attr_mismatches(actual, expected, keys) => [
  for (key in keys, let expected_value = attr(expected, key, null)
    where expected_value != null and
      comparable_attr(actual, key) != comparable_attr(expected, key))
    // mark fixtures use symbols while SVG parsing yields strings; graph
    // identity compares their textual value rather than storage type.
    mismatch(key, comparable_attr(expected, key), comparable_attr(actual, key),
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
      ["id", "from", "to", "from-side", "to-side", "marker-start", "marker-end",
        "from-port", "to-port", "from-compass", "to-compass",
        "tail-cluster", "head-cluster", "route-kind", "stroke", "dash-array",
        "arrow-size"],
      ["stroke-width", "opacity"], geometry_tolerance),
    *point_mismatches(actual, expected, attr(expected, "id", null), route_tolerance)
  ]
}

fn policy_enabled(policy, key, fallback) {
  let value = if (policy != null) policy[key] else null;
  if (value == null) fallback
  else not (value == false or value == "false" or value == "none" or value == "off")
}

fn box_contains(outer, inner, tolerance) =>
  number_attr(inner, "x") >= number_attr(outer, "x") - tolerance and
    number_attr(inner, "y") >= number_attr(outer, "y") - tolerance and
    number_attr(inner, "x") + number_attr(inner, "width") <=
      number_attr(outer, "x") + number_attr(outer, "width") + tolerance and
    number_attr(inner, "y") + number_attr(inner, "height") <=
      number_attr(outer, "y") + number_attr(outer, "height") + tolerance

fn boxes_intersect(a, b, tolerance) =>
  number_attr(a, "x") + number_attr(a, "width") > number_attr(b, "x") + tolerance and
    number_attr(b, "x") + number_attr(b, "width") > number_attr(a, "x") + tolerance and
    number_attr(a, "y") + number_attr(a, "height") > number_attr(b, "y") + tolerance and
    number_attr(b, "y") + number_attr(b, "height") > number_attr(a, "y") + tolerance

fn node_overlap_mismatches(scene, tolerance) {
  let nodes = children_by_tag(scene, "node");
  [for (i, node in nodes, j in (i + 1) to (len(nodes) - 1)
    where i + 1 < len(nodes) and boxes_intersect(node, nodes[j], tolerance))
    mismatch("relation.node-overlap", attr(node, "id", null),
      attr(nodes[j], "id", null), attr(node, "id", null))]
}

fn containment_mismatches(scene, tolerance) {
  let nodes = children_by_tag(scene, "node");
  let clusters = children_by_tag(scene, "cluster");
  [
    for (node in nodes, let group = attr(node, "group", null),
      let cluster = child_by_id(scene, "cluster", group)
      where group != null and (cluster == null or not box_contains(cluster, node, tolerance)))
      mismatch("relation.cluster-contains-node", group, attr(node, "id", null),
        attr(node, "id", null)),
    for (cluster in clusters, let parent_id = attr(cluster, "parent", null),
      let parent = child_by_id(scene, "cluster", parent_id)
      where parent_id != null and (parent == null or not box_contains(parent, cluster, tolerance)))
      mismatch("relation.cluster-contains-cluster", parent_id, attr(cluster, "id", null),
        attr(cluster, "id", null))
  ]
}

fn scene_route_points(edge) {
  let routes = children_by_tag(edge, "route");
  if (len(routes) > 0) children_by_tag(routes[0], "point") else []
}

fn point_inside_node(point, node, tolerance) =>
  number_attr(point, "x") >= number_attr(node, "x") - tolerance and
    number_attr(point, "x") <= number_attr(node, "x") + number_attr(node, "width") + tolerance and
    number_attr(point, "y") >= number_attr(node, "y") - tolerance and
    number_attr(point, "y") <= number_attr(node, "y") + number_attr(node, "height") + tolerance

fn boundary_error(point, node) {
  let half_w = number_attr(node, "width") / 2.0;
  let half_h = number_attr(node, "height") / 2.0;
  let dx = abs(number_attr(point, "x") - (number_attr(node, "x") + half_w));
  let dy = abs(number_attr(point, "y") - (number_attr(node, "y") + half_h));
  let shape = string(attr(node, "shape", "box"));
  if (half_w <= 0.0 or half_h <= 0.0) 0.0
  else if (shape == "diamond") abs(dx / half_w + dy / half_h - 1.0) * min([half_w, half_h])
  else if (contains(["circle", "doublecircle", "ellipse", "stadium", "f-circ"], shape))
    abs(math.sqrt((dx * dx) / (half_w * half_w) + (dy * dy) / (half_h * half_h)) - 1.0) *
      min([half_w, half_h])
  else if (contains(["box", "rounded", "subroutine", "cylinder"], shape))
    min([abs(dx - half_w), abs(dy - half_h)])
  // Polygon metadata is sufficient to require an in-box endpoint; exact
  // polygon equations remain renderer-independent adapter work.
  else 0.0
}

fn edge_endpoints(edge) {
  let points = scene_route_points(edge);
  [
    {role: "from", id: attr(edge, "from", null),
      point: if (len(points) > 0) points[0] else null},
    {role: "to", id: attr(edge, "to", null),
      point: if (len(points) > 0) points[len(points) - 1] else null}
  ]
}

fn attachment_mismatches(scene, tolerance) {
  let nodes = children_by_tag(scene, "node");
  [for (edge in children_by_tag(scene, "edge"), endpoint in edge_endpoints(edge),
    let node = scene_node_by_id(nodes, endpoint.id)
    where endpoint.point == null or node == null or
      not point_inside_node(endpoint.point, node, tolerance) or
      boundary_error(endpoint.point, node) > tolerance)
    mismatch("relation.endpoint-attachment." ++ endpoint.role,
      endpoint.id, if (endpoint.point == null) null else
        [number_attr(endpoint.point, "x"), number_attr(endpoint.point, "y")],
      attr(edge, "id", null))]
}

fn rank_order_mismatches(scene, tolerance) {
  let nodes = children_by_tag(scene, "node");
  let direction = string(attr(scene, "direction", "TB"));
  [for (edge in children_by_tag(scene, "edge"),
    let from_node = scene_node_by_id(nodes, attr(edge, "from", null)),
    let to_node = scene_node_by_id(nodes, attr(edge, "to", null)),
    let from_axis = if (direction == "LR" or direction == "RL")
      number_attr(from_node, "x") else number_attr(from_node, "y"),
    let to_axis = if (direction == "LR" or direction == "RL")
      number_attr(to_node, "x") else number_attr(to_node, "y"),
    let forward = if (direction == "RL" or direction == "BT")
      from_axis + tolerance >= to_axis else from_axis <= to_axis + tolerance
    where from_node != null and to_node != null and from_node != to_node and not forward)
    mismatch("relation.rank-order", direction, [from_axis, to_axis], attr(edge, "id", null))]
}

pub fn compare_scenes(actual, expected, policy = null) {
  let geometry_tolerance = if (policy != null and policy["geometry-tolerance"] != null)
    float(policy["geometry-tolerance"]) else 1.5;
  let route_tolerance = if (policy != null and policy["route-tolerance"] != null)
    float(policy["route-tolerance"]) else 2.0;
  let expected_nodes = children_by_tag(expected, "node");
  let expected_clusters = children_by_tag(expected, "cluster");
  let expected_edges = children_by_tag(expected, "edge");
  let relations = policy_enabled(policy, "relations", true);
  let diagnostics = [
    *exact_attr_mismatches(actual, expected, ["direction"]),
    for (issue in if (len(children_by_tag(actual, "node")) != len(expected_nodes))
      [mismatch("node-count", len(expected_nodes), len(children_by_tag(actual, "node")))] else []) issue,
    for (issue in if (len(children_by_tag(actual, "cluster")) != len(expected_clusters))
      [mismatch("cluster-count", len(expected_clusters), len(children_by_tag(actual, "cluster")))] else []) issue,
    for (issue in if (len(children_by_tag(actual, "edge")) != len(expected_edges))
      [mismatch("edge-count", len(expected_edges), len(children_by_tag(actual, "edge")))] else []) issue,
    for (node in expected_nodes, issue in entity_mismatches(actual, node, "node",
      ["id", "shape", "shape-family", "polygon-sides", "polygon-orientation",
        "polygon-skew", "polygon-distortion", "regular", "peripheries",
        "group", "fill", "stroke", "color", "dash-array"],
      ["x", "y", "width", "height", "stroke-width", "opacity"], geometry_tolerance)) issue,
    for (cluster in expected_clusters, issue in entity_mismatches(actual, cluster, "cluster",
      ["id", "parent", "fill", "stroke"],
      ["x", "y", "width", "height", "stroke-width", "opacity"], geometry_tolerance)) issue,
    for (edge in expected_edges, issue in edge_mismatches(
      actual, edge, geometry_tolerance, route_tolerance)) issue,
    for (issue in if (relations and policy_enabled(policy, "node-non-overlap", true))
      node_overlap_mismatches(actual, geometry_tolerance) else []) issue,
    for (issue in if (relations and policy_enabled(policy, "cluster-containment", true))
      containment_mismatches(actual, geometry_tolerance) else []) issue,
    for (issue in if (relations and policy_enabled(policy, "endpoint-attachment", true))
      attachment_mismatches(actual, route_tolerance) else []) issue,
    for (issue in if (relations and policy_enabled(policy, "rank-order", false))
      rank_order_mismatches(actual, geometry_tolerance) else []) issue
  ];
  {'equal': len(diagnostics) == 0, diagnostics: diagnostics}
}
