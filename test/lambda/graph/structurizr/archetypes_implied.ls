import model: lambda.package.graph.model
import structurizr: lambda.package.graph.structurizr.structurizr

fn children(value, wanted) => [
  for (child in model.element_children(value) where model.tag(child) == wanted) child
]

fn tags(value) => [for (tag in children(value, "tag")) string(tag.name)]
fn properties(value) => [
  for (property in children(value, "property")) [property.name, property.value]
]

let source^err = input("test/lambda/graph/structurizr/archetypes_implied.dsl",
  {type: "graph", flavor: "structurizr"})
let workspace = structurizr.normalize(source)
let c4_model = children(workspace, "c4-model")[0]
let graph = structurizr.project(workspace, "Context")
let invalid_source^invalid_err = input(
  "test/lambda/graph/structurizr/archetype_diagnostics.dsl",
  {type: "graph", flavor: "structurizr"})
let invalid_workspace = structurizr.normalize(invalid_source)
let disabled_source^disabled_err = input(
  "test/lambda/graph/structurizr/implied_disabled.dsl",
  {type: "graph", flavor: "structurizr"})
let disabled_workspace = structurizr.normalize(disabled_source)
let disabled_model = children(disabled_workspace, "c4-model")[0]

{
  elements: [for (entry in children(c4_model, "c4-element"))
    [entry.id, entry.kind, entry.archetype, entry.description, entry.technology,
      tags(entry), properties(entry)]],
  relationships: [for (entry in children(c4_model, "c4-relationship"))
    [entry.id, entry.source, entry.destination, entry.archetype,
      entry.description, entry.technology, entry.implied, entry["implied-from"],
      tags(entry), properties(entry)]],
  projection: {
    nodes: [for (entry in children(graph, "node")) string(entry.id)],
    edges: [for (entry in children(graph, "edge"))
      [entry.id, entry.from, entry.to, entry.label]]
  },
  diagnostics: [for (block in children(workspace, "diagnostics"))
    for (entry in children(block, "diagnostic")) entry.code],
  disabled_relationships: [for (entry in children(disabled_model, "c4-relationship"))
    [entry.source, entry.destination, entry.implied]],
  invalid_diagnostics: [for (block in children(invalid_workspace, "diagnostics"))
    for (entry in children(block, "diagnostic")) [entry.code, entry.path]]
}
