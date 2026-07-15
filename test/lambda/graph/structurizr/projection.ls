import model: lambda.package.graph.model
import structurizr: lambda.package.graph.structurizr.structurizr

fn direct(value, wanted) => [
  for (child in model.element_children(value) where model.tag(child) == wanted) child
]

let source^err = input("test/lambda/graph/structurizr/basic.dsl",
  {type: "graph", flavor: "structurizr"})
let workspace = structurizr.normalize(source)
let context = structurizr.project(workspace, "Context")
let containers = structurizr.project(workspace, "Containers")
let cluster = direct(containers, "subgraph")[0]
let html = structurizr.to_html(workspace, "Context")

{
  keys: structurizr.view_keys(workspace),
  context: [context["diagram-type"], context.direction,
    [for (node in model.nodes(context)) [node.id, node.role, node.label]],
    [for (edge in model.edges(context)) [edge.from, edge.to, edge.label]]],
  containers: [cluster.id, cluster.label,
    [for (node in model.nodes(cluster)) [node.id, node["c4-kind"]]],
    [for (node in direct(containers, "node")) node.id],
    [for (edge in model.edges(containers)) [edge.from, edge.to]]],
  html: [string(name(html)), html.class, html["data-radiant-layout"], len(html)]
}
