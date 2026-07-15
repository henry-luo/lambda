// C4 workspace view projection to canonical Graph IR.

import graph_model: lambda.package.graph.model

fn children(value, wanted = null) => [
  for (child in graph_model.element_children(value)
    where wanted == null or graph_model.tag(child) == wanted) child
]

fn first(values) => if (len(values) > 0) values[0] else null

fn workspace_model(workspace) => first(children(workspace, "c4-model"))
fn workspace_views(workspace) => first(children(workspace, "c4-views"))

fn elements(workspace) => children(workspace_model(workspace), "c4-element")
fn relationships(workspace) => children(workspace_model(workspace), "c4-relationship")

pub fn view_keys(workspace) => [
  for (diagram in children(workspace_views(workspace), "c4-view")) string(diagram.key)
]

fn selected_view(workspace, key) {
  let matches = [for (diagram in children(workspace_views(workspace), "c4-view")
    where string(diagram.key) == string(key)) diagram];
  first(matches)
}

fn connected_ids_at(relations, ids, index) {
  if (index >= len(relations)) { [] }
  else {
    let relation = relations[index];
    let rest = connected_ids_at(relations, ids, index + 1);
    if (contains(ids, string(relation.source)) or contains(ids, string(relation.destination))) {
      [string(relation.source), string(relation.destination), *rest]
    } else { rest }
  }
}

fn connected_ids(workspace, ids) => connected_ids_at(relationships(workspace), ids, 0)

fn base_ids(workspace, diagram) {
  let all = elements(workspace);
  let diagram_kind = string(diagram.kind);
  let scope_id = string(diagram.scope);
  if (diagram_kind == "systemLandscape") { [
    for (entry in all where contains(["person", "software-system"], string(entry.kind)))
      string(entry.id)
  ] }
  else if (diagram_kind == "systemContext") {
    let seed = [scope_id];
    [for (entry in all
      where contains(["person", "software-system"], string(entry.kind)) and
        contains([*seed, *connected_ids(workspace, seed)], string(entry.id))) string(entry.id)]
  }
  else if (diagram_kind == "container") {
    let inside = [for (entry in all
      where string(entry.parent) == scope_id and string(entry.kind) == "container") string(entry.id)];
    [for (entry in all where contains([*inside, *connected_ids(workspace, inside)],
      string(entry.id))) string(entry.id)]
  }
  else if (diagram_kind == "component") {
    let inside = [for (entry in all
      where string(entry.parent) == scope_id and string(entry.kind) == "component") string(entry.id)];
    [for (entry in all where contains([*inside, *connected_ids(workspace, inside)],
      string(entry.id))) string(entry.id)]
  }
  else { [for (entry in all) string(entry.id)] }
}

fn include_expressions(diagram) => [
  for (include_rule in children(diagram, "include")) string(include_rule.expression)
]

fn explicit_ids(workspace, expressions) {
  let all = elements(workspace);
  [for (entry in all where contains(expressions, string(entry.identifier))) string(entry.id)]
}

fn selected_ids(workspace, diagram) {
  let expressions = include_expressions(diagram);
  let defaults = base_ids(workspace, diagram);
  if (len(expressions) == 0 or contains(expressions, "*") or
      contains(expressions, "*?")) defaults
  else if (contains(expressions, "element.type==container")) [
    for (entry in elements(workspace) where string(entry.kind) == "container") string(entry.id)
  ]
  else explicit_ids(workspace, expressions)
}

fn node_role(kind) => "c4-" ++ string(kind)

fn node_shape(kind) => if (kind == "person") "person" else "box"

fn node_content(entry) =>
  <content;
    <div class: "c4-node-content";
      <strong; entry.name>
      <small class: "c4-node-kind"; "[" ++ string(entry.kind) ++
        (if (entry.technology != null) ": " ++ string(entry.technology) else "") ++ "]">
      if (entry.description != null and entry.description != "") {
        <p; entry.description>
      }
    >
  >

fn graph_node(entry) =>
  <node id: entry.id, role: node_role(entry.kind), 'c4-kind': entry.kind,
    label: entry.name, shape: node_shape(entry.kind),
    'source-start': entry["source-start"], 'source-end': entry["source-end"];
    <label; entry.name>
    node_content(entry)
  >

fn graph_edge(relation, ordered = false) {
  let sequence_label = graph_model.optional(relation, "order");
  let description = graph_model.optional(relation, "description");
  let display_label = if (ordered and sequence_label != null)
    string(sequence_label) ++ "." ++
      (if (description != null and description != "") " " ++ string(description) else "")
    else description;
  <edge id: relation.id, from: relation.source, to: relation.destination,
    directed: true, label: display_label,
    'interaction-label': if (ordered) description else null,
    technology: graph_model.optional(relation, "technology"), order: sequence_label,
    sequence: graph_model.optional(relation, "sequence"),
    'parallel-group': graph_model.optional(relation, "parallel-group"),
    'relationship-ref': graph_model.optional(relation, "relationship-ref"),
    'source-start': relation["source-start"], 'source-end': relation["source-end"];
    if (display_label != null) { <label; display_label> }
  >
}

fn selected_entries(workspace, ids) => [
  for (entry in elements(workspace) where contains(ids, string(entry.id))) entry
]

fn boundary_kind(diagram) =>
  if (diagram.kind == "container") "software-system"
  else if (diagram.kind == "component") "container"
  else null

fn boundary(workspace, diagram, entries) {
  let wanted = boundary_kind(diagram);
  let owner = first([for (entry in elements(workspace)
    where string(entry.id) == string(diagram.scope)) entry]);
  if (wanted == null or owner == null) { null }
  else {
    <subgraph id: owner.id, role: "cluster", 'c4-kind': wanted,
      label: owner.name;
      <label; owner.name>
      for (entry in entries where string(entry.parent) == string(owner.id)) graph_node(entry)
    >
  }
}

fn interaction_entries(workspace, diagram) {
  let interactions = children(diagram, "c4-interaction");
  [for (entry in elements(workspace) where len([
    for (item in interactions where string(item.source) == string(entry.id) or
      string(item.destination) == string(entry.id)) item
  ]) > 0) entry]
}

fn dynamic_project(workspace, diagram) {
  let interactions = children(diagram, "c4-interaction");
  let entries = interaction_entries(workspace, diagram);
  <graph id: diagram.key, flavor: "structurizr-c4", version: "1",
    layout: "dot", directed: true, 'ir-stage': "canonical",
    'diagram-type': "dynamic", 'source-view-key': diagram.key,
    direction: diagram.direction, 'rank-sep': diagram["rank-sep"],
    'node-sep': diagram["node-sep"];
    for (entry in entries) graph_node(entry)
    for (item in interactions) graph_edge(item, true)
  >
}

fn parent_entry(all, id) => first([
  for (entry in all where string(entry.id) == string(id)) entry
])

fn below(all, entry, ancestor) {
  let parent_id = graph_model.optional(entry, "parent");
  if (parent_id == null) false
  else if (string(parent_id) == string(ancestor)) true
  else {
    let parent = parent_entry(all, parent_id);
    if (parent == null) false else below(all, parent, ancestor)
  }
}

fn deployment_leaf(entry) => contains([
  "infrastructure-node", "software-system-instance", "container-instance"
], string(entry.kind))

fn model_in_scope(all, entry, scope) {
  let model_ref = graph_model.optional(entry, "model-ref");
  if (scope == "*" or model_ref == null) true
  else if (string(model_ref) == string(scope)) true
  else {
    let model = parent_entry(all, model_ref);
    model != null and below(all, model, scope)
  }
}

fn deployment_entries(workspace, diagram) {
  let all = elements(workspace);
  let environment = string(diagram.environment);
  let scope = string(diagram.scope);
  let candidates = [for (entry in all where below(all, entry, environment) and
    (string(entry.kind) == "deployment-node" or deployment_leaf(entry))) entry];
  if (scope == "*") candidates
  else {
    let leaves = [for (entry in candidates where deployment_leaf(entry) and
      model_in_scope(all, entry, scope)) entry];
    [for (entry in candidates where deployment_leaf(entry) and contains([
        for (leaf in leaves) string(leaf.id)
      ], string(entry.id)) or string(entry.kind) == "deployment-node" and len([
        for (leaf in leaves where below(all, leaf, entry.id)) leaf
      ]) > 0) entry]
  }
}

fn deployment_boundary(entry, entries) =>
  <subgraph id: entry.id, role: "cluster", 'c4-kind': "deployment-node",
    label: entry.name;
    <label; entry.name>
    for (child in entries where string(child.parent) == string(entry.id) and
      string(child.kind) == "deployment-node") deployment_boundary(child, entries)
    for (child in entries where string(child.parent) == string(entry.id) and
      deployment_leaf(child)) graph_node(child)
  >

fn group_refs(entry) => [
  for (group_ref in children(entry, "deployment-group-ref"))
    string(group_ref.identifier)
]

fn groups_compatible(source, destination) {
  let source_groups = group_refs(source);
  let destination_groups = group_refs(destination);
  if (len(source_groups) == 0 and len(destination_groups) == 0) true
  else len([for (group_name in source_groups
    where contains(destination_groups, group_name)) group_name]) > 0
}

fn deployed_endpoints(entries, logical_id) => [
  for (entry in entries where deployment_leaf(entry) and
    string(graph_model.optional(entry, "model-ref")) == string(logical_id)) entry
]

fn lifted_relationships(workspace, entries) => [
  for (relation in relationships(workspace))
    for (source in deployed_endpoints(entries, relation.source))
      for (destination in deployed_endpoints(entries, relation.destination)
        where groups_compatible(source, destination))
        {id: string(relation.id) ++ "@" ++ string(source.id) ++ "@" ++
            string(destination.id), source: source.id, destination: destination.id,
          description: relation.description, technology: relation.technology,
          'source-start': relation["source-start"],
          'source-end': relation["source-end"]}
]

fn direct_deployment_relationships(workspace, entries) {
  let ids = [for (entry in entries where deployment_leaf(entry)) string(entry.id)];
  [for (relation in relationships(workspace)
    where contains(ids, string(relation.source)) and
      contains(ids, string(relation.destination))) relation]
}

fn deployment_project(workspace, diagram) {
  let entries = deployment_entries(workspace, diagram);
  let environment = string(diagram.environment);
  let direct = direct_deployment_relationships(workspace, entries);
  let lifted = lifted_relationships(workspace, entries);
  <graph id: diagram.key, flavor: "structurizr-c4", version: "1",
    layout: "dot", directed: true, 'ir-stage': "canonical",
    'diagram-type': "deployment", 'source-view-key': diagram.key,
    environment: diagram.environment, direction: diagram.direction,
    'rank-sep': diagram["rank-sep"], 'node-sep': diagram["node-sep"];
    for (entry in entries where string(entry.parent) == environment and
      string(entry.kind) == "deployment-node") deployment_boundary(entry, entries)
    for (entry in entries where string(entry.parent) == environment and
      deployment_leaf(entry)) graph_node(entry)
    for (relation in direct) graph_edge(relation)
    for (relation in lifted) graph_edge(relation)
  >
}

pub fn project(workspace, key) {
  let diagram = selected_view(workspace, key);
  if (diagram == null) { null }
  else if (string(diagram.kind) == "dynamic") dynamic_project(workspace, diagram)
  else if (string(diagram.kind) == "deployment") deployment_project(workspace, diagram)
  else {
    let ids = selected_ids(workspace, diagram);
    let entries = selected_entries(workspace, ids);
    let grouped = [for (entry in entries
      where string(entry.parent) == string(diagram.scope)) entry.id];
    let cluster = boundary(workspace, diagram, entries);
    <graph id: diagram.key, flavor: "structurizr-c4", version: "1",
      layout: "dot", directed: true, 'ir-stage': "canonical",
      'diagram-type': diagram.kind, 'source-view-key': diagram.key,
      direction: diagram.direction, 'rank-sep': diagram["rank-sep"],
      'node-sep': diagram["node-sep"];
      if (cluster != null) { cluster }
      for (entry in entries where not contains(grouped, entry.id)) graph_node(entry)
      for (relation in relationships(workspace)
        where contains(ids, string(relation.source)) and contains(ids, string(relation.destination)))
        graph_edge(relation)
    >
  }
}

pub fn project_all(workspace) => [
  for (key in view_keys(workspace)) project(workspace, key)
]
