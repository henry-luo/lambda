// Pure source-order DOT semantics to canonical Graph IR.

import model: lambda.package.graph.model
import diagnostic: lambda.package.graph.diagnostics
import attributes: .attributes
import labels: .labels
import markers: .markers

let MAX_EXPANDED_EDGES = 100000

fn children(value, wanted) => [
  for (child in model.element_children(value) where model.tag(child) == wanted) child
]

fn first(values) => if (len(values) > 0) values[0] else null

fn statement_id(value) {
  if (value.id != null) string(value.id)
  else "dot@" ++ string(if (value["source-start"] != null) value["source-start"] else 0)
}

fn property_entries(value, origin, scope) => [
  for (group in children(value, "properties"), property in children(group, "property")) {
    name: string(property.name), value: string(property.value),
    kind: if (property["value-source-kind"] != null)
      string(property["value-source-kind"]) else "bare",
    origin: origin, scope: scope, statement: statement_id(value), source: property
  }
]

fn assignment_entry(value, scope) => {
  name: string(value.name), value: string(value.value),
  kind: if (value["value-source-kind"] != null)
    string(value["value-source-kind"]) else "bare",
  origin: "direct", scope: scope, statement: statement_id(value), source: value
}

fn inherited_properties(properties) => [
  for (entry in properties) {*:entry, origin: "inherited"}
]

fn unique(values) => [
  for (i, value in values
    where len([for (j, earlier in values where j < i and earlier == value) earlier]) == 0)
    value
]

fn node_at(nodes, id) {
  let matches = [for (entry in nodes where entry.id == id) entry];
  first(matches)
}

fn edge_at(edges, key) {
  let matches = [for (entry in edges where entry.key == key) entry];
  first(matches)
}

fn replace_node(nodes, value) => [
  for (entry in nodes) if (entry.id == value.id) value else entry
]

fn replace_edge(edges, value) => [
  for (entry in edges) if (entry.key == value.key) value else entry
]

fn scope_at(scopes, id) {
  let matches = [for (entry in scopes where entry.id == id) entry];
  first(matches)
}

fn replace_scope(scopes, value) => [
  for (entry in scopes) if (entry.id == value.id) value else entry
]

fn add_diagnostic(state, value) =>
  {*:state, diagnostics: [*state.diagnostics, value]}

fn append_scope_properties(state, scope, properties) {
  if (scope == "root") { {*:state, graph_properties: [*state.graph_properties, *properties]} }
  else {
    let current = scope_at(state.scopes, scope);
    if (current == null) state
    else { {*:state, scopes: replace_scope(state.scopes,
      {*:current, properties: [*current.properties, *properties]})} }
  }
}

fn set_scope_members(state, scope, members) {
  let current = scope_at(state.scopes, scope);
  if (current == null) state
  else { {*:state, scopes: replace_scope(state.scopes,
    {*:current, members: unique([*current.members, *members])})} }
}

fn ensure_node(state, id, source, defaults, direct, owner, explicit) {
  let current = node_at(state.nodes, id);
  if (current == null) {
    let value = {
      id: id, source: source, source_kind: source["source-kind"],
      properties: [*defaults, *direct], owner: owner
    };
    {*:state, nodes: [*state.nodes, value]}
  }
  else {
    // DOT defaults apply only when a node identity is first materialized.
    let value = {*:current,
      properties: if (explicit) [*current.properties, *direct] else current.properties,
      owner: if (explicit and owner != null and current.owner == null) owner else current.owner};
    {*:state, nodes: replace_node(state.nodes, value)}
  }
}

fn canonical_edge_key(from, to, directed) =>
  if (directed or from <= to) from ++ "\n" ++ to else to ++ "\n" ++ from

fn edge_id(statement, segment, expansion) =>
  statement_id(statement) ++ "-s" ++ string(segment) ++ "-e" ++ string(expansion)

fn edge_source_attrs(statement, segment, expansion, count) => {
  'source-statement-start': statement["source-start"],
  'source-statement-end': statement["source-end"],
  'source-statement-line': statement["source-line"],
  'source-statement-column': statement["source-column"],
  'source-segment-index': segment,
  'source-expansion-index': expansion,
  'source-expansion-count': count
}

fn edge_operator_diagnostic(state, statement, operator, directed) {
  let expected = if (directed) "->" else "--";
  if (operator == null or operator == expected) state
  else add_diagnostic(state, diagnostic.for_value(
    "graph.graphviz.edge-operator", "error",
    (if (directed) "Directed" else "Undirected") ++
      " DOT graph requires '" ++ expected ++ "' edges",
    "edge:" ++ statement_id(statement), statement))
}

fn add_edge(state, statement, segment, expansion, count, from, to,
    defaults, direct, directed, strict, operator) {
  let checked = edge_operator_diagnostic(state, statement, operator, directed);
  if (checked.expansion_count >= MAX_EXPANDED_EDGES) {
    if (checked.expansion_count == MAX_EXPANDED_EDGES)
      add_diagnostic(checked, diagnostic.for_value(
        "graph.graphviz.expansion-limit", "error",
        "DOT endpoint expansion exceeds " ++ string(MAX_EXPANDED_EDGES) ++ " edges",
        "edge:" ++ statement_id(statement), statement))
    else checked
  }
  else {
    let key = canonical_edge_key(from.id, to.id, directed);
    let current = if (strict) edge_at(checked.edges, key) else null;
    if (current != null) {
      let updated = {*:current, properties: [*current.properties, *direct],
        from_port: from.port, from_compass: from.compass,
        to_port: to.port, to_compass: to.compass};
      {*:checked, edges: replace_edge(checked.edges, updated),
        expansion_count: checked.expansion_count + 1}
    }
    else {
      let value = {
        id: edge_id(statement, segment, expansion), key: key,
        from: from.id, to: to.id, from_port: from.port, to_port: to.port,
        from_compass: from.compass, to_compass: to.compass,
        directed: directed, operator: operator, properties: [*defaults, *direct],
        source: statement,
        provenance: edge_source_attrs(statement, segment, expansion, count)
      };
      {*:checked, edges: [*checked.edges, value],
        expansion_count: checked.expansion_count + 1}
    }
  }
}

fn add_pairs(pairs, i, state, statement, segment, defaults, direct,
    directed, strict, operator) {
  if (i >= len(pairs)) state
  else add_pairs(pairs, i + 1,
    add_edge(state, statement, segment, i, len(pairs), pairs[i][0], pairs[i][1],
      defaults, direct, directed, strict, operator),
    statement, segment, defaults, direct, directed, strict, operator)
}

fn expand_segments(groups, i, state, statement, defaults, direct, directed, strict) {
  if (i >= len(groups)) state
  else {
    let pairs = [for (from in groups[i - 1].values, to in groups[i].values) [from, to]];
    let next = add_pairs(pairs, 0, state, statement, i - 1, defaults, direct,
      directed, strict, groups[i].operator);
    expand_segments(groups, i + 1, next, statement, defaults, direct, directed, strict)
  }
}

fn valid_compass(value) => value == null or
  contains(["n", "ne", "e", "se", "s", "sw", "w", "nw", "c", "_"], lower(value))

fn endpoint_compass_diagnostic(state, endpoint) {
  if (valid_compass(endpoint.compass)) state
  else add_diagnostic(state, diagnostic.for_value(
    "graph.graphviz.invalid-compass", "error",
    "Invalid DOT compass point '" ++ string(endpoint.compass) ++ "'",
    "endpoint:" ++ string(endpoint.id), endpoint))
}

fn endpoint_values(endpoint, state, env, scope, owner, directed, strict) {
  if (endpoint.kind == "node") {
    let id = string(endpoint.id);
    let next = endpoint_compass_diagnostic(
      ensure_node(state, id, endpoint, env.node, [], owner, false), endpoint);
    {state: next, values: [{id: id, port: endpoint.port, compass: endpoint.compass}],
      members: [id]}
  }
  else {
    let nested = first(children(endpoint, "subgraph"));
    if (nested == null) { {state: state, values: [], members: []} }
    else {
      let result = walk_subgraph(nested, state, env, scope, owner, directed, strict);
      {state: result.state,
        values: [for (id in result.members) {id: id, port: null, compass: null}],
        members: result.members}
    }
  }
}

fn endpoint_groups(endpoints, i, state, env, scope, owner, directed, strict, groups, members) {
  if (i >= len(endpoints)) { {state: state, groups: groups, members: unique(members)} }
  else {
    let endpoint = endpoints[i];
    let result = endpoint_values(endpoint, state, env, scope, owner, directed, strict);
    let group = {values: result.values, operator: endpoint.operator};
    endpoint_groups(endpoints, i + 1, result.state, env, scope, owner, directed, strict,
      [*groups, group], [*members, *result.members])
  }
}

fn edge_statement(value, state, env, scope, owner, directed, strict) {
  let endpoints = children(value, "dot-endpoint");
  let grouped = endpoint_groups(endpoints, 0, state, env, scope, owner,
    directed, strict, [], []);
  let direct = property_entries(value, "direct", scope);
  {
    state: if (len(grouped.groups) > 1)
      expand_segments(grouped.groups, 1, grouped.state, value, env.edge, direct,
        directed, strict)
      else grouped.state,
    members: grouped.members
  }
}

fn scope_role(id) => if (starts_with(lower(id), "cluster")) "cluster" else "scope"

fn walk_statement(value, state, env, scope, owner, directed, strict) {
  let tag = model.tag(value);
  if (tag == "dot-attr-statement") {
    let target = string(value["target-kind"]);
    let origin = if (target == "graph") "direct" else "default";
    let properties = property_entries(value, origin, scope);
    let next_env = if (target == "node") { {*:env, node: [*env.node, *properties]} }
      else if (target == "edge") { {*:env, edge: [*env.edge, *properties]} }
      else { {*:env, graph: [*env.graph, *properties]} };
    {state: if (target == "graph") append_scope_properties(state, scope, properties) else state,
      env: next_env, members: []}
  }
  else if (tag == "dot-assignment") {
    let property = assignment_entry(value, scope);
    {state: append_scope_properties(state, scope, [property]),
      env: {*:env, graph: [*env.graph, property]}, members: []}
  }
  else if (tag == "node") {
    let id = string(value.id);
    let direct = property_entries(value, "direct", scope);
    {state: ensure_node(state, id, value, env.node, direct, owner, true),
      env: env, members: [id]}
  }
  else if (tag == "dot-edge-statement") {
    let result = edge_statement(value, state, env, scope, owner, directed, strict);
    {state: result.state, env: env, members: result.members}
  }
  else if (tag == "subgraph") {
    let result = walk_subgraph(value, state, env, scope, owner, directed, strict);
    {state: result.state, env: env, members: result.members}
  }
  else { {state: state, env: env, members: []} }
}

fn walk_at(items, i, state, env, scope, owner, directed, strict, members) {
  if (i >= len(items)) {
    {state: set_scope_members(state, scope, members), members: unique(members)}
  }
  else {
    let result = walk_statement(items[i], state, env, scope, owner, directed, strict);
    walk_at(items, i + 1, result.state, result.env, scope, owner, directed, strict,
      [*members, *result.members])
  }
}

fn walk_subgraph(value, state, env, parent_scope, parent_owner, directed, strict) {
  let id = string(value.id);
  let role = scope_role(id);
  let owner = if (role == "cluster") id else parent_owner;
  let scope = {
    id: id, role: role, parent_scope: parent_scope, parent_owner: parent_owner,
    owner: owner, source: value, properties: inherited_properties(env.graph), members: []
  };
  let opened = {*:state, scopes: [*state.scopes, scope]};
  walk_at(model.element_children(value), 0, opened, env, id, owner, directed, strict, [])
}

fn property_element(entry) {
  <property name: entry.name, value: entry.value,
    'value-source-kind': entry.kind, origin: entry.origin,
    'defining-scope': entry.scope, 'defining-statement': entry.statement,
    'source-start': entry.source["source-start"],
    'source-end': entry.source["source-end"],
    'source-line': entry.source["source-line"],
    'source-column': entry.source["source-column"]>
}

fn properties_element(properties) {
  if (len(properties) == 0) null
  else <properties namespace: "graphviz";
    for (entry in properties) property_element(entry)
  >
}

fn node_element(entry, graph_id) {
  let known = attributes.canonical("node", entry.properties);
  <node id: entry.id, 'source-kind': entry.source_kind,
    label: labels.node(known.label, entry.id, graph_id),
    'label-format': known["label-format"], shape: known.shape,
    'shape-family': known["shape-family"], 'graphviz-shape': known["graphviz-shape"],
    width: known.width, height: known.height, 'fixed-size': known["fixed-size"],
    fill: known.fill, stroke: known.stroke, 'stroke-width': known["stroke-width"],
    group: known.group, style: known.style, 'stroke-dasharray': known["stroke-dasharray"],
    opacity: known.opacity, radius: known.radius,
    'source-start': entry.source["source-start"], 'source-end': entry.source["source-end"],
    'source-line': entry.source["source-line"], 'source-column': entry.source["source-column"];
    let properties = properties_element(entry.properties)
    if (properties != null) { properties }
  >
}

fn edge_element(entry, graph_id) {
  let known = attributes.canonical("edge", entry.properties);
  <edge id: entry.id, 'source-id': statement_id(entry.source),
    from: entry.from, to: entry.to, directed: entry.directed,
    'from-port': entry.from_port, 'to-port': entry.to_port,
    'from-compass': entry.from_compass, 'to-compass': entry.to_compass,
    label: labels.edge(known.label, entry.from, entry.to, entry.directed, graph_id),
    'label-format': known["label-format"],
    'arrow-head': markers.head(known["arrow-head"], known["arrow-direction"], entry.directed),
    'arrow-tail': markers.tail(known["arrow-tail"], known["arrow-direction"], entry.directed),
    'arrow-direction': known["arrow-direction"],
    'min-length': known["min-length"], weight: known.weight,
    constraint: known.constraint, stroke: known.stroke,
    'stroke-width': known["stroke-width"], style: known.style,
    'stroke-dasharray': known["stroke-dasharray"], opacity: known.opacity,
    'head-cluster': known["head-cluster"], 'tail-cluster': known["tail-cluster"],
    'source-start': entry.source["source-start"], 'source-end': entry.source["source-end"],
    'source-line': entry.source["source-line"], 'source-column': entry.source["source-column"],
    'source-statement-start': entry.provenance["source-statement-start"],
    'source-statement-end': entry.provenance["source-statement-end"],
    'source-statement-line': entry.provenance["source-statement-line"],
    'source-statement-column': entry.provenance["source-statement-column"],
    'source-segment-index': entry.provenance["source-segment-index"],
    'source-expansion-index': entry.provenance["source-expansion-index"],
    'source-expansion-count': entry.provenance["source-expansion-count"];
    let properties = properties_element(entry.properties)
    if (properties != null) { properties }
  >
}

fn member_element(id) => <member node: id>

fn local_scope_property(scope, property_name) {
  let matches = [for (entry in scope.properties
    where lower(entry.name) == property_name and entry.scope == scope.id) entry];
  if (len(matches) > 0) matches[len(matches) - 1] else null
}

fn rank_constraint(scope) {
  let entry = local_scope_property(scope, "rank");
  if (entry == null or not contains(["same", "min", "max", "source", "sink"],
      lower(entry.value))) null
  else <constraint kind: "rank", value: lower(entry.value), scope: scope.id;
    for (id in scope.members) member_element(id)
  >
}

fn constraints_element(scopes) {
  let values = [for (scope in scopes, let value = rank_constraint(scope)
    where value != null) value];
  if (len(values) == 0) null
  else <constraints;
    for (value in values) value
  >
}

fn rank_diagnostic(scope, entry) =>
  if (entry == null or contains(["same", "min", "max", "source", "sink"],
      lower(entry.value))) []
  else [diagnostic.for_value(
    "graph.graphviz.invalid-rank", "error",
    "Unsupported DOT rank constraint '" ++ entry.value ++ "'",
    "subgraph:" ++ scope.id ++ ".rank", entry.source)]

fn rank_diagnostics(state) => [
  for (scope in state.scopes,
    value in rank_diagnostic(scope, local_scope_property(scope, "rank"))) value
]

fn engine_diagnostic(source, layout) =>
  if (layout == null or lower(layout) == "dot") []
  else [diagnostic.for_value(
    "graph.graphviz.unsupported-engine", "error",
    "Graphviz layout engine '" ++ layout ++ "' is not implemented",
    "graph.layout", source)]

fn engine_diagnostics(source, state) =>
  engine_diagnostic(source, attributes.value(state.graph_properties, "layout"))

fn semantic_diagnostics(source, state) => [
  *rank_diagnostics(state), *engine_diagnostics(source, state)
]

fn cluster_element(scope, state, graph_id) {
  let known = attributes.canonical("cluster", scope.properties);
  <subgraph id: scope.id, role: scope.role, 'parent-scope': scope.parent_scope,
    label: labels.graph(known.label, graph_id), 'label-format': known["label-format"],
    direction: known.direction, fill: known.fill,
    'source-start': scope.source["source-start"], 'source-end': scope.source["source-end"],
    'source-line': scope.source["source-line"], 'source-column': scope.source["source-column"];
    let properties = properties_element(scope.properties)
    if (properties != null) { properties }
    for (entry in state.nodes where entry.owner == scope.id) node_element(entry, graph_id)
    for (nested in state.scopes
      where nested.role == "cluster" and nested.parent_owner == scope.id)
      cluster_element(nested, state, graph_id)
    for (id in scope.members) member_element(id)
  >
}

fn scope_element(scope) {
  <subgraph id: scope.id, role: "scope", 'parent-scope': scope.parent_scope,
    'source-start': scope.source["source-start"], 'source-end': scope.source["source-end"],
    'source-line': scope.source["source-line"], 'source-column': scope.source["source-column"];
    let properties = properties_element(scope.properties)
    if (properties != null) { properties }
    for (id in scope.members) member_element(id)
  >
}

fn canonical_graph(source, state) {
  let known = attributes.canonical("graph", state.graph_properties);
  <graph id: source.id, flavor: "dot", version: source.version,
    layout: if (known.layout != null) known.layout else "dot",
    directed: source.directed, strict: source.strict, 'ir-stage': "canonical",
    direction: known.direction, 'node-sep': known["node-sep"],
    'rank-sep': known["rank-sep"], fill: known.fill,
    label: labels.graph(known.label, string(source.id)),
    'label-format': known["label-format"],
    'source-start': source["source-start"], 'source-end': source["source-end"],
    'source-line': source["source-line"], 'source-column': source["source-column"];
    let properties = properties_element(state.graph_properties)
    if (properties != null) { properties }
    let constraints = constraints_element(state.scopes)
    if (constraints != null) { constraints }
    for (entry in state.nodes where entry.owner == null) node_element(entry, string(source.id))
    for (scope in state.scopes
      where scope.role == "cluster" and scope.parent_owner == null)
      cluster_element(scope, state, string(source.id))
    for (scope in state.scopes where scope.role == "scope") scope_element(scope)
    for (entry in state.edges) edge_element(entry, string(source.id))
    for (child in model.element_children(source) where model.tag(child) == "diagnostics") child
  >
}

pub fn normalize(source) {
  let initial = {
    nodes: [], edges: [], scopes: [], graph_properties: [], diagnostics: [],
    expansion_count: 0
  };
  let env = {graph: [], node: [], edge: []};
  let result = walk_at(model.element_children(source), 0, initial, env, "root", null,
    source.directed != false, source.strict == true, []);
  {graph: canonical_graph(source, result.state),
    diagnostics: [*result.state.diagnostics, *semantic_diagnostics(source, result.state)]}
}
