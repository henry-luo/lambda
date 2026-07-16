import model: lambda.package.graph.model
import structurizr: lambda.package.graph.structurizr.structurizr

fn children(value, wanted) => [
  for (child in model.element_children(value) where model.tag(child) == wanted) child
]

let source^err = input("test/lambda/graph/structurizr/deployment_groups.dsl",
  {type: "graph", flavor: "structurizr"})
let workspace = structurizr.normalize(source)
let c4_model = children(workspace, "c4-model")[0]
let elements = children(c4_model, "c4-element")
let instances = [for (entry in elements where ends_with(string(entry.kind), "-instance")) entry]
let graph = structurizr.project(workspace, "Production")

{
  groups: [for (entry in elements where entry.kind == "deployment-group")
    [entry.id, entry.name]],
  instances: [for (entry in instances) [entry.id,
    [for (group_ref in children(entry, "deployment-group-ref")) group_ref.identifier]]],
  health: [for (entry in instances, check in children(entry, "health-check"))
    [entry.id, check.name, check.url, check.interval, check.timeout]],
  graph_health: [for (entry in model.nodes(graph), check in children(entry, "health-check"))
    [entry.id, check.name, check.interval, check.timeout]],
  boundaries: [for (entry in model.subgraphs(graph)) entry.id],
  edges: [for (edge in model.edges(graph)) [edge.from, edge.to, edge.label]]
}
