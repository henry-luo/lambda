import polygon: .polygon
import route_geometry: .route_geometry

pub fn make_options() {
  let opts = {
    algorithm: "dagre",
    direction: "TB",
    node_sep: 60.0,
    rank_sep: 80.0,
    edge_sep: 10.0,
    loop_side: "right",
    use_splines: false,
    max_iterations: 8
  };
  opts
}

fn opt(opts, key, fallback) {
  if (opts != null and opts[key] != null) opts[key] else fallback
}

fn find_node_at(nodes, id, i, n) {
  if (i >= n) null
  else {
    let node = nodes[i];
    if (node.id == id) node else find_node_at(nodes, id, i + 1, n)
  }
}

fn find_node(nodes, id) => find_node_at(nodes, id, 0, len(nodes))

fn has_id(nodes, id) => find_node(nodes, id) != null

// Keep the first node for an ID, as the ordered node walk does.
fn nodes_by_id(nodes) => map([for (node in (reverse_items(nodes) ^ { [] })) (node.id, node)])

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
  let wd = if (node.width != null and not (node.width is error)) float(node.width)
    else if (node.wd != null and not (node.wd is error)) float(node.wd)
    else 80.0;
  let hg = if (node.height != null and not (node.height is error)) float(node.height)
    else if (node.hg != null and not (node.hg is error)) float(node.hg)
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

fn normalize_edge(edge, node_index, index, directed) any^ {
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
  if (node_index[from_id] != null and node_index[to_id] != null) {
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
      route_mode: if (edge.route_mode != null) normalize_route_mode(edge.route_mode, false)
        else if (edge["route-mode"] != null) normalize_route_mode(edge["route-mode"], false)
        else null,
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
  *:if (cluster.dash_array == null) {} else {dash_array:cluster.dash_array},
  radius: float(if (cluster.radius != null) cluster.radius else 6.0),
  z: if (cluster.z != null) int(cluster.z) else -2,
  index: index
}

fn normalize_route_mode(raw, use_splines) any^ {
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
  let node_index = nodes_by_id(nodes);
  let directed = if (input.directed != null) input.directed else true;
  let edges0 = if (input.edges != null) input.edges else [];
  let edges = [for (i, edge in edges0,
    let e = normalize_edge(edge, node_index, i, directed)
    where ((e != null) ^ { false })) e];
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
      marker_scale: float(opt(opts, "marker_scale", opt(input, "marker_scale", 1.0))),
      route_mode: normalize_route_mode(
        opt(opts, "route_mode", opt(input, "route_mode", null)), use_splines),
      loop_side: string(opt(opts, "loop_side", opt(input, "loop_side", "right"))),
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

fn rank_path_reaches(start: int, goal: int, heads: int[], links: int[], targets: int[]) bool^ {
  if (start == goal) { return true }
  if (heads[start] < 0) { return false }
  var visited: bool[] = [for (head in heads) false]
  var stack: int[] = [for (head in heads) 0]
  stack[0] = start
  visited[start] = true
  var count = 1
  while (count > 0) {
    count = count - 1
    var edge = heads[stack[count]]
    while (edge >= 0) {
      let target = targets[edge]
      if (target == goal) { return true }
      if (not visited[target]) {
        visited[target] = true
        stack[count] = target
        count = count + 1
      }
      edge = links[edge]
    }
  }
  return false
}

fn rank_dag(nodes, edges) map^ {
  let indices = map([for (i, node in nodes) (node.id, i)]);
  let sources: int[] = [for (edge in edges) indices[edge.from]]
  let targets: int[] = [for (edge in edges) indices[edge.to]]
  var heads: int[] = [for (node in nodes) -1]
  var links: int[] = [for (edge in edges) -2]
  var degrees: int[] = [for (node in nodes) 0]
  let edge_count = len(edges)^
  var i = 0
  while (i < edge_count) {
    let from = sources[i]
    let to = targets[i]
    // Source order still decides which edge closes a cycle.
    if (not rank_path_reaches(to, from, heads, links, targets)^) {
      links[i] = heads[from]
      heads[from] = i
      degrees[to] = degrees[to] + 1
    }
    i = i + 1
  }

  // Visit an accepted edge once, after its source rank is final.
  var ranks: int[] = [for (node in nodes) 0]
  var queue: int[] = [for (node in nodes) 0]
  var tail = 0
  let node_count = len(nodes)^
  i = 0
  while (i < node_count) {
    if (degrees[i] == 0) {
      queue[tail] = i
      tail = tail + 1
    }
    i = i + 1
  }
  var first = 0
  while (first < tail) {
    let from = queue[first]
    first = first + 1
    var edge = heads[from]
    while (edge >= 0) {
      let to = targets[edge]
      let rank = ranks[from] + edges[edge].min_length
      if ((rank > ranks[to])^) { ranks[to] = rank }
      degrees[to] = degrees[to] - 1
      if (degrees[to] == 0) {
        queue[tail] = to
        tail = tail + 1
      }
      edge = links[edge]
    }
  }
  return {
    nodes: [for (node in nodes, let rank = ranks[indices[node.id]])
      if (rank == node.rank) node else {*:node, rank:rank}],
    edges: [for (index, edge in edges where ((links[index] != -2) ^ { false })) edge]
  }
}

fn same_scopes(id, constraints) => [for (constraint in constraints
  where constraint.kind == "rank" and constraint.value == "same" and
    constraint.member == id) constraint.scope]

fn same_rank_in_scopes(to, constraints, scopes) =>
  len([for (constraint in constraints where ((constraint.kind == "rank" and
    constraint.value == "same" and constraint.member == to and
    contains(scopes, constraint.scope)) ^ { false })) constraint]) > 0

fn same_rank(from, to, constraints) =>
  // Invalid external constraints establish no rank relation, never a layout-wide error.
  same_rank_in_scopes(to, constraints, same_scopes(from, constraints)) or false

fn has_rank_value(id, constraints, values) =>
  len([for (constraint in constraints where ((constraint.kind == "rank" and
    constraint.member == id and contains(values, constraint.value)) ^ { false })) constraint]) > 0

fn constrained_rank(node, ranked, constraints) any^ {
  let scopes = same_scopes(node.id, constraints);
  if (len(scopes) == 0) node.rank
  else {
    // Only members of the node's same-rank scopes can promote it.
    let ids = [for (constraint in constraints
      where ((constraint.kind == "rank" and constraint.value == "same" and
        contains(scopes, constraint.scope)) ^ { false })) constraint.member];
    let peers = [for (entry in ranked where (contains(ids, entry.id) ^ { false })) entry.rank];
    if (len(peers) > 0) max(peers) else node.rank
  }
}

fn predecessor_rank(node, node_index, edges) {
  let floors = [for (edge in edges where edge.to == node.id)
    node_index[edge.from].rank + edge.min_length];
  if (len(floors) > 0) max([node.rank, *floors]) else node.rank
}

fn unify_same_ranks(nodes, constraints) => [
  for (node in nodes, let rank = constrained_rank(node, nodes, constraints))
    if (rank == node.rank) node else {*:node, rank: rank}
]

fn rank_values_equal(before, after) bool^ {
  let count = len(before)^
  var i = 0
  while (i < count) {
    if ((before[i].rank != after[i].rank)^) { return false }
    i = i + 1
  }
  return true
}

fn relax_ranks(nodes, edges, constraints, remaining) any^ {
  if (remaining <= 0) nodes
  else {
    let node_index = nodes_by_id(nodes);
    let propagated = [for (node in nodes, let rank = predecessor_rank(node, node_index, edges))
      if (rank == node.rank) node else {*:node, rank: rank}];
    let unified = unify_same_ranks(propagated, constraints);
    // Once ranks agree, later rounds have no new promotion to propagate.
    if (rank_values_equal(nodes, unified)) unified
    else relax_ranks(unified, edges, constraints, remaining - 1)
  }
}

fn boundary_ranks(nodes, constraints) {
  let max_layer = max_rank(nodes);
  [for (node in nodes,
    let rank = if (has_rank_value(node.id, constraints, ["min", "source"])) 0
      else if (has_rank_value(node.id, constraints, ["max", "sink"])) max_layer
      else node.rank)
    if (rank == node.rank) node else {*:node, rank:rank}]
}

fn node_cluster(nodes, id) =>
  first_or([for (node in nodes where node.id == id)
    if (node.group != null) string(node.group) else ""], "")

fn constraint_crosses_clusters(scope, constraints, nodes) => len(unique([
  for (entry in constraints where entry.kind == "rank" and entry.scope == scope)
    node_cluster(nodes, entry.member)
])) > 1

fn rank_constraints(constraints, nodes, new_rank) {
  if (new_rank or len(constraints) == 0) constraints
  else {
    // Recursive DOT ranking cannot apply one rank set across structural clusters.
    let scopes = unique([for (entry in constraints) entry.scope]);
    let excluded = [for (scope in scopes
      where (constraint_crosses_clusters(scope, constraints, nodes) ^ { true })) scope];
    [for (value in constraints where ((not contains(excluded, value.scope)) ^ { false })) value]
  }
}

fn cluster_rank_members(cluster_id, nodes, clusters, fallback) {
  if (cluster_id == null) [fallback]
  else {
    let members = [for (node in nodes,
      let ancestors = cluster_chain(node.group, clusters, []),
      let ids = [for (cluster in ((ancestors) ^ { [] })) cluster.id]
      where ((contains(ids, cluster_id)) ^ { false })) node.id];
    if (len(members) == 0) [fallback] else members
  }
}

fn compound_rank_edges(edges, nodes, clusters) => [
  for (edge in [for (candidate in edges
      where ((candidate.tail_cluster != null or candidate.head_cluster != null) ^ { false })) candidate],
    from_id in ((cluster_rank_members(edge.tail_cluster, nodes, clusters, edge.from)) ^ { [] }),
    to_id in ((cluster_rank_members(edge.head_cluster, nodes, clusters, edge.to)) ^ { [] })
    where ((from_id != to_id and (from_id != edge.from or to_id != edge.to)) ^ { false }))
    {*:edge, from:from_id, to:to_id}
]

fn assign_ranks(nodes, edges, clusters, constraints, new_rank, compound) any^ {
  let scoped_constraints = rank_constraints(constraints, nodes, new_rank);
  let constrained = len(scoped_constraints) > 0;
  // Expand each cluster relationship in source order before removing rank cycles.
  let candidates = if (compound) [for (edge in edges,
    candidate in [edge, *compound_rank_edges([edge], nodes, clusters)]) candidate] else edges;
  let active = [for (edge in candidates
    where ((edge.constraint and (not constrained or (not same_rank(edge.from, edge.to, scoped_constraints) and
      not has_rank_value(edge.to, scoped_constraints, ["min", "source"]) and
      not has_rank_value(edge.from, scoped_constraints, ["max", "sink"])))) ^ { false })) edge];
  let ranked = rank_dag(nodes, active)^;
  let same = [for (entry in scoped_constraints
    where ((entry.kind == "rank" and entry.value == "same") ^ { false })) entry];
  // Longest paths already satisfy minlen until a same-rank constraint promotes a node.
  let promoted = if (len(same) == 0) ranked.nodes else
    relax_ranks(unify_same_ranks(ranked.nodes, same), ranked.edges, same, len(nodes));
  if (constrained) boundary_ranks(promoted, scoped_constraints) else promoted
}

fn max_rank(nodes) {
  if (len(nodes) == 0) 0 else max([for (node in nodes) node.rank])
}

fn node_group_keys(node, clusters) {
  let structural = [for (cluster in ((reverse_items(cluster_chain(node.group, clusters, []))) ^ { [] }))
    "cluster:" ++ cluster.id];
  let leaf = if (node.order_group != null) "order:" ++ string(node.order_group)
    else "node:" ++ node.id;
  [*structural, leaf]
}

fn layer_with_order(rank, nodes, clusters, grouped) {
  let layer_nodes = [for (node in nodes where node.rank == rank) node];
  // Singleton groups preserve input order at creation and need no grouping pass.
  let ordered = if (grouped and len(layer_nodes) > 1) grouped_nodes_by_score([
    for (i, node in layer_nodes) {node: node, score: float(i),
      group_keys: node_group_keys(node, clusters)}]) else layer_nodes;
  {
    rank: rank,
    nodes: [for (i, node in ordered) if (node.order == i) node else {*:node, order: i}]
  }
}

fn create_layers(nodes, clusters, grouped) {
  let last_rank = max_rank(nodes);
  let result = [for (rank in 0 to last_rank) layer_with_order(rank, nodes, clusters, grouped)];
  result
}

fn all_layer_nodes(layers) {
  let result = [for (layer in layers, node in layer.nodes) node];
  result
}

fn order_of(nodes, id) {
  first_or([for (node in nodes where node.id == id) node.order], 0)
}

fn neighbor_order(edge, ordered_nodes, clusters, use_predecessors, compound) any^ {
  let id = if (use_predecessors) edge.from else edge.to;
  let cluster_id = if (not compound) null
    else if (use_predecessors) edge.tail_cluster else edge.head_cluster;
  if (cluster_id == null) float(order_of(ordered_nodes, id))
  else {
    let peer = find_node(ordered_nodes, id);
    let members = cluster_rank_members(cluster_id, ordered_nodes, clusters, id);
    let orders = [for (member in ordered_nodes
      where ((peer != null and member.rank == peer.rank and contains(members, member.id)) ^ { false }))
      float(member.order)];
    // The cluster boundary is centered on the members, not on its routing anchor.
    if (len(orders) == 0) float(order_of(ordered_nodes, id))
    else (min(orders) + max(orders)) / 2.0
  }
}

fn barycenter(node, edges, ordered_nodes, use_predecessors, clusters, compound) any^ {
  let related = if (use_predecessors) incoming_edges(edges, node.id)
    else outgoing_edges(edges, node.id);
  let weights = [for (edge in related) max([0.0, edge.weight])];
  let total = sum(weights);
  if (len(related) == 0 or total <= 0.0) float(node.order)
  else sum([for (i, edge in related)
    neighbor_order(edge, ordered_nodes, clusters, use_predecessors, compound) * weights[i]]) /
    total
}

fn score_group_key(entry, depth) any^ =>
  if (depth < len(entry.group_keys)) entry.group_keys[depth]
  else "node:" ++ entry.node.id

fn scored_group(members) {
  {
    score: min([for (member in members) member.score]),
    members: members
  }
}

fn grouped_entries_by_score(scored, depth) any^ {
  // A computed group key needs an explicit name; `members` is the group's
  // array of entries (S14.1.1v2).
  let grouped = [for (entry in scored group by score_group_key(entry, depth)^ as score_key into members)
    scored_group(members)];
  [for (group in ((sort(grouped, (entry) => entry.score)) ^ { [] }),
    member in ((if (len(group.members) > 1)
      grouped_entries_by_score(group.members, depth + 1)
      else group.members) ^ { [] })) member]
}

fn grouped_nodes_by_score(scored) {
  [for (entry in ((grouped_entries_by_score(scored, 0)) ^ { [] })) entry.node]
}

fn reorder_layer(layer, edges, ordered_nodes, use_predecessors, clusters, compound, grouped) any^ {
  if (len(layer.nodes)^ < 2) { return layer }
  let scored = [for (node in layer.nodes) {
    node: node,
    score: barycenter(node, edges, ordered_nodes, use_predecessors, clusters, compound),
    // Cluster ancestry and authored groups are independent nesting levels.
    group_keys: if (grouped) node_group_keys(node, clusters) else null
  }];
  let sorted_nodes = if (grouped) grouped_nodes_by_score(scored) else [
    for (entry in sort(scored, (entry) => entry.score)^) entry.node];
  {
    rank: layer.rank,
    nodes: [for (i, node in sorted_nodes) if (node.order == i) node else {*:node, order: i}]
  }
}

fn reduce_crossings_once(layers, edges, clusters, compound, grouped) {
  let ordered0 = all_layer_nodes(layers);
  let down = [for (layer in layers)
    if (layer.rank == 0) layer else reorder_layer(layer, edges, ordered0, true, clusters, compound, grouped)];
  let ordered1 = all_layer_nodes(down);
  let result = [for (layer in down)
    if (layer.rank == 0) layer else reorder_layer(layer, edges, ordered1, false, clusters, compound, grouped)];
  result
}

fn reduce_crossings(layers, edges, max_iterations, clusters, compound, grouped) any^ {
  if (max_iterations <= 0 or len(layers) < 2) layers
  else {
    let next = reduce_crossings_once(layers, edges, clusters, compound, grouped);
    // A full down/up sweep with the same order has reached a fixed point.
    if (next == layers) next
    else reduce_crossings(next, edges, max_iterations - 1, clusters, compound, grouped)
  }
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
    where ((len(ids) > 1) ^ { false })) ids
]

fn marked_before(nodes, ids, limit) => len([
  for (i, node in nodes where ((i < limit and contains(ids, node.id)) ^ { false })) node
])

fn apply_order_sequence(nodes, ids) {
  let present = [for (id in ids where ((has_id(nodes, id)) ^ { false })) id];
  let members = [for (id in present, node in nodes where node.id == id) node];
  if (len(members) < 2) nodes
  else [for (i, node in nodes)
    if (contains(present, node.id)) members[marked_before(nodes, present, i)] else node]
}

fn apply_order_sequences(nodes, sequences, i) any^ {
  if (i >= len(sequences)) nodes
  else apply_order_sequences(apply_order_sequence(nodes, sequences[i]), sequences, i + 1)
}

fn enforce_ordering(layers, nodes, edges, fallback) {
  let sequences = ordering_sequences(nodes, edges, fallback);
  [for (layer in layers,
    let ordered = apply_order_sequences(layer.nodes, sequences, 0)) {
    rank: layer.rank,
    nodes: [for (i, node in ordered) if (node.order == i) node else {*:node, order: i}]
  }]
}

fn cluster_padding(clusters, id) {
  let matches = [for (cluster in clusters where cluster.id == id) cluster.padding];
  if (len(matches) > 0) matches[0] else 0.0
}

fn node_gap(left, right, node_sep, clusters) any^ {
  if (left.group == right.group) node_sep
  else node_sep + cluster_padding(clusters, left.group) +
    cluster_padding(clusters, right.group)
}

fn horizontal_rank_direction(direction) => direction == "LR" or direction == "RL"

fn cross_axis_size(node, direction) =>
  if (horizontal_rank_direction(direction)) node.height else node.width

fn rank_axis_size(node, direction) =>
  if (horizontal_rank_direction(direction)) node.width else node.height

fn layer_gaps(nodes, node_sep, clusters) =>
  if (len(nodes) < 2) [] else [
    for (i in 1 to (len(nodes) - 1)) node_gap(nodes[i - 1], nodes[i], node_sep, clusters)
  ]

fn layer_rank_size(layer, direction) any^ =>
  if (len(layer.nodes) == 0) 0.0
  else max([for (node in layer.nodes) rank_axis_size(node, direction)])

fn position_layer(layer, node_sep, rank_y, clusters, direction) any^ {
  let sizes = [for (node in layer.nodes) cross_axis_size(node, direction)];
  let gaps = layer_gaps(layer.nodes, node_sep, clusters);
  let total_width = sum(sizes) + sum(gaps);
  // Keep separate size and gap sums so each offset uses the same addition order.
  let widths_before = math.cumsum([0.0, *sizes]);
  let gaps_before = math.cumsum([0.0, *gaps]);
  let result = [for (i, node in layer.nodes) {*:node,
    order: i,
    x: 0.0 - total_width / 2.0 + (widths_before[i] + gaps_before[i]) + sizes[i] / 2.0,
    y: rank_y
  }];
  result
}

fn position_nodes(layers, opts, clusters) {
  // Rank separation follows the oriented rank axis so wide LR nodes retain
  // their requested box-to-box gap for routed edge labels.
  let rank_sizes = [for (layer in layers) layer_rank_size(layer, opts.direction)];
  let ranks_before = math.cumsum([0.0, *rank_sizes]);
  let placed = [for (i, layer in layers,
    node in ((position_layer(layer, opts.node_sep,
      ranks_before[i] + float(i) * opts.rank_sep + rank_sizes[i] / 2.0, clusters,
      opts.direction)) ^ { [] })) node];
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

fn feedback_adjusted_nodes(nodes, edges, opts) {
  let node_index = nodes_by_id(nodes);
  [
  for (node in nodes,
    // Find peers only for this node's edges, before testing the rank relation.
    let targets = [for (edge in (outgoing_edges(edges, node.id) ^ { [] }),
      let target = node_index[edge.to]
      where ((target != null and target.rank < node.rank) ^ { false }))
      target],
    let peers = if (len(targets) == 0) [] else [for (peer in nodes where peer.rank == node.rank) peer],
    let span = if (len(targets) > 0)
      max([for (target in targets) node.rank - target.rank]) else 0,
    let offset = min([opts.node_sep / 2.0, opts.edge_sep * 3.0]))
    // singleton feedback ranks can move without changing their layer order.
    if (len(peers) == 1 and span > 0) {*:node,
      x: node.x + (if (span > 1) 0.0 - offset else offset)}
    else node
  ]
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
    if (min_x == 0.0 and min_y == 0.0) nodes else [for (node in nodes) {*:node,
      x: node.x - min_x,
      y: node.y - min_y
    }]
  }
}

fn orient_nodes(nodes, direction) any^ {
  normalize_oriented_nodes(if (direction == "TB") nodes
    else [for (node in nodes) oriented_node(node, direction)])
}

fn find_cluster(clusters, id) =>
  first_or([for (cluster in clusters where cluster.id == id) cluster], null)

fn cluster_geometry(spec, specs, nodes, loops, stack) any^ {
  if (contains(stack, spec.id)) {
    {*:spec, x: 0.0, y: 0.0, width: 0.0, height: 0.0,
      label_x: 0.0, label_y: 0.0}
  }
  else {
    let member_nodes = [for (node in nodes where node.group == spec.id) node];
    // Internal loops must fit inside their owning cluster and its ancestors.
    let member_loops = [for (loop in loops, let owner = find_node(nodes, loop.node)
      where ((owner != null and owner.group == spec.id) ^ { false })) loop.rect];
    let child_specs = [for (child in specs where child.parent == spec.id) child];
    let child_clusters = [for (child in child_specs)
      cluster_geometry(child, specs, nodes, loops, [*stack, spec.id])];
    let lefts = [for (node in member_nodes) node.x - node.width / 2.0,
      *[for (loop in member_loops) loop.left],
      *[for (child in child_clusters) child.x]];
    let tops = [for (node in member_nodes) node.y - node.height / 2.0,
      *[for (loop in member_loops) loop.top],
      *[for (child in child_clusters) child.y]];
    let rights = [for (node in member_nodes) node.x + node.width / 2.0,
      *[for (loop in member_loops) loop.right],
      *[for (child in child_clusters) child.x + child.width]];
    let bottoms = [for (node in member_nodes) node.y + node.height / 2.0,
      *[for (loop in member_loops) loop.bottom],
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

fn compute_clusters(specs, nodes, loops) {
  [for (spec in specs) cluster_geometry(spec, specs, nodes, loops, [])]
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

fn reverse_items(items) =>
  // Route normalization can discard an unusable dynamic item list without aborting layout.
  reverse(items) ^ { [] }

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

fn intersection_scale(dx, dy, ax, ay, sx, sy) {
  let denominator = cross(dx, dy, sx, sy);
  if (abs(denominator) < 0.0001) null
  else {
    let ray_scale = cross(ax, ay, sx, sy) / denominator;
    let edge_scale = cross(ax, ay, dx, dy) / denominator;
    if (ray_scale >= 0.0 and edge_scale >= 0.0 and edge_scale <= 1.0) ray_scale else null
  }
}

fn polygon_intersection(cx, cy, tx, ty, a, b) any^ {
  let dx = tx - cx;
  let dy = ty - cy;
  let scale = intersection_scale(dx, dy, a.x - cx, a.y - cy, b.x - a.x, b.y - a.y);
  if (scale == null) null else {scale: scale, x: cx + dx * scale, y: cy + dy * scale}
}

fn polygon_intersections(cx, cy, tx, ty, vertices) => [
  for (i, vertex in vertices,
    let next = vertices[(i + 1) % len(vertices)],
    let point = polygon_intersection(cx, cy, tx, ty, vertex, next)
    where ((point != null) ^ { false })) point
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

fn shape_size(node) any^ {
  let port = shape_port(node);
  if (port == null) dimensions(node.width, node.height)
  else dimensions(node.width * port.x_offset, node.height * port.y_offset)
}

fn shape_vertices(node) any^ {
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

fn find_port(node, port_id) any^ {
  let matches = if (port_id == null or port_id == "") []
    else [for (port in node.ports
      where ((port.id != null and string(port.id) == port_id) ^ { false })) port];
  if (len(matches) > 0) matches[0] else null
}

fn exposed_port_side(port, node, target_x, target_y) any^ {
  let fallback = normalized_port_side(port.side, node, target_x, target_y);
  if (port.side != null and port.side != "" and port.side != "auto" or
      port.left_offset == null or port.top_offset == null or
      port.right_offset == null or port.bottom_offset == null) fallback
  else {
    let tolerance_x = max([1.5, node.width * 0.02]) / max([1.0, node.width]);
    let tolerance_y = max([1.5, node.height * 0.02]) / max([1.0, node.height]);
    let cx = node.x - node.width / 2.0 + node.width * port.x_offset;
    let cy = node.y - node.height / 2.0 + node.height * port.y_offset;
    let possible = [
      if (port.top_offset <= tolerance_y) {side:"north", x:cx, y:node.y - node.height / 2.0},
      if (port.bottom_offset >= 1.0 - tolerance_y) {side:"south", x:cx, y:node.y + node.height / 2.0},
      if (port.left_offset <= tolerance_x) {side:"west", x:node.x - node.width / 2.0, y:cy},
      if (port.right_offset >= 1.0 - tolerance_x) {side:"east", x:node.x + node.width / 2.0, y:cy}
    ];
    let candidates = [for (point in possible where point != null) point];
    // A named record field can expose only part of the node perimeter.
    if (len(candidates) == 0) fallback
    else sort(candidates, (point) => (point.x - target_x) ** 2.0 + (point.y - target_y) ** 2.0)[0].side
  }
}

fn port_point(node, port, target_x, target_y) any^ {
  let side = exposed_port_side(port, node, target_x, target_y);
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

fn port_escape(node, port_id, peer, point, gap) any^ {
  let port = find_port(node, port_id);
  if (port == null) point else {
    let side = exposed_port_side(port, node, peer.x, peer.y);
    if (side == "north") {x:point.x, y:point.y - gap}
    else if (side == "south") {x:point.x, y:point.y + gap}
    else if (side == "west") {x:point.x - gap, y:point.y}
    else {x:point.x + gap, y:point.y}
  }
}

fn clip_shape(node, target_x, target_y) any^ {
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

fn compass_point(node, compass, target_x, target_y) any^ {
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

fn clip_node(node, target_x, target_y, port_id = null, compass = null) any^ {
  let port = find_port(node, port_id);
  if (port != null) port_point(node, port, target_x, target_y)
  else if (compass != null and compass != "")
    compass_point(node, compass, target_x, target_y)
  else clip_shape(node, target_x, target_y)
}

fn automatic_endpoint(port_id, compass) =>
  (port_id == null or port_id == "") and (compass == null or compass == "")

fn rank_axis_target(node, peer, vertical_rank_axis) =>
  if (vertical_rank_axis) {x: node.x, y: peer.y}
  else {x: peer.x, y: node.y}

fn points_close(a, b) => abs(a.x - b.x) < 0.001 and abs(a.y - b.y) < 0.001

fn points_collinear(a, b, c) =>
  (abs(a.x - b.x) < 0.001 and abs(b.x - c.x) < 0.001) or
  (abs(a.y - b.y) < 0.001 and abs(b.y - c.y) < 0.001)

fn append_route_point(points, point) any^ {
  let count = len(points);
  if (count > 0 and points_close(points[count - 1], point)) points
  else if (count > 1 and points_collinear(points[count - 2], points[count - 1], point))
    [*slice(points, 0, count - 1), point]
  else [*points, point]
}

fn simplify_route_at(points, i, result) any^ {
  if (i >= len(points)) result
  else simplify_route_at(points, i + 1, append_route_point(result, points[i]))
}

fn simplify_route(points) any^ =>
  // Keep the original route when a dynamic point cannot be normalized.
  simplify_route_at(points, 0, []) or points

fn orthogonal_points(points, vertical_rank_axis) any^ {
  if (len(points) < 2) points
  else {
    let start = points[0];
    let finish = points[len(points) - 1];
    let dx = abs(finish.x - start.x);
    let dy = abs(finish.y - start.y);
    if (dx < 1.0 or dy < 1.0) points
    else {
      // Keep both terminal segments on the rank axis so markers face their nodes.
      let middle = if (vertical_rank_axis) (start.y + finish.y) / 2.0
        else (start.x + finish.x) / 2.0;
      simplify_route([start,
        if (vertical_rank_axis) {x:start.x, y:middle} else {x:middle, y:start.y},
        if (vertical_rank_axis) {x:finish.x, y:middle} else {x:middle, y:finish.y}, finish])
    }
  }
}

fn orthogonal_waypoints_at(points, i, vertical_rank_axis, result) any^ {
  if (i >= len(points)) result
  else {
    let segment = orthogonal_points([points[i - 1], points[i]], vertical_rank_axis);
    let appended = [*result, for (j, point in segment where j > 0) point];
    orthogonal_waypoints_at(points, i + 1, vertical_rank_axis, simplify_route(appended))
  }
}

fn orthogonal_waypoints(points, vertical_rank_axis) any^ {
  if (len(points) < 2) points
  else orthogonal_waypoints_at(points, 1, vertical_rank_axis, [points[0]])
}

fn routing_rect(x, y, width, height, margin) =>
  {left: x - margin, top: y - margin, right: x + width + margin,
    bottom: y + height + margin}

fn point_inside_rect(point, rect) =>
  point.x > rect.left and point.x < rect.right and
  point.y > rect.top and point.y < rect.bottom

fn hits_rect_side(dx, dy, x, y, side_x, side_y) any^ {
  let scale = intersection_scale(dx, dy, x, y, side_x, side_y);
  scale != null and scale <= 1.0
}

fn segment_hits_rect(x, y, end_x, end_y, dx, dy, rect) any^ {
  let left = rect.left;
  let right = rect.right;
  let top = rect.top;
  let bottom = rect.bottom;
  let x_inside = x > left and x < right;
  let y_inside = y > top and y < bottom;
  if ((x_inside and y_inside) or
      (end_x > left and end_x < right and end_y > top and end_y < bottom)) true
  else if (abs(dx) < 0.001)
    (x_inside and (y > top or end_y > top) and (y < bottom or end_y < bottom))
  else if (abs(dy) < 0.001)
    (y_inside and (x > left or end_x > left) and (x < right or end_x < right))
  else if ((x < left and end_x < left) or (x > right and end_x > right) or
      (y < top and end_y < top) or (y > bottom and end_y > bottom)) false
  else {
    // Share polygon clipping's intersection test without allocating the four vertices.
    let width = right - left;
    let height = bottom - top;
    hits_rect_side(dx, dy, left - x, top - y, width, 0.0) or
      hits_rect_side(dx, dy, right - x, top - y, 0.0, height) or
      hits_rect_side(dx, dy, right - x, bottom - y, -width, 0.0) or
      hits_rect_side(dx, dy, left - x, bottom - y, 0.0, -height)
  }
}

fn route_hits_obstacle(points, obstacles) any^ {
  let count = len(points)
  let obstacle_count = len(obstacles)
  var i = 1
  while (i < count) {
    let start = points[i - 1]
    let finish = points[i]
    let x = start.x, y = start.y, end_x = finish.x, end_y = finish.y
    let dx = end_x - x, dy = end_y - y
    var j = 0
    while (j < obstacle_count) {
      if ((segment_hits_rect(x, y, end_x, end_y, dx, dy, obstacles[j]) ^ { false })) { return true }
      j = j + 1
    }
    i = i + 1
  }
  return false
}

fn route_manhattan_length(points) =>
  if (len(points) < 2) 0.0 else sum([for (i in 1 to (len(points) - 1))
    abs(points[i].x - points[i - 1].x) + abs(points[i].y - points[i - 1].y)])

fn obstacle_lane_routes(start, finish, obstacles, vertical_rank_axis, gap) {
  if (vertical_rank_axis) [
    for (obstacle in obstacles, lane in [obstacle.left - gap, obstacle.right + gap])
      simplify_route([start, {x: lane, y: start.y}, {x: lane, y: finish.y}, finish])
  ] else [
    for (obstacle in obstacles, lane in [obstacle.top - gap, obstacle.bottom + gap])
      simplify_route([start, {x: start.x, y: lane}, {x: finish.x, y: lane}, finish])
  ]
}

fn curved_obstacle_routes(start, finish, obstacles, gap) any^ {
  let dx = finish.x - start.x;
  let dy = finish.y - start.y;
  let length2 = dx * dx + dy * dy;
  if (length2 < 0.001) [] else [
    for (obstacle in [for (box in obstacles
        where (segment_hits_rect(start.x, start.y, finish.x, finish.y, dx, dy, box) ^ { false })) box],
      x in [obstacle.left - gap, obstacle.right + gap],
      y in [obstacle.top - gap, obstacle.bottom + gap],
      let t = max([0.05, min([0.95, ((x - start.x) * dx + (y - start.y) * dy) / length2])]),
      let control = {x:(x - (1.0 - t) ** 2.0 * start.x - t ** 2.0 * finish.x) / (2.0 * t * (1.0 - t)),
        y:(y - (1.0 - t) ** 2.0 * start.y - t ** 2.0 * finish.y) / (2.0 * t * (1.0 - t))},
      let along = (control.x - start.x) * dx + (control.y - start.y) * dy,
      let points = [start, control, finish],
      let samples = (route_geometry.sample_points({route_mode:"curved", points:points})) ^ { [] }
      // Permit a control point beyond the target to clear an obstacle near the source,
      // while bounding its excursion by the endpoint span on each axis.
      where ((along > 0.0 and along < length2 and
        control.x >= min([start.x, finish.x]) - abs(dx) and
        control.x <= max([start.x, finish.x]) + abs(dx) and
        control.y >= min([start.y, finish.y]) - abs(dy) and
        control.y <= max([start.y, finish.y]) + abs(dy) and
        not route_hits_obstacle(samples, obstacles)) ^ { false }))
      {points:points, length:sum([for (i in 1 to (len(samples) - 1))
        math.sqrt((samples[i].x - samples[i - 1].x) ** 2.0 +
          (samples[i].y - samples[i - 1].y) ** 2.0)])}
  ]
}

fn shortest_clear_lane(candidates, obstacles, fallback) any^ {
  // Stable length order keeps the same tie break and stops at the first clear lane (S6.2).
  let ordered = sort(candidates, (candidate) => route_manhattan_length(candidate))^
  let count = len(ordered)^
  var i = 0
  while (i < count) {
    let candidate = ordered[i]
    if (not route_hits_obstacle(candidate, obstacles)^) { return candidate }
    i = i + 1
  }
  return fallback
}

fn route_around_obstacles(start, finish, obstacles, vertical_rank_axis, gap, orthogonal, curved) any^ {
  // A clear direct route is already shortest; floating-point ties must not select a detour.
  let direct = if (orthogonal) orthogonal_points([start, finish], vertical_rank_axis)
    else [start, finish];
  if (not route_hits_obstacle(direct, obstacles)) direct
  else {
    // Fit a quadratic through a clear obstacle corner before falling back to lanes.
    let curves = if (curved) curved_obstacle_routes(start, finish, obstacles, gap) else [];
    if (len(curves) > 0) sort(curves, (curve) => curve.length)[0].points
    else shortest_clear_lane(obstacle_lane_routes(start, finish, obstacles, vertical_rank_axis, gap),
      obstacles, direct)
  }
}

fn avoid_route_segments_at(points, obstacles, vertical_rank_axis, gap, orthogonal, curved, i, result) any^ {
  if (i >= len(points)) simplify_route(result)
  else {
    let segment = route_around_obstacles(points[i - 1], points[i], obstacles,
      vertical_rank_axis, gap, orthogonal, curved);
    let appended = [*result, for (j, point in segment where j > 0) point];
    avoid_route_segments_at(points, obstacles, vertical_rank_axis, gap, orthogonal, curved,
      i + 1, appended)
  }
}

fn avoid_route_segments(points, obstacles, vertical_rank_axis, gap, orthogonal, curved) any^ =>
  if (len(points) < 2) points
  else avoid_route_segments_at(points, obstacles, vertical_rank_axis, gap, orthogonal, curved,
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

fn clip_lane_to_cluster(points, cluster, fallback) any^ {
  if (cluster == null or len(points) < 2) { return points }
  let bounds = routing_rect(cluster.x, cluster.y, cluster.width, cluster.height, 0.0);
  // A feedback lane may spend several segments inside the cluster before exiting.
  let outside = [for (i, point in points where ((i > 0 and not point_inside_rect(point, bounds)^) ^ { false })) i];
  let at = outside[0] or 1;
  let a = points[at - 1];
  let b = points[at];
  let boundary = if (len(outside) > 0) cluster_boundary(cluster, a.x, a.y, b.x, b.y)
    else fallback;
  simplify_route([boundary, for (i, point in points where i >= at) point])
}

fn chain_outside_endpoint(chain, endpoint_id) {
  let matches = [for (i, cluster in chain where cluster.id == endpoint_id) i];
  if (len(matches) == 0) chain
  else [for (i, cluster in chain where i > matches[0]) cluster]
}

fn compound_crossings(from_node, to_node, from_chain, to_chain, tail_cluster, head_cluster) {
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

fn lane_waypoint(start, finish, offset, vertical_rank_axis) {
  let point = if (vertical_rank_axis) {
    x: (start.x + finish.x) / 2.0 + offset,
    y: (start.y + finish.y) / 2.0
  } else {
    x: (start.x + finish.x) / 2.0,
    y: (start.y + finish.y) / 2.0 + offset
  };
  point
}

fn self_loop_points(node, edge_sep, sibling_index, top_loop,
    from_port = null, to_port = null,
    from_compass = null, to_compass = null) any^ {
  let half_w = node.width / 2.0;
  let half_h = node.height / 2.0;
  let spread = max([10.0, (if (top_loop) half_w else half_h) / 2.0]);
  // sibling rank keeps loop geometry independent of unrelated edge source order.
  let loop_gap = max([20.0,
    edge_sep * float(sibling_index + (if (top_loop) 1 else 2))]);
  let outer_x = node.x + half_w + loop_gap;
  let outer_y = node.y - half_h - loop_gap;
  // on DOT LR graphs, loops use the open cross-axis side; named ports still control anchors.
  let start = if (from_port == null and from_compass == null)
    if (not top_loop) {x: node.x + half_w, y: node.y - spread}
    else {x: node.x - spread, y: node.y - half_h}
    else clip_node(node,
      if (not top_loop) outer_x else node.x - spread,
      if (not top_loop) node.y - spread else outer_y,
      from_port, from_compass);
  let finish = if (to_port == null and to_compass == null)
    if (not top_loop) {x: node.x + half_w, y: node.y + spread}
    else {x: node.x + spread, y: node.y - half_h}
    else clip_node(node,
      if (not top_loop) outer_x else node.x + spread,
      if (not top_loop) node.y + spread else outer_y,
      to_port, to_compass);
  simplify_route([
    start,
    if (not top_loop) {x: outer_x, y: start.y}
      else {x: start.x, y: outer_y},
    if (not top_loop) {x: outer_x, y: finish.y}
      else {x: finish.x, y: outer_y},
    finish
  ])
}

fn route_cross_center(node, vertical_rank_axis) =>
  if (vertical_rank_axis) node.x else node.y

fn route_cross_half(node, vertical_rank_axis) =>
  (if (vertical_rank_axis) node.width else node.height) / 2.0

fn feedback_lane_offset(edge, from_node, to_node, nodes, edges,
    edge_sep, vertical_rank_axis, obstacles, parallel_offset) any^ {
  let span = from_node.rank - to_node.rank;
  let upper_side = span > 1;
  let from_axis = route_cross_center(from_node, vertical_rank_axis);
  let to_axis = route_cross_center(to_node, vertical_rank_axis);
  let corridor_half = max([route_cross_half(from_node, vertical_rank_axis),
    route_cross_half(to_node, vertical_rank_axis)]) + edge_sep;
  // other nodes in an endpoint rank matter only when their boxes meet this edge's corridor.
  let spanned = [for (node in nodes,
    let axis = route_cross_center(node, vertical_rank_axis),
    let half = route_cross_half(node, vertical_rank_axis)
    where ((node.rank >= to_node.rank and node.rank <= from_node.rank and
      axis + half >= min([from_axis, to_axis]) - corridor_half and
      axis - half <= max([from_axis, to_axis]) + corridor_half) ^ { false })) node];
  let outgoing = [for (candidate in edges
    where candidate.from == edge.from and candidate.to != edge.to) candidate];
  let nearer = [for (candidate in outgoing,
    let target = find_node(nodes, candidate.to)
    where ((target != null and target.rank > to_node.rank and
      target.rank < from_node.rank) ^ { false })) candidate];
  let clearance = edge_sep * float(len(nearer) + 2);
  let middle = (from_axis + to_axis) / 2.0;
  let low = min([for (node in spanned)
    route_cross_center(node, vertical_rank_axis) -
      route_cross_half(node, vertical_rank_axis)]) - clearance - middle + parallel_offset;
  let high = max([for (node in spanned)
    route_cross_center(node, vertical_rank_axis) +
      route_cross_half(node, vertical_rank_axis)]) + clearance - middle + parallel_offset;
  let low_route = lane_route_points(from_node, to_node, low, vertical_rank_axis,
    edge.from_port, edge.to_port, edge.from_compass, edge.to_compass, true)^;
  let high_route = lane_route_points(from_node, to_node, high, vertical_rank_axis,
    edge.from_port, edge.to_port, edge.from_compass, edge.to_compass, true)^;
  let low_clear = not route_hits_obstacle(low_route, obstacles);
  let high_clear = not route_hits_obstacle(high_route, obstacles);
  // Prefer a clear lane, then the shorter one; keep the established side on ties.
  if (low_clear != high_clear) if (low_clear) low else high
  else if (low_clear and route_manhattan_length(low_route) != route_manhattan_length(high_route))
    if (route_manhattan_length(low_route) < route_manhattan_length(high_route)) low else high
  else if (upper_side) low else high
}

fn parallel_edges(edge, edges) => [
  for (candidate in edges
    where candidate.from == edge.from and candidate.to == edge.to) candidate
]

fn marker_gap(siblings) => max([0.0, for (sibling in siblings,
  let width = if (sibling.stroke_width == null) route_geometry.default_edge_width else sibling.stroke_width)
  if (sibling.arrow_start or sibling.arrow_end)
    (route_geometry.marker_height * sibling.arrow_size + 2.0) * width else 0.0])

fn parallel_info(edge, edges) {
  let siblings = parallel_edges(edge, edges);
  let positions = [for (i, candidate in siblings where candidate.index == edge.index) i];
  {
    count: len(siblings),
    index: if (len(positions) > 0) positions[0] else 0,
    marker_gap: marker_gap(siblings)
  }
}

fn lane_route_points(from_node, to_node, offset, vertical_rank_axis,
    from_port = null, to_port = null, from_compass = null, to_compass = null,
    side_endpoints = false) any^ {
  // Lane reconstruction must retain authored ports instead of reverting to shape clipping.
  // outside feedback lanes clip along their terminal segments, not toward the peer center.
  if (vertical_rank_axis) {
    let lane_x = (from_node.x + to_node.x) / 2.0 + offset;
    let start = clip_node(from_node, lane_x,
      if (side_endpoints) from_node.y else to_node.y, from_port, from_compass);
    let finish = clip_node(to_node, lane_x,
      if (side_endpoints) to_node.y else from_node.y, to_port, to_compass);
    simplify_route([
      start,
      {x: lane_x, y: start.y},
      {x: lane_x, y: finish.y},
      finish
    ])
  } else {
    let lane_y = (from_node.y + to_node.y) / 2.0 + offset;
    let start = clip_node(from_node,
      if (side_endpoints) from_node.x else to_node.x, lane_y, from_port, from_compass);
    let finish = clip_node(to_node,
      if (side_endpoints) to_node.x else from_node.x, lane_y, to_port, to_compass);
    simplify_route([
      start,
      {x: start.x, y: lane_y},
      {x: finish.x, y: lane_y},
      finish
    ])
  }
}

// Intersect a rank-parallel line with the shape so parallel arrows meet its
// rank-facing edge at separate positions and have a rank-aligned tangent.
fn rank_parallel_endpoint(node, peer, offset, vertical) any^ {
  let size = shape_size(node);
  let cross_half = (if (vertical) size.width else size.height) / 2.0;
  let rank_half = (if (vertical) size.height else size.width) / 2.0;
  let cross_offset = max([-cross_half * 0.8, min([cross_half * 0.8, offset])]);
  let rank_sign = if ((if (vertical) peer.y - node.y else peer.x - node.x) >= 0.0) 1.0 else -1.0;
  let origin = if (vertical) {x:node.x + cross_offset, y:node.y}
    else {x:node.x, y:node.y + cross_offset};
  let vertices = shape_vertices(node);
  let rank_distance = if (cross_half <= 0.0) rank_half
    else if (contains(["circle", "doublecircle", "ellipse", "f-circ", "stadium",
      "cloud", "delay", "h-cyl", "curv-trap"], node.shape))
      rank_half * math.sqrt(max([0.0, 1.0 - (cross_offset / cross_half) ** 2.0]))
    else if (node.shape == "diamond") rank_half * (1.0 - abs(cross_offset) / cross_half)
    else rank_half;
  if (len(vertices) > 0 and node.shape != "diamond")
    clip_polygon(origin.x, origin.y,
      if (vertical) origin.x else origin.x + rank_sign * size.width,
      if (vertical) origin.y + rank_sign * size.height else origin.y, vertices)
  else if (vertical) {x:origin.x, y:node.y + rank_sign * rank_distance}
  else {x:node.x + rank_sign * rank_distance, y:origin.y}
}

fn marker_fan_offset(edge, node, peer, node_index, edges, at_start, edge_sep, marker_scale, vertical) any^ {
  // Only marked siblings on this endpoint can enter the fan.
  let candidates = [for (candidate in edges where ((if (at_start)
    candidate.from == edge.from and candidate.arrow_start and
      automatic_endpoint(candidate.from_port, candidate.from_compass)
    else candidate.to == edge.to and candidate.arrow_end and
      automatic_endpoint(candidate.to_port, candidate.to_compass)) ^ { false })) candidate];
  // Long forward edges can share a target with adjacent-rank edges.
  let siblings = [for (candidate in candidates,
    let other = node_index[if (at_start) candidate.to else candidate.from]
    where ((other != null and (if (at_start) other.rank > node.rank
      else other.rank < node.rank)) ^ { false }))
    {edge:candidate, peer:other}];
  if (len(siblings) < 2) null
  else {
    let axis = route_cross_center(peer, vertical);
    let preceding = [for (sibling in siblings,
      let other = route_cross_center(sibling.peer, vertical)
      where ((other < axis or (other == axis and sibling.edge.index < edge.index)) ^ { false })) sibling];
    let available = route_cross_half(node, vertical) * 1.6;
    let spacing = min([max([edge_sep, marker_gap([for (sibling in siblings) sibling.edge]) * marker_scale]),
      available / float(len(siblings) - 1)]);
    (float(len(preceding)) - float(len(siblings) - 1) / 2.0) * spacing
  }
}

fn parallel_rank_route(from_node, to_node, parallel, edge_sep, marker_scale, vertical) any^ {
  let available = min([route_cross_half(from_node, vertical), route_cross_half(to_node, vertical)]) * 1.6;
  let spacing = min([max([edge_sep, parallel.marker_gap * marker_scale]),
    available / float(max([1, parallel.count - 1]))]);
  let offset = (float(parallel.index) - float(parallel.count - 1) / 2.0) * spacing;
  let start = rank_parallel_endpoint(from_node, to_node, offset, vertical);
  let finish = rank_parallel_endpoint(to_node, from_node, offset, vertical);
  let first_rank = if (vertical) start.y else start.x;
  let last_rank = if (vertical) finish.y else finish.x;
  let middle_rank = (first_rank + last_rank) / 2.0;
  // Separated endpoints already distinguish siblings; an extra lane creates needless bulges.
  simplify_route([start,
    if (vertical) {x:start.x, y:middle_rank} else {x:middle_rank, y:start.y},
    if (vertical) {x:finish.x, y:middle_rank} else {x:middle_rank, y:finish.y}, finish])
}

fn curved_lane_terminals(points, vertical, feedback) any^ {
  if (len(points) != 4) points
  else {
    let start = points[0];
    let first = points[1];
    let penultimate = points[2];
    let finish = points[3];
    let rank_span = if (vertical) finish.y - start.y else finish.x - start.x;
    let first_cross = if (vertical) abs(first.x - start.x) else abs(first.y - start.y);
    let last_cross = if (vertical) abs(penultimate.x - finish.x) else abs(penultimate.y - finish.y);
    // Spread the terminal turn over the lane, keeping the arrow's tangent
    // aligned with the visible approach instead of bending at the marker.
    let sign = if (rank_span >= 0.0) 1.0 else -1.0;
    let first_offset = if (feedback and first_cross > 0.001) rank_span / 3.0
      else min([abs(rank_span) / 3.0, first_cross * 0.75]) * sign;
    let last_offset = if (feedback and last_cross > 0.001) rank_span / 3.0
      else min([abs(rank_span) / 3.0, last_cross * 0.75]) * sign;
    let lane_shape = if (vertical) abs(first.y - start.y) < 0.001 and
      abs(penultimate.y - finish.y) < 0.001 and abs(first.x - penultimate.x) < 0.001
      else abs(first.x - start.x) < 0.001 and abs(penultimate.x - finish.x) < 0.001 and
        abs(first.y - penultimate.y) < 0.001;
    // Only change side-lane terminals; other four-point routes may encode obstacle detours.
    if (not lane_shape) points else [start,
      if (vertical) {x:first.x, y:start.y + first_offset} else {x:start.x + first_offset, y:first.y},
      if (vertical) {x:penultimate.x, y:finish.y - last_offset} else {x:finish.x - last_offset, y:penultimate.y},
      finish]
  }
}


// The loop bounds depend on their own edge and node. Compute them once per layout.
fn loop_obstacles(nodes, edges, opts, margin) {
  let loops = [for (edge in edges where edge.from == edge.to) edge];
  let top_loop = horizontal_rank_direction(opts.direction) and opts.loop_side == "top";
  [for (edge in loops,
    let node = find_node(nodes, edge.from),
    let points = if (node == null) [] else self_loop_points(node, opts.edge_sep,
      parallel_info(edge, loops).index, top_loop,
      edge.from_port, edge.to_port, edge.from_compass, edge.to_compass)
    where ((node != null and len(points) > 0) ^ { false })) {
    let left = min([for (point in points) point.x]);
    let top = min([for (point in points) point.y]);
    {node: node.id, rect: routing_rect(left, top,
      max([for (point in points) point.x]) - left,
      max([for (point in points) point.y]) - top, margin)}
  }]
}

fn node_routing_rect(node, margin) any^ =>
  routing_rect(node.x - node.width / 2.0, node.y - node.height / 2.0,
    node.width, node.height, margin)

fn routing_obstacles(obstacle_boxes, from_id, to_id, from_chain, to_chain) {
  [
    for (entry in obstacle_boxes.nodes where entry.node != from_id and entry.node != to_id) entry.rect,
    for (entry in obstacle_boxes.clusters
      where ((not contains(from_chain, entry.id) and not contains(to_chain, entry.id)) ^ { false })) entry.rect
  ]
}

fn route_edge(edge, nodes, node_index, clusters, edges, opts, obstacle_boxes, obstacle_margin) any^ {
  let from_node = node_index[edge.from];
  let to_node = node_index[edge.to];
  if (from_node == null or to_node == null) null
  else {
    let route_mode = if (edge.route_mode != null) edge.route_mode else opts.route_mode;
    let explicit_tail = if (opts.compound) find_cluster(clusters, edge.tail_cluster) else null;
    let explicit_head = if (opts.compound) find_cluster(clusters, edge.head_cluster) else null;
    let tail_center = if (explicit_tail == null) {x:from_node.x, y:from_node.y}
      else {x:explicit_tail.x + explicit_tail.width / 2.0, y:explicit_tail.y + explicit_tail.height / 2.0};
    let head_center = if (explicit_head == null) {x:to_node.x, y:to_node.y}
      else {x:explicit_head.x + explicit_head.width / 2.0, y:explicit_head.y + explicit_head.height / 2.0};
    let vertical_rank_axis = not (opts.direction == "LR" or opts.direction == "RL");
    let top_loop = not vertical_rank_axis and opts.loop_side == "top";
    let from_target = if (route_mode == "orthogonal" and explicit_tail == null and
        automatic_endpoint(edge.from_port, edge.from_compass))
      rank_axis_target(from_node, head_center, vertical_rank_axis)
      else head_center;
    let to_target = if (route_mode == "orthogonal" and explicit_head == null and
        automatic_endpoint(edge.to_port, edge.to_compass))
      rank_axis_target(to_node, tail_center, vertical_rank_axis)
      else tail_center;
    // Distinct marked relationships need distinct attachment points on a shared node.
    let start_fan = if (route_mode != "none" and explicit_tail == null and
        edge.arrow_start and to_node.rank > from_node.rank and
        automatic_endpoint(edge.from_port, edge.from_compass))
      marker_fan_offset(edge, from_node, to_node, node_index, edges, true,
        opts.edge_sep, opts.marker_scale, vertical_rank_axis)
      else null;
    let end_fan = if (route_mode != "none" and explicit_head == null and
        edge.arrow_end and to_node.rank > from_node.rank and
        automatic_endpoint(edge.to_port, edge.to_compass))
      marker_fan_offset(edge, to_node, from_node, node_index, edges, false,
        opts.edge_sep, opts.marker_scale, vertical_rank_axis)
      else null;
    // Explicit compound endpoints clip to cluster bounds, not the enclosed node bounds.
    let start = if (explicit_tail != null)
      cluster_boundary(explicit_tail, tail_center.x, tail_center.y, head_center.x, head_center.y)
      else if (start_fan != null) rank_parallel_endpoint(from_node, to_node, start_fan, vertical_rank_axis)
      else clip_node(from_node, from_target.x, from_target.y,
        edge.from_port, edge.from_compass);
    let finish = if (explicit_head != null)
      cluster_boundary(explicit_head, head_center.x, head_center.y, tail_center.x, tail_center.y)
      else if (end_fan != null) rank_parallel_endpoint(to_node, from_node, end_fan, vertical_rank_axis)
      else clip_node(to_node, to_target.x, to_target.y, edge.to_port, edge.to_compass);
    let parallel = parallel_info(edge, edges);
    let lane_offset = (float(parallel.index) - float(parallel.count - 1) / 2.0) * opts.edge_sep;
    let backtracks_rank = from_node.rank >= to_node.rank;
    let from_chain = cluster_chain(from_node.group, clusters, []);
    let to_chain = cluster_chain(to_node.group, clusters, []);
    let avoid_boxes = edge.from != edge.to and route_mode != "none" and route_mode != "line";
    let other_obstacles = if (avoid_boxes) routing_obstacles(obstacle_boxes, edge.from, edge.to,
      [for (cluster in (from_chain ^ { [] })) cluster.id],
      [for (cluster in (to_chain ^ { [] })) cluster.id]) else [];
    // Named field ports may face away from the peer; keep their routes outside the node.
    let obstacles = if (avoid_boxes) [*other_obstacles,
      for (endpoint in [if (edge.from_port != null) from_node, if (edge.to_port != null) to_node]
        where ((endpoint != null) ^ { false }))
        node_routing_rect(endpoint, 0.0)] else [];
    // feedback lanes clear boxes in the traversed corridor and separate return fanout.
    let back_lane_offset = if (backtracks_rank and avoid_boxes)
      feedback_lane_offset(edge, from_node, to_node,
        nodes, edges, opts.edge_sep, vertical_rank_axis, obstacles, lane_offset)
      else 0.0;
    let crossings = compound_crossings(from_node, to_node, from_chain, to_chain,
      if (explicit_tail != null) explicit_tail.id else null,
      if (explicit_head != null) explicit_head.id else null);
    let central_start = if (len(crossings.source) > 0)
      crossings.source[len(crossings.source) - 1] else start;
    let central_finish = if (len(crossings.target) > 0) crossings.target[0] else finish;
    let lane = lane_waypoint(central_start, central_finish, lane_offset, vertical_rank_axis);
    let lane_points = if (parallel.count > 1) [lane] else [];
    let compound_points = [start, *crossings.source,
      *lane_points,
      *crossings.target, finish];
    let has_compound = explicit_tail != null or explicit_head != null or
      len(crossings.source) > 0 or len(crossings.target) > 0;
    let base_points = if (route_mode == "none") []
      else if (edge.from == edge.to)
      self_loop_points(from_node, opts.edge_sep, parallel.index,
        top_loop,
        edge.from_port, edge.to_port, edge.from_compass, edge.to_compass)
      else if (route_mode == "line") [start, finish]
      // A feedback edge needs its own lane: a direct route would paint over the
      // forward edge that established the rank relation.
      else if (backtracks_rank)
        // a clear long return can connect directly; short returns need a lane
        // to stay distinct from their forward partner.
        if (from_node.rank - to_node.rank > 1 and
            not route_hits_obstacle([start, finish], obstacles)) [start, finish]
        else {
          let lane_route = lane_route_points(from_node, to_node, back_lane_offset,
            vertical_rank_axis, edge.from_port, edge.to_port, edge.from_compass, edge.to_compass, true);
          let clipped_tail = clip_lane_to_cluster(lane_route, explicit_tail, start);
          if (explicit_head == null) clipped_tail else
            reverse_items(clip_lane_to_cluster(reverse_items(clipped_tail), explicit_head, finish))
        }
      else if (has_compound)
        if (route_mode == "orthogonal")
          orthogonal_waypoints(compound_points, vertical_rank_axis)
        else simplify_route(compound_points)
      else if (parallel.count > 1)
        if (automatic_endpoint(edge.from_port, edge.from_compass) and
            automatic_endpoint(edge.to_port, edge.to_compass))
          parallel_rank_route(from_node, to_node, parallel, opts.edge_sep, opts.marker_scale, vertical_rank_axis)
        else lane_route_points(from_node, to_node, lane_offset, vertical_rank_axis,
          edge.from_port, edge.to_port, edge.from_compass, edge.to_compass)
      else if (route_mode == "orthogonal")
        orthogonal_points([start, finish], vertical_rank_axis)
      else [start, finish];
    let avoided = if (edge.from == edge.to or route_mode == "line" or route_mode == "none")
      base_points else avoid_route_segments(base_points, obstacles, vertical_rank_axis,
        max([4.0, opts.edge_sep / 2.0]), route_mode == "orthogonal", route_mode == "curved" and len(base_points) == 2);
    // A lane around an intermediate node must leave and enter through the
    // endpoint sides, rather than running along their rank-facing borders.
    let can_use_side_lane = route_mode == "orthogonal" and not backtracks_rank and
      to_node.rank - from_node.rank > 1 and not has_compound and parallel.count == 1 and
      automatic_endpoint(edge.from_port, edge.from_compass) and
      automatic_endpoint(edge.to_port, edge.to_compass);
    // Probe centered endpoints only when this edge can use the bypass.
    let centered_base = if (can_use_side_lane) orthogonal_points([
      clip_node(from_node, from_target.x, from_target.y),
      clip_node(to_node, to_target.x, to_target.y)], vertical_rank_axis) else [];
    let centered_avoided = if (len(centered_base) == 2)
      avoid_route_segments(centered_base, obstacles, vertical_rank_axis,
        obstacle_margin, true, false) else [];
    let side_lane = len(centered_base) == 2 and
      len(centered_avoided) == 4 and route_hits_obstacle(centered_base, obstacles);
    let lane_cross = if (side_lane)
      if (vertical_rank_axis) centered_avoided[1].x else centered_avoided[1].y else 0.0;
    let lane_middle = (route_cross_center(from_node, vertical_rank_axis) +
      route_cross_center(to_node, vertical_rank_axis)) / 2.0;
    let lane_clearance = max([4.0, opts.edge_sep / 2.0]);
    // The obstacle lane can lie inside a wider endpoint; move it past both
    // endpoint boxes before clipping to their sides.
    let side_cross = if (not side_lane) 0.0 else if (lane_cross < lane_middle) min([lane_cross,
      route_cross_center(from_node, vertical_rank_axis) - route_cross_half(from_node, vertical_rank_axis) - lane_clearance,
      route_cross_center(to_node, vertical_rank_axis) - route_cross_half(to_node, vertical_rank_axis) - lane_clearance])
      else max([lane_cross,
        route_cross_center(from_node, vertical_rank_axis) + route_cross_half(from_node, vertical_rank_axis) + lane_clearance,
        route_cross_center(to_node, vertical_rank_axis) + route_cross_half(to_node, vertical_rank_axis) + lane_clearance]);
    let side_base = if (side_lane) lane_route_points(from_node, to_node,
      side_cross - lane_middle,
      vertical_rank_axis, null, null, null, null, true) else [];
    // A bypass meets the cross-axis side, so spread its markers along that side.
    let side_start = if (side_lane and start_fan != null)
      rank_parallel_endpoint(from_node, {x:if (vertical_rank_axis) side_cross else from_node.x,
        y:if (vertical_rank_axis) from_node.y else side_cross}, start_fan, not vertical_rank_axis)
      else if (side_lane) side_base[0] else null;
    let side_finish = if (side_lane and end_fan != null)
      rank_parallel_endpoint(to_node, {x:if (vertical_rank_axis) side_cross else to_node.x,
        y:if (vertical_rank_axis) to_node.y else side_cross}, end_fan, not vertical_rank_axis)
      else if (side_lane) side_base[len(side_base) - 1] else null;
    let side_points = if (side_lane and len(side_base) == 4) [side_start,
      if (vertical_rank_axis) {x:side_base[1].x, y:side_start.y} else {x:side_start.x, y:side_base[1].y},
      if (vertical_rank_axis) {x:side_base[2].x, y:side_finish.y} else {x:side_finish.x, y:side_base[2].y},
      side_finish] else side_base;
    let obstacle_safe_points = if (side_lane and not route_hits_obstacle(side_points, obstacles))
      side_points else avoided;
    let angled_points = if (route_mode == "curved" and
        edge.from != edge.to and not has_compound and
        automatic_endpoint(edge.from_port, edge.from_compass) and
        automatic_endpoint(edge.to_port, edge.to_compass))
      curved_lane_terminals(obstacle_safe_points, vertical_rank_axis, backtracks_rank) else obstacle_safe_points;
    let angled_samples = if (route_mode == "curved" and len(angled_points) > 2)
      route_geometry.sample_points({route_mode:route_mode, points:angled_points}) else angled_points;
    let candidate_points = if (not route_hits_obstacle(angled_samples, obstacles))
      angled_points else obstacle_safe_points;
    let has_named_port = edge.from_port != null or edge.to_port != null;
    let sampled = if (has_named_port and route_mode == "curved")
      route_geometry.sample_points({route_mode:route_mode, points:candidate_points}) else candidate_points;
    let port_detour = has_named_port and
      route_mode != "none" and route_mode != "line" and edge.from != edge.to and
      route_hits_obstacle(sampled, obstacles);
    let gap = max([12.0, opts.edge_sep]);
    let start_escape = if (port_detour) port_escape(from_node, edge.from_port, to_node, start, gap) else start;
    let finish_escape = if (port_detour) port_escape(to_node, edge.to_port, from_node, finish, gap) else finish;
    let detour_base = if (port_detour) simplify_route([start,
      *route_around_obstacles(start_escape, finish_escape, obstacles, vertical_rank_axis, gap,
        route_mode == "orthogonal", false), finish]) else candidate_points;
    let detour = if (port_detour and route_mode == "curved")
      route_geometry.corner_waypoints(detour_base, gap / 3.0) else detour_base;
    let detour_samples = if (port_detour and route_mode == "curved")
      route_geometry.sample_points({route_mode:route_mode, points:detour}) else detour;
    // Smoothing a clear polyline can cut through a port's node at a corner.
    let routed_points = if (port_detour and not route_hits_obstacle(detour_samples, obstacles))
      detour else candidate_points;
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
      points: routed_points,
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
      route_mode: route_mode,
      is_bezier: route_mode == "curved"
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
    // The outermost box often starts at the origin already.
    let shift = min_x != 0.0 or min_y != 0.0;
    {
      width: max_x - min_x,
      height: max_y - min_y,
      nodes: if (not shift) nodes else [for (node in nodes) {*:node, x: node.x - min_x, y: node.y - min_y}],
      edges: if (not shift) edges else [for (edge in edges) {*:edge, points: [for (point in edge.points) {
        x: point.x - min_x,
        y: point.y - min_y
      }]}],
      clusters: if (not shift) clusters else [for (cluster in clusters) {*:cluster,
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
  let ranked = assign_ranks(graph.nodes, graph.edges, graph.clusters, graph.constraints,
    graph.options.new_rank, graph.options.compound);
  let grouped = len(graph.clusters) > 0 or len([for (node in ranked where node.order_group != null) node]) > 0
    or len(unique([for (node in ranked) node.id])) != len(ranked);
  let layers0 = create_layers(ranked, graph.clusters, grouped);
  let crossed = reduce_crossings(layers0, graph.edges, graph.options.max_iterations,
    graph.clusters, graph.options.compound, grouped);
  // Crossing reduction may reverse Graphviz's authored in/out edge sequence.
  let layers = enforce_ordering(crossed, ranked, graph.edges, graph.options.ordering);
  let positioned_nodes = position_nodes(layers, graph.options, graph.clusters);
  let canonical_nodes = if (graph.options.loop_side == "top" and
    horizontal_rank_direction(graph.options.direction))
    feedback_adjusted_nodes(positioned_nodes, graph.edges, graph.options)
    else positioned_nodes;
  let routed_nodes = orient_nodes(canonical_nodes, graph.options.direction);
  let routed_node_index = nodes_by_id(routed_nodes);
  let obstacle_margin = max([4.0, graph.options.edge_sep / 2.0]);
  let loops = loop_obstacles(routed_nodes, graph.edges, graph.options, obstacle_margin);
  let routed_clusters = compute_clusters(graph.clusters, routed_nodes, loops);
  // The clearance boxes are fixed for the layout. Each edge only excludes its endpoints.
  let boxes = {nodes:[for (node in routed_nodes)
    {node:node.id, rect:node_routing_rect(node, obstacle_margin)}, *loops],
    clusters:[for (cluster in routed_clusters) {id:cluster.id,
      rect:routing_rect(cluster.x, cluster.y, cluster.width, cluster.height, obstacle_margin)}]};
  let routed_edges = [for (edge in graph.edges,
    let path = route_edge(edge, routed_nodes, routed_node_index, routed_clusters, graph.edges, graph.options,
      boxes, obstacle_margin)
    where ((path != null) ^ { false })) path];
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
