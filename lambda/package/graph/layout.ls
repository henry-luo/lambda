// Public graph geometry and Radiant Velmt adapter.

import dagre: .dagre

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
  [for (i, child in children where child_tag(child) == "edge") {
    id: string(attr_or(child, "data-edge-id", "e" ++ string(i))),
    from: string(attr_or(child, "data-from", "")),
    to: string(attr_or(child, "data-to", "")),
    directed: attr_or(child, "data-directed", "true") != "false",
    arrow_start: attr_or(child, "data-arrow-start", "false") == "true",
    arrow_end: attr_or(child, "data-arrow-end", attr_or(child, "data-directed", "true")) == "true",
    style: string(attr_or(child, "data-style", "solid")),
    z: child_z(child, -1),
    child_index: child_index(child, i)
  }]
}

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

fn route_anchor(edge) {
  if (edge == null or len(edge.points) == 0) null
  else if (len(edge.points) == 1) edge.points[0]
  else if (len(edge.points) == 2) {
    {
      x: (edge.points[0].x + edge.points[1].x) / 2.0,
      y: (edge.points[0].y + edge.points[1].y) / 2.0
    }
  }
  else edge.points[int(len(edge.points) / 2)]
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
  let edges = if (opts != null and opts.edges != null) opts.edges else metadata_edges;
  let node_sep = float(graph_option(parent, opts, "node_sep", "data-node-sep", 60.0));
  let rank_sep = float(graph_option(parent, opts, "rank_sep", "data-rank-sep", 80.0));
  let direction = string(graph_option(parent, opts, "direction", "data-direction", "TB"));
  let graph_input = {
    nodes: [for (entry in nodes) {
      id: child_id(entry.child, entry.order),
      index: child_index(entry.child, entry.order),
      width: child_width(entry.child),
      height: child_height(entry.child),
      shape: string(attr_or(entry.child, "data-shape", "box")),
      z: child_z(entry.child, 0)
    }],
    edges: edges,
    direction: direction,
    node_sep: node_sep,
    rank_sep: rank_sep
  };
  let result = compute(graph_input, opts);
  {
    width: result.width,
    height: result.height,
    nodes: result.nodes,
    edges: result.edges,
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
      for (label in edge_labels) label_placement(label, result.edges)
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
