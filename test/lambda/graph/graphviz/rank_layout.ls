import layout: lambda.package.graph.layout
import model: lambda.package.graph.model
import normalize: lambda.package.graph.normalize
import transform: lambda.package.graph.transform

fn children(value, wanted) => [
  for (child in model.element_children(value) where model.tag(child) == wanted) child
]

let source^source_error = input(
  "test/lambda/graph/graphviz/rank_layout.dot", {type: "graph", flavor: "dot"})
let normalized = normalize.normalize(source)
let graph = normalized.graph
let constraints = [for (constraint in model.constraints(graph)) {
  kind: constraint.kind, value: constraint.value, scope: constraint.scope,
  members: [for (member in children(constraint, "member")) member.node]
}]
let result = layout.compute({
  nodes: [for (node in model.nodes(graph)) {id: node.id, width: 50, height: 30}],
  edges: model.edges(graph), constraints: constraints, directed: true
})
let html = transform.to_html(graph)

{
  valid: normalized.valid,
  layers: [for (layer in result.layers) [layer.rank, [for (node in layer.nodes) node.id]]],
  ranks: [for (node in result.nodes) [node.id, node.rank]],
  constraints: [for (value in children(html, "constraint")) [
    value["data-constraint-scope"], value["data-constraint-value"],
    value["data-constraint-member"]]],
  edges: [for (value in children(html, "edge")) [value["data-from"], value["data-to"],
    value["data-weight"], value["data-constraint"]]]
}
