import model: lambda.package.graph.model
import normalize: lambda.package.graph.normalize

let invalid = <graph direction: "sideways";
  <node id: "A">
  <node id: "A">
  <edge id: "same", from: "A", to: "B">
  <edge id: "same", from: "A", to: "A">
>
let invalid_result = normalize.normalize(invalid)

let parsed^parse_err = input("test/lambda/graph/mermaid/edge_ids_markers.mmd",
  {type: "graph", flavor: "mermaid"})
let parsed_result = normalize.normalize(parsed)

let chart^chart_err = input("test/lambda/graph/mermaid/chart_routing.mmd",
  {type: "graph", flavor: "mermaid"})
let chart_result = normalize.normalize(chart)

{
  invalid_valid: invalid_result.valid,
  invalid_codes: [for (value in invalid_result.diagnostics) value.code],
  invalid_paths: [for (value in invalid_result.diagnostics) value.path],
  parsed_valid: parsed_result.valid,
  parsed_diagnostics: parsed_result.diagnostics,
  node_spans: [for (node in model.nodes(parsed)) [
    node.id,
    node["source-start"] < node["source-end"],
    node["source-line"] > 0
  ]],
  edge_spans: [for (edge in model.edges(parsed)) [
    edge.id,
    edge["source-start"] < edge["source-end"],
    edge["source-line"] > 0
  ]],
  chart_valid: chart_result.valid,
  chart_codes: [for (value in chart_result.diagnostics) value.code]
}
