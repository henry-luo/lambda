import model: lambda.package.graph.model
import structurizr: lambda.package.graph.structurizr.structurizr
import transform: lambda.package.graph.transform

fn children(value, wanted) => [
  for (child in model.element_children(value) where model.tag(child) == wanted) child
]

fn route_points(edge) => [
  for (route in children(edge, "route"), point in children(route, "point")) point
]

let source^source_error = input(
  "test/lambda/graph/structurizr/advanced_static.dsl",
  {type: "graph", flavor: "structurizr"})
let workspace = structurizr.normalize(source)
let graph = structurizr.project(workspace, "All")
let installed = transform.install()
let first = transform.render_scene(graph, 800, 600)
let second = transform.render_scene(graph, 800, 600)
let comparison = transform.compare_scenes(second.scene, first.scene, {
  'geometry-tolerance': 0.01, 'route-tolerance': 0.01,
  relations: true, 'rank-order': true
})
let nodes = children(first.scene, "node")
let clusters = children(first.scene, "cluster")
let edges = children(first.scene, "edge")

{
  installed: installed,
  retained: comparison.equal,
  scene: [first.scene.direction, first.scene.width > 0, first.scene.height > 0,
    len(clusters), len(nodes), len(edges)],
  clusters: [for (cluster in clusters)
    [cluster.id, cluster.parent, cluster.width > 0, cluster.height > 0]],
  nodes: [for (node in nodes)
    [node.id, node.shape, node.group, node.width > 0, node.height > 0]],
  edges: [for (edge in edges)
    [edge.id, edge.from, edge.to, edge["route-mode"],
      len(route_points(edge)) >= 2]],
  svg: contains(first.svg, "data-graph-role=\"graph\"") and
    contains(first.svg, "data-shape=\"hexagon\"")
}
