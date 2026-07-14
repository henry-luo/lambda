// Public graph geometry and Radiant Velmt adapter.

import dagre: .dagre
import graph_style: .style

pub fn make_options() => dagre.make_options()

pub fn compute(input, opts = null) => dagre.layout(input, opts)

// Compatibility name retained while callers migrate to compute().
pub fn layout(input, opts = null) => compute(input, opts)

fn attr_or(child, key, fallback) {
  if (child.attrs != null and child.attrs[key] != null) child.attrs[key]
  else if (child[key] != null) child[key]
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
    directed: attr_or(child, "data-directed", "true") != "false",
    arrow_start: attr_or(child, "data-arrow-start", "false") == "true",
    arrow_end: attr_or(child, "data-arrow-end", attr_or(child, "data-directed", "true")) == "true",
    marker_start: string(attr_or(child, "data-marker-start",
      if (attr_or(child, "data-arrow-start", "false") == "true") "normal" else "none")),
    marker_end: string(attr_or(child, "data-marker-end",
      if (attr_or(child, "data-arrow-end", attr_or(child, "data-directed", "true")) == "true")
        "normal" else "none")),
    style: string(attr_or(child, "data-style", "solid")),
    stroke: parsed_style.stroke,
    stroke_width: parsed_style.stroke_width,
    opacity: parsed_style.opacity,
    dash_array: parsed_style.dash_array,
    min_length: int(attr_or(child, "data-min-length", 1)),
    z: child_z(child, -1),
    child_index: child_index(child, i)
  }]
}

fn semantic_ports(children) => [
  for (i, child in children where child_tag(child) == "port") {
    node_id: string(attr_or(child, "data-node-id", "")),
    id: string(attr_or(child, "data-port-id", "p" ++ string(i))),
    side: string(attr_or(child, "data-port-side", "auto")),
    offset: float(attr_or(child, "data-port-offset", 0.5)),
    z: child_z(child, 0),
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
    edge_id: string(attr_or(child, "data-edge-id", "")),
    width: child_width(child),
    height: child_height(child),
    z: child_z(child, 0),
    child_index: child_index(child, i)
  }]
}

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

fn label_placement(label, edges) {
  let anchor = route_anchor(routed_edge(edges, label.edge_id));
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
  let edge_labels = semantic_edge_labels(children);
  let ports = semantic_ports(children);
  let cluster_labels = semantic_cluster_labels(children);
  let clusters = semantic_clusters(children, cluster_labels);
  let edges = if (opts != null and opts.edges != null) opts.edges else metadata_edges;
  let node_sep = float(graph_option(parent, opts, "node_sep", "data-node-sep", 60.0));
  let rank_sep = float(graph_option(parent, opts, "rank_sep", "data-rank-sep", 80.0));
  let edge_sep = float(graph_option(parent, opts, "edge_sep", "data-edge-sep", 10.0));
  let direction = string(graph_option(parent, opts, "direction", "data-direction", "TB"));
  let graph_input = {
    nodes: [for (entry in nodes) {
      id: child_id(entry.child, entry.order),
      index: child_index(entry.child, entry.order),
      width: child_width(entry.child),
      height: child_height(entry.child),
      shape: string(attr_or(entry.child, "data-shape", "box")),
      group: attr_or(entry.child, "data-subgraph-id", null),
      ports: ports_for(ports, child_id(entry.child, entry.order)),
      z: child_z(entry.child, 0)
    }],
    edges: edges,
    clusters: clusters,
    direction: direction,
    node_sep: node_sep,
    rank_sep: rank_sep,
    edge_sep: edge_sep
  };
  let result = compute(graph_input, opts);
  {
    width: result.width,
    height: result.height,
    nodes: result.nodes,
    edges: result.edges,
    clusters: result.clusters,
    layers: result.layers,
    placements: [
      for (place in result.placements) {
        index: place.index,
        x: place.x,
        y: place.y,
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
      for (label in edge_labels) label_placement(label, result.edges),
      for (label in cluster_labels) cluster_label_placement(label, result.clusters)
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
