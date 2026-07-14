// Validation boundary from parser output or authored Mark to canonical Graph IR.

import model: .model
import diagnostic: .diagnostics

fn item_id(value) =>
  if (value.id != null and value.id != "") string(value.id) else ""

fn missing_id_diagnostics(entries, kind) => [
  for (i, value in entries where item_id(value) == "") diagnostic.for_value(
    "graph." ++ kind ++ ".missing-id", "error",
    "Graph " ++ kind ++ " requires a stable id", kind ++ "[" ++ string(i) ++ "]", value)
]

fn duplicate_id_diagnostics_at(entries, kind, i) {
  if (i >= len(entries)) { [] }
  else {
    let entry = entries[i];
    let local = if (i > 0 and entry.id == entries[i - 1].id) [
      diagnostic.for_value(
        "graph." ++ kind ++ ".duplicate-id", "error",
        "Duplicate graph " ++ kind ++ " id '" ++ entry.id ++ "'",
        kind ++ ":" ++ entry.id, entry.value)
    ] else [];
    [*local, *duplicate_id_diagnostics_at(entries, kind, i + 1)]
  }
}

fn duplicate_id_diagnostics(entries, kind) {
  let identified = [for (value in entries, let id = item_id(value)
    where id != "") {id: id, value: value}];
  duplicate_id_diagnostics_at(sort(identified, (entry) => entry.id), kind, 0)
}

fn edge_identity_diagnostics(edges) => [
  for (i, edge in edges where item_id(edge) == "") diagnostic.for_value(
    "graph.edge.generated-id", "warning",
    "Graph edge has no explicit id; consumers will derive one from source order",
    "edge[" ++ string(i) ++ "]", edge)
]

fn has_sorted_id(ids, id, low, high) {
  if (low > high) false
  else {
    let middle = int((low + high) / 2);
    if (ids[middle] == id) true
    else if (ids[middle] < id) has_sorted_id(ids, id, middle + 1, high)
    else has_sorted_id(ids, id, low, middle - 1)
  }
}

fn has_node_id(node_ids, id) =>
  has_sorted_id(node_ids, id, 0, len(node_ids) - 1)

fn endpoint_diagnostics(edges, node_ids) => [
  for (i, edge in edges,
    endpoint in [
      {role: "from", id: if (edge.from != null) string(edge.from) else ""},
      {role: "to", id: if (edge.to != null) string(edge.to) else ""}
    ], let code = if (endpoint.id == "") "graph.edge.missing-endpoint"
      else "graph.edge.unresolved-endpoint",
    let message = if (endpoint.id == "")
      "Graph edge requires a '" ++ endpoint.role ++ "' endpoint"
    else
      "Graph edge " ++ endpoint.role ++ " endpoint '" ++ endpoint.id ++ "' does not resolve"
    where endpoint.id == "" or not has_node_id(node_ids, endpoint.id)) diagnostic.for_value(
    code, "error", message,
    "edge[" ++ string(i) ++ "]." ++ endpoint.role, edge)
]

fn direction_diagnostics(graph) {
  let direction = model.direction(graph);
  if (contains(["TB", "BT", "LR", "RL"], direction)) { [] }
  else [diagnostic.for_value(
    "graph.invalid-direction", "error",
    "Unsupported graph direction '" ++ direction ++ "'", "graph.direction", graph)]
}

fn parser_diagnostics(graph) =>
  [for (value in model.diagnostics(graph)) diagnostic.from_element(value)]

fn graph_diagnostics(graph) {
  let nodes = model.nodes(graph);
  let edges = model.edges(graph);
  let subgraphs = model.subgraphs(graph);
  let node_ids = sort([for (node in nodes, let id = item_id(node) where id != "") id]);
  [
    *parser_diagnostics(graph),
    *direction_diagnostics(graph),
    *missing_id_diagnostics(nodes, "node"),
    *missing_id_diagnostics(subgraphs, "subgraph"),
    *duplicate_id_diagnostics(nodes, "node"),
    *duplicate_id_diagnostics(subgraphs, "subgraph"),
    *duplicate_id_diagnostics(edges, "edge"),
    *edge_identity_diagnostics(edges),
    *endpoint_diagnostics(edges, node_ids)
  ]
}

pub fn validate(graph) {
  if (not (graph is element) or string(name(graph)) != "graph") { [
    diagnostic.make("graph.invalid-root", "error",
      "Canonical Graph IR root must be a <graph> element", "graph", null)
  ] }
  else { graph_diagnostics(graph) }
}

pub fn normalize(graph) {
  let values = validate(graph);
  {
    graph: graph,
    diagnostics: values,
    valid: not diagnostic.has_errors(values)
  }
}
