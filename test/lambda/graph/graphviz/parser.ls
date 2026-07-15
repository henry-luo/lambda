import model: lambda.package.graph.model
import schema: lambda.package.graph.schema

fn children(value, wanted) => [
  for (child in model.element_children(value) where model.tag(child) == wanted) child
]

fn property_lists(value) => [
  for (group in children(value, "properties")) [
    for (entry in model.element_children(group))
      [entry.name, entry.value, entry["value-source-kind"],
        entry["source-start"] < entry["source-end"]]
  ]
]

let graph^parse_error = input("test/lambda/graph/graphviz/grammar.dot",
  {type: "graph", flavor: "dot"})
let direct = model.element_children(graph)
let edge = children(graph, "dot-edge-statement")[0]
let recovered^recovery_error = input("test/lambda/graph/graphviz/recovery.dot",
  {type: "graph", flavor: "dot"})
let ids^ids_error = input("test/lambda/graph/graphviz/ids.dot",
  {type: "graph", flavor: "dot"})
let id_nodes = children(ids, "node")

{
  header: [graph.id, graph.directed, graph.strict, graph["ir-stage"],
    graph["source-kind"]],
  order: [for (child in direct) model.tag(child)],
  defaults: [for (value in children(graph, "dot-attr-statement"))
    [value["target-kind"], property_lists(value)]],
  nodes: [for (value in children(graph, "node"))
    [value.id, value["source-kind"], value.port, value.compass, property_lists(value)]],
  edge: {
    id: edge.id,
    endpoints: [for (value in children(edge, "dot-endpoint"))
      [value.kind, value.id, value.port, value.compass, value.operator]],
    properties: property_lists(edge)
  },
  assignment: [for (value in children(graph, "dot-assignment"))
    [value.name, value.value]],
  schema_codes: [for (value in schema.validate(graph)) value.code],
  recovery_codes: [for (value in model.diagnostics(recovered)) value.code],
  recovered_statements: [for (value in model.element_children(recovered)) model.tag(value)],
  ids: [for (value in id_nodes) [value.id, value["source-kind"], value.port,
    value.compass]],
  ids_parser_codes: [for (value in model.diagnostics(ids)) value.code],
  ids_schema_codes: [for (value in schema.validate(ids)) value.code]
}
