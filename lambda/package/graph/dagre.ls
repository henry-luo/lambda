import polygon: .polygon

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
  let regular = edge_bool(node.regular, false);
  {
    id: id,
    label: if (node.label != null) string(node.label) else id,
    shape: if (node.shape != null) string(node.shape) else "box",
    polygon_sides: if (node.polygon_sides != null) int(node.polygon_sides) else null,
    polygon_orientation: if (node.polygon_orientation != null)
      float(node.polygon_orientation) else 0.0,
    polygon_skew: if (node.polygon_skew != null) float(node.polygon_skew) else 0.0,
    polygon_distortion: if (node.polygon_distortion != null)
      float(node.polygon_distortion) else 0.0,
    regular: regular,
    peripheries: if (node.peripheries != null) int(node.peripheries) else 1,
    width: wd,
    height: hg,
    group: if (node.group != null and node.group != "") string(node.group) else null,
    order_group: if (node.order_group != null and node.order_group != "")
      string(node.order_group) else null,
    ordering: normalize_ordering(node.ordering),
    ports: if (node.ports != null) node.ports else [],
    index: if (node.index != null) int(node.index) else index,
    rank: 0,
    order: index,
    x: 0.0,
    y: 0.0
  }
}

fn edge_bool(value, fallback) {
  let actual = if (value == null) fallback else value;
  not (actual == null or actual == false or actual == "false" or actual == "none" or actual == "no")
}

fn edge_marker(value, enabled) {
  if (value == null or value == "") { if (enabled) "normal" else "none" }
  else if (value == true or value == "true") "normal"
  else if (value == false or value == "false") "none"
  else string(value)
}

fn normalize_edge(edge, nodes, index, directed) {
  let from_id = if (edge.from != null) string(edge.from)
    else if (edge.from_id != null) string(edge.from_id)
    else "";
  let to_id = if (edge.to != null) string(edge.to)
    else if (edge.to_id != null) string(edge.to_id)
    else "";
  let arrow_start = edge_bool(if (edge.arrow_start != null) edge.arrow_start
    else edge["arrow-start"], false);
  let arrow_end = edge_bool(if (edge.arrow_end != null) edge.arrow_end
    else edge["arrow-end"], directed);
  let marker_start = edge_marker(if (edge.marker_start != null) edge.marker_start
    else if (edge["marker-start"] != null) edge["marker-start"]
    else edge["arrow-tail"], arrow_start);
  let marker_end = edge_marker(if (edge.marker_end != null) edge.marker_end
    else if (edge["marker-end"] != null) edge["marker-end"]
    else edge["arrow-head"], arrow_end);
  if (has_id(nodes, from_id) and has_id(nodes, to_id)) {
    {
      id: if (edge.id != null) string(edge.id) else "e" ++ string(index),
      from: from_id,
      to: to_id,
      from_port: if (edge.from_port != null) string(edge.from_port)
        else if (edge["from-port"] != null) string(edge["from-port"]) else null,
      to_port: if (edge.to_port != null) string(edge.to_port)
        else if (edge["to-port"] != null) string(edge["to-port"]) else null,
      from_compass: if (edge.from_compass != null) lower(string(edge.from_compass))
        else if (edge["from-compass"] != null) lower(string(edge["from-compass"])) else null,
      to_compass: if (edge.to_compass != null) lower(string(edge.to_compass))
        else if (edge["to-compass"] != null) lower(string(edge["to-compass"])) else null,
      tail_cluster: if (edge.tail_cluster != null) string(edge.tail_cluster)
        else if (edge["tail-cluster"] != null) string(edge["tail-cluster"]) else null,
      head_cluster: if (edge.head_cluster != null) string(edge.head_cluster)
        else if (edge["head-cluster"] != null) string(edge["head-cluster"]) else null,
      label: if (edge.label != null) string(edge.label) else null,
      directed: if (edge.directed != null) edge.directed else directed,
      arrow_start: marker_start != "none",
      arrow_end: marker_end != "none",
      marker_start: marker_start,
      marker_end: marker_end,
      arrow_size: float(if (edge.arrow_size != null) edge.arrow_size
        else if (edge["arrow-size"] != null) edge["arrow-size"] else 1.0),
      style: if (edge.style != null) string(edge.style) else "solid",
      stroke: if (edge.stroke != null) string(edge.stroke) else null,
      stroke_width: if (edge.stroke_width != null) float(edge.stroke_width) else null,
      opacity: if (edge.opacity != null) float(edge.opacity) else null,
      dash_array: if (edge.dash_array != null) string(edge.dash_array) else null,
      min_length: max([1, int(if (edge.min_length != null) edge.min_length
        else if (edge["min-length"] != null) edge["min-length"] else 1)]),
      weight: float(if (edge.weight != null) edge.weight else 1.0),
      constraint: edge_bool(edge.constraint, true),
      z: if (edge.z != null) int(edge.z) else -1,
      index: index
    }
  } else null
}

fn normalize_cluster(cluster, index) => {
  id: if (cluster.id != null and cluster.id != "") string(cluster.id)
    else "cluster" ++ string(index),
  parent: if (cluster.parent != null and cluster.parent != "") string(cluster.parent) else null,
  padding: float(if (cluster.padding != null) cluster.padding else 16.0),
  label_gap: float(if (cluster.label_gap != null) cluster.label_gap else 8.0),
  label_width: float(if (cluster.label_width != null) cluster.label_width else 0.0),
  label_height: float(if (cluster.label_height != null) cluster.label_height else 0.0),
  fill: if (cluster.fill != null) string(cluster.fill) else "none",
  stroke: if (cluster.stroke != null) string(cluster.stroke) else "#8a949e",
  stroke_width: float(if (cluster.stroke_width != null) cluster.stroke_width else 1.0),
  radius: float(if (cluster.radius != null) cluster.radius else 6.0),
  z: if (cluster.z != null) int(cluster.z) else -2,
  index: index
}

fn normalize_route_mode(raw, use_splines) {
  let value = lower(string(if (raw == null) "" else raw));
  if (contains(["none", "line", "polyline", "orthogonal", "curved"], value)) value
  // Missing route_mode must preserve callers of the legacy use_splines option.
  else if (use_splines == true) "curved"
  else "orthogonal"
}

fn normalize_ordering(raw) {
  let value = lower(string(if (raw == null) "" else raw));
  if (value == "in" or value == "out") value else null
}

fn normalize_graph(input, opts) {
  let nodes0 = if (input.nodes != null) input.nodes else [];
  let nodes = [for (i, node in nodes0) normalize_node(node, i)];
  let directed = if (input.directed != null) input.directed else true;
  let edges0 = if (input.edges != null) input.edges else [];
  let edges = [for (i, edge in edges0,
    let e = normalize_edge(edge, nodes, i, directed)
    where e != null) e];
  let clusters0 = if (input.clusters != null) input.clusters else [];
  let clusters = [for (i, cluster in clusters0) normalize_cluster(cluster, i)];
  let constraints = if (input.constraints != null) [
    for (constraint in input.constraints,
      member in if (constraint.members != null) constraint.members else [constraint.member]
      where member != null and member != "") {
      kind: if (constraint.kind != null) string(constraint.kind) else "rank",
      value: if (constraint.value != null) string(constraint.value) else "same",
      scope: if (constraint.scope != null) string(constraint.scope) else "",
      member: string(member)
    }
  ] else [];
  let use_splines = opt(opts, "use_splines", opt(input, "use_splines", false));
  {
    nodes: nodes,
    edges: edges,
    clusters: clusters,
    constraints: constraints,
    directed: directed,
    options: {
      algorithm: opt(opts, "algorithm", opt(input, "layout", "dagre")),
      direction: opt(opts, "direction", opt(input, "direction", "TB")),
      node_sep: float(opt(opts, "node_sep", opt(input, "node_sep", 60.0))),
      rank_sep: float(opt(opts, "rank_sep", opt(input, "rank_sep", 80.0))),
      edge_sep: float(opt(opts, "edge_sep", opt(input, "edge_sep", 10.0))),
      route_mode: normalize_route_mode(
        opt(opts, "route_mode", opt(input, "route_mode", null)), use_splines),
      ordering: normalize_ordering(opt(opts, "ordering", opt(input, "ordering", null))),
      new_rank: edge_bool(opt(opts, "new_rank", opt(input, "new_rank", false)), false),
      compound: edge_bool(opt(opts, "compound", opt(input, "compound", false)), false),
      use_splines: use_splines,
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
    else max([for (edge in preds)
      rank_of(edge.from, edges, [*stack, id]) + edge.min_length])
  }
}

fn same_scopes(id, constraints) => [for (constraint in constraints
  where constraint.kind == "rank" and constraint.value == "same" and
    constraint.member == id) constraint.scope]

fn same_rank_in_scopes(to, constraints, scopes) =>
  len([for (constraint in constraints where constraint.kind == "rank" and
    constraint.value == "same" and constraint.member == to and
    contains(scopes, constraint.scope)) constraint]) > 0

fn same_rank(from, to, constraints) =>
  same_rank_in_scopes(to, constraints, same_scopes(from, constraints))

fn has_rank_value(id, constraints, values) =>
  len([for (constraint in constraints where constraint.kind == "rank" and
    constraint.member == id and contains(values, constraint.value)) constraint]) > 0

fn constrained_rank(node, ranked, constraints) {
  let scopes = same_scopes(node.id, constraints);
  let peers = [for (entry in ranked, constraint in constraints
    where constraint.kind == "rank" and constraint.value == "same" and
      constraint.member == entry.id and contains(scopes, constraint.scope)) entry.rank];
  if (len(peers) > 0) max(peers) else node.rank
}

fn node_rank(nodes, id) =>
  first_or([for (node in nodes where node.id == id) node.rank], 0)

fn predecessor_rank(node, nodes, edges) {
  let floors = [for (edge in edges where edge.to == node.id)
    node_rank(nodes, edge.from) + edge.min_length];
  if (len(floors) > 0) max([node.rank, *floors]) else node.rank
}

fn unify_same_ranks(nodes, constraints) => [
  for (node in nodes) {*:node, rank: constrained_rank(node, nodes, constraints)}
]

fn relax_ranks(nodes, edges, constraints, remaining) {
  if (remaining <= 0) nodes
  else {
    let propagated = [for (node in nodes)
      {*:node, rank: predecessor_rank(node, nodes, edges)}];
    relax_ranks(unify_same_ranks(propagated, constraints), edges, constraints,
      remaining - 1)
  }
}

fn boundary_ranks(nodes, constraints) {
  let max_layer = max_rank(nodes);
  [for (node in nodes) {*:node,
    rank: if (has_rank_value(node.id, constraints, ["min", "source"])) 0
      else if (has_rank_value(node.id, constraints, ["max", "sink"])) max_layer
      else node.rank}]
}

fn node_cluster(nodes, id) =>
  first_or([for (node in nodes where node.id == id)
    if (node.group != null) string(node.group) else ""], "")

fn constraint_crosses_clusters(value, constraints, nodes) => len(unique([
  for (entry in constraints where entry.kind == "rank" and entry.scope == value.scope)
    node_cluster(nodes, entry.member)
])) > 1

fn rank_constraints(constraints, nodes, new_rank) =>
  if (new_rank) constraints
  else [for (value in constraints
    // Recursive DOT ranking cannot apply one rank set across structural clusters.
    where not constraint_crosses_clusters(value, constraints, nodes)) value]

fn assign_ranks(nodes, edges, constraints, new_rank) {
  let scoped_constraints = rank_constraints(constraints, nodes, new_rank);
  let active = [for (edge in edges
    where edge.constraint and not same_rank(edge.from, edge.to, scoped_constraints) and
      not has_rank_value(edge.to, scoped_constraints, ["min", "source"]) and
      not has_rank_value(edge.from, scoped_constraints, ["max", "sink"])) edge];
  let ranked = [for (node in nodes) {*:node, rank: rank_of(node.id, active, [])}];
  // Cycle back-edges and self-loops cannot constrain rank during promotion relaxation.
  let forward = [for (edge in active
    where node_rank(ranked, edge.from) < node_rank(ranked, edge.to)) edge];
  // Same-rank promotion must be propagated to successors or minlen can collapse to zero.
  boundary_ranks(relax_ranks(unify_same_ranks(ranked, scoped_constraints), forward,
    scoped_constraints, len(nodes)), scoped_constraints)
}

fn max_rank(nodes) {
  if (len(nodes) == 0) 0 else max([for (node in nodes) node.rank])
}

fn node_group_keys(node, clusters) {
  let structural = [for (cluster in reverse_items(cluster_chain(node.group, clusters, [])))
    "cluster:" ++ cluster.id];
  let leaf = if (node.order_group != null) "order:" ++ string(node.order_group)
    else "node:" ++ node.id;
  [*structural, leaf]
}

fn layer_with_order(rank, nodes, clusters) {
  let layer_nodes = [for (node in nodes where node.rank == rank) node];
  let scored = [for (i, node in layer_nodes)
    {node: node, score: float(i), group_keys: node_group_keys(node, clusters)}];
  let ordered = grouped_nodes_by_score(scored);
  {
    rank: rank,
    nodes: [for (i, node in ordered) {*:node, order: i}]
  }
}

fn create_layers(nodes, clusters) {
  let last_rank = max_rank(nodes);
  let result = [for (rank in 0 to last_rank) layer_with_order(rank, nodes, clusters)];
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
  let related = if (use_predecessors) incoming_edges(edges, node.id)
    else outgoing_edges(edges, node.id);
  let weights = [for (edge in related) max([0.0, edge.weight])];
  let total = sum(weights);
  if (len(related) == 0 or total <= 0.0) float(node.order)
  else sum([for (i, edge in related)
    float(order_of(ordered_nodes, if (use_predecessors) edge.from else edge.to)) * weights[i]]) /
    total
}

fn score_group_key(entry, depth) =>
  if (depth < len(entry.group_keys)) entry.group_keys[depth]
  else "node:" ++ entry.node.id

fn grouped_entries_by_score(scored, depth) {
  let grouped = [for (entry in scored group by score_group_key(entry, depth) into members) {
    score: min([for (member in members) member.score]),
    members: members
  }];
  [for (group in sort(grouped, (entry) => entry.score),
    member in if (len(group.members) > 1)
      grouped_entries_by_score(group.members, depth + 1)
      else group.members) member]
}

fn grouped_nodes_by_score(scored) {
  [for (entry in grouped_entries_by_score(scored, 0)) entry.node]
}

fn reorder_layer(layer, edges, ordered_nodes, use_predecessors, clusters) {
  let scored = [for (node in layer.nodes) {
    node: node,
    score: barycenter(node, edges, ordered_nodes, use_predecessors),
    // Cluster ancestry and authored groups are independent nesting levels.
    group_keys: node_group_keys(node, clusters)
  }];
  let sorted_nodes = grouped_nodes_by_score(scored);
  {
    rank: layer.rank,
    nodes: [for (i, node in sorted_nodes) {*:node, order: i}]
  }
}

fn reduce_crossings_once(layers, edges, clusters) {
  let ordered0 = all_layer_nodes(layers);
  let down = [for (layer in layers)
    if (layer.rank == 0) layer else reorder_layer(layer, edges, ordered0, true, clusters)];
  let ordered1 = all_layer_nodes(down);
  let result = [for (layer in down)
    if (layer.rank == 0) layer else reorder_layer(layer, edges, ordered1, false, clusters)];
  result
}

fn reduce_crossings(layers, edges, max_iterations, clusters) {
  if (max_iterations <= 0 or len(layers) < 2) layers
  else reduce_crossings(reduce_crossings_once(layers, edges, clusters), edges,
    max_iterations - 1, clusters)
}

fn ordering_mode(node, fallback) =>
  if (node.ordering != null) node.ordering else fallback

fn ordering_sequences(nodes, edges, fallback) => [
  for (node in nodes,
    let mode = ordering_mode(node, fallback),
    let ids = unique(if (mode == "out")
      [for (edge in edges where edge.from == node.id) edge.to]
      else if (mode == "in") [for (edge in edges where edge.to == node.id) edge.from]
      else [])
    where len(ids) > 1) ids
]

fn marked_before(nodes, ids, limit) => len([
  for (i, node in nodes where i < limit and contains(ids, node.id)) node
])

fn apply_order_sequence(nodes, ids) {
  let present = [for (id in ids where has_id(nodes, id)) id];
  let members = [for (id in present, node in nodes where node.id == id) node];
  if (len(members) < 2) nodes
  else [for (i, node in nodes)
    if (contains(present, node.id)) members[marked_before(nodes, present, i)] else node]
}

fn apply_order_sequences(nodes, sequences, i) {
  if (i >= len(sequences)) nodes
  else apply_order_sequences(apply_order_sequence(nodes, sequences[i]), sequences, i + 1)
}

fn enforce_ordering(layers, nodes, edges, fallback) {
  let sequences = ordering_sequences(nodes, edges, fallback);
  [for (layer in layers,
    let ordered = apply_order_sequences(layer.nodes, sequences, 0)) {
    rank: layer.rank,
    nodes: [for (i, node in ordered) {*:node, order: i}]
  }]
}

fn cluster_padding(clusters, id) {
  let matches = [for (cluster in clusters where cluster.id == id) cluster.padding];
  if (len(matches) > 0) matches[0] else 0.0
}

fn node_gap(left, right, node_sep, clusters) {
  if (left.group == right.group) node_sep
  else node_sep + cluster_padding(clusters, left.group) +
    cluster_padding(clusters, right.group)
}

fn layer_gaps(nodes, node_sep, clusters) =>
  if (len(nodes) < 2) [] else [
    for (i in 1 to (len(nodes) - 1)) node_gap(nodes[i - 1], nodes[i], node_sep, clusters)
  ]

fn layer_total_width(nodes, node_sep, clusters) {
  if (len(nodes) == 0) 0.0
  else sum([for (node in nodes) node.width]) + sum(layer_gaps(nodes, node_sep, clusters))
}

fn prefix_width(nodes, index, node_sep, clusters) {
  if (index <= 0) 0.0
  else sum([for (i, node in nodes where i < index) node.width]) +
    sum([for (i in 1 to index) node_gap(nodes[i - 1], nodes[i], node_sep, clusters)])
}

fn layer_height(layer) =>
  if (len(layer.nodes) == 0) 0.0 else max([for (node in layer.nodes) node.height])

fn rank_center(layers, index, rank_sep) =>
  sum([for (i, layer in layers where i < index) layer_height(layer)]) +
    float(index) * rank_sep + layer_height(layers[index]) / 2.0

fn position_layer(layer, node_sep, rank_y, clusters) {
  let total_width = layer_total_width(layer.nodes, node_sep, clusters);
  let result = [for (i, node in layer.nodes) {*:node,
    order: i,
    x: 0.0 - total_width / 2.0 + prefix_width(layer.nodes, i, node_sep, clusters) +
      node.width / 2.0,
    y: rank_y
  }];
  result
}

fn position_nodes(layers, opts, clusters) {
  // Rank separation is a box-to-box gap; center-only spacing makes wide LR
  // nodes touch and collapses clipped routes to a single point.
  let placed = [for (i, layer in layers,
    node in position_layer(layer, opts.node_sep,
      rank_center(layers, i, opts.rank_sep), clusters)) node];
  if (len(placed) == 0) { [] }
  else {
    let min_x = min([for (node in placed) node.x - node.width / 2.0]);
    let min_y = min([for (node in placed) node.y - node.height / 2.0]);
    let result = [for (node in placed) {*:node,
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
  {*:node,
    x: tx,
    y: ty
  }
}

fn normalize_oriented_nodes(nodes) {
  if (len(nodes) == 0) { [] }
  else {
    let min_x = min([for (node in nodes) node.x - node.width / 2.0]);
    let min_y = min([for (node in nodes) node.y - node.height / 2.0]);
    [for (node in nodes) {*:node,
      x: node.x - min_x,
      y: node.y - min_y
    }]
  }
}

fn orient_nodes(nodes, direction) {
  normalize_oriented_nodes([for (node in nodes) oriented_node(node, direction)])
}

fn find_node(nodes, id) => first_or([for (node in nodes where node.id == id) node], null)

fn find_cluster(clusters, id) =>
  first_or([for (cluster in clusters where cluster.id == id) cluster], null)

fn cluster_geometry(spec, specs, nodes, stack) {
  if (contains(stack, spec.id)) {
    {*:spec, x: 0.0, y: 0.0, width: 0.0, height: 0.0,
      label_x: 0.0, label_y: 0.0}
  }
  else {
    let member_nodes = [for (node in nodes where node.group == spec.id) node];
    let child_specs = [for (child in specs where child.parent == spec.id) child];
    let child_clusters = [for (child in child_specs)
      cluster_geometry(child, specs, nodes, [*stack, spec.id])];
    let lefts = [for (node in member_nodes) node.x - node.width / 2.0,
      *[for (child in child_clusters) child.x]];
    let tops = [for (node in member_nodes) node.y - node.height / 2.0,
      *[for (child in child_clusters) child.y]];
    let rights = [for (node in member_nodes) node.x + node.width / 2.0,
      *[for (child in child_clusters) child.x + child.width]];
    let bottoms = [for (node in member_nodes) node.y + node.height / 2.0,
      *[for (child in child_clusters) child.y + child.height]];
    let content_left = if (len(lefts) > 0) min(lefts) else 0.0;
    let content_top = if (len(tops) > 0) min(tops) else 0.0;
    let content_right = if (len(rights) > 0) max(rights) else content_left;
    let content_bottom = if (len(bottoms) > 0) max(bottoms) else content_top;
    let label_band = if (spec.label_height > 0.0) spec.label_height + spec.label_gap else 0.0;
    let content_width = content_right - content_left;
    let width = max([content_width + spec.padding * 2.0,
      spec.label_width + spec.padding * 2.0]);
    let center_x = (content_left + content_right) / 2.0;
    let x = center_x - width / 2.0;
    let y = content_top - spec.padding - label_band;
    {
      *:spec,
      x: x,
      y: y,
      width: width,
      height: content_bottom - content_top + spec.padding * 2.0 + label_band,
      label_x: x + (width - spec.label_width) / 2.0,
      label_y: y + spec.padding
    }
  }
}

fn compute_clusters(specs, nodes) {
  [for (spec in specs) cluster_geometry(spec, specs, nodes, [])]
}

fn cluster_chain(id, clusters, stack) {
  if (id == null or id == "" or contains(stack, id)) { [] }
  else {
    let cluster = find_cluster(clusters, id);
    if (cluster == null) { [] }
    else [cluster, *cluster_chain(cluster.parent, clusters, [*stack, id])]
  }
}

fn exclusive_clusters(chain, other_chain) => [
  for (cluster in chain
    where len([for (other in other_chain where other.id == cluster.id) other]) == 0) cluster
]

fn reverse_items_at(items, i) {
  if (i < 0) { [] } else [items[i], *reverse_items_at(items, i - 1)]
}

fn reverse_items(items) => reverse_items_at(items, len(items) - 1)

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

fn cross(a_x, a_y, b_x, b_y) => a_x * b_y - a_y * b_x

fn polygon_intersection(cx, cy, tx, ty, a, b) {
  let dx = tx - cx;
  let dy = ty - cy;
  let sx = b.x - a.x;
  let sy = b.y - a.y;
  let denominator = cross(dx, dy, sx, sy);
  if (abs(denominator) < 0.0001) null
  else {
    let ax = a.x - cx;
    let ay = a.y - cy;
    let ray_scale = cross(ax, ay, sx, sy) / denominator;
    let edge_scale = cross(ax, ay, dx, dy) / denominator;
    if (ray_scale >= 0.0 and edge_scale >= 0.0 and edge_scale <= 1.0) {
      {
        scale: ray_scale,
        x: cx + dx * ray_scale,
        y: cy + dy * ray_scale
      }
    } else null
  }
}

fn polygon_intersections(cx, cy, tx, ty, vertices) => [
  for (i, vertex in vertices,
    let next = vertices[(i + 1) % len(vertices)],
    let point = polygon_intersection(cx, cy, tx, ty, vertex, next)
    where point != null) point
]

fn clip_polygon(cx, cy, tx, ty, vertices) {
  let points = sort(polygon_intersections(cx, cy, tx, ty, vertices),
    (point) => point.scale);
  let point = if (len(points) > 0) points[0] else {x: cx, y: cy};
  {x: point.x, y: point.y}
}

fn shape_port(node) {
  let matches = [for (port in node.ports where port.side == "shape") port];
  if (len(matches) > 0) matches[0] else null
}

fn dimensions(width, height) => {width: width, height: height}

fn shape_size(node) {
  let port = shape_port(node);
  if (port == null) dimensions(node.width, node.height)
  else dimensions(node.width * port.x_offset, node.height * port.y_offset)
}

fn shape_vertices(node) {
  let size = shape_size(node);
  let width = size.width;
  let height = size.height;
  let left = node.x - width / 2.0;
  let right = node.x + width / 2.0;
  let top = node.y - height / 2.0;
  let bottom = node.y + height / 2.0;
  let quarter = width / 4.0;
  let vertices = if (node.polygon_sides != null) polygon.vertices(
    node.x, node.y, width, height, node.polygon_sides,
    node.polygon_orientation, node.polygon_skew, node.polygon_distortion)
  else if (node.shape == "hexagon") [
    {x: left + quarter, y: top}, {x: right - quarter, y: top},
    {x: right, y: node.y}, {x: right - quarter, y: bottom},
    {x: left + quarter, y: bottom}, {x: left, y: node.y}
  ]
  else if (node.shape == "octagon") [
    {x: left + quarter, y: top}, {x: right - quarter, y: top},
    {x: right, y: top + height / 4.0}, {x: right, y: bottom - height / 4.0},
    {x: right - quarter, y: bottom}, {x: left + quarter, y: bottom},
    {x: left, y: bottom - height / 4.0}, {x: left, y: top + height / 4.0}
  ]
  else if (node.shape == "house") [
    {x: node.x, y: top}, {x: right, y: node.y}, {x: right, y: bottom},
    {x: left, y: bottom}, {x: left, y: node.y}
  ]
  else if (node.shape == "invhouse") [
    {x: left, y: top}, {x: right, y: top}, {x: right, y: node.y},
    {x: node.x, y: bottom}, {x: left, y: node.y}
  ]
  else if (node.shape == "trapezoid") [
    {x: left + quarter, y: top}, {x: right - quarter, y: top},
    {x: right, y: bottom}, {x: left, y: bottom}
  ]
  else if (node.shape == "trapezoid-alt") [
    {x: left, y: top}, {x: right, y: top},
    {x: right - quarter, y: bottom}, {x: left + quarter, y: bottom}
  ]
  else if (node.shape == "asymmetric") [
    {x: left, y: top}, {x: right - quarter / 2.0, y: top},
    {x: right, y: node.y}, {x: right - quarter / 2.0, y: bottom},
    {x: left, y: bottom}, {x: left + quarter / 2.0, y: node.y}
  ]
  else if (node.shape == "asymmetric-left") [
    {x: left + quarter / 2.0, y: top}, {x: right, y: top},
    {x: right - quarter / 2.0, y: node.y}, {x: right, y: bottom},
    {x: left + quarter / 2.0, y: bottom}, {x: left, y: node.y}
  ]
  else if (node.shape == "lean-r") [
    {x: left + quarter / 2.0, y: top}, {x: right, y: top},
    {x: right - quarter / 2.0, y: bottom}, {x: left, y: bottom}
  ]
  else if (node.shape == "lean-l" or node.shape == "sl-rect") [
    {x: left, y: top}, {x: right - quarter / 2.0, y: top},
    {x: right, y: bottom}, {x: left + quarter / 2.0, y: bottom}
  ]
  else if (node.shape == "tri") [
    {x: node.x, y: top}, {x: right, y: bottom}, {x: left, y: bottom}
  ]
  else if (node.shape == "flip-tri") [
    {x: left, y: top}, {x: right, y: top}, {x: node.x, y: bottom}
  ]
  else if (node.shape == "hourglass") [
    {x: left, y: top}, {x: right, y: top}, {x: node.x + quarter / 2.0, y: node.y},
    {x: right, y: bottom}, {x: left, y: bottom}, {x: node.x - quarter / 2.0, y: node.y}
  ]
  else if (node.shape == "notch-rect" or node.shape == "card") [
    {x: left + quarter / 2.0, y: top}, {x: right, y: top}, {x: right, y: bottom},
    {x: left, y: bottom}, {x: left, y: top + height / 4.0}
  ]
  else if (node.shape == "notch-rect-right") [
    {x: left, y: top}, {x: right - quarter / 2.0, y: top},
    {x: right, y: top + height / 4.0}, {x: right, y: bottom}, {x: left, y: bottom}
  ]
  else if (node.shape == "notch-pent") [
    {x: left + quarter / 2.0, y: top}, {x: right, y: top},
    {x: right, y: node.y + height / 4.0}, {x: node.x, y: bottom},
    {x: left, y: node.y + height / 4.0}, {x: left, y: top + height / 4.0}
  ]
  else if (node.shape == "tag-rect" or node.shape == "tag-doc") [
    {x: left, y: top}, {x: right - quarter / 2.0, y: top},
    {x: right, y: node.y}, {x: right - quarter / 2.0, y: bottom}, {x: left, y: bottom}
  ]
  else if (node.shape == "tag-rect-left") [
    {x: left + quarter / 2.0, y: top}, {x: right, y: top}, {x: right, y: bottom},
    {x: left + quarter / 2.0, y: bottom}, {x: left, y: node.y}
  ]
  else if (node.shape == "bolt") [
    {x: node.x - quarter / 4.0, y: top}, {x: right, y: top},
    {x: node.x + quarter / 2.0, y: node.y - height / 12.0},
    {x: right - quarter / 2.0, y: node.y - height / 12.0},
    {x: node.x - quarter / 2.0, y: bottom},
    {x: node.x - quarter / 4.0, y: node.y + height / 12.0},
    {x: left + quarter / 2.0, y: node.y + height / 12.0}
  ]
  else [];
  vertices
}

fn normalized_port_side(side, node, target_x, target_y) {
  if (side == "top") "north"
  else if (side == "right") "east"
  else if (side == "bottom") "south"
  else if (side == "left") "west"
  else if (side != null and side != "auto" and side != "") string(side)
  else {
    let dx = target_x - node.x;
    let dy = target_y - node.y;
    if (abs(dx) >= abs(dy)) { if (dx >= 0.0) "east" else "west" }
    else if (dy >= 0.0) "south" else "north"
  }
}

fn find_port(node, port_id) {
  let matches = if (port_id == null or port_id == "") []
    else [for (port in node.ports
      where port.id != null and string(port.id) == port_id) port];
  if (len(matches) > 0) matches[0] else null
}

fn port_point(node, port, target_x, target_y) {
  let side = normalized_port_side(port.side, node, target_x, target_y);
  // Direct layout callers omit metadata defaults that semantic HTML materializes.
  let measured = if (contains(["north", "south"], side)) port.x_offset
    else port.y_offset;
  let offset = min([1.0, max([0.0, float(if (measured != null) measured
    else if (port.offset != null) port.offset else 0.5)])]);
  let left = node.x - node.width / 2.0;
  let top = node.y - node.height / 2.0;
  let point = if (side == "north") {x: left + node.width * offset, y: top}
  else if (side == "south") {x: left + node.width * offset, y: top + node.height}
  else if (side == "west") {x: left, y: top + node.height * offset}
  else {x: left + node.width, y: top + node.height * offset};
  point
}

fn clip_shape(node, target_x, target_y) {
  let size = shape_size(node);
  let half_w = size.width / 2.0;
  let half_h = size.height / 2.0;
  let vertices = shape_vertices(node);
  if (contains(["circle", "doublecircle", "ellipse", "f-circ", "stadium",
      "cloud", "delay", "h-cyl", "curv-trap"], node.shape))
    clip_ellipse(node.x, node.y, target_x, target_y, half_w, half_h)
  else if (node.shape == "diamond")
    clip_diamond(node.x, node.y, target_x, target_y, half_w, half_h)
  else if (len(vertices) > 0)
    clip_polygon(node.x, node.y, target_x, target_y, vertices)
  else clip_rect(node.x, node.y, target_x, target_y, half_w, half_h)
}

fn compass_point(node, compass, target_x, target_y) {
  let value = lower(string(compass));
  if (value == "c") { {x: node.x, y: node.y} }
  else if (value == "_") clip_shape(node, target_x, target_y)
  else {
    let dx = if (contains(["ne", "e", "se"], value)) 1.0
      else if (contains(["nw", "w", "sw"], value)) -1.0 else 0.0;
    let dy = if (contains(["sw", "s", "se"], value)) 1.0
      else if (contains(["nw", "n", "ne"], value)) -1.0 else 0.0;
    let size = shape_size(node);
    clip_shape(node, node.x + dx * size.width, node.y + dy * size.height)
  }
}

fn clip_node(node, target_x, target_y, port_id = null, compass = null) {
  let port = find_port(node, port_id);
  if (port != null) port_point(node, port, target_x, target_y)
  else if (compass != null and compass != "")
    compass_point(node, compass, target_x, target_y)
  else clip_shape(node, target_x, target_y)
}

fn points_close(a, b) => abs(a.x - b.x) < 0.001 and abs(a.y - b.y) < 0.001

fn points_collinear(a, b, c) =>
  (abs(a.x - b.x) < 0.001 and abs(b.x - c.x) < 0.001) or
  (abs(a.y - b.y) < 0.001 and abs(b.y - c.y) < 0.001)

fn append_route_point(points, point) {
  let count = len(points);
  if (count > 0 and points_close(points[count - 1], point)) points
  else if (count > 1 and points_collinear(points[count - 2], points[count - 1], point))
    [*slice(points, 0, count - 1), point]
  else [*points, point]
}

fn simplify_route_at(points, i, result) {
  if (i >= len(points)) result
  else simplify_route_at(points, i + 1, append_route_point(result, points[i]))
}

fn simplify_route(points) => simplify_route_at(points, 0, [])

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
      simplify_route([start, bend, finish])
    }
  }
}

fn orthogonal_waypoints_at(points, i, vertical_first, result) {
  if (i >= len(points)) result
  else {
    let segment = orthogonal_points([points[i - 1], points[i]], vertical_first);
    let appended = [*result, for (j, point in segment where j > 0) point];
    orthogonal_waypoints_at(points, i + 1, vertical_first, simplify_route(appended))
  }
}

fn orthogonal_waypoints(points, vertical_first) {
  if (len(points) < 2) points
  else orthogonal_waypoints_at(points, 1, vertical_first, [points[0]])
}

fn routing_rect(x, y, width, height, margin) =>
  {left: x - margin, top: y - margin, right: x + width + margin,
    bottom: y + height + margin}

fn point_inside_rect(point, rect) =>
  point.x > rect.left and point.x < rect.right and
  point.y > rect.top and point.y < rect.bottom

fn segment_hits_rect(start, finish, rect) =>
  if (point_inside_rect(start, rect) or point_inside_rect(finish, rect)) true
  else if (abs(start.x - finish.x) < 0.001)
    (start.x > rect.left and start.x < rect.right and
      max([start.y, finish.y]) > rect.top and min([start.y, finish.y]) < rect.bottom)
  else if (abs(start.y - finish.y) < 0.001)
    (start.y > rect.top and start.y < rect.bottom and
      max([start.x, finish.x]) > rect.left and min([start.x, finish.x]) < rect.right)
  else {
    let vertices = [
      {x: rect.left, y: rect.top}, {x: rect.right, y: rect.top},
      {x: rect.right, y: rect.bottom}, {x: rect.left, y: rect.bottom}
    ];
    let intersections = polygon_intersections(start.x, start.y,
      finish.x, finish.y, vertices);
    // Ray intersections beyond the finish point do not belong to this segment.
    any([for (point in intersections) point.scale <= 1.0])
  }

fn route_hits_obstacle_at(points, obstacles, i) {
  if (i >= len(points)) false
  else if (len([for (obstacle in obstacles
    where segment_hits_rect(points[i - 1], points[i], obstacle)) obstacle]) > 0) true
  else route_hits_obstacle_at(points, obstacles, i + 1)
}

fn route_hits_obstacle(points, obstacles) =>
  if (len(points) < 2) false else route_hits_obstacle_at(points, obstacles, 1)

fn route_manhattan_length(points) =>
  if (len(points) < 2) 0.0 else sum([for (i in 1 to (len(points) - 1))
    abs(points[i].x - points[i - 1].x) + abs(points[i].y - points[i - 1].y)])

fn obstacle_lane_routes(start, finish, obstacles, vertical_first, gap, orthogonal) {
  let default_route = if (orthogonal) orthogonal_points([start, finish], vertical_first)
    else [start, finish];
  let detours = if (vertical_first) [
    for (obstacle in obstacles, lane in [obstacle.left - gap, obstacle.right + gap])
      simplify_route([start, {x: lane, y: start.y}, {x: lane, y: finish.y}, finish])
  ] else [
    for (obstacle in obstacles, lane in [obstacle.top - gap, obstacle.bottom + gap])
      simplify_route([start, {x: start.x, y: lane}, {x: finish.x, y: lane}, finish])
  ];
  [default_route, *detours]
}

fn route_around_obstacles(start, finish, obstacles, vertical_first, gap, orthogonal) {
  let candidates = obstacle_lane_routes(start, finish, obstacles, vertical_first, gap,
    orthogonal);
  let clear = [for (candidate in candidates
    where not route_hits_obstacle(candidate, obstacles)) candidate];
  if (len(clear) == 0) candidates[0]
  else sort(clear, (candidate) => route_manhattan_length(candidate))[0]
}

fn avoid_route_segments_at(points, obstacles, vertical_first, gap, orthogonal, i, result) {
  if (i >= len(points)) simplify_route(result)
  else {
    let segment = route_around_obstacles(points[i - 1], points[i], obstacles,
      vertical_first, gap, orthogonal);
    let appended = [*result, for (j, point in segment where j > 0) point];
    avoid_route_segments_at(points, obstacles, vertical_first, gap, orthogonal,
      i + 1, appended)
  }
}

fn avoid_route_segments(points, obstacles, vertical_first, gap, orthogonal) =>
  if (len(points) < 2) points
  else avoid_route_segments_at(points, obstacles, vertical_first, gap, orthogonal,
    1, [points[0]])

fn cluster_boundary(cluster, source_x, source_y, target_x, target_y) {
  let vertices = [
    {x: cluster.x, y: cluster.y},
    {x: cluster.x + cluster.width, y: cluster.y},
    {x: cluster.x + cluster.width, y: cluster.y + cluster.height},
    {x: cluster.x, y: cluster.y + cluster.height}
  ];
  // Compound crossings follow the endpoint ray; the cluster center is unrelated to that route.
  clip_polygon(source_x, source_y, target_x, target_y, vertices)
}

fn chain_outside_endpoint(chain, endpoint_id) {
  let matches = [for (i, cluster in chain where cluster.id == endpoint_id) i];
  if (len(matches) == 0) chain
  else [for (i, cluster in chain where i > matches[0]) cluster]
}

fn compound_crossings(from_node, to_node, clusters, tail_cluster, head_cluster) {
  let from_chain = cluster_chain(from_node.group, clusters, []);
  let to_chain = cluster_chain(to_node.group, clusters, []);
  let source = exclusive_clusters(chain_outside_endpoint(from_chain, tail_cluster), to_chain);
  let target = reverse_items(exclusive_clusters(
    chain_outside_endpoint(to_chain, head_cluster), from_chain));
  {
    source: [for (cluster in source)
      cluster_boundary(cluster, from_node.x, from_node.y, to_node.x, to_node.y)],
    target: [for (cluster in target)
      cluster_boundary(cluster, to_node.x, to_node.y, from_node.x, from_node.y)]
  }
}

fn lane_waypoint(start, finish, offset, vertical_first) {
  let point = if (vertical_first) {
    x: (start.x + finish.x) / 2.0 + offset,
    y: (start.y + finish.y) / 2.0
  } else {
    x: (start.x + finish.x) / 2.0,
    y: (start.y + finish.y) / 2.0 + offset
  };
  point
}

fn self_loop_points(node, edge_sep, sibling_index, from_port = null, to_port = null,
    from_compass = null, to_compass = null) {
  let half_w = node.width / 2.0;
  let half_h = node.height / 2.0;
  let spread = max([10.0, half_h / 2.0]);
  // sibling rank keeps loop geometry independent of unrelated edge source order.
  let loop_gap = max([20.0, edge_sep * float(sibling_index + 2)]);
  // Named loop ports are authoritative; unported loops retain the historical anchors.
  let start = if (from_port == null and from_compass == null)
    {x: node.x + half_w, y: node.y - spread}
    else clip_node(node, node.x + half_w + loop_gap, node.y - spread,
      from_port, from_compass);
  let finish = if (to_port == null and to_compass == null)
    {x: node.x + half_w, y: node.y + spread}
    else clip_node(node, node.x + half_w + loop_gap, node.y + spread,
      to_port, to_compass);
  simplify_route([
    start,
    {x: node.x + half_w + loop_gap, y: start.y},
    {x: node.x + half_w + loop_gap, y: finish.y},
    finish
  ])
}

fn parallel_edges(edge, edges) => [
  for (candidate in edges
    where candidate.from == edge.from and candidate.to == edge.to) candidate
]

fn parallel_info(edge, edges) {
  let siblings = parallel_edges(edge, edges);
  let positions = [for (i, candidate in siblings where candidate.index == edge.index) i];
  {
    count: len(siblings),
    index: if (len(positions) > 0) positions[0] else 0
  }
}

fn parallel_route_points(from_node, to_node, offset, vertical_first,
    from_port = null, to_port = null, from_compass = null, to_compass = null) {
  // Lane reconstruction must retain authored ports instead of reverting to shape clipping.
  if (vertical_first) {
    let lane_x = (from_node.x + to_node.x) / 2.0 + offset;
    let start = clip_node(from_node, lane_x, to_node.y, from_port, from_compass);
    let finish = clip_node(to_node, lane_x, from_node.y, to_port, to_compass);
    simplify_route([
      start,
      {x: lane_x, y: start.y},
      {x: lane_x, y: finish.y},
      finish
    ])
  } else {
    let lane_y = (from_node.y + to_node.y) / 2.0 + offset;
    let start = clip_node(from_node, to_node.x, lane_y, from_port, from_compass);
    let finish = clip_node(to_node, from_node.x, lane_y, to_port, to_compass);
    simplify_route([
      start,
      {x: start.x, y: lane_y},
      {x: finish.x, y: lane_y},
      finish
    ])
  }
}

fn routing_obstacles(nodes, clusters, from_node, to_node, margin) {
  let endpoint_cluster_ids = [
    for (cluster in cluster_chain(from_node.group, clusters, [])) cluster.id,
    for (cluster in cluster_chain(to_node.group, clusters, [])) cluster.id
  ];
  [
    for (node in nodes where node.id != from_node.id and node.id != to_node.id)
      routing_rect(node.x - node.width / 2.0, node.y - node.height / 2.0,
        node.width, node.height, margin),
    for (cluster in clusters where not contains(endpoint_cluster_ids, cluster.id))
      routing_rect(cluster.x, cluster.y, cluster.width, cluster.height, margin)
  ]
}

fn route_edge(edge, nodes, clusters, edges, opts) {
  let from_node = find_node(nodes, edge.from);
  let to_node = find_node(nodes, edge.to);
  if (from_node == null or to_node == null) null
  else {
    let explicit_tail = if (opts.compound) find_cluster(clusters, edge.tail_cluster) else null;
    let explicit_head = if (opts.compound) find_cluster(clusters, edge.head_cluster) else null;
    // Explicit compound endpoints clip to cluster bounds, not the enclosed node bounds.
    let start = if (explicit_tail != null)
      cluster_boundary(explicit_tail, from_node.x, from_node.y, to_node.x, to_node.y)
      else clip_node(from_node, to_node.x, to_node.y, edge.from_port, edge.from_compass);
    let finish = if (explicit_head != null)
      cluster_boundary(explicit_head, to_node.x, to_node.y, from_node.x, from_node.y)
      else clip_node(to_node, from_node.x, from_node.y, edge.to_port, edge.to_compass);
    let vertical_first = not (opts.direction == "LR" or opts.direction == "RL");
    let parallel = parallel_info(edge, edges);
    let lane_offset = (float(parallel.index) - float(parallel.count - 1) / 2.0) * opts.edge_sep;
    let crossings = compound_crossings(from_node, to_node, clusters,
      if (explicit_tail != null) explicit_tail.id else null,
      if (explicit_head != null) explicit_head.id else null);
    let central_start = if (len(crossings.source) > 0)
      crossings.source[len(crossings.source) - 1] else start;
    let central_finish = if (len(crossings.target) > 0) crossings.target[0] else finish;
    let lane = lane_waypoint(central_start, central_finish, lane_offset, vertical_first);
    let lane_points = if (parallel.count > 1) [lane] else [];
    let compound_points = [start, *crossings.source,
      *lane_points,
      *crossings.target, finish];
    let has_compound = explicit_tail != null or explicit_head != null or
      len(crossings.source) > 0 or len(crossings.target) > 0;
    let obstacles = routing_obstacles(nodes, clusters, from_node, to_node,
      max([4.0, opts.edge_sep / 2.0]));
    let base_points = if (opts.route_mode == "none") []
      else if (edge.from == edge.to)
      self_loop_points(from_node, opts.edge_sep, parallel.index,
        edge.from_port, edge.to_port, edge.from_compass, edge.to_compass)
      else if (opts.route_mode == "line") [start, finish]
      else if (has_compound)
        if (opts.route_mode == "orthogonal")
          orthogonal_waypoints(compound_points, vertical_first)
        else simplify_route(compound_points)
      else if (parallel.count > 1)
        parallel_route_points(from_node, to_node, lane_offset, vertical_first,
          edge.from_port, edge.to_port, edge.from_compass, edge.to_compass)
      else if (opts.route_mode == "orthogonal")
        orthogonal_points([start, finish], vertical_first)
      else [start, finish];
    {
      id: edge.id,
      from: edge.from,
      to: edge.to,
      from_port: edge.from_port,
      to_port: edge.to_port,
      from_compass: edge.from_compass,
      to_compass: edge.to_compass,
      tail_cluster: edge.tail_cluster,
      head_cluster: edge.head_cluster,
      points: if (edge.from == edge.to or opts.route_mode == "line" or
          opts.route_mode == "none") base_points
        else avoid_route_segments(base_points, obstacles, vertical_first,
          max([4.0, opts.edge_sep / 2.0]), opts.route_mode == "orthogonal"),
      directed: edge.directed,
      arrow_start: edge.arrow_start,
      arrow_end: edge.arrow_end,
      marker_start: edge.marker_start,
      marker_end: edge.marker_end,
      // routed edges must retain marker scale after geometry reconstruction.
      arrow_size: edge.arrow_size,
      style: edge.style,
      stroke: edge.stroke,
      stroke_width: edge.stroke_width,
      opacity: edge.opacity,
      dash_array: edge.dash_array,
      min_length: edge.min_length,
      z: edge.z,
      index: edge.index,
      route_mode: opts.route_mode,
      is_bezier: opts.route_mode == "curved"
    }
  }
}

fn normalize_graph_geometry(nodes, edges, clusters) {
  if (len(nodes) == 0 and len(clusters) == 0) {
    {width: 0.0, height: 0.0, nodes: nodes, edges: edges, clusters: clusters}
  }
  else {
    let edge_points = [for (edge in edges, point in edge.points) point];
    // parallel lanes may extend before the node origin, so bounds and coordinates shift together.
    let min_x = min([for (node in nodes) node.x - node.width / 2.0,
      *[for (cluster in clusters) cluster.x],
      *[for (point in edge_points) point.x]]);
    let min_y = min([for (node in nodes) node.y - node.height / 2.0,
      *[for (cluster in clusters) cluster.y],
      *[for (point in edge_points) point.y]]);
    let max_x = max([for (node in nodes) node.x + node.width / 2.0,
      *[for (cluster in clusters) cluster.x + cluster.width],
      *[for (point in edge_points) point.x]]);
    let max_y = max([for (node in nodes) node.y + node.height / 2.0,
      *[for (cluster in clusters) cluster.y + cluster.height],
      *[for (point in edge_points) point.y]]);
    {
      width: max_x - min_x,
      height: max_y - min_y,
      nodes: [for (node in nodes) {*:node, x: node.x - min_x, y: node.y - min_y}],
      edges: [for (edge in edges) {*:edge, points: [for (point in edge.points) {
        x: point.x - min_x,
        y: point.y - min_y
      }]}],
      clusters: [for (cluster in clusters) {*:cluster,
        x: cluster.x - min_x,
        y: cluster.y - min_y,
        label_x: cluster.label_x - min_x,
        label_y: cluster.label_y - min_y
      }]
    }
  }
}

pub fn layout(input, opts = null) {
  let graph = normalize_graph(input, opts);
  let ranked = assign_ranks(graph.nodes, graph.edges, graph.constraints,
    graph.options.new_rank);
  let layers0 = create_layers(ranked, graph.clusters);
  let crossed = reduce_crossings(layers0, graph.edges, graph.options.max_iterations,
    graph.clusters);
  // Crossing reduction may reverse Graphviz's authored in/out edge sequence.
  let layers = enforce_ordering(crossed, ranked, graph.edges, graph.options.ordering);
  let canonical_nodes = position_nodes(layers, graph.options, graph.clusters);
  let routed_nodes = orient_nodes(canonical_nodes, graph.options.direction);
  let routed_clusters = compute_clusters(graph.clusters, routed_nodes);
  let routed_edges = [for (edge in graph.edges,
    let path = route_edge(edge, routed_nodes, routed_clusters, graph.edges, graph.options)
    where path != null) path];
  let geometry = normalize_graph_geometry(routed_nodes, routed_edges, routed_clusters);
  let nodes = geometry.nodes;
  let edges = geometry.edges;
  let clusters = geometry.clusters;
  {
    width: geometry.width,
    height: geometry.height,
    nodes: nodes,
    edges: edges,
    clusters: clusters,
    layers: layers,
    placements: [for (node in nodes) {
      index: node.index,
      id: node.id,
      x: node.x - node.width / 2.0,
      y: node.y - node.height / 2.0
    }],
    algorithm: graph.options.algorithm,
    direction: graph.options.direction,
    ordering: graph.options.ordering,
    new_rank: graph.options.new_rank,
    compound: graph.options.compound
  }
}
