import model: lambda.package.graph.model
import normalize: lambda.package.graph.normalize

fn property_values(value) => [
  for (group in model.element_children(value), property in model.element_children(group)
    where model.tag(group) == "properties" and model.tag(property) == "property")
    [property.name, property.value, property.origin, property["defining-scope"]]
]

let invalid_source^invalid_parse_error = input(
  "test/lambda/graph/graphviz/semantic_errors.dot", {type: "graph", flavor: "dot"})
let invalid = normalize.normalize(invalid_source)
let strict_source^strict_parse_error = input(
  "test/lambda/graph/graphviz/strict_undirected.dot", {type: "graph", flavor: "dot"})
let strict = normalize.normalize(strict_source)

{
  invalid: {
    valid: invalid.valid,
    codes: [for (value in invalid.diagnostics) value.code],
    nodes: [for (value in model.nodes(invalid.graph)) value.id]
  },
  strict: {
    valid: strict.valid,
    directed: strict.graph.directed,
    edges: [for (value in model.edges(strict.graph))
      [value.id, value.from, value.to, value.stroke, value.weight, property_values(value)]]
  }
}
