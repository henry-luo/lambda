import model: lambda.package.graph.model
import structurizr: lambda.package.graph.structurizr.structurizr

fn children(value, wanted) => [
  for (child in model.element_children(value) where model.tag(child) == wanted) child
]

fn tags(value) => [for (tag in children(value, "tag")) tag.name]

let source^err = input("test/lambda/graph/structurizr/basic.dsl",
  {type: "graph", flavor: "structurizr"})
let workspace = structurizr.normalize(source)
let c4_model = children(workspace, "c4-model")[0]
let c4_views = children(workspace, "c4-views")[0]
let c4_styles = children(workspace, "c4-styles")[0]

{
  workspace: [workspace.name, workspace.description, workspace.flavor,
    workspace["ir-stage"], workspace["identifier-mode"]],
  elements: [for (entry in children(c4_model, "c4-element"))
    [entry.id, entry.kind, entry.parent, entry.name, entry.technology,
      entry["model-ref"], tags(entry)]],
  relationships: [for (entry in children(c4_model, "c4-relationship"))
    [entry.id, entry.source, entry.destination, entry.description, entry.technology]],
  views: [for (diagram in children(c4_views, "c4-view"))
    [diagram.key, diagram.kind, diagram.scope, diagram.direction,
      diagram["rank-sep"], diagram["node-sep"],
      [for (include_rule in children(diagram, "include")) include_rule.expression]]],
  styles: [for (style in children(c4_styles, "c4-style"))
    [style["target-kind"], style.tag,
      [for (property in children(style, "property")) [property.name, property.value]]]]
}
