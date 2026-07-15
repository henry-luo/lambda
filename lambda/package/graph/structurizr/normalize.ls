// Pure Structurizr source Mark to canonical C4 workspace normalization.

import graph_model: lambda.package.graph.model

fn children(value, wanted = null) => [
  for (child in graph_model.element_children(value)
    where wanted == null or graph_model.tag(child) == wanted) child
]

fn first(values) => if (len(values) > 0) values[0] else null

fn optional(value, key) {
  let found = value[key];
  if (found is error) null else found
}

fn arguments(value) => [
  for (child in children(value, "argument")) string(child.value)
]

fn arg(value, index, fallback = null) {
  let values = arguments(value);
  if (index < len(values)) values[index] else fallback
}

fn statement_children(value, keyword) => [
  for (child in children(value) where optional(child, "keyword") == keyword) child
]

fn c4_kind(keyword) {
  if (keyword == "softwareSystem") "software-system"
  else if (keyword == "deploymentEnvironment") "deployment-environment"
  else if (keyword == "deploymentGroup") "deployment-group"
  else if (keyword == "deploymentNode") "deployment-node"
  else if (keyword == "infrastructureNode") "infrastructure-node"
  else if (keyword == "softwareSystemInstance") "software-system-instance"
  else if (keyword == "containerInstance") "container-instance"
  else keyword
}

fn is_c4_declaration(value) => graph_model.tag(value) == "declaration" and contains([
  "person", "softwareSystem", "container", "component", "element", "group",
  "deploymentEnvironment", "deploymentGroup", "deploymentNode", "infrastructureNode",
  "softwareSystemInstance", "containerInstance"
], string(value.keyword))

fn declared_identifier(value, parent_identifier, hierarchical) {
  let local = optional(value, "identifier");
  let stable = if (local != null) string(local)
    else string(value.keyword) ++ "@" ++ string(value["source-start"]);
  if (hierarchical and parent_identifier != null and local != null)
    parent_identifier ++ "." ++ stable
  else stable
}

fn declaration_tags(value, kind) {
  let authored = [
    for (statement in statement_children(value, "tags"))
      for (tag in split(arg(statement, 0, ""), ",")
      where trim(tag) != "") trim(tag)
  ];
  ["Element", kind, *authored]
}

fn element_value(value, parent, parent_identifier, hierarchical) {
  let kind = c4_kind(string(value.keyword));
  let id = declared_identifier(value, parent_identifier, hierarchical);
  let model_ref = if (contains(["container-instance", "software-system-instance"], kind))
    arg(value, 0) else null;
  {
    id: id, identifier: id, local_identifier: optional(value, "identifier"),
    kind: kind, parent: parent, name: if (model_ref != null) null else arg(value, 0, id),
    description: if (model_ref != null) null else arg(value, 1),
    technology: if (model_ref != null) null else arg(value, 2),
    model_ref: model_ref, tags: declaration_tags(value, kind), source: value
  }
}

fn append_relation(state, value, parent_identifier) {
  let from = if (string(value.from) == "this") parent_identifier else string(value.from);
  let id = if (optional(value, "identifier") != null) string(value.identifier)
    else "relationship@" ++ string(value["source-start"]);
  let relation = {
    id: id, from: from, to: string(value.to), description: arg(value, 0),
    technology: arg(value, 1), tags: ["Relationship"], source: value
  };
  {*:state, relationships: [*state.relationships, relation]}
}

fn walk_values(values, index, parent, parent_identifier, hierarchical, state) {
  if (index >= len(values)) state
  else {
    let value = values[index];
    let tag = graph_model.tag(value);
    let next = if (is_c4_declaration(value)) {
      let entry = element_value(value, parent, parent_identifier, hierarchical);
      let added = {*:state, elements: [*state.elements, entry]};
      walk_values(children(value), 0, entry.id, entry.identifier, hierarchical, added)
    }
    else if (tag == "relationship") append_relation(state, value, parent_identifier)
    else walk_values(children(value), 0, parent, parent_identifier, hierarchical, state);
    walk_values(values, index + 1, parent, parent_identifier, hierarchical, next)
  }
}

fn identifier_mode(source) {
  let settings = statement_children(source, "!identifiers");
  if (len(settings) > 0) arg(settings[len(settings) - 1], 0, "flat") else "flat"
}

fn resolve_ref(elements, value) {
  if (value == null) null
  else {
    let exact = [for (entry in elements where entry.identifier == value) entry.id];
    let suffix = [for (entry in elements
      where ends_with(entry.identifier, "." ++ value)) entry.id];
    if (len(exact) == 1) exact[0]
    else if (len(suffix) == 1) suffix[0]
    else value
  }
}

fn resolved_element(entry, elements) {
  let model_id = resolve_ref(elements, entry.model_ref);
  let model = first([for (candidate in elements where candidate.id == model_id) candidate]);
  {*:entry, model_ref: model_id,
    name: if (entry.name == null and model != null) model.name else entry.name,
    description: if (entry.description == null and model != null) model.description
      else entry.description,
    technology: if (entry.technology == null and model != null) model.technology
      else entry.technology}
}

fn resolved_relationship(entry, elements) => {*:entry,
  from: resolve_ref(elements, entry.from), to: resolve_ref(elements, entry.to)}

fn layout_arg(value, index, fallback) {
  let layouts = children(value, "auto-layout");
  if (len(layouts) > 0) arg(layouts[0], index, fallback) else fallback
}

fn view_value(value) => {
    kind: string(value.kind),
    scope: if (value.kind == "systemLandscape" or value.kind == "custom")
      null else arg(value, 0),
    environment: if (value.kind == "deployment") arg(value, 1) else null,
    key: if (value.kind == "systemLandscape" or value.kind == "custom")
      arg(value, 0, string(value.kind)) else if (value.kind == "deployment")
      arg(value, 2, string(value.kind)) else arg(value, 1, string(value.kind)),
    includes: [for (include_rule in children(value, "include"))
      for (item in arguments(include_rule)) item],
    excludes: [for (exclude_rule in children(value, "exclude"))
      for (item in arguments(exclude_rule)) item],
    direction: upper(layout_arg(value, 0, "tb")),
    rank_sep: int(layout_arg(value, 1, "300")),
    node_sep: int(layout_arg(value, 2, "300")),
    source: value
}

fn view_values(source) => [
  for (block in children(source, "views"))
    for (value in children(block, "view")) view_value(value)
]

fn style_value(rule) => {
      target_kind: string(rule["target-kind"]), tag: arg(rule, 0, "Element"),
      properties: [
        for (property in children(rule) where graph_model.tag(property) == "statement")
          {name: string(property.keyword), value: arg(property, 0)}
      ],
      source: rule
}

fn style_values(source) => [
  for (block in children(source, "views"))
    for (styles in children(block, "styles"))
      for (rule in children(styles, "style-rule")) style_value(rule)
]

fn c4_element(value) =>
  <'c4-element' id: value.id, identifier: value.identifier,
    'local-identifier': value.local_identifier, kind: value.kind, parent: value.parent,
    name: value.name, description: value.description, technology: value.technology,
    'model-ref': value.model_ref,
    'source-start': value.source["source-start"], 'source-end': value.source["source-end"];
    for (tag in value.tags) <tag name: tag>
  >

fn c4_relationship(value) =>
  <'c4-relationship' id: value.id, source: value.from, destination: value.to,
    description: value.description, technology: value.technology,
    'source-start': value.source["source-start"], 'source-end': value.source["source-end"];
    for (tag in value.tags) <tag name: tag>
  >

fn c4_view(value, elements) =>
  <'c4-view' key: value.key, kind: value.kind,
    scope: resolve_ref(elements, value.scope), environment: value.environment,
    direction: value.direction, 'rank-sep': value.rank_sep, 'node-sep': value.node_sep,
    'source-start': value.source["source-start"], 'source-end': value.source["source-end"];
    for (include_rule in value.includes) <include expression: include_rule>
    for (exclude_rule in value.excludes) <exclude expression: exclude_rule>
  >

fn c4_style(value) =>
  <'c4-style' 'target-kind': value.target_kind, tag: value.tag,
    'source-start': value.source["source-start"], 'source-end': value.source["source-end"];
    for (property in value.properties) <property name: property.name, value: property.value>
  >

fn c4_workspace(name, description, mode, elements, relationships, views, styles,
    source) =>
  <'c4-workspace' name: name, description: description,
    flavor: "structurizr", 'ir-stage': "canonical", 'identifier-mode': mode;
    <'c4-model';
      for (entry in elements) c4_element(entry)
      for (entry in relationships) c4_relationship(entry)
    >
    <'c4-views';
      for (diagram in views) c4_view(diagram, elements)
    >
    <'c4-styles';
      for (style in styles) c4_style(style)
    >
    for (value in children(source, "diagnostics")) value
  >

pub fn normalize(source) {
  let models = children(source, "model");
  let hierarchical = identifier_mode(source) == "hierarchical";
  let walked = if (len(models) > 0)
    walk_values(children(models[0]), 0, null, null, hierarchical,
      {elements: [], relationships: []})
    else {elements: [], relationships: []};
  let elements = [for (entry in walked.elements) resolved_element(entry, walked.elements)];
  let relationships = [
    for (entry in walked.relationships) resolved_relationship(entry, elements)
  ];
  let views = view_values(source);
  let styles = style_values(source);
  let workspace_args = arguments(source);
  let workspace_name = if (len(workspace_args) > 0) workspace_args[0] else null;
  let workspace_description = if (len(workspace_args) > 1) workspace_args[1] else null;
  let mode = if (hierarchical) "hierarchical" else "flat";
  c4_workspace(workspace_name, workspace_description, mode, elements, relationships,
    views, styles, source)
}
