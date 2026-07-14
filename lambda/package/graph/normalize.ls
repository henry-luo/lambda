// Validation boundary from parser output or authored Mark to canonical Graph IR.

import model: .model
import diagnostic: .diagnostics
import schema: .schema
import graph_content: .transform.content

fn noncanonical_children(value) => [
  for (child in model.child_items(value)
    where not (child is element) or
      (model.tag(child) != "label" and model.tag(child) != "content")) child
]

fn node_content_children(value) => [
  for (child in noncanonical_children(value)
    where not (child is element and model.tag(child) == "port")) child
]

fn node_ports(value) => [
  for (child in model.child_items(value)
    where child is element and model.tag(child) == "port") child
]

fn direct_tag_count(value, wanted_tag) => len([
  for (child in model.child_items(value)
    where child is element and model.tag(child) == wanted_tag) child
])

fn has_canonical_label_content(value, require_pair) {
  let labels = direct_tag_count(value, "label");
  let contents = direct_tag_count(value, "content");
  (labels <= 1) and (contents <= 1) and labels == contents and
    (not require_pair or labels == 1)
}

fn is_canonical_child(child) {
  if (not (child is element)) true
  else {
    let child_tag = model.tag(child);
    if (child_tag == "node") {
      has_canonical_label_content(child, true) and
        len(noncanonical_children(child)) == len(node_ports(child))
    }
    else if (child_tag == "edge") { has_canonical_label_content(child, false) }
    else if (child_tag == "subgraph") {
      has_canonical_label_content(child, child.id != null and child.id != "") and
        all([for (nested in noncanonical_children(child)) is_canonical_child(nested)])
    }
    else { true }
  }
}

fn is_canonical_graph(graph) =>
  all([for (child in model.child_items(graph)) is_canonical_child(child)])

fn canonical_label(value, fallback = null) {
  let source = model.label_source(value, fallback);
  let format = model.label_format(value);
  if (source == null) { null }
  else { <label format: format; source> }
}

fn canonical_content(value, label, preserve_other_children, authored_children = null) {
  let existing = model.content_element(value);
  let authored = if (existing != null) model.content_items(value)
    else if (authored_children != null) authored_children
    else if (preserve_other_children) noncanonical_children(value)
    else [];
  let lowered = if (len(authored) > 0) authored
    else if (label != null and len(label) > 0)
      graph_content.lower(label[0], string(label.format))
    else [];
  if (label == null and len(lowered) == 0) { null }
  else {
    let result = <content;
      for (child in lowered) child
    >
    result
  }
}

fn canonical_node(node) {
  let attrs = map(node);
  let fallback = if (node.id != null and node.id != "") string(node.id) else null;
  let label = canonical_label(node, fallback);
  let content = canonical_content(node, label, true, node_content_children(node));
  <node *:attrs;
    if (label != null) { label }
    if (content != null) { content }
    for (port in node_ports(node)) port
  >
}

fn canonical_edge(edge) {
  let attrs = map(edge);
  let label = canonical_label(edge);
  let content = canonical_content(edge, label, false);
  <edge *:attrs;
    if (label != null) { label }
    if (content != null) { content }
    for (child in noncanonical_children(edge)) child
  >
}

fn canonical_subgraph(subgraph) {
  let attrs = map(subgraph);
  let fallback = if (subgraph.id != null and subgraph.id != "") string(subgraph.id) else null;
  let label = canonical_label(subgraph, fallback);
  let content = canonical_content(subgraph, label, false);
  <subgraph *:attrs;
    if (label != null) { label }
    if (content != null) { content }
    for (child in noncanonical_children(subgraph)) canonical_child(child)
  >
}

fn canonical_child(child) {
  if (not (child is element)) child
  else {
    let child_tag = model.tag(child);
    if (child_tag == "node") canonical_node(child)
    else if (child_tag == "edge") canonical_edge(child)
    else if (child_tag == "subgraph") canonical_subgraph(child)
    else child
  }
}

fn canonical_graph(graph) {
  let attrs = map(graph);
  <graph *:attrs;
    for (child in model.child_items(graph)) canonical_child(child)
  >
}

fn item_id(value) =>
  if (value.id != null and value.id != "") string(value.id) else ""

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
    ], let message = "Graph edge " ++ endpoint.role ++ " endpoint '" ++
      endpoint.id ++ "' does not resolve"
    where endpoint.id != "" and not has_node_id(node_ids, endpoint.id)) diagnostic.for_value(
    "graph.edge.unresolved-endpoint", "error", message,
    "edge[" ++ string(i) ++ "]." ++ endpoint.role, edge)
]

fn duplicate_port_diagnostics(entries) => [
  for (i, entry in entries, let id = item_id(entry.value),
    let earlier = [for (j, other in entries
      where j < i and other.node == entry.node and item_id(other.value) == id) other]
    where id != "" and len(earlier) > 0) diagnostic.for_value(
    "graph.port.duplicate-id", "error",
    "Duplicate port id '" ++ id ++ "' on graph node '" ++ entry.node ++ "'",
    "node:" ++ entry.node ++ ".port:" ++ id, entry.value)
]

fn has_port(entries, node_id, port_id) => len([
  for (entry in entries
    where entry.node == node_id and item_id(entry.value) == port_id) entry
]) > 0

fn edge_port_diagnostics(edges, entries) => [
  for (i, edge in edges, reference in [
      {role: "from-port", node: if (edge.from != null) string(edge.from) else "",
        id: if (edge["from-port"] != null) string(edge["from-port"]) else ""},
      {role: "to-port", node: if (edge.to != null) string(edge.to) else "",
        id: if (edge["to-port"] != null) string(edge["to-port"]) else ""}
    ]
    where reference.id != "" and not has_port(entries, reference.node, reference.id))
    diagnostic.for_value(
      "graph.edge.unresolved-port", "error",
      "Graph edge " ++ reference.role ++ " '" ++ reference.id ++
        "' does not resolve on node '" ++ reference.node ++ "'",
      "edge[" ++ string(i) ++ "]." ++ reference.role, edge)
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
  let ports = model.port_entries(graph);
  let node_ids = sort([for (node in nodes, let id = item_id(node) where id != "") id]);
  [
    *parser_diagnostics(graph),
    *direction_diagnostics(graph),
    *duplicate_id_diagnostics(nodes, "node"),
    *duplicate_id_diagnostics(subgraphs, "subgraph"),
    *duplicate_id_diagnostics(edges, "edge"),
    *duplicate_port_diagnostics(ports),
    *edge_identity_diagnostics(edges),
    *endpoint_diagnostics(edges, node_ids),
    *edge_port_diagnostics(edges, ports)
  ]
}

pub fn validate(graph) {
  if (not (graph is element) or string(name(graph)) != "graph") { [
    diagnostic.make("graph.invalid-root", "error",
      "Canonical Graph IR root must be a <graph> element", "graph", null)
  ] }
  else { [*schema.validate(graph), *graph_diagnostics(graph)] }
}

pub fn normalize(graph) {
  let canonical = if (graph is element and string(name(graph)) == "graph")
    (if (is_canonical_graph(graph)) graph else canonical_graph(graph)) else graph;
  // Validate authored structure before rebuilding so duplicate canonical children
  // cannot disappear without a diagnostic when the canonical pair is selected.
  let values = validate(graph);
  {
    graph: canonical,
    diagnostics: values,
    valid: not diagnostic.has_errors(values)
  }
}
