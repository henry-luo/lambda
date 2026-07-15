// Public graph geometry and Radiant Velmt adapter.

import dagre: .dagre
import graph_style: .style

pub fn make_options() => dagre.make_options()

pub fn compute(input, opts = null) => dagre.layout(input, opts)

// Compatibility name retained while callers migrate to compute().
pub fn layout(input, opts = null) => compute(input, opts)

fn attr_or(child, key, fallback) {
  let attrs = child.attrs;
  let nested = if (attrs == null or attrs is error) null else attrs[key];
  let direct = child[key];
  // missing Velmt attributes may be errors; optional metadata must use its fallback.
  if (nested != null and not (nested is error)) nested
  else if (direct != null and not (direct is error)) direct
  else fallback
}

fn child_tag(child) {
  if (child.tag != null) lower(string(child.tag)) else ""
}

fn child_id(child, fallback_index) {
  let value = attr_or(child, "data-node-id", attr_or(child, "id", null));
  if (value != null and value != "") string(value)
  else "n" ++ string(fallback_index)
}

fn child_width(child) {
  if (child.width != null) float(child.width)
  else if (child.wd != null) float(child.wd)
  else 80.0
}

fn child_height(child) {
  if (child.height != null) float(child.height)
  else if (child.hg != null) float(child.hg)
  else 40.0
}

fn child_index(child, fallback_index) {
  if (child.index != null) int(child.index) else fallback_index
}

fn child_z(child, fallback) {
  let value = attr_or(child, "data-z", null);
  if (value != null) int(value) else fallback
}

fn bool_attr(child, key, fallback = false) {
  let value = attr_or(child, key, fallback);
  if (value == null or value is error) fallback
  else value == true or lower(string(value)) == "true"
}

fn semantic_nodes(children) {
  let tagged = [for (i, child in children where child_tag(child) == "node") {child: child, order: i}];
  if (len(tagged) > 0) tagged
  else [for (i, child in children) {child: child, order: i}]
}

fn semantic_edges(children) {
  [for (i, child in children,
    let parsed_style = graph_style.parse(attr_or(child, "data-style-declarations", ""))
    where child_tag(child) == "edge") {
    id: string(attr_or(child, "data-edge-id", "e" ++ string(i))),
    from: string(attr_or(child, "data-from", "")),
    to: string(attr_or(child, "data-to", "")),
    from_port: attr_or(child, "data-from-port", null),
    to_port: attr_or(child, "data-to-port", null),
    from_compass: attr_or(child, "data-from-compass", null),
    to_compass: attr_or(child, "data-to-compass", null),
    tail_cluster: attr_or(child, "data-tail-cluster", null),
    head_cluster: attr_or(child, "data-head-cluster", null),
    directed: attr_or(child, "data-directed", "true") != "false",
    arrow_start: attr_or(child, "data-arrow-start", "false") == "true",
    arrow_end: attr_or(child, "data-arrow-end", attr_or(child, "data-directed", "true")) == "true",
    marker_start: string(attr_or(child, "data-marker-start",
      if (attr_or(child, "data-arrow-start", "false") == "true") "normal" else "none")),
    marker_end: string(attr_or(child, "data-marker-end",
      if (attr_or(child, "data-arrow-end", attr_or(child, "data-directed", "true")) == "true")
        "normal" else "none")),
    arrow_size: float(attr_or(child, "data-arrow-size", 1.0)),
    style: string(attr_or(child, "data-style", "solid")),
    stroke: parsed_style.stroke,
    stroke_width: parsed_style.stroke_width,
    opacity: parsed_style.opacity,
    dash_array: parsed_style.dash_array,
    min_length: int(attr_or(child, "data-min-length", 1)),
    weight: float(attr_or(child, "data-weight", 1.0)),
    constraint: attr_or(child, "data-constraint", "true") != "false",
    z: child_z(child, -1),
    child_index: child_index(child, i)
  }]
}

fn child_x(child) {
  let box = child.box;
  if (box != null and not (box is error) and box.x != null) float(box.x)
  else if (child.x != null and not (child.x is error)) float(child.x) else 0.0
}

fn child_y(child) {
  let box = child.box;
  if (box != null and not (box is error) and box.y != null) float(box.y)
  else if (child.y != null and not (child.y is error)) float(child.y) else 0.0
}

fn child_children(child) {
  let values = child.children;
  if (values is array or values is list) values else []
}

fn measured_ports_at(stack, node_width, node_height, result) {
  if (len(stack) == 0) result
  else {
    let entry = stack[len(stack) - 1];
    let remaining = slice(stack, 0, len(stack) - 1);
    let child = entry.child;
    let x = entry.x + child_x(child);
    let y = entry.y + child_y(child);
    let port_id = attr_or(child, "data-record-port", null);
    let found = if (port_id != null and port_id != "") [{
      id: string(port_id),
      x_offset: (x + child_width(child) / 2.0) / max([1.0, node_width]),
      y_offset: (y + child_height(child) / 2.0) / max([1.0, node_height])
    }] else [];
    let nested = [for (value in child_children(child)) {child: value, x: x, y: y}];
    measured_ports_at([*remaining, *nested], node_width, node_height,
      [*result, *found])
  }
}

fn measured_ports(node) => measured_ports_at([
  for (child in child_children(node)) {child: child, x: 0.0, y: 0.0}
], child_width(node), child_height(node), [])

fn measured_port(nodes, node_id, port_id) {
  let matches = [for (entry in nodes,
    port in measured_ports(entry.child)
    where child_id(entry.child, entry.order) == node_id and port.id == port_id) port];
  if (len(matches) > 0) matches[0] else null
}

fn semantic_ports(children, nodes) => [
  for (i, child in children,
    let node_id = string(attr_or(child, "data-node-id", "")),
    let port_id = string(attr_or(child, "data-port-id", "p" ++ string(i))),
    let measured = measured_port(nodes, node_id, port_id)
    where child_tag(child) == "port") {
    node_id: node_id,
    id: port_id,
    side: string(attr_or(child, "data-port-side", "auto")),
    offset: float(attr_or(child, "data-port-offset", 0.5)),
    // Measured cell centers supersede source-order estimates after Radiant prelayout.
    x_offset: if (measured != null) measured.x_offset else null,
    y_offset: if (measured != null) measured.y_offset else null,
    z: child_z(child, 0),
    child_index: child_index(child, i)
  }
]

fn semantic_constraints(children) => [
  for (i, child in children where child_tag(child) == "constraint") {
    kind: string(attr_or(child, "data-constraint-kind", "rank")),
    value: string(attr_or(child, "data-constraint-value", "same")),
    scope: string(attr_or(child, "data-constraint-scope", "")),
    member: string(attr_or(child, "data-constraint-member", "")),
    child_index: child_index(child, i)
  }
]

fn semantic_cluster_labels(children) => [
  for (i, child in children where child_tag(child) == "cluster-label") {
    cluster_id: string(attr_or(child, "data-cluster-id", "")),
    width: child_width(child),
    height: child_height(child),
    z: child_z(child, 0),
    child_index: child_index(child, i)
  }
]

fn cluster_label(labels, cluster_id) {
  let matches = [for (label in labels where label.cluster_id == cluster_id) label];
  if (len(matches) > 0) matches[0] else null
}

fn semantic_clusters(children, labels) => [
  for (i, child in children, let id = string(attr_or(child, "data-cluster-id", "")),
    let label = cluster_label(labels, id)
    where child_tag(child) == "cluster" and id != "") {
    id: id,
    parent: attr_or(child, "data-parent-cluster-id", null),
    padding: float(attr_or(child, "data-cluster-padding", 16.0)),
    label_gap: float(attr_or(child, "data-cluster-label-gap", 8.0)),
    label_width: if (label != null) label.width else 0.0,
    label_height: if (label != null) label.height else 0.0,
    fill: string(attr_or(child, "data-cluster-fill", "none")),
    stroke: string(attr_or(child, "data-cluster-stroke", "#8a949e")),
    stroke_width: float(attr_or(child, "data-cluster-stroke-width", 1.0)),
    radius: float(attr_or(child, "data-cluster-radius", 6.0)),
    z: child_z(child, -2),
    child_index: child_index(child, i)
  }
]

fn ports_for(ports, node_id) => [for (port in ports where port.node_id == node_id) port]

fn semantic_edge_labels(children) {
  [for (i, child in children where child_tag(child) == "edge-label") {
    owner_kind: "edge",
    owner_id: string(attr_or(child, "data-edge-id", "")),
    kind: "center",
    width: child_width(child),
    height: child_height(child),
    z: child_z(child, 0),
    child_index: child_index(child, i)
  }]
}

fn semantic_annotations(children) => [
  for (i, child in children where child_tag(child) == "annotation") {
    owner_kind: string(attr_or(child, "data-owner-kind", "edge")),
    owner_id: string(attr_or(child, "data-owner-id", "")),
    kind: string(attr_or(child, "data-annotation-kind", "external")),
    width: child_width(child),
    height: child_height(child),
    z: child_z(child, 0),
    child_index: child_index(child, i)
  }
]

fn routed_edge(edges, edge_id) {
  let matches = [for (edge in edges where edge.id == edge_id) edge];
  if (len(matches) > 0) matches[0] else null
}

fn segment_length(a, b) =>
  math.sqrt((b.x - a.x) ** 2.0 + (b.y - a.y) ** 2.0)

fn route_length_at(points, i, total) {
  if (i >= len(points)) total
  else route_length_at(points, i + 1, total + segment_length(points[i - 1], points[i]))
}

fn point_along_route(points, i, remaining) {
  if (i >= len(points)) points[len(points) - 1]
  else {
    let start = points[i - 1];
    let finish = points[i];
    let distance = segment_length(start, finish);
    if (distance < 0.001) point_along_route(points, i + 1, remaining)
    else if (remaining <= distance) {
      let ratio = remaining / distance;
      {
        x: start.x + (finish.x - start.x) * ratio,
        y: start.y + (finish.y - start.y) * ratio
      }
    }
    else point_along_route(points, i + 1, remaining - distance)
  }
}

fn route_anchor(edge) {
  if (edge == null or len(edge.points) == 0) null
  else if (len(edge.points) == 1) edge.points[0]
  // point-count midpoints bias labels toward short segments on bent routes.
  else point_along_route(edge.points, 1,
    route_length_at(edge.points, 1, 0.0) / 2.0)
}

fn node_annotation_anchor(label, nodes) {
  let matches = [for (node in nodes where node.id == label.owner_id) node];
  if (len(matches) == 0) null
  else {
    let node = matches[0];
    {x: node.x + node.width / 2.0 + 6.0 + label.width / 2.0, y: node.y}
  }
}

fn label_anchor(label, edges, nodes) {
  if (label.owner_kind == "node") node_annotation_anchor(label, nodes)
  else {
    let edge = routed_edge(edges, label.owner_id);
    if (edge == null or len(edge.points) == 0) null
    else if (label.kind == "head") edge.points[len(edge.points) - 1]
    else if (label.kind == "tail") edge.points[0]
    else route_anchor(edge)
  }
}

fn label_placement(label, edges, nodes) {
  let anchor = label_anchor(label, edges, nodes);
  if (anchor == null) {
    {index: label.child_index, x: 0.0, y: 0.0, z: label.z}
  } else {
    {
      index: label.child_index,
      x: anchor.x - label.width / 2.0,
      y: anchor.y - label.height / 2.0,
      z: label.z
    }
  }
}

fn placement_box(x, y, width, height) =>
  {left: x, top: y, right: x + width, bottom: y + height}

fn boxes_overlap(a, b) =>
  a.left < b.right and a.right > b.left and a.top < b.bottom and a.bottom > b.top

fn candidate_placement(label, anchor, dx, dy) => {
  index: label.child_index,
  x: anchor.x - label.width / 2.0 + dx,
  y: anchor.y - label.height / 2.0 + dy,
  width: label.width,
  height: label.height,
  z: label.z
}

fn placement_clear(candidate, occupied) {
  let box = placement_box(candidate.x, candidate.y, candidate.width, candidate.height);
  len([for (obstacle in occupied where boxes_overlap(box, obstacle)) obstacle]) == 0
}

fn first_clear_candidate(candidates, occupied, i) {
  if (i >= len(candidates)) candidates[0]
  else if (placement_clear(candidates[i], occupied)) candidates[i]
  else first_clear_candidate(candidates, occupied, i + 1)
}

fn place_edge_labels_at(labels, edges, nodes, occupied, i, result) {
  if (i >= len(labels)) result
  else {
    let label = labels[i];
    let anchor = label_anchor(label, edges, nodes);
    let fallback = label_placement(label, edges, nodes);
    let gap = 6.0;
    let candidates = if (anchor == null) [{*:fallback,
      width: label.width, height: label.height}]
    else [
      candidate_placement(label, anchor, 0.0, 0.0),
      candidate_placement(label, anchor, 0.0, 0.0 - label.height - gap),
      candidate_placement(label, anchor, 0.0, label.height + gap),
      candidate_placement(label, anchor, 0.0 - label.width - gap, 0.0),
      candidate_placement(label, anchor, label.width + gap, 0.0),
      candidate_placement(label, anchor, 0.0, 0.0 - 2.0 * (label.height + gap)),
      candidate_placement(label, anchor, 0.0, 2.0 * (label.height + gap))
    ];
    let chosen = first_clear_candidate(candidates, occupied, 0);
    let box = placement_box(chosen.x, chosen.y, chosen.width, chosen.height);
    place_edge_labels_at(labels, edges, nodes, [*occupied, box], i + 1,
      [*result, chosen])
  }
}

fn edge_label_placements(labels, edges, nodes, cluster_labels, clusters) {
  let occupied = [
    for (node in nodes) placement_box(node.x - node.width / 2.0,
      node.y - node.height / 2.0, node.width, node.height),
    for (label in cluster_labels,
      let placement = cluster_label_placement(label, clusters))
      placement_box(placement.x, placement.y, label.width, label.height)
  ];
  place_edge_labels_at(labels, edges, nodes, occupied, 0, [])
}

fn cluster_label_placement(label, clusters) {
  let matches = [for (cluster in clusters where cluster.id == label.cluster_id) cluster];
  if (len(matches) == 0) {
    {index: label.child_index, x: 0.0, y: 0.0, z: label.z}
  } else {
    let cluster = matches[0];
    {
      index: label.child_index,
      x: cluster.label_x,
      y: cluster.label_y,
      z: label.z
    }
  }
}

fn graph_option(parent, opts, key, attr_key, fallback) {
  if (opts != null and opts[key] != null) opts[key]
  else attr_or(parent, attr_key, fallback)
}

fn placement_z(nodes, index) {
  let matches = [for (entry in nodes where child_index(entry.child, entry.order) == index)
    child_z(entry.child, 0)];
  if (len(matches) > 0) matches[0] else 0
}

// Convert already-laid-out semantic children into the canonical geometry model.
pub fn from_velmts(parent, children, ctx, opts = null) {
  let nodes = semantic_nodes(children);
  let metadata_edges = semantic_edges(children);
  let edge_labels = [*semantic_edge_labels(children), *semantic_annotations(children)];
  let ports = semantic_ports(children, nodes);
  let constraints = semantic_constraints(children);
  let cluster_labels = semantic_cluster_labels(children);
  let clusters = semantic_clusters(children, cluster_labels);
  let edges = if (opts != null and opts.edges != null) opts.edges else metadata_edges;
  let node_sep = float(graph_option(parent, opts, "node_sep", "data-node-sep", 60.0));
  let rank_sep = float(graph_option(parent, opts, "rank_sep", "data-rank-sep", 80.0));
  let edge_sep = float(graph_option(parent, opts, "edge_sep", "data-edge-sep", 10.0));
  let direction = string(graph_option(parent, opts, "direction", "data-direction", "TB"));
  let route_mode = string(graph_option(parent, opts, "route_mode", "data-route-mode",
    if (string(graph_option(parent, opts, "use_splines",
        "data-use-splines", "false")) == "true") "curved" else "orthogonal"));
  let graph_input = {
    nodes: [for (entry in nodes) {
      id: child_id(entry.child, entry.order),
      index: child_index(entry.child, entry.order),
      width: child_width(entry.child),
      height: child_height(entry.child),
      shape: string(attr_or(entry.child, "data-shape", "box")),
      polygon_sides: attr_or(entry.child, "data-polygon-sides", null),
      polygon_orientation: attr_or(entry.child, "data-polygon-orientation", null),
      polygon_skew: attr_or(entry.child, "data-polygon-skew", null),
      polygon_distortion: attr_or(entry.child, "data-polygon-distortion", null),
      regular: bool_attr(entry.child, "data-regular"),
      peripheries: attr_or(entry.child, "data-peripheries", null),
      // DOT group is an ordering hint; structural subgraph membership still owns cluster geometry.
      order_group: attr_or(entry.child, "data-order-group", null),
      ordering: attr_or(entry.child, "data-ordering", null),
      group: attr_or(entry.child, "data-subgraph-id", null),
      ports: ports_for(ports, child_id(entry.child, entry.order)),
      z: child_z(entry.child, 0)
    }],
    edges: edges,
    constraints: constraints,
    clusters: clusters,
    direction: direction,
    node_sep: node_sep,
    rank_sep: rank_sep,
    edge_sep: edge_sep,
    route_mode: route_mode,
    ordering: graph_option(parent, opts, "ordering", "data-ordering", null),
    new_rank: graph_option(parent, opts, "new_rank", "data-new-rank", false),
    compound: graph_option(parent, opts, "compound", "data-compound", false),
    use_splines: route_mode == "curved"
  };
  let result = compute(graph_input, opts);
  let placed_edge_labels = edge_label_placements(edge_labels, result.edges, result.nodes,
    cluster_labels, result.clusters);
  // Label collision resolution may place labels beyond the graph geometry. Shift
  // every visual primitive together so the parent remains the true containing box.
  let min_x = if (len(placed_edge_labels) > 0)
    min([0.0, for (placement in placed_edge_labels) placement.x]) else 0.0;
  let min_y = if (len(placed_edge_labels) > 0)
    min([0.0, for (placement in placed_edge_labels) placement.y]) else 0.0;
  let max_x = if (len(placed_edge_labels) > 0)
    max([result.width, for (placement in placed_edge_labels)
      placement.x + placement.width]) else result.width;
  let max_y = if (len(placed_edge_labels) > 0)
    max([result.height, for (placement in placed_edge_labels)
      placement.y + placement.height]) else result.height;
  let shift_x = 0.0 - min_x;
  let shift_y = 0.0 - min_y;
  let shifted_nodes = [for (node in result.nodes) {*:node,
    x: node.x + shift_x, y: node.y + shift_y}];
  let shifted_edges = [for (edge in result.edges) {*:edge,
    points: [for (point in edge.points) {x: point.x + shift_x, y: point.y + shift_y}]}];
  let shifted_clusters = [for (cluster in result.clusters) {*:cluster,
    x: cluster.x + shift_x, y: cluster.y + shift_y,
    label_x: cluster.label_x + shift_x, label_y: cluster.label_y + shift_y}];
  {
    width: max_x - min_x,
    height: max_y - min_y,
    nodes: shifted_nodes,
    edges: shifted_edges,
    clusters: shifted_clusters,
    layers: result.layers,
    placements: [
      for (place in result.placements) {
        index: place.index,
        x: place.x + shift_x,
        y: place.y + shift_y,
        z: placement_z(nodes, place.index)
      },
      for (edge in metadata_edges) {
        index: edge.child_index,
        x: 0.0,
        y: 0.0,
        z: edge.z
      },
      for (cluster in clusters) {
        index: cluster.child_index,
        x: 0.0,
        y: 0.0,
        z: cluster.z
      },
      for (port in ports) {
        index: port.child_index,
        x: 0.0,
        y: 0.0,
        z: port.z
      },
      for (constraint in constraints) {
        index: constraint.child_index,
        x: 0.0,
        y: 0.0,
        z: 0
      },
      for (placement in placed_edge_labels) {
        index: placement.index, x: placement.x + shift_x,
        y: placement.y + shift_y, z: placement.z
      },
      for (label in cluster_labels) cluster_label_placement(label, shifted_clusters)
    ],
    algorithm: result.algorithm,
    direction: result.direction
  }
}

// Historical adapter retained for callers that pass plain measured children.
pub fn layout_custom(parent, children, ctx, opts = null) {
  let result = from_velmts(parent, children, ctx, opts);
  {
    width: result.width,
    height: result.height,
    placements: [for (place in result.placements) {
      index: place.index,
      x: place.x,
      y: place.y
    }]
  }
}
