// Validation boundary from parser output or authored Mark to canonical Graph IR.

import model: .model
import diagnostic: .diagnostics
import schema: .schema
import graph_content: .transform.content
import graphviz_normalize: .graphviz.normalize
import mermaid_class: .mermaid.class

fn noncanonical_children(value) => [
  for (child in model.child_items(value)
    where not (child is element) or
      (model.tag(child) != "label" and model.tag(child) != "content")) child
]

fn node_content_children(value) => [
  for (child in noncanonical_children(value)
    where not (child is element and
      (model.tag(child) == "port" or model.tag(child) == "properties"))) child
]

fn node_ports(value) => [
  for (child in model.child_items(value)
    where child is element and model.tag(child) == "port") child
]

fn node_metadata(value) => [
  for (child in noncanonical_children(value)
    where child is element and model.tag(child) == "properties") child
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
        len(noncanonical_children(child)) == len(node_ports(child)) + len(node_metadata(child))
    }
    else if (child_tag == "edge") { has_canonical_label_content(child, false) }
    else if (child_tag == "subgraph") {
      has_canonical_label_content(child,
        child.role != "scope" and child.id != null and child.id != "") and
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
  let authored_ports = node_ports(node);
  let names = if (content != null)
    graph_content.port_names(model.child_items(content)) else [];
  let generated_names = [for (name in names where len([
    for (port in authored_ports where port.id != null and
      string(port.id) == string(name)) port
  ]) == 0) name];
  let ports = [*authored_ports, for (i, name in generated_names)
    <port id: name, side: "auto",
      offset: (float(i) + 0.5) / float(len(generated_names))>];
  <node *:attrs;
    if (label != null) { label }
    if (content != null) { content }
    for (port in ports) port
    for (metadata in node_metadata(node)) metadata
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
  let fallback = if (subgraph.role != "scope" and subgraph.id != null and subgraph.id != "")
    string(subgraph.id) else null;
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

fn is_mermaid_source_graph(graph) =>
  graph is element and model.tag(graph) == "graph" and
    graph.flavor == "mermaid" and graph["ir-stage"] == "source"

fn is_dot_source_graph(graph) =>
  graph is element and model.tag(graph) == "graph" and
    graph.flavor == "dot" and graph["ir-stage"] == "source"

fn node_declaration_groups(graph) {
  let entries = [for (entry in model.node_entries(graph)
    where entry.value.id != null) {
      id: string(entry.value.id), value: entry.value
    }];
  let grouped = [for (entry in entries group by entry.id into declarations) {
    id: declarations.id,
    // group keys are attributes on the grouped value and are not declarations.
    values: [for (declaration in declarations
      where declaration.value != null) declaration.value]
  }];
  map([for (declarations in grouped,
    pair in [declarations.id, declarations.values]) pair])
}

fn merge_node_attrs_at(values, i, attrs) {
  if (i >= len(values)) { attrs }
  else {
    let next_attrs = map(values[i]);
    let merged = {*:attrs, *:next_attrs};
    merge_node_attrs_at(values, i + 1, merged)
  }
}

fn source_declaration_matches(a, b) =>
  a["source-start"] == b["source-start"] and
    a["source-end"] == b["source-end"] and
    a["source-line"] == b["source-line"] and
    a["source-column"] == b["source-column"]

fn is_first_node_declaration(groups, node) {
  let values = if (node.id != null) groups[string(node.id)] else null;
  if (values != null and len(values) > 0) { source_declaration_matches(node, values[0]) }
  // missing ids must survive merging so canonical validation can diagnose them.
  else if (node.id == null) { true }
  else { false }
}

fn merged_source_node(groups, node) {
  let values = if (node.id != null) groups[string(node.id)] else [node];
  let final_declaration = values[len(values) - 1];
  let attrs = merge_node_attrs_at(values, 0, {});
  <node *:attrs;
    // mermaid redeclarations replace authored node content, while ports remain
    // cumulative metadata attached to the graph-global node identity.
    for (child in model.child_items(final_declaration)
      where not (child is element and model.tag(child) == "port")) child
    for (declaration in values, port in node_ports(declaration)) port
  >
}

fn merged_source_children(groups, container) => [
  for (child in model.child_items(container), merged in
    if (not (child is element)) [child]
    else if (model.tag(child) == "node")
      (if (is_first_node_declaration(groups, child)) [merged_source_node(groups, child)] else [])
    else if (model.tag(child) == "subgraph") [merged_source_subgraph(groups, child)]
    else [child]) merged
]

fn merged_source_subgraph(groups, subgraph) {
  let attrs = map(subgraph);
  <subgraph *:attrs;
    for (child in merged_source_children(groups, subgraph)) child
  >
}

fn merge_mermaid_source_graph(graph) {
  let attrs = {*:map(graph), 'ir-stage': "canonical"};
  let groups = node_declaration_groups(graph);
  <graph *:attrs;
    for (child in merged_source_children(groups, graph)) child
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

fn resolve_dot_endpoint(edge, role, entries) {
  let port_key = role ++ "-port";
  let compass_key = role ++ "-compass";
  let port = edge[port_key];
  let compass = edge[compass_key];
  let candidate = if (port != null) lower(string(port)) else null;
  // DOT's one-token suffix is a compass only when no generated/authored port has that name.
  let implicit_compass = compass == null and candidate != null and
    graphviz_normalize.valid_compass(candidate) and
    not has_port(entries, string(edge[if (role == "from") "from" else "to"]),
      string(port));
  {
    port: if (implicit_compass) null else port,
    compass: if (implicit_compass) candidate else compass,
    changed: implicit_compass
  }
}

fn resolve_dot_edge(edge, entries) {
  let from = resolve_dot_endpoint(edge, "from", entries);
  let to = resolve_dot_endpoint(edge, "to", entries);
  let attrs = {*:map(edge), 'from-port': from.port, 'from-compass': from.compass,
    'to-port': to.port, 'to-compass': to.compass};
  <edge *:attrs;
    for (child in model.child_items(edge)) child
  >
}

fn resolve_dot_child(child, entries) {
  if (not (child is element)) child
  else if (model.tag(child) == "edge") resolve_dot_edge(child, entries)
  else if (model.tag(child) == "subgraph") {
    let attrs = map(child);
    <subgraph *:attrs;
      for (nested in model.child_items(child)) resolve_dot_child(nested, entries)
    >
  }
  else child
}

fn dot_child_needs_compass_resolution(child, entries) {
  if (not (child is element)) false
  else if (model.tag(child) == "edge")
    resolve_dot_endpoint(child, "from", entries).changed or
      resolve_dot_endpoint(child, "to", entries).changed
  else if (model.tag(child) == "subgraph") all([
    for (nested in model.child_items(child))
      not dot_child_needs_compass_resolution(nested, entries)
  ]) == false
  else false
}

fn resolve_dot_compass_ports(graph) {
  let entries = model.port_entries(graph);
  let needed = [for (child in model.child_items(graph)
    where dot_child_needs_compass_resolution(child, entries)) child];
  if (len(needed) == 0) graph
  else {
    let attrs = map(graph);
    <graph *:attrs;
      for (child in model.child_items(graph)) resolve_dot_child(child, entries)
    >
  }
}

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
  // mermaid parser output retains redeclarations; merge that source stage before
  // canonical uniqueness checks while authored canonical Mark keeps strict IDs.
  let dot_result = if (is_dot_source_graph(graph)) graphviz_normalize.normalize(graph)
    else {graph: graph, diagnostics: []};
  let resolved = if (is_mermaid_source_graph(dot_result.graph)) {
    let merged = merge_mermaid_source_graph(dot_result.graph);
    if (merged["diagram-type"] == "class") mermaid_class.adapt(merged) else merged
  } else dot_result.graph;
  let canonical = if (resolved is element and string(name(resolved)) == "graph")
    (if (is_canonical_graph(resolved)) resolved else canonical_graph(resolved)) else resolved;
  let final_graph = if (canonical is element and canonical.flavor == "dot")
    resolve_dot_compass_ports(canonical) else canonical;
  // Validate authored structure before rebuilding so duplicate canonical children
  // cannot disappear without a diagnostic when the canonical pair is selected.
  let values = [*dot_result.diagnostics,
    *validate(if (is_dot_source_graph(graph)) final_graph else resolved)];
  {
    graph: final_graph,
    diagnostics: values,
    valid: not diagnostic.has_errors(values)
  }
}
