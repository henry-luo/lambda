import model: lambda.package.graph.model
import structurizr: lambda.package.graph.structurizr.structurizr

fn descendants(value) => [
  for (child in model.element_children(value),
    nested in [child, *descendants(child)]) nested
]

fn children(value, wanted) => [
  for (child in model.element_children(value) where model.tag(child) == wanted) child
]

let source^source_error = input(
  "test/lambda/graph/structurizr/includes/root.dsl",
  {type: "graph", flavor: "structurizr"})
let workspace = structurizr.normalize(source)
let graph = structurizr.project(workspace, "Containers")
let canonical_model = children(workspace, "c4-model")[0]
let canonical_views = children(workspace, "c4-views")[0]
let included = [for (value in descendants(source),
  let file = model.optional(value, "source-file") where file != null) string(file)]

let cycle_source^cycle_error = input(
  "test/lambda/graph/structurizr/includes/cycle.dsl",
  {type: "graph", flavor: "structurizr"})
let cycle_workspace = structurizr.normalize(cycle_source)
let policy_source^policy_error = input(
  "test/lambda/graph/structurizr/includes/policy.dsl",
  {type: "graph", flavor: "structurizr"})
let policy_workspace = structurizr.normalize(policy_source)

{
  errors: [source_error, cycle_error, policy_error],
  resolved: [source["include-file-count"], source["include-byte-count"] > 0,
    len(unique(included)),
    len([for (file in included where ends_with(file, "containers.dsl")) file]) > 0],
  elements: [for (value in children(canonical_model, "c4-element"))
    [value.id, value.kind, value.parent]],
  relationships: [for (value in children(canonical_model, "c4-relationship"))
    [value.source, value.destination, value.description]],
  views: [for (value in children(canonical_views, "c4-view")) value.key],
  graph: [for (value in model.nodes(graph)) value.id],
  diagnostics: [for (value in model.diagnostics(workspace)) value.code],
  cycle: [cycle_source["include-file-count"],
    for (value in model.diagnostics(cycle_workspace)) value.code],
  policy: [policy_source["include-file-count"],
    for (value in model.diagnostics(policy_workspace)) value.code]
}
