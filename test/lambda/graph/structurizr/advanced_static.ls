import expression: lambda.package.graph.structurizr.expressions
import layout: lambda.package.graph.layout
import model: lambda.package.graph.model
import structurizr: lambda.package.graph.structurizr.structurizr

fn children(value, wanted) => [
  for (child in model.element_children(value) where model.tag(child) == wanted) child
]

let source^err = input("test/lambda/graph/structurizr/advanced_static.dsl",
  {type: "graph", flavor: "structurizr"})
let workspace = structurizr.normalize(source)
let c4_model = children(workspace, "c4-model")[0]
let elements = children(c4_model, "c4-element")
let relationships = children(c4_model, "c4-relationship")
let graph = structurizr.project(workspace, "All")
let selected = structurizr.project(workspace, "Expression")
let html = structurizr.to_html(workspace, "All")
let routed = layout.compute({
  nodes: [{id: "a", width: 40.0, height: 20.0},
    {id: "b", width: 40.0, height: 20.0},
    {id: "c", width: 40.0, height: 20.0}],
  edges: [{id: "line", from: "a", to: "b", route_mode: "line"},
    {id: "curve", from: "b", to: "c", route_mode: "curved"}],
  direction: "LR", route_mode: "orthogonal"
})

{
  terminology: [for (term in children(children(workspace, "c4-terminology")[0], "term"))
    [term.kind, term.value]],
  elements: [for (entry in elements where contains(["api", "worker", "user"], entry.id))
    [entry.id, entry.terminology, entry.group]],
  canonical_groups: [for (entry in elements where entry.kind == "group")
    [entry.id, entry.parent, entry.group]],
  expression: expression.element_ids(elements, relationships,
    "(element.tag==Core || element.tag==External) && element.tag!=Hidden"),
  groups: [for (entry in model.subgraph_entries(graph))
    [entry.value.id, entry.value.label, entry.parent]],
  nodes: [for (node in model.nodes(graph))
    [node.id, node.shape, node["structurizr-shape"]]],
  edges: [for (edge in model.edges(graph)) [edge.id, edge["route-mode"], edge.style]],
  selected: [for (node in model.nodes(selected)) node.id],
  html: [
    for (node in children(html, "node"))
      [node["data-node-id"], node["data-shape"], node["data-style-declarations"]],
    for (edge in children(html, "edge"))
      [edge["data-edge-id"], edge["data-route-mode"], edge["data-dash-array"]]
  ],
  routed: [for (edge in routed.edges) [edge.id, edge.route_mode, edge.is_bezier]],
  diagnostics: [for (value in model.diagnostics(workspace)) [value.code, value.path]]
}
