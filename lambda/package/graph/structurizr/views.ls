// C4 workspace view projection to canonical Graph IR.

import graph_model: lambda.package.graph.model
import expression: lambda.package.graph.structurizr.expressions
import style_rules: lambda.package.graph.structurizr.styles

fn children(value, wanted = null) => [
  for (child in graph_model.element_children(value)
    where wanted == null or graph_model.tag(child) == wanted) child
]

fn first(values) => if (len(values) > 0) values[0] else null

fn workspace_model(workspace) => first(children(workspace, "c4-model"))
fn workspace_views(workspace) => first(children(workspace, "c4-views"))
fn workspace_styles(workspace) => first(children(workspace, "c4-styles"))

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
  else if (diagram_kind == "custom") { [
    for (entry in all where string(entry.kind) == "custom") string(entry.id)
  ] }
  else { [for (entry in all) string(entry.id)] }
}

fn include_expressions(diagram) => [
  for (include_rule in children(diagram, "include")) string(include_rule.expression)
]

fn exclude_expressions(diagram) => [
  for (exclude_rule in children(diagram, "exclude")) string(exclude_rule.expression)
]

fn expression_ids(workspace, expressions) {
  let all = elements(workspace);
  [for (entry in all where contains([
    for (rule in expressions where not expression.relationship_expression(rule))
      for (id in expression.element_ids(all, relationships(workspace), rule)) id
  ], string(entry.id))) string(entry.id)]
}

fn selected_ids(workspace, diagram) {
  let includes = include_expressions(diagram);
  let excludes = exclude_expressions(diagram);
  let defaults = base_ids(workspace, diagram);
  let included = if (len(includes) == 0 or contains(includes, "*") or
      contains(includes, "*?")) defaults else expression_ids(workspace, includes);
  let excluded = expression_ids(workspace, excludes);
  [for (id in included where not contains(excluded, id)) id]
}

fn node_role(kind) => "c4-" ++ string(kind)

fn node_shape(kind) => if (kind == "person") "person" else "box"

fn node_meta(entry) => if (entry.kind == "custom" and entry.metadata != null)
  string(entry.metadata)
else string(entry.terminology) ++
  (if (entry.technology != null) ": " ++ string(entry.technology) else "")

fn node_content(entry) =>
  <content;
    <div class: "c4-node-content";
      <strong; entry.name>
      <small class: "c4-node-kind"; "[" ++ node_meta(entry) ++ "]">
      if (entry.description != null and entry.description != "") {
        <p; entry.description>
      }
    >
  >

fn graph_node(workspace, entry) {
  let authored_shape = style_property(workspace, entry, "node", "shape");
  let mapped_shape = if (authored_shape == null) null else style_rules.shape(authored_shape);
  let shape = if (mapped_shape == null) node_shape(entry.kind) else mapped_shape;
  <node id: entry.id, role: node_role(entry.kind), 'c4-kind': entry.kind,
    label: entry.name, metadata: graph_model.optional(entry, "metadata"),
    group: graph_model.optional(entry, "group"),
    shape: shape, 'structurizr-shape': authored_shape,
    'source-start': entry["source-start"], 'source-end': entry["source-end"];
    <label; entry.name>
    node_content(entry)
    for (tag in children(entry, "tag")) <tag name: tag.name>
    for (property in children(entry, "property"))
      <property name: property.name, value: property.value>
    for (perspective in children(entry, "perspective"))
      <perspective name: perspective.name, description: perspective.description,
        value: perspective.value, url: perspective.url>
    for (check in children(entry, "health-check"))
      <'health-check' name: check.name, url: check.url,
        interval: check.interval, timeout: check.timeout>
  >
}

fn graph_edge(workspace, relation, ordered = false) {
  let sequence_label = graph_model.optional(relation, "order");
  let description = graph_model.optional(relation, "description");
  let routing = style_property(workspace, relation, "edge", "routing");
  let line_style = style_property(workspace, relation, "edge", "style");
  let display_label = if (ordered and sequence_label != null)
    string(sequence_label) ++ "." ++
      (if (description != null and description != "") " " ++ string(description) else "")
    else description;
  <edge id: relation.id, from: relation.source, to: relation.destination,
    directed: true, label: display_label,
    'route-mode': if (routing == null) null else style_rules.route(routing),
    style: if (line_style == null) "solid" else lower(string(line_style)),
    'interaction-label': if (ordered) description else null,
    technology: graph_model.optional(relation, "technology"), order: sequence_label,
    sequence: graph_model.optional(relation, "sequence"),
    'parallel-group': graph_model.optional(relation, "parallel-group"),
    'relationship-ref': graph_model.optional(relation, "relationship-ref"),
    'source-start': relation["source-start"], 'source-end': relation["source-end"];
    if (display_label != null) { <label; display_label> }
    for (tag in children(relation, "tag")) <tag name: tag.name>
    for (property in children(relation, "property"))
      <property name: property.name, value: property.value>
    for (perspective in children(relation, "perspective"))
      <perspective name: perspective.name, description: perspective.description,
        value: perspective.value, url: perspective.url>
  >
}

fn selected_entries(workspace, ids) => [
  for (entry in elements(workspace) where contains(ids, string(entry.id))) entry
]

fn group_names(entries) => unique([
  for (entry in entries, let group_name = graph_model.optional(entry, "group")
    where group_name != null and string(entry.kind) != "group") string(group_name)
])

fn group_separator(workspace) => string(graph_model.optional(workspace, "group-separator"))

fn group_paths(entries, separator) => unique([
  for (group_name in group_names(entries), let parts = split(group_name, separator))
    for (i in 1 to len(parts)) join(slice(parts, 0, i), separator)
])

fn group_parent(path, separator) {
  let parts = split(path, separator);
  if (len(parts) < 2) { null } else { join(slice(parts, 0, len(parts) - 1), separator) }
}

fn group_label(path, separator) {
  let parts = split(path, separator);
  parts[len(parts) - 1]
}

fn group_boundary(workspace, path, entries, paths, separator) =>
  <subgraph id: "group:" ++ path, role: "cluster", 'c4-kind': "group",
    label: group_label(path, separator);
    <label; group_label(path, separator)>
    for (child in paths where group_parent(child, separator) == path)
      group_boundary(workspace, child, entries, paths, separator)
    for (entry in entries where string(graph_model.optional(entry, "group")) == path)
      graph_node(workspace, entry)
  >

fn group_boundaries(workspace, entries) {
  let separator = group_separator(workspace);
  let paths = group_paths(entries, separator);
  [for (path in paths where group_parent(path, separator) == null)
    group_boundary(workspace, path, entries, paths, separator)]
}

fn grouped(entries) => [
  for (entry in entries where graph_model.optional(entry, "group") != null) entry.id
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
    let inside = [for (entry in entries
      where string(entry.parent) == string(owner.id)) entry];
    let grouped_ids = grouped(inside);
    <subgraph id: owner.id, role: "cluster", 'c4-kind': wanted,
      label: owner.name;
      <label; owner.name>
      for (group in group_boundaries(workspace, inside)) group
      for (entry in inside where not contains(grouped_ids, entry.id)) graph_node(workspace, entry)
    >
  }
}

fn scope_seed_ids(workspace, diagram) {
  let all = elements(workspace);
  let kind = string(diagram.kind);
  let scope = string(diagram.scope);
  if (kind == "systemContext") { [scope] }
  else if (kind == "container") { [for (entry in all
    where string(entry.parent) == scope and string(entry.kind) == "container") string(entry.id)] }
  else if (kind == "component") { [for (entry in all
    where string(entry.parent) == scope and string(entry.kind) == "component") string(entry.id)] }
  else { base_ids(workspace, diagram) }
}

fn relationship_excluded(workspace, diagram, relation) => len([
  for (rule in exclude_expressions(diagram)
    where expression.relationship_expression(rule) and
      expression.relationship_matches(elements(workspace), relation, rule)) rule
]) > 0

fn selected_relationships(workspace, diagram, ids) {
  let reluctant = contains(include_expressions(diagram), "*?");
  let seeds = scope_seed_ids(workspace, diagram);
  [for (relation in relationships(workspace)
    where contains(ids, string(relation.source)) and
      contains(ids, string(relation.destination)) and
      (not reluctant or contains(seeds, string(relation.source)) or
        contains(seeds, string(relation.destination))) and
      not relationship_excluded(workspace, diagram, relation)) relation]
}

fn has_filter_tag(value, wanted) => len([
  for (tag in children(value, "tag") where contains(wanted, string(tag.name))) tag
]) > 0

fn filter_tags(diagram) => [
  for (tag in children(diagram, "filter-tag")) string(tag.name)
]

fn filter_values(values, diagram) {
  let wanted = filter_tags(diagram);
  let include = string(diagram["filter-mode"]) == "include";
  [for (value in values where has_filter_tag(value, wanted) == include) value]
}

fn style_declarations(style, target) => join([
  for (property in children(style, "property"),
    let declaration = style_rules.declaration(target, property)
    where declaration != "") declaration
], ";")

fn style_matches(value, style, target) =>
  (target == "node" and string(style.tag) == "Element") or
  (target == "edge" and string(style.tag) == "Relationship") or
  has_filter_tag(value, [string(style.tag)])

fn style_property(workspace, value, target, name) {
  let values = [
    for (style in children(workspace_styles(workspace), "c4-style")
      where style_matches(value, style, target))
      for (property in children(style, "property")
        where lower(string(property.name)) == lower(name)) property.value
  ];
  if (len(values) == 0) { null } else { values[len(values) - 1] }
}

fn style_assignments(workspace, entries, relations) => [
  for (style in children(workspace_styles(workspace), "c4-style"),
    let target = if (string(style["target-kind"]) == "element") "node" else "edge",
    let values = if (target == "node") entries else relations,
    let targets = [for (value in values where style_matches(value, style, target)) string(value.id)],
    let declarations = style_declarations(style, target)
    where len(targets) > 0 and declarations != "")
    <'style-assignment' 'target-kind': target, targets: join(targets, ","),
      declarations: declarations,
      'source-start': style["source-start"], 'source-end': style["source-end"]>
]

fn static_graph(workspace, diagram, structure, entries, relations) {
  let cluster = boundary(workspace, structure, entries);
  let contained = if (cluster == null) [] else [for (entry in entries
    where string(entry.parent) == string(structure.scope)) entry.id];
  let root_entries = [for (entry in entries where not contains(contained, entry.id)) entry];
  let grouped_ids = grouped(root_entries);
  <graph id: diagram.key, flavor: "structurizr-c4", version: "1",
    layout: "dot", directed: true, 'ir-stage': "canonical",
    'diagram-type': diagram.kind, 'source-view-key': diagram.key,
    'base-view-key': graph_model.optional(diagram, "base-key"),
    direction: structure.direction, 'rank-sep': structure["rank-sep"],
    'node-sep': structure["node-sep"];
    if (cluster != null) { cluster }
    for (group in group_boundaries(workspace, root_entries)) group
    for (entry in root_entries where not contains(grouped_ids, entry.id))
      graph_node(workspace, entry)
    for (relation in relations) graph_edge(workspace, relation)
    for (assignment in style_assignments(workspace, entries, relations)) assignment
  >
}

fn static_project(workspace, diagram) {
  let ids = selected_ids(workspace, diagram);
  static_graph(workspace, diagram, diagram, selected_entries(workspace, ids),
    selected_relationships(workspace, diagram, ids))
}

fn filtered_project(workspace, diagram) {
  let base = selected_view(workspace, diagram["base-key"]);
  if (base == null or string(base.kind) == "filtered" or
      string(base.kind) == "dynamic" or string(base.kind) == "deployment") null
  else {
    let base_ids = selected_ids(workspace, base);
    let entries = filter_values(selected_entries(workspace, base_ids), diagram);
    let ids = [for (entry in entries) string(entry.id)];
    let relations = filter_values(selected_relationships(workspace, base, base_ids), diagram);
    static_graph(workspace, diagram, base, entries, [for (relation in relations
      where contains(ids, string(relation.source)) and
        contains(ids, string(relation.destination))) relation])
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
    for (entry in entries) graph_node(workspace, entry)
    for (item in interactions) graph_edge(workspace, item, true)
    for (assignment in style_assignments(workspace, entries, interactions)) assignment
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

fn expanded_deployment_ids(all, candidates, seeds) => [
  for (entry in candidates where contains(seeds, string(entry.id)) or len([
    for (seed in seeds, let owner = parent_entry(all, seed)
      where owner != null and string(owner.kind) == "deployment-node" and
        below(all, entry, seed)) seed
  ]) > 0) string(entry.id)
]

fn deployment_filtered(workspace, candidates, diagram) {
  let all = elements(workspace);
  let includes = include_expressions(diagram);
  let excludes = exclude_expressions(diagram);
  let candidate_ids = [for (entry in candidates) string(entry.id)];
  let included = if (len(includes) == 0 or contains(includes, "*")) candidate_ids
    else expanded_deployment_ids(all, candidates, expression_ids(workspace, includes));
  let excluded = expanded_deployment_ids(all, candidates, expression_ids(workspace, excludes));
  let selected = [for (id in included where not contains(excluded, id)) id];
  [for (entry in candidates where contains(selected, string(entry.id)) or
    string(entry.kind) == "deployment-node" and len([
      for (id in selected, let child = parent_entry(all, id)
        where child != null and below(all, child, entry.id)) id
    ]) > 0) entry]
}

fn deployment_entries(workspace, diagram) {
  let all = elements(workspace);
  let environment = string(diagram.environment);
  let scope = string(diagram.scope);
  let candidates = [for (entry in all where below(all, entry, environment) and
    (string(entry.kind) == "deployment-node" or deployment_leaf(entry))) entry];
  let scoped = if (scope == "*") candidates
  else {
    let leaves = [for (entry in candidates where deployment_leaf(entry) and
      model_in_scope(all, entry, scope)) entry];
    [for (entry in candidates where deployment_leaf(entry) and contains([
        for (leaf in leaves) string(leaf.id)
      ], string(entry.id)) or string(entry.kind) == "deployment-node" and len([
        for (leaf in leaves where below(all, leaf, entry.id)) leaf
      ]) > 0) entry]
  };
  deployment_filtered(workspace, scoped, diagram)
}

fn deployment_boundary(workspace, entry, entries) =>
  <subgraph id: entry.id, role: "cluster", 'c4-kind': "deployment-node",
    label: entry.name;
    <label; entry.name>
    for (child in entries where string(child.parent) == string(entry.id) and
      string(child.kind) == "deployment-node") deployment_boundary(workspace, child, entries)
    for (child in entries where string(child.parent) == string(entry.id) and
      deployment_leaf(child)) graph_node(workspace, child)
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
      string(entry.kind) == "deployment-node") deployment_boundary(workspace, entry, entries)
    for (entry in entries where string(entry.parent) == environment and
      deployment_leaf(entry)) graph_node(workspace, entry)
    for (relation in direct) graph_edge(workspace, relation)
    for (relation in lifted) graph_edge(workspace, relation)
    for (assignment in style_assignments(workspace, entries, [*direct, *lifted])) assignment
  >
}

pub fn project(workspace, key) {
  let diagram = selected_view(workspace, key);
  if (diagram == null) { null }
  else if (string(diagram.kind) == "dynamic") dynamic_project(workspace, diagram)
  else if (string(diagram.kind) == "deployment") deployment_project(workspace, diagram)
  else if (string(diagram.kind) == "filtered") filtered_project(workspace, diagram)
  else static_project(workspace, diagram)
}

pub fn project_all(workspace) => [
  for (key in view_keys(workspace)) project(workspace, key)
]
