// graph/graph.ls - Public entry point for Lambda graph layout.

import dagre: .dagre

pub fn make_options() => dagre.make_options()

// Layout a graph map:
//
// {
//   nodes: [{id, width, height, shape?}, ...],
//   edges: [{from, to, directed?, style?}, ...],
//   direction?: "TB" | "LR" | "BT" | "RL"
// }
pub fn layout(input, opts = null) => dagre.layout(input, opts)

fn child_id(child, fallback_index) {
  if (child.id != null and child.id != "") string(child.id)
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

// Adapter for Radiant custom layout callbacks. Edges can be supplied through
// opts.edges by wrapper code; without edges this still arranges children in a
// single layer using their resolved Velmt sizes.
pub fn layout_custom(parent, children, ctx, opts = null) {
  let node_sep = if (opts != null and opts.node_sep != null) opts.node_sep else 60.0;
  let rank_sep = if (opts != null and opts.rank_sep != null) opts.rank_sep else 80.0;
  let graph_input = {
    nodes: [for (i, child in children) {
      id: child_id(child, i),
      index: if (child.index != null) int(child.index) else i,
      width: child_width(child),
      height: child_height(child),
      shape: if (child.shape != null) child.shape else "box"
    }],
    edges: if (opts != null and opts.edges != null) opts.edges else [],
    direction: if (opts != null and opts.direction != null) opts.direction else "TB",
    node_sep: node_sep,
    rank_sep: rank_sep
  };
  let result = layout(graph_input, opts);
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
