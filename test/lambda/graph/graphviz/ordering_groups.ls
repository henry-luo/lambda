import layout: lambda.package.graph.layout
import model: lambda.package.graph.model
import normalize: lambda.package.graph.normalize
import transform: lambda.package.graph.transform

fn children(value, wanted) => [
  for (child in model.element_children(value) where model.tag(child) == wanted) child
]

fn layer_projection(result, ids) => [
  for (layer in result.layers, node in layer.nodes where contains(ids, node.id)) node.id
]

fn item_by_id(items, id) {
  let matches = [for (item in items where item.id == id) item];
  if (len(matches) > 0) matches[0] else null
}

fn on_boundary(cluster, point) =>
  point.x >= cluster.x and point.x <= cluster.x + cluster.width and
  point.y >= cluster.y and point.y <= cluster.y + cluster.height and
  (abs(point.x - cluster.x) < 0.001 or
    abs(point.x - (cluster.x + cluster.width)) < 0.001 or
    abs(point.y - cluster.y) < 0.001 or
    abs(point.y - (cluster.y + cluster.height)) < 0.001)

fn compute_graph(graph) {
  let node_entries = model.node_entries(graph);
  let cluster_entries = model.visual_subgraph_entries(graph);
  layout.compute({
    nodes: [for (entry in node_entries, let node = entry.value) {
      id: node.id, width: 50, height: 30,
      group: entry.group, order_group: node.group, ordering: node.ordering
    }],
    edges: model.edges(graph), directed: true,
    constraints: [for (constraint in model.constraints(graph),
      member in children(constraint, "member")) {
      kind: constraint.kind, value: constraint.value,
      scope: constraint.scope, member: member.node
    }],
    clusters: [for (entry in cluster_entries) {
      id: entry.group, parent: entry.parent, padding: 12
    }],
    ordering: graph.ordering, new_rank: graph["new-rank"], compound: graph.compound
  })
}

fn new_rank_case(enabled) {
  let source^parse_error = parse(
    "digraph Rank { graph [newrank=" ++ string(enabled) ++ "] " ++
      "subgraph cluster_a { a } subgraph cluster_b { b } " ++
      "root -> b subgraph align { rank=same; a; b } }",
    {type: "graph", flavor: "dot"});
  compute_graph(normalize.normalize(source).graph)
}

fn rank_of(result, id) => item_by_id(result.nodes, id).rank

fn invalid_source() => parse(
  "digraph Invalid { graph [ordering=sideways, compound=true] " ++
    "a [ordering=random] subgraph cluster_known { c } " ++
    "a -> b [lhead=cluster_missing] a -> b [lhead=cluster_known] }",
  {type: "graph", flavor: "dot"})

let source^source_error = input(
  "test/lambda/graph/graphviz/ordering_groups.dot", {type: "graph", flavor: "dot"})
let normalized = normalize.normalize(source)
let graph = normalized.graph
let nodes = model.nodes(graph)
let html = transform.to_html(graph)
let result = compute_graph(graph)
let old_rank = new_rank_case(false)
let new_rank = new_rank_case(true)
let nested_groups = layout.compute({
  nodes: [
    {id: "left", width: 20, height: 20, group: "cluster_left"},
    {id: "outside", width: 20, height: 20},
    {id: "right", width: 20, height: 20, group: "cluster_right"},
    {id: "lane1", width: 20, height: 20, group: "cluster_outer", order_group: "lane"},
    {id: "middle", width: 20, height: 20, group: "cluster_outer"},
    {id: "lane2", width: 20, height: 20, group: "cluster_outer", order_group: "lane"}
  ],
  clusters: [
    {id: "cluster_outer"},
    {id: "cluster_left", parent: "cluster_outer"},
    {id: "cluster_right", parent: "cluster_outer"}
  ]
})
let invalid = normalize.normalize(invalid_source())
let compound_edge = [for (edge in result.edges where edge.tail_cluster != null) edge][0]
let source_cluster = item_by_id(result.clusters, "cluster_source")
let target_cluster = item_by_id(result.clusters, "cluster_target")
let html_compound = [for (edge in children(html, "edge")
  where edge["data-tail-cluster"] != null) edge][0]

{
  valid: normalized.valid,
  graph: [graph.ordering, graph["new-rank"], graph.compound,
    result.ordering, result.new_rank, result.compound],
  source_order: layer_projection(result, ["a", "b", "c"]),
  incoming_order: layer_projection(result, ["left", "right"]),
  group_order: layer_projection(result, ["lane1", "lane2", "middle"]),
  new_rank_policy: [rank_of(old_rank, "a"), rank_of(old_rank, "b"),
    rank_of(new_rank, "a"), rank_of(new_rank, "b")],
  nested_groups: layer_projection(nested_groups,
    ["left", "right", "lane1", "lane2", "middle", "outside"]),
  compound_route: [compound_edge.tail_cluster, compound_edge.head_cluster,
    on_boundary(source_cluster, compound_edge.points[0]),
    on_boundary(target_cluster, compound_edge.points[len(compound_edge.points) - 1])],
  nodes: [for (node in nodes where node.group != null or node.ordering != null)
    [node.id, node.group, node.ordering]],
  html: [html["data-ordering"], html["data-new-rank"], html["data-compound"],
    [html_compound["data-tail-cluster"], html_compound["data-head-cluster"]],
    for (node in children(html, "node")
      where node["data-order-group"] != null or node["data-ordering"] != null)
      [node["data-node-id"], node["data-order-group"], node["data-ordering"]]],
  invalid: [invalid.valid,
    [for (value in invalid.diagnostics) [value.code, value.path]]]
}
