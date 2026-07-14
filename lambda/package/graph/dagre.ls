pub fn make_options() {
  let opts = {
    algorithm: "dagre",
    direction: "TB",
    node_sep: 60.0,
    rank_sep: 80.0,
    edge_sep: 10.0,
    use_splines: false,
    max_iterations: 8
  };
  opts
}

fn opt(opts, key, fallback) {
  if (opts != null and opts[key] != null) opts[key] else fallback
}

fn has_id_at(nodes, id, i, n) {
  if (i >= n) false
  else if (nodes[i].id == id) true
  else has_id_at(nodes, id, i + 1, n)
}

fn has_id(nodes, id) => has_id_at(nodes, id, 0, len(nodes))

fn contains_at(items, value, i, n) {
  if (i >= n) false
  else if (items[i] == value) true
  else contains_at(items, value, i + 1, n)
}

fn contains(items, value) => contains_at(items, value, 0, len(items))

fn first_or(items, fallback) {
  if (len(items) > 0) items[0] else fallback
}

fn normalize_node(node, index) {
  let id = if (node.id != null and node.id != "") string(node.id) else "n" ++ string(index);
  let wd = if (node.width != null) float(node.width)
    else if (node.wd != null) float(node.wd)
    else 80.0;
  let hg = if (node.height != null) float(node.height)
    else if (node.hg != null) float(node.hg)
    else 40.0;
  {
    id: id,
    label: if (node.label != null) string(node.label) else id,
    shape: if (node.shape != null) string(node.shape) else "box",
    width: wd,
    height: hg,
    index: if (node.index != null) int(node.index) else index,
    rank: 0,
    order: index,
    x: 0.0,
    y: 0.0
  }
}

fn normalize_edge(edge, nodes, index, directed) {
  let from_id = if (edge.from != null) string(edge.from)
    else if (edge.from_id != null) string(edge.from_id)
    else "";
  let to_id = if (edge.to != null) string(edge.to)
    else if (edge.to_id != null) string(edge.to_id)
    else "";
  if (has_id(nodes, from_id) and has_id(nodes, to_id)) {
    {
      id: if (edge.id != null) string(edge.id) else "e" ++ string(index),
      from: from_id,
      to: to_id,
      label: if (edge.label != null) string(edge.label) else null,
      directed: if (edge.directed != null) edge.directed else directed,
      arrow_start: if (edge.arrow_start != null) edge.arrow_start
        else if (edge["arrow-start"] != null) edge["arrow-start"]
        else false,
      arrow_end: if (edge.arrow_end != null) edge.arrow_end
        else if (edge["arrow-end"] != null) edge["arrow-end"]
        else directed,
      style: if (edge.style != null) string(edge.style) else "solid",
      z: if (edge.z != null) int(edge.z) else -1,
      index: index
    }
  } else null
}

fn normalize_graph(input, opts) {
  let nodes0 = if (input.nodes != null) input.nodes else [];
  let nodes = [for (i, node in nodes0) normalize_node(node, i)];
  let directed = if (input.directed != null) input.directed else true;
  let edges0 = if (input.edges != null) input.edges else [];
  let edges = [for (i, edge in edges0,
    let e = normalize_edge(edge, nodes, i, directed)
    where e != null) e];
  {
    nodes: nodes,
    edges: edges,
    directed: directed,
    options: {
      algorithm: opt(opts, "algorithm", opt(input, "layout", "dagre")),
      direction: opt(opts, "direction", opt(input, "direction", "TB")),
      node_sep: float(opt(opts, "node_sep", opt(input, "node_sep", 60.0))),
      rank_sep: float(opt(opts, "rank_sep", opt(input, "rank_sep", 80.0))),
      edge_sep: float(opt(opts, "edge_sep", opt(input, "edge_sep", 10.0))),
      use_splines: opt(opts, "use_splines", opt(input, "use_splines", false)),
      max_iterations: int(opt(opts, "max_iterations", opt(input, "max_iterations", 8)))
    }
  }
}

fn incoming_edges(edges, id) => [for (edge in edges where edge.to == id) edge]
fn outgoing_edges(edges, id) => [for (edge in edges where edge.from == id) edge]

fn rank_of(id, edges, stack) {
  if (contains(stack, id)) 0
  else {
    let preds = incoming_edges(edges, id);
    if (len(preds) == 0) 0
    else max([for (edge in preds) rank_of(edge.from, edges, [*stack, id]) + 1])
  }
}

fn assign_ranks(nodes, edges) {
  let result = [for (node in nodes) {
    id: node.id,
    label: node.label,
    shape: node.shape,
    width: node.width,
    height: node.height,
    index: node.index,
    rank: rank_of(node.id, edges, []),
    order: node.order,
    x: node.x,
    y: node.y
  }];
  result
}

fn max_rank(nodes) {
  if (len(nodes) == 0) 0 else max([for (node in nodes) node.rank])
}

fn layer_with_order(rank, nodes) {
  let layer_nodes = [for (node in nodes where node.rank == rank) node];
  {
    rank: rank,
    nodes: [for (i, node in layer_nodes) {
      id: node.id,
      label: node.label,
      shape: node.shape,
      width: node.width,
      height: node.height,
      index: node.index,
      rank: node.rank,
      order: i,
      x: node.x,
      y: node.y
    }]
  }
}

fn create_layers(nodes) {
  let last_rank = max_rank(nodes);
  let result = [for (rank in 0 to last_rank) layer_with_order(rank, nodes)];
  result
}

fn all_layer_nodes(layers) {
  let result = [for (layer in layers, node in layer.nodes) node];
  result
}

fn order_of(nodes, id) {
  first_or([for (node in nodes where node.id == id) node.order], 0)
}

fn barycenter(node, edges, ordered_nodes, use_predecessors) {
  let ids = if (use_predecessors)
    [for (edge in incoming_edges(edges, node.id)) edge.from]
  else
    [for (edge in outgoing_edges(edges, node.id)) edge.to];
  if (len(ids) == 0) float(node.order)
  else sum([for (id in ids) float(order_of(ordered_nodes, id))]) / float(len(ids))
}

fn reorder_layer(layer, edges, ordered_nodes, use_predecessors) {
  let sorted_nodes = sort(layer.nodes, (node) => barycenter(node, edges, ordered_nodes, use_predecessors));
  {
    rank: layer.rank,
    nodes: [for (i, node in sorted_nodes) {
      id: node.id,
      label: node.label,
      shape: node.shape,
      width: node.width,
      height: node.height,
      index: node.index,
      rank: node.rank,
      order: i,
      x: node.x,
      y: node.y
    }]
  }
}

fn reduce_crossings_once(layers, edges) {
  let ordered0 = all_layer_nodes(layers);
  let down = [for (layer in layers)
    if (layer.rank == 0) layer else reorder_layer(layer, edges, ordered0, true)];
  let ordered1 = all_layer_nodes(down);
  let result = [for (layer in down)
    if (layer.rank == 0) layer else reorder_layer(layer, edges, ordered1, false)];
  result
}

fn reduce_crossings(layers, edges, max_iterations) {
  if (max_iterations <= 0 or len(layers) < 2) layers
  else reduce_crossings_once(layers, edges)
}

fn layer_total_width(nodes, node_sep) {
  if (len(nodes) == 0) 0.0
  else sum([for (node in nodes) node.width]) + float(len(nodes) - 1) * node_sep
}

fn prefix_width(nodes, index, node_sep) {
  if (index <= 0) 0.0
  else sum([for (i, node in nodes where i < index) node.width]) + float(index) * node_sep
}

fn position_layer(layer, node_sep, rank_sep) {
  let total_width = layer_total_width(layer.nodes, node_sep);
  let result = [for (i, node in layer.nodes) {
    id: node.id,
    label: node.label,
    shape: node.shape,
    width: node.width,
    height: node.height,
    index: node.index,
    rank: node.rank,
    order: i,
    x: 0.0 - total_width / 2.0 + prefix_width(layer.nodes, i, node_sep) + node.width / 2.0,
    y: float(layer.rank) * rank_sep
  }];
  result
}

fn position_nodes(layers, opts) {
  let placed = [for (layer in layers, node in position_layer(layer, opts.node_sep, opts.rank_sep)) node];
  if (len(placed) == 0) { [] }
  else {
    let min_x = min([for (node in placed) node.x - node.width / 2.0]);
    let min_y = min([for (node in placed) node.y - node.height / 2.0]);
    let result = [for (node in placed) {
      id: node.id,
      label: node.label,
      shape: node.shape,
      width: node.width,
      height: node.height,
      index: node.index,
      rank: node.rank,
      order: node.order,
      x: node.x - min_x,
      y: node.y - min_y
    }];
    result
  }
}

fn oriented_node(node, direction) {
  let tx = if (direction == "LR") node.y
    else if (direction == "RL") 0.0 - node.y
    else node.x;
  let ty = if (direction == "LR" or direction == "RL") node.x
    else if (direction == "BT") 0.0 - node.y
    else node.y;
  {
    id: node.id,
    label: node.label,
    shape: node.shape,
    width: node.width,
    height: node.height,
    index: node.index,
    rank: node.rank,
    order: node.order,
    x: tx,
    y: ty
  }
}

fn normalize_oriented_nodes(nodes) {
  if (len(nodes) == 0) { [] }
  else {
    let min_x = min([for (node in nodes) node.x - node.width / 2.0]);
    let min_y = min([for (node in nodes) node.y - node.height / 2.0]);
    [for (node in nodes) {
      id: node.id,
      label: node.label,
      shape: node.shape,
      width: node.width,
      height: node.height,
      index: node.index,
      rank: node.rank,
      order: node.order,
      x: node.x - min_x,
      y: node.y - min_y
    }]
  }
}

fn orient_nodes(nodes, direction) {
  normalize_oriented_nodes([for (node in nodes) oriented_node(node, direction)])
}

fn find_node(nodes, id) => first_or([for (node in nodes where node.id == id) node], null)

fn clip_rect(cx, cy, tx, ty, half_w, half_h) {
  let dx = tx - cx;
  let dy = ty - cy;
  if (abs(dx) < 0.001 and abs(dy) < 0.001) { {x: cx, y: cy} }
  else {
    let tx_scale = if (dx > 0.0) half_w / dx else if (dx < 0.0) -half_w / dx else 10000000000.0;
    let ty_scale = if (dy > 0.0) half_h / dy else if (dy < 0.0) -half_h / dy else 10000000000.0;
    let scale = min([abs(tx_scale), abs(ty_scale)]);
    {x: cx + dx * scale, y: cy + dy * scale}
  }
}

fn clip_ellipse(cx, cy, tx, ty, radius_x, radius_y) {
  let dx = tx - cx;
  let dy = ty - cy;
  let distance = math.sqrt((dx * dx) / (radius_x * radius_x) +
    (dy * dy) / (radius_y * radius_y));
  if (distance < 0.001) { {x: cx, y: cy} }
  else { {x: cx + dx / distance, y: cy + dy / distance} }
}

fn clip_diamond(cx, cy, tx, ty, half_w, half_h) {
  let dx = tx - cx;
  let dy = ty - cy;
  let distance = abs(dx) / half_w + abs(dy) / half_h;
  if (distance < 0.001) { {x: cx, y: cy} }
  else { {x: cx + dx / distance, y: cy + dy / distance} }
}

fn clip_node(node, target_x, target_y) {
  let half_w = node.width / 2.0;
  let half_h = node.height / 2.0;
  if (node.shape == "circle" or node.shape == "doublecircle" or node.shape == "ellipse")
    clip_ellipse(node.x, node.y, target_x, target_y, half_w, half_h)
  else if (node.shape == "diamond")
    clip_diamond(node.x, node.y, target_x, target_y, half_w, half_h)
  else clip_rect(node.x, node.y, target_x, target_y, half_w, half_h)
}

fn remove_collinear(points) {
  if (len(points) < 3) points
  else {
    let first = points[0];
    let mid = points[1];
    let tail = points[len(points) - 1];
    let same_x = abs(first.x - mid.x) < 1.0 and abs(mid.x - tail.x) < 1.0;
    let same_y = abs(first.y - mid.y) < 1.0 and abs(mid.y - tail.y) < 1.0;
    if (same_x or same_y) { [first, tail] } else { points }
  }
}

fn orthogonal_points(points, vertical_first) {
  if (len(points) < 2) points
  else {
    let start = points[0];
    let finish = points[len(points) - 1];
    let dx = abs(finish.x - start.x);
    let dy = abs(finish.y - start.y);
    if (dx < 1.0 or dy < 1.0) points
    else {
      let bend = if (vertical_first) {x: start.x, y: finish.y} else {x: finish.x, y: start.y};
      remove_collinear([start, bend, finish])
    }
  }
}

fn self_loop_points(node, edge_sep, edge_index) {
  let half_w = node.width / 2.0;
  let half_h = node.height / 2.0;
  let spread = max([10.0, half_h / 2.0]);
  let loop_gap = max([20.0, edge_sep * float(edge_index + 2)]);
  [
    {x: node.x + half_w, y: node.y - spread},
    {x: node.x + half_w + loop_gap, y: node.y - spread},
    {x: node.x + half_w + loop_gap, y: node.y + spread},
    {x: node.x + half_w, y: node.y + spread}
  ]
}

fn route_edge(edge, nodes, opts) {
  let from_node = find_node(nodes, edge.from);
  let to_node = find_node(nodes, edge.to);
  if (from_node == null or to_node == null) null
  else {
    let start = clip_node(from_node, to_node.x, to_node.y);
    let finish = clip_node(to_node, from_node.x, from_node.y);
    let vertical_first = not (opts.direction == "LR" or opts.direction == "RL");
    {
      id: edge.id,
      from: edge.from,
      to: edge.to,
      points: if (edge.from == edge.to)
        self_loop_points(from_node, opts.edge_sep, edge.index)
        else orthogonal_points([start, finish], vertical_first),
      directed: edge.directed,
      arrow_start: edge.arrow_start,
      arrow_end: edge.arrow_end,
      style: edge.style,
      z: edge.z,
      index: edge.index,
      is_bezier: opts.use_splines
    }
  }
}

fn graph_bounds(nodes, edges) {
  if (len(nodes) == 0) { {width: 0.0, height: 0.0} }
  else {
    let min_x = min([for (node in nodes) node.x - node.width / 2.0]);
    let min_y = min([for (node in nodes) node.y - node.height / 2.0]);
    let edge_points = [for (edge in edges, point in edge.points) point];
    let max_x = max([for (node in nodes) node.x + node.width / 2.0,
      for (point in edge_points) point.x]);
    let max_y = max([for (node in nodes) node.y + node.height / 2.0,
      for (point in edge_points) point.y]);
    {width: max_x - min_x, height: max_y - min_y}
  }
}

pub fn layout(input, opts = null) {
  let graph = normalize_graph(input, opts);
  let ranked = assign_ranks(graph.nodes, graph.edges);
  let layers0 = create_layers(ranked);
  let layers = reduce_crossings(layers0, graph.edges, graph.options.max_iterations);
  let canonical_nodes = position_nodes(layers, graph.options);
  let nodes = orient_nodes(canonical_nodes, graph.options.direction);
  let edges = [for (edge in graph.edges,
    let path = route_edge(edge, nodes, graph.options)
    where path != null) path];
  let bounds = graph_bounds(nodes, edges);
  {
    width: bounds.width,
    height: bounds.height,
    nodes: nodes,
    edges: edges,
    layers: layers,
    placements: [for (node in nodes) {
      index: node.index,
      id: node.id,
      x: node.x - node.width / 2.0,
      y: node.y - node.height / 2.0
    }],
    algorithm: graph.options.algorithm,
    direction: graph.options.direction
  }
}
