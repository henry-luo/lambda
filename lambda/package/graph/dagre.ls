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
    group: if (node.group != null and node.group != "") string(node.group) else null,
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
      label: if (edge.label != null) string(edge.label) else null,
      directed: if (edge.directed != null) edge.directed else directed,
      arrow_start: marker_start != "none",
      arrow_end: marker_end != "none",
      marker_start: marker_start,
      marker_end: marker_end,
      style: if (edge.style != null) string(edge.style) else "solid",
      stroke: if (edge.stroke != null) string(edge.stroke) else null,
      stroke_width: if (edge.stroke_width != null) float(edge.stroke_width) else null,
      opacity: if (edge.opacity != null) float(edge.opacity) else null,
      dash_array: if (edge.dash_array != null) string(edge.dash_array) else null,
      min_length: max([1, int(if (edge.min_length != null) edge.min_length
        else if (edge["min-length"] != null) edge["min-length"] else 1)]),
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
  {
    nodes: nodes,
    edges: edges,
    clusters: clusters,
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
    else max([for (edge in preds)
      rank_of(edge.from, edges, [*stack, id]) + edge.min_length])
  }
}

fn assign_ranks(nodes, edges) {
  let result = [for (node in nodes) {*:node, rank: rank_of(node.id, edges, [])}];
  result
}

fn max_rank(nodes) {
  if (len(nodes) == 0) 0 else max([for (node in nodes) node.rank])
}

fn layer_with_order(rank, nodes) {
  let layer_nodes = [for (node in nodes where node.rank == rank) node];
  {
    rank: rank,
    nodes: [for (i, node in layer_nodes) {*:node, order: i}]
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
    nodes: [for (i, node in sorted_nodes) {*:node, order: i}]
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
  let result = [for (i, node in layer.nodes) {*:node,
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

fn shape_vertices(node) {
  let left = node.x - node.width / 2.0;
  let right = node.x + node.width / 2.0;
  let top = node.y - node.height / 2.0;
  let bottom = node.y + node.height / 2.0;
  let quarter = node.width / 4.0;
  let vertices = if (node.shape == "hexagon") [
    {x: left + quarter, y: top}, {x: right - quarter, y: top},
    {x: right, y: node.y}, {x: right - quarter, y: bottom},
    {x: left + quarter, y: bottom}, {x: left, y: node.y}
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
  let offset = min([1.0, max([0.0,
    float(if (port.offset != null) port.offset else 0.5)])]);
  let left = node.x - node.width / 2.0;
  let top = node.y - node.height / 2.0;
  let point = if (side == "north") {x: left + node.width * offset, y: top}
  else if (side == "south") {x: left + node.width * offset, y: top + node.height}
  else if (side == "west") {x: left, y: top + node.height * offset}
  else {x: left + node.width, y: top + node.height * offset};
  point
}

fn clip_node(node, target_x, target_y, port_id = null) {
  let port = find_port(node, port_id);
  let half_w = node.width / 2.0;
  let half_h = node.height / 2.0;
  let vertices = shape_vertices(node);
  if (port != null) port_point(node, port, target_x, target_y)
  else if (node.shape == "circle" or node.shape == "doublecircle" or node.shape == "ellipse" or
      node.shape == "stadium")
    clip_ellipse(node.x, node.y, target_x, target_y, half_w, half_h)
  else if (node.shape == "diamond")
    clip_diamond(node.x, node.y, target_x, target_y, half_w, half_h)
  else if (len(vertices) > 0)
    clip_polygon(node.x, node.y, target_x, target_y, vertices)
  else clip_rect(node.x, node.y, target_x, target_y, half_w, half_h)
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

fn compound_crossings(from_node, to_node, clusters) {
  let from_chain = cluster_chain(from_node.group, clusters, []);
  let to_chain = cluster_chain(to_node.group, clusters, []);
  let source = exclusive_clusters(from_chain, to_chain);
  let target = reverse_items(exclusive_clusters(to_chain, from_chain));
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

fn self_loop_points(node, edge_sep, sibling_index, from_port = null, to_port = null) {
  let half_w = node.width / 2.0;
  let half_h = node.height / 2.0;
  let spread = max([10.0, half_h / 2.0]);
  // sibling rank keeps loop geometry independent of unrelated edge source order.
  let loop_gap = max([20.0, edge_sep * float(sibling_index + 2)]);
  // Named loop ports are authoritative; unported loops retain the historical anchors.
  let start = if (from_port == null)
    {x: node.x + half_w, y: node.y - spread}
    else clip_node(node, node.x + half_w + loop_gap, node.y - spread, from_port);
  let finish = if (to_port == null)
    {x: node.x + half_w, y: node.y + spread}
    else clip_node(node, node.x + half_w + loop_gap, node.y + spread, to_port);
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
    from_port = null, to_port = null) {
  // Lane reconstruction must retain authored ports instead of reverting to shape clipping.
  if (vertical_first) {
    let lane_x = (from_node.x + to_node.x) / 2.0 + offset;
    let start = clip_node(from_node, lane_x, to_node.y, from_port);
    let finish = clip_node(to_node, lane_x, from_node.y, to_port);
    simplify_route([
      start,
      {x: lane_x, y: start.y},
      {x: lane_x, y: finish.y},
      finish
    ])
  } else {
    let lane_y = (from_node.y + to_node.y) / 2.0 + offset;
    let start = clip_node(from_node, to_node.x, lane_y, from_port);
    let finish = clip_node(to_node, from_node.x, lane_y, to_port);
    simplify_route([
      start,
      {x: start.x, y: lane_y},
      {x: finish.x, y: lane_y},
      finish
    ])
  }
}

fn route_edge(edge, nodes, clusters, edges, opts) {
  let from_node = find_node(nodes, edge.from);
  let to_node = find_node(nodes, edge.to);
  if (from_node == null or to_node == null) null
  else {
    let start = clip_node(from_node, to_node.x, to_node.y, edge.from_port);
    let finish = clip_node(to_node, from_node.x, from_node.y, edge.to_port);
    let vertical_first = not (opts.direction == "LR" or opts.direction == "RL");
    let parallel = parallel_info(edge, edges);
    let lane_offset = (float(parallel.index) - float(parallel.count - 1) / 2.0) * opts.edge_sep;
    let crossings = compound_crossings(from_node, to_node, clusters);
    let central_start = if (len(crossings.source) > 0)
      crossings.source[len(crossings.source) - 1] else start;
    let central_finish = if (len(crossings.target) > 0) crossings.target[0] else finish;
    let lane = lane_waypoint(central_start, central_finish, lane_offset, vertical_first);
    let lane_points = if (parallel.count > 1) [lane] else [];
    let compound_points = [start, *crossings.source,
      *lane_points,
      *crossings.target, finish];
    {
      id: edge.id,
      from: edge.from,
      to: edge.to,
      from_port: edge.from_port,
      to_port: edge.to_port,
      points: if (edge.from == edge.to)
        self_loop_points(from_node, opts.edge_sep, parallel.index,
          edge.from_port, edge.to_port)
        else if (len(crossings.source) > 0 or len(crossings.target) > 0)
          orthogonal_waypoints(compound_points, vertical_first)
        else if (parallel.count > 1)
          parallel_route_points(from_node, to_node, lane_offset, vertical_first,
            edge.from_port, edge.to_port)
        else orthogonal_points([start, finish], vertical_first),
      directed: edge.directed,
      arrow_start: edge.arrow_start,
      arrow_end: edge.arrow_end,
      marker_start: edge.marker_start,
      marker_end: edge.marker_end,
      style: edge.style,
      stroke: edge.stroke,
      stroke_width: edge.stroke_width,
      opacity: edge.opacity,
      dash_array: edge.dash_array,
      min_length: edge.min_length,
      z: edge.z,
      index: edge.index,
      is_bezier: opts.use_splines
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
  let ranked = assign_ranks(graph.nodes, graph.edges);
  let layers0 = create_layers(ranked);
  let layers = reduce_crossings(layers0, graph.edges, graph.options.max_iterations);
  let canonical_nodes = position_nodes(layers, graph.options);
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
    direction: graph.options.direction
  }
}
