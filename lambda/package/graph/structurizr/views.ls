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

fn graph_edge(relation) =>
  <edge id: relation.id, from: relation.source, to: relation.destination,
    directed: true, label: relation.description, technology: relation.technology,
    'source-start': relation["source-start"], 'source-end': relation["source-end"];
    if (relation.description != null) { <label; relation.description> }
  >

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

pub fn project(workspace, key) {
  let diagram = selected_view(workspace, key);
  if (diagram == null) { null }
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
