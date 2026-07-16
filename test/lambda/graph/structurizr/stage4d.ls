import model: lambda.package.graph.model
import structurizr: lambda.package.graph.structurizr.structurizr

fn children(value, wanted) => [
  for (child in model.element_children(value) where model.tag(child) == wanted) child
]

fn first(values) => if (len(values) > 0) values[0] else null

let source^err = input("test/lambda/graph/structurizr/stage4d.dsl",
  {type: "graph", flavor: "structurizr"})
let source_views = children(source, "views")[0]
let source_dynamic = children(source_views, "view")[0]
let source_parallel = children(source_dynamic, "parallel")[0]
let workspace = structurizr.normalize(source)
let c4_views = children(workspace, "c4-views")[0]
let dynamic_view = children(c4_views, "c4-view")[0]
let interactions = children(dynamic_view, "c4-interaction")
let c4_model = children(workspace, "c4-model")[0]
let api_instance = first([for (entry in children(c4_model, "c4-element")
  where entry["model-ref"] == "store.api") entry])
let dynamic_graph = structurizr.project(workspace, "PlaceOrder")
let deployment_graph = structurizr.project(workspace, "Production")
let dynamic_html = structurizr.to_html(workspace, "PlaceOrder")
let deployment_html = structurizr.to_html(workspace, "Production")

{
  source: [model.tag(source_parallel),
    [for (branch in children(source_parallel, "parallel"))
      [for (statement in children(branch, "statement"))
        [statement.keyword, [for (argument in children(statement, "argument"))
          argument.value]]]],
    [for (statement in children(source_dynamic, "statement"))
      [statement.keyword, [for (argument in children(statement, "argument"))
        argument.value]]]],
  canonical: [for (item in interactions)
    [item.source, item.destination, item.order, item.sequence,
      item["parallel-group"] != null, item["relationship-ref"]]],
  instance: [api_instance.id, api_instance.parent, api_instance.name,
    [for (group_ref in children(api_instance, "deployment-group-ref"))
      group_ref.identifier]],
  dynamic: [[for (node in model.nodes(dynamic_graph)) node.id],
    [for (edge in model.edges(dynamic_graph))
      [edge.from, edge.to, edge.label, edge.order, edge["parallel-group"] != null]]],
  deployment: [[for (cluster in model.subgraphs(deployment_graph))
      [cluster.id, cluster.label]],
    [for (node in model.nodes(deployment_graph)) [node.id, node["c4-kind"]]],
    [for (edge in model.edges(deployment_graph)) [edge.from, edge.to, edge.label]]],
  diagnostics: [for (value in model.diagnostics(workspace))
    [value.code, value.severity]],
  html: [[string(name(dynamic_html)), dynamic_html["data-radiant-layout"]],
    [string(name(deployment_html)), deployment_html["data-radiant-layout"]]]
}
