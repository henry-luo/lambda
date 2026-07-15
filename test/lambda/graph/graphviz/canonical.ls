import model: lambda.package.graph.model
import normalize: lambda.package.graph.normalize

fn children(value, wanted) => [
  for (child in model.element_children(value) where model.tag(child) == wanted) child
]

fn property_values(value) => [
  for (group in children(value, "properties"), property in children(group, "property"))
    [property.name, property.value, property.origin, property["defining-scope"]]
]

fn named_properties(value, wanted) => [
  for (group in children(value, "properties"), property in children(group, "property")
    where property.name == wanted)
    [property.value, property.origin, property["defining-scope"]]
]

let source^parse_error = input("test/lambda/graph/graphviz/canonical.dot",
  {type: "graph", flavor: "dot"})
let result = normalize.normalize(source)
let graph = result.graph
let nodes = model.nodes(graph)
let edges = model.edges(graph)
let groups = model.subgraphs(graph)

{
  stage: graph["ir-stage"],
  valid: result.valid,
  diagnostics: [for (value in result.diagnostics) value.code],
  graph: [graph.id, graph.directed, graph.strict, graph.direction],
  nodes: [for (node in nodes) [node.id, model.label_source(node), node.shape,
    node.stroke, property_values(node)]],
  edges: [for (edge in edges) [edge.id, edge.from, edge.to, edge.label,
    edge.stroke, edge.weight, edge["source-segment-index"],
    edge["source-expansion-index"], property_values(edge)]],
  groups: [for (group in groups) [group.id, group.role,
    [for (member in children(group, "member")) member.node],
    named_properties(group, "rankdir"), named_properties(group, "rank")]],
  constraints: [for (value in model.constraints(graph)) [value.kind, value.value,
    value.scope, [for (member in children(value, "member")) member.node]]],
  idempotent: normalize.normalize(graph).graph == graph
}
