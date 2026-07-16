// Pure Structurizr source Mark to canonical C4 workspace normalization.

import graph_model: lambda.package.graph.model
import archetype: lambda.package.graph.structurizr.archetypes
import diagnostic: lambda.package.graph.diagnostics
import implied: lambda.package.graph.structurizr.implied
import schema: lambda.package.graph.structurizr.schema

fn resolved_include(value) => graph_model.tag(value) == "statement" and
  string(graph_model.optional(value, "keyword")) == "!include"

fn expanded_children(value) => [
  for (child in graph_model.element_children(value),
    expanded in if (resolved_include(child)) expanded_children(child) else [child]) expanded
]

fn children(value, wanted = null) => [
  for (child in expanded_children(value)
    where wanted == null or graph_model.tag(child) == wanted) child
]

fn first(values) => if (len(values) > 0) values[0] else null

fn arguments(value) => [
  for (child in children(value, "argument")) string(child.value)
]

fn arg(value, index, fallback = null) {
  let values = arguments(value);
  if (index < len(values)) values[index] else fallback
}

fn statement_children(value, keyword) => [
  for (child in children(value)
    where graph_model.optional(child, "keyword") == keyword) child
]

fn nested_arg(value, keyword, index = 0) {
  let matches = statement_children(value, keyword);
  if (len(matches) > 0) arg(matches[0], index) else null
}

fn property_values(value) => [
  for (block in children(value, "properties"))
    for (property in children(block)
      where graph_model.tag(property) == "statement")
      {name: string(property.keyword), value: arg(property, 0), source: property}
]

fn perspective_value(value) {
  if (string(value.keyword) == "perspective") {
    {name: arg(value, 0), description: nested_arg(value, "description"),
      value: nested_arg(value, "value"), url: nested_arg(value, "url"), source: value}
  } else {
    {name: string(value.keyword), description: arg(value, 0), value: arg(value, 1),
      url: null, source: value}
  }
}

fn perspective_values(value) => [
  for (block in children(value, "perspectives"))
    for (perspective in children(block)
      where graph_model.tag(perspective) == "statement") perspective_value(perspective)
]

fn health_check_values(value) => [
  for (check in statement_children(value, "healthCheck"))
    {name: arg(check, 0), url: arg(check, 1),
      interval: int(arg(check, 2, "60")), timeout: int(arg(check, 3, "0")),
      source: check}
]

fn tag_values(value) => [
  for (statement in statement_children(value, "tags"))
    for (tag in split(arg(statement, 0, ""), ",")
    where trim(tag) != "") trim(tag)
]

fn archetype_definition(value) => {
  name: string(value.identifier), base: string(value.base),
  target_kind: string(value["target-kind"]),
  description: nested_arg(value, "description"),
  technology: nested_arg(value, "technology"), metadata: nested_arg(value, "metadata"),
  tags: tag_values(value), properties: property_values(value),
  perspectives: perspective_values(value), source: value
}

fn archetype_definitions(model) => [
  for (block in children(model, "archetypes"))
    for (value in children(block, "archetype")) archetype_definition(value)
]

fn c4_kind(keyword) {
  if (keyword == "softwareSystem") "software-system"
  else if (keyword == "element") "custom"
  else if (keyword == "deploymentEnvironment") "deployment-environment"
  else if (keyword == "deploymentGroup") "deployment-group"
  else if (keyword == "deploymentNode") "deployment-node"
  else if (keyword == "infrastructureNode") "infrastructure-node"
  else if (keyword == "softwareSystemInstance") "software-system-instance"
  else if (keyword == "containerInstance") "container-instance"
  else keyword
}

fn default_tag(kind) {
  if (kind == "person") "Person"
  else if (kind == "software-system") "Software System"
  else if (kind == "container") "Container"
  else if (kind == "component") "Component"
  else if (kind == "deployment-environment") "Deployment Environment"
  else if (kind == "deployment-node") "Deployment Node"
  else if (kind == "infrastructure-node") "Infrastructure Node"
  else if (kind == "software-system-instance") "Software System Instance"
  else if (kind == "container-instance") "Container Instance"
  else if (kind == "group") "Group"
  else null
}

fn is_c4_declaration(value, definitions) =>
  (graph_model.tag(value) == "declaration" and contains([
    "person", "softwareSystem", "container", "component", "element", "group",
    "deploymentEnvironment", "deploymentGroup", "deploymentNode", "infrastructureNode",
    "softwareSystemInstance", "containerInstance"
  ], string(value.keyword))) or
  (graph_model.optional(value, "identifier") != null and
    archetype.find(definitions, string(value.keyword), "element") != null)

fn declared_identifier(value, parent_identifier, hierarchical) {
  let local = graph_model.optional(value, "identifier");
  let stable = if (local != null) string(local)
    else string(value.keyword) ++ "@" ++ source_identity(value);
  if (hierarchical and parent_identifier != null and local != null)
    parent_identifier ++ "." ++ stable
  else stable
}

fn source_identity(value) {
  let file = graph_model.optional(value, "source-file");
  (if (file == null) "" else string(file) ++ ":") ++ string(value["source-start"])
}

fn declaration_tags(value, kind, definition) {
  let structural = default_tag(kind);
  let inherited = if (definition == null) [] else definition.tags;
  unique(["Element", for (tag in [structural] where tag != null) tag,
    for (name in [if (definition == null) null else definition.name] where name != null) name,
    *inherited, *tag_values(value)])
}

fn node_deployment_groups(value) => [
  for (child in children(value)
    where graph_model.tag(child) == "declaration" and
      string(child.keyword) == "deploymentGroup" and
      graph_model.optional(child, "identifier") == null)
    for (group_name in arguments(child)) group_name
]

fn element_value(value, parent, parent_identifier, group_name, inherited_groups,
    hierarchical, definitions) {
  let definition = archetype.find(definitions, string(value.keyword), "element");
  let kind = c4_kind(if (definition == null) string(value.keyword) else definition.kind);
  let id = declared_identifier(value, parent_identifier, hierarchical);
  let model_ref = if (contains(["container-instance", "software-system-instance"], kind))
    arg(value, 0) else null;
  let explicit_groups = if (model_ref != null and arg(value, 1) != null) [
    for (group_name in split(arg(value, 1), ",") where trim(group_name) != "")
      trim(group_name)
  ] else [];
  let deployment_groups = if (kind == "deployment-node")
    unique([*inherited_groups, *node_deployment_groups(value)])
    else if (model_ref != null) unique([*inherited_groups, *explicit_groups])
    else [];
  let custom = kind == "custom";
  let explicit = {
    description: if (model_ref != null) null else arg(value, if (custom) 2 else 1),
    technology: if (model_ref != null or custom) null else arg(value, 2),
    metadata: if (custom) arg(value, 1) else null,
    tags: tag_values(value), properties: property_values(value),
    perspectives: perspective_values(value)
  };
  let effective = if (definition == null) explicit else archetype.merge(definition, explicit);
  {
    id: id, identifier: id,
    local_identifier: graph_model.optional(value, "identifier"),
    kind: kind, parent: parent, group: group_name,
    name: if (model_ref != null) null else arg(value, 0, id),
    archetype: if (definition == null) null else definition.name,
    metadata: effective.metadata, description: effective.description,
    technology: effective.technology,
    model_ref: model_ref, deployment_groups: deployment_groups,
    tags: declaration_tags(value, kind, definition), properties: effective.properties,
    perspectives: effective.perspectives, health_checks: health_check_values(value),
    source: value
  }
}

fn relationship_tags(value, definition) {
  let inherited = if (definition == null) [] else definition.tags;
  unique(["Relationship", for (name in [
      if (definition == null) null else definition.name] where name != null) name,
    *inherited,
    for (tag in split(arg(value, 2, ""), ",") where trim(tag) != "") trim(tag),
    *tag_values(value)])
}

fn unknown_archetype(name, target_kind, value) => diagnostic.for_value(
  "structurizr.unknown-archetype", "error",
  "Unknown " ++ target_kind ++ " archetype '" ++ string(name) ++ "'",
  target_kind ++ "-archetype:" ++ string(name), value)

fn append_relation(state, value, parent_identifier, definitions) {
  // both relationship endpoints may use the scoped `this` identifier.
  let from = if (string(value.from) == "this") parent_identifier else string(value.from);
  let to = if (string(value.to) == "this") parent_identifier else string(value.to);
  let id = if (graph_model.optional(value, "identifier") != null)
    string(value.identifier)
    else "relationship@" ++ source_identity(value);
  let definition = archetype.find(definitions,
    graph_model.optional(value, "archetype"), "relationship");
  let explicit = {description: arg(value, 0), technology: arg(value, 1), metadata: null,
    tags: [], properties: property_values(value), perspectives: perspective_values(value)};
  let effective = if (definition == null) explicit else archetype.merge(definition, explicit);
  let relation = {
    id: id, from: from, to: to,
    archetype: if (definition == null) null else definition.name,
    implied: false, implied_from: null,
    description: effective.description, technology: effective.technology,
    tags: relationship_tags(value, definition),
    properties: effective.properties, perspectives: effective.perspectives,
    source: value
  };
  let missing = graph_model.optional(value, "archetype") != null and definition == null;
  {*:state, relationships: [*state.relationships, relation],
    diagnostics: if (missing) [*state.diagnostics,
      unknown_archetype(value.archetype, "relationship", value)] else state.diagnostics}
}

fn joined_group(parent_group, name, separator) =>
  if (parent_group == null) name else parent_group ++ separator ++ name

fn walk_values(values, index, parent, parent_identifier, parent_kind, group_name,
    group_separator, inherited_groups, hierarchical, definitions, state) {
  if (index >= len(values)) state
  else {
    let value = values[index];
    let tag = graph_model.tag(value);
    let group_assignment = tag == "declaration" and
      string(value.keyword) == "deploymentGroup" and
      graph_model.optional(value, "identifier") == null and
      parent_kind == "deployment-node";
    let next = if (group_assignment) state
    else if (is_c4_declaration(value, definitions)) {
      let entry = element_value(value, parent, parent_identifier, group_name,
        inherited_groups, hierarchical, definitions);
      let added = {*:state, elements: [*state.elements, entry]};
      if (entry.kind == "group")
        walk_values(children(value), 0, parent, parent_identifier, parent_kind,
          joined_group(group_name, entry.name, group_separator), group_separator,
          inherited_groups, hierarchical, definitions, added)
      else walk_values(children(value), 0, entry.id, entry.identifier, entry.kind,
        group_name, group_separator, entry.deployment_groups, hierarchical, definitions, added)
    }
    else if (tag == "relationship")
      append_relation(state, value, parent_identifier, definitions)
    else if (tag == "archetypes") state
    else if (tag == "statement" and graph_model.optional(value, "identifier") != null)
      {*:state, diagnostics: [*state.diagnostics,
        unknown_archetype(value.keyword, "element", value)]}
    else walk_values(children(value), 0, parent, parent_identifier, parent_kind,
      group_name, group_separator, inherited_groups, hierarchical, definitions, state);
    walk_values(values, index + 1, parent, parent_identifier, parent_kind, group_name,
      group_separator, inherited_groups, hierarchical, definitions, next)
  }
}

fn identifier_mode(source) {
  let settings = statement_children(source, "!identifiers");
  if (len(settings) > 0) arg(settings[len(settings) - 1], 0, "flat") else "flat"
}

fn implied_config(source) {
  let settings = statement_children(source, "!impliedRelationships");
  let setting = if (len(settings) == 0) null else settings[len(settings) - 1];
  let value = lower(if (setting == null) "true" else arg(setting, 0, "true"));
  if (value == "true") { {enabled: true, diagnostics: []} }
  else if (value == "false") { {enabled: false, diagnostics: []} }
  else { {enabled: false, diagnostics: [diagnostic.for_value(
    "structurizr.unsafe-directive", "warning",
    "Custom implied relationship strategies are preserved but not executed",
    "source.!impliedRelationships", setting)]} }
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

fn relationship_ref(relations, source, destination) => first([
  for (relation in relations
    where relation.from == source and relation.to == destination) relation.id
])

fn interaction(value, elements, relationships, key, sequence, parallel_group) {
  let source = resolve_ref(elements, string(value.from));
  let destination = resolve_ref(elements, string(value.to));
  let authored_order = graph_model.optional(value, "order");
  let sequence_label = if (authored_order != null) string(authored_order)
    else string(sequence);
  {
    id: key ++ "@" ++ source_identity(value),
    source: source, destination: destination,
    relationship_ref: relationship_ref(relationships, source, destination),
    description: arg(value, 0), technology: arg(value, 1),
    order: sequence_label, sequence: sequence,
    parallel_group: parallel_group, source_mark: value
  }
}

fn statement_order(value) {
  let keyword = string(value.keyword);
  if (ends_with(keyword, ":")) slice(keyword, 0, len(keyword) - 1) else null
}

fn interaction_by_ref(value, relationships, key, sequence, parallel_group) {
  let authored_order = statement_order(value);
  let relation_id = if (authored_order != null) arg(value, 0) else string(value.keyword);
  let matches = [for (relation in relationships where relation.id == relation_id) relation];
  if (len(matches) == 0) null
  else {
    let relation = matches[0];
    let description_index = if (authored_order != null) 1 else 0;
    {
      id: key ++ "@" ++ source_identity(value),
      source: relation.from, destination: relation.to, relationship_ref: relation.id,
      description: arg(value, description_index, relation.description),
      technology: relation.technology,
      order: if (authored_order != null) authored_order else string(sequence),
      sequence: sequence, parallel_group: parallel_group, source_mark: value
    }
  }
}

fn interaction_values_at(values, index, elements, relationships, key, state,
    forced_order = null, parallel_group = null) {
  if (index >= len(values)) state
  else {
    let value = values[index];
    let tag = graph_model.tag(value);
    let next = if (tag == "relationship") {
      let item = interaction(value, elements, relationships, key,
        if (forced_order != null) forced_order else state.next_order, parallel_group);
      {items: [*state.items, item], next_order:
        if (forced_order != null) state.next_order else state.next_order + 1}
    }
    else if (tag == "statement") {
      let item = interaction_by_ref(value, relationships, key,
        if (forced_order != null) forced_order else state.next_order, parallel_group);
      if (item == null) state
      else {items: [*state.items, item], next_order:
        if (forced_order != null) state.next_order else state.next_order + 1}
    }
    else if (tag == "parallel") {
      let group_id = if (parallel_group != null) parallel_group
        else key ++ ":parallel@" ++ source_identity(value);
      let block_order = if (forced_order != null) forced_order else state.next_order;
      let nested = interaction_values_at(children(value), 0, elements, relationships,
        key, state, block_order, group_id);
      {items: nested.items, next_order:
        if (forced_order != null) state.next_order else state.next_order + 1}
    }
    else state;
    interaction_values_at(values, index + 1, elements, relationships, key,
      next, forced_order, parallel_group)
  }
}

fn dynamic_interactions(value, elements, relationships, key) =>
  interaction_values_at(children(value), 0, elements, relationships, key,
    {items: [], next_order: 1}).items

fn view_value(value, elements, relationships) {
  let view_key = if (value.kind == "systemLandscape" or value.kind == "custom")
    arg(value, 0, string(value.kind)) else if (value.kind == "filtered")
    arg(value, 3, "filtered@" ++ source_identity(value))
    else if (value.kind == "deployment")
    arg(value, 2, string(value.kind)) else arg(value, 1, string(value.kind));
  {
    kind: string(value.kind),
    scope: if (value.kind == "systemLandscape" or value.kind == "custom")
      null else arg(value, 0),
    environment: if (value.kind == "deployment") arg(value, 1) else null,
    base_key: if (value.kind == "filtered") arg(value, 0) else null,
    filter_mode: if (value.kind == "filtered") lower(arg(value, 1, "include")) else null,
    filter_tags: if (value.kind == "filtered") [
      for (tag in split(arg(value, 2, ""), ",") where trim(tag) != "") trim(tag)
    ] else [],
    key: view_key,
    includes: [for (include_rule in children(value, "include"))
      for (item in arguments(include_rule)) {expression: item, source: include_rule}],
    excludes: [for (exclude_rule in children(value, "exclude"))
      for (item in arguments(exclude_rule)) {expression: item, source: exclude_rule}],
    interactions: if (value.kind == "dynamic")
      dynamic_interactions(value, elements, relationships, view_key) else [],
    direction: upper(layout_arg(value, 0, "tb")),
    rank_sep: int(layout_arg(value, 1, "300")),
    node_sep: int(layout_arg(value, 2, "300")),
    source: value
  }
}

fn view_values(source, elements, relationships) => [
  for (block in children(source, "views"))
    for (value in children(block, "view")) view_value(value, elements, relationships)
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

fn terminology_kind(value) {
  if (value == "softwareSystem") "software-system"
  else if (value == "deploymentNode") "deployment-node"
  else if (value == "infrastructureNode") "infrastructure-node"
  else value
}

fn terminology_values(source) => [
  for (block in children(source, "views"), terminology in statement_children(block, "terminology"),
    term in children(terminology)
      where contains(["statement", "declaration"], graph_model.tag(term)))
    {kind: terminology_kind(string(term.keyword)), value: arg(term, 0), source: term}
]

fn default_terminology(kind) {
  if (kind == "person") "Person"
  else if (kind == "software-system") "Software System"
  else if (kind == "container") "Container"
  else if (kind == "component") "Component"
  else if (kind == "deployment-node") "Deployment Node"
  else if (kind == "infrastructure-node") "Infrastructure Node"
  else if (kind == "relationship") "Relationship"
  else kind
}

fn terminology_value(terms, kind) {
  let values = [for (term in terms where term.kind == kind) term.value];
  if (len(values) > 0) values[len(values) - 1] else default_terminology(kind)
}

fn c4_element(value, terminology) =>
  <'c4-element' id: value.id, identifier: value.identifier,
    'local-identifier': value.local_identifier, kind: value.kind, parent: value.parent,
    group: value.group, archetype: value.archetype,
    name: value.name, metadata: value.metadata,
    terminology: terminology_value(terminology,
      if (value.kind == "software-system-instance") "software-system"
      else if (value.kind == "container-instance") "container" else value.kind),
    description: value.description, technology: value.technology,
    'model-ref': value.model_ref,
    'source-start': value.source["source-start"], 'source-end': value.source["source-end"];
    for (tag in value.tags) <tag name: tag>
    for (property in value.properties)
      <property name: property.name, value: property.value,
        'source-start': property.source["source-start"],
        'source-end': property.source["source-end"]>
    for (perspective in value.perspectives)
      <perspective name: perspective.name, description: perspective.description,
        value: perspective.value, url: perspective.url,
        'source-start': perspective.source["source-start"],
        'source-end': perspective.source["source-end"]>
    for (check in value.health_checks)
      <'health-check' name: check.name, url: check.url,
        interval: check.interval, timeout: check.timeout,
        'source-start': check.source["source-start"],
        'source-end': check.source["source-end"]>
    for (group_name in value.deployment_groups)
      <'deployment-group-ref' identifier: group_name>
  >

fn c4_relationship(value) =>
  <'c4-relationship' id: value.id, source: value.from, destination: value.to,
    archetype: value.archetype,
    implied: value.implied, 'implied-from': value.implied_from,
    description: value.description, technology: value.technology,
    'source-start': value.source["source-start"], 'source-end': value.source["source-end"];
    for (tag in value.tags) <tag name: tag>
    for (property in value.properties)
      <property name: property.name, value: property.value,
        'source-start': property.source["source-start"],
        'source-end': property.source["source-end"]>
    for (perspective in value.perspectives)
      <perspective name: perspective.name, description: perspective.description,
        value: perspective.value, url: perspective.url,
        'source-start': perspective.source["source-start"],
        'source-end': perspective.source["source-end"]>
  >

fn c4_view(value, elements) =>
  <'c4-view' key: value.key, kind: value.kind,
    scope: resolve_ref(elements, value.scope),
    environment: resolve_ref(elements, value.environment),
    'base-key': value.base_key, 'filter-mode': value.filter_mode,
    direction: value.direction, 'rank-sep': value.rank_sep, 'node-sep': value.node_sep,
    'source-start': value.source["source-start"], 'source-end': value.source["source-end"];
    for (rule in value.includes) <include expression: rule.expression,
      'source-start': rule.source["source-start"], 'source-end': rule.source["source-end"],
      'source-line': rule.source["source-line"], 'source-column': rule.source["source-column"]>
    for (rule in value.excludes) <exclude expression: rule.expression,
      'source-start': rule.source["source-start"], 'source-end': rule.source["source-end"],
      'source-line': rule.source["source-line"], 'source-column': rule.source["source-column"]>
    for (tag in value.filter_tags) <'filter-tag' name: tag>
    for (item in value.interactions)
      <'c4-interaction' id: item.id, source: item.source,
        destination: item.destination, 'relationship-ref': item.relationship_ref,
        description: item.description, technology: item.technology,
        order: item.order, sequence: item.sequence,
        'parallel-group': item.parallel_group,
        'source-start': item.source_mark["source-start"],
        'source-end': item.source_mark["source-end"]>
  >

fn c4_style(value) =>
  <'c4-style' 'target-kind': value.target_kind, tag: value.tag,
    'source-start': value.source["source-start"], 'source-end': value.source["source-end"];
    for (property in value.properties) <property name: property.name, value: property.value>
  >

fn c4_workspace(name, description, mode, elements, relationships, views, styles,
    terminology, group_separator, model_properties, source) =>
  <'c4-workspace' name: name, description: description,
    flavor: "structurizr", 'ir-stage': "canonical", 'identifier-mode': mode,
    'group-separator': group_separator;
    <'c4-model';
      for (property in model_properties)
        <property name: property.name, value: property.value,
          'source-start': property.source["source-start"],
          'source-end': property.source["source-end"]>
      for (entry in elements) c4_element(entry, terminology)
      for (entry in relationships) c4_relationship(entry)
    >
    <'c4-views';
      for (diagram in views) c4_view(diagram, elements)
    >
    <'c4-styles';
      for (style in styles) c4_style(style)
    >
    <'c4-terminology';
      for (term in terminology)
        <term kind: term.kind, value: term.value,
          'source-start': term.source["source-start"],
          'source-end': term.source["source-end"]>
    >
    <diagnostics;
      for (value in graph_model.diagnostics(source)) value
    >
  >

pub fn normalize(source) {
  let source_diagnostics = schema.validate_source(source);
  let models = children(source, "model");
  let hierarchical = identifier_mode(source) == "hierarchical";
  let model_properties = if (len(models) > 0) property_values(models[0]) else [];
  let separators = [for (property in model_properties
    where property.name == "structurizr.groupSeparator") property.value];
  let group_separator = if (len(separators) > 0) separators[len(separators) - 1] else "/";
  let archetypes = archetype.resolve(
    if (len(models) > 0) archetype_definitions(models[0]) else []);
  let walked = if (len(models) > 0)
    walk_values(children(models[0]), 0, null, null, null, null, group_separator, [],
      hierarchical, archetypes.values, {elements: [], relationships: [], diagnostics: []})
    else {elements: [], relationships: [], diagnostics: []};
  let elements = [for (entry in walked.elements) resolved_element(entry, walked.elements)];
  let explicit_relationships = [
    for (entry in walked.relationships) resolved_relationship(entry, elements)
  ];
  let implied_setting = implied_config(source);
  let relationships = implied.expand(elements, explicit_relationships,
    implied_setting.enabled);
  let views = view_values(source, elements, relationships);
  let styles = style_values(source);
  let terminology = terminology_values(source);
  let workspace_args = arguments(source);
  let workspace_name = if (len(workspace_args) > 0) workspace_args[0] else null;
  let workspace_description = if (len(workspace_args) > 1) workspace_args[1] else null;
  let mode = if (hierarchical) "hierarchical" else "flat";
  let workspace = c4_workspace(workspace_name, workspace_description, mode, elements,
    relationships, views, styles, terminology, group_separator, model_properties, source);
  schema.attach(workspace, [*source_diagnostics, *archetypes.diagnostics,
    *walked.diagnostics, *implied_setting.diagnostics, *schema.validate(workspace)])
}
