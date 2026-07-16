// Canonical Structurizr workspace validation and diagnostic attachment.

import graph_model: lambda.package.graph.model
import diagnostic: lambda.package.graph.diagnostics
import expression: lambda.package.graph.structurizr.expressions
import style_rules: lambda.package.graph.structurizr.styles

fn children(value, wanted = null) => [
  for (child in graph_model.element_children(value)
    where wanted == null or graph_model.tag(child) == wanted) child
]

fn first(values) => if (len(values) > 0) values[0] else null

fn model_values(workspace, wanted) {
  let blocks = children(workspace, "c4-model");
  if (len(blocks) == 0) { [] } else { children(blocks[0], wanted) }
}

fn view_values(workspace) {
  let blocks = children(workspace, "c4-views");
  if (len(blocks) == 0) { [] } else { children(blocks[0], "c4-view") }
}

fn style_values(workspace) {
  let blocks = children(workspace, "c4-styles");
  if (len(blocks) == 0) { [] } else { children(blocks[0], "c4-style") }
}

fn source_block_diagnostics(source, wanted) => [
  for (i, value in children(source, wanted) where i > 0)
    diagnostic.for_value("structurizr.invalid-context", "error",
      "Structurizr workspace must not contain multiple '" ++ wanted ++ "' blocks",
      "source." ++ wanted ++ "[" ++ string(i) ++ "]", value)
]

fn source_child_diagnostics(source) => [
  for (i, value in children(source), let tag = graph_model.tag(value)
    where not contains([
      "argument", "statement", "model", "views", "properties", "diagnostics"
    ], tag))
    diagnostic.for_value("structurizr.invalid-context", "error",
      "Structurizr '" ++ tag ++ "' is not valid directly under workspace",
      "source." ++ tag ++ "[" ++ string(i) ++ "]", value)
]

pub fn validate_source(source) {
  if (not (source is element) or graph_model.tag(source) != "workspace" or
      string(source.flavor) != "structurizr" or string(source["ir-stage"]) != "source") { [
    diagnostic.for_value("structurizr.invalid-root", "error",
      "Structurizr source root must be a source-stage <workspace>", "source", source)
  ] } else { [
    *source_block_diagnostics(source, "model"),
    *source_block_diagnostics(source, "views"),
    *source_child_diagnostics(source)
  ] }
}

fn duplicate_diagnostics(values, kind, field = "id",
    code = "structurizr.duplicate-identifier") => [
  for (i, value in values, let id = string(value[field]),
    let earlier = [for (j, other in values where j < i and string(other[field]) == id) other]
    where id != "" and len(earlier) > 0)
    diagnostic.for_value(code, "error",
      "Duplicate " ++ kind ++ " identifier '" ++ id ++ "'",
      kind ++ ":" ++ id, value)
]

fn element_by_id(elements, id) => first([
  for (entry in elements where string(entry.id) == string(id)) entry
])

fn reference_matches(entry, reference) => string(entry.id) == string(reference) or
  string(entry.identifier) == string(reference) or
  string(graph_model.optional(entry, "local-identifier")) == string(reference) or
  ends_with(string(entry.identifier), "." ++ string(reference))

fn reference_entries(elements, reference) => [
  for (entry in elements where reference_matches(entry, reference)) entry
]

fn parent_allowed(kind, parent_kind) {
  if (contains(["person", "software-system", "custom", "deployment-environment"], kind)) {
    parent_kind == null
  } else if (kind == "container") { parent_kind == "software-system" }
  else if (kind == "component") { parent_kind == "container" }
  // Root groups have a null parent; Lambda membership intentionally skips absence values.
  else if (kind == "group") { parent_kind == null or contains([
    "software-system", "container", "deployment-environment", "deployment-node"
  ], parent_kind) }
  else if (kind == "deployment-group") { parent_kind == "deployment-environment" }
  else if (kind == "deployment-node") {
    parent_kind == "deployment-environment" or parent_kind == "deployment-node"
  } else if (contains([
    "infrastructure-node", "software-system-instance", "container-instance"
  ], kind)) { parent_kind == "deployment-node" }
  else { false }
}

fn containment_diagnostics(elements) => [
  for (entry in elements, let parent_id = graph_model.optional(entry, "parent"),
    let parent = if (parent_id == null) { null } else { element_by_id(elements, parent_id) },
    let parent_kind = if (parent == null) { null } else { string(parent.kind) }
    where not parent_allowed(string(entry.kind), parent_kind))
    diagnostic.for_value("structurizr.invalid-containment", "error",
      "Element '" ++ string(entry.id) ++ "' of kind '" ++ string(entry.kind) ++
        "' is not valid under '" ++ string(parent_kind) ++ "'",
      "element:" ++ string(entry.id) ++ ".parent", entry)
]

fn model_reference_diagnostics(elements) => [
  for (entry in elements, let kind = string(entry.kind),
    let wanted = if (kind == "software-system-instance") { "software-system" }
      else if (kind == "container-instance") { "container" } else { null },
    let reference = graph_model.optional(entry, "model-ref"),
    let targets = if (wanted == null or reference == null) { [] }
      else { reference_entries(elements, reference) }
    where wanted != null and (len(targets) != 1 or string(targets[0].kind) != wanted))
    diagnostic.for_value("structurizr.unresolved-identifier", "error",
      "Instance '" ++ string(entry.id) ++ "' must reference one " ++ wanted,
      "element:" ++ string(entry.id) ++ ".model-ref", entry)
]

fn deployment_group_diagnostics(elements) => [
  for (entry in elements, group_ref in children(entry, "deployment-group-ref"),
    let targets = [for (candidate in reference_entries(elements, group_ref.identifier)
      where string(candidate.kind) == "deployment-group") candidate]
    where len(targets) != 1)
    diagnostic.for_value("structurizr.unresolved-identifier", "error",
      "Deployment group '" ++ string(group_ref.identifier) ++ "' does not resolve",
      "element:" ++ string(entry.id) ++ ".deployment-group", group_ref)
]

fn logical_kind(kind) => contains([
  "person", "software-system", "container", "component", "custom"
], kind)

fn relationship_allowed(source_kind, destination_kind) {
  if (logical_kind(source_kind)) { logical_kind(destination_kind) }
  else if (source_kind == "deployment-node") { destination_kind == "deployment-node" }
  else if (source_kind == "infrastructure-node") { contains([
    "deployment-node", "infrastructure-node", "software-system-instance",
    "container-instance"
  ], destination_kind) }
  else if (contains(["software-system-instance", "container-instance"], source_kind)) {
    destination_kind == "infrastructure-node"
  } else { false }
}

fn relationship_diagnostic(elements, entry) {
  let source = element_by_id(elements, entry.source);
  let destination = element_by_id(elements, entry.destination);
  if (source == null or destination == null) { [
      diagnostic.for_value("structurizr.unresolved-identifier", "error",
        "Relationship '" ++ string(entry.id) ++ "' has an unresolved endpoint",
        "relationship:" ++ string(entry.id), entry)
    ] } else if (not relationship_allowed(string(source.kind), string(destination.kind))) { [
      diagnostic.for_value("structurizr.invalid-relationship", "error",
        "Relationship from '" ++ string(source.kind) ++ "' to '" ++
          string(destination.kind) ++ "' is not allowed",
        "relationship:" ++ string(entry.id), entry)
    ] } else { [] }
}

fn relationship_diagnostics(elements, relationships) => [
  for (entry in relationships, value in relationship_diagnostic(elements, entry)) value
]

fn view_scope_valid(elements, view) {
  let kind = string(view.kind);
  let scope = graph_model.optional(view, "scope");
  let target = if (scope == null or string(scope) == "*") { null }
    else { element_by_id(elements, scope) };
  if (contains(["systemLandscape", "custom", "filtered"], kind)) { true }
  else if (kind == "systemContext" or kind == "container") {
    target != null and string(target.kind) == "software-system"
  } else if (kind == "component") { target != null and string(target.kind) == "container" }
  else if (kind == "dynamic") { string(scope) == "*" or
    (target != null and logical_kind(string(target.kind)))
  }
  else if (kind == "deployment") {
    let environments = reference_entries(elements, view.environment);
    (string(scope) == "*" or
      (target != null and string(target.kind) == "software-system")) and
      len([for (entry in environments
        where string(entry.kind) == "deployment-environment") entry]) == 1
  }
  else { false }
}

fn view_diagnostics(elements, views) => [
  *duplicate_diagnostics(views, "view", "key", "structurizr.duplicate-view-key"),
  for (view in views, value in if (not view_scope_valid(elements, view)) { [
    diagnostic.for_value("structurizr.invalid-view-scope", "error",
      "View '" ++ string(view.key) ++ "' has an invalid scope",
      "view:" ++ string(view.key) ++ ".scope", view)
  ] } else if (string(view.kind) == "filtered" and len([
    for (base in views where string(base.key) == string(view["base-key"]) and
      not contains(["filtered", "dynamic", "deployment"], string(base.kind))) base
  ]) != 1) { [
    diagnostic.for_value("structurizr.invalid-view-scope", "error",
      "Filtered view '" ++ string(view.key) ++ "' has an invalid base view",
      "view:" ++ string(view.key) ++ ".base-key", view)
  ] } else if (string(view.kind) == "image") { [
    diagnostic.for_value("structurizr.unsupported-view", "warning",
      "Image views are preserved but not rendered", "view:" ++ string(view.key), view)
  ] } else { [] }) value
]

fn expression_diagnostics(views) => [
  for (view in views, rule_kind in ["include", "exclude"],
    i, rule in children(view, rule_kind), let found = expression.validate(rule.expression)
    where found != null)
    diagnostic.for_value(found.code,
      if (found.code == "structurizr.invalid-expression") "error" else "warning", found.message,
      "view:" ++ string(view.key) ++ "." ++ rule_kind ++ "[" ++ string(i) ++ "]", rule)
]

fn style_diagnostics(workspace) => [
  for (style in style_values(workspace), property in children(style, "property"),
    let target = if (string(style["target-kind"]) == "element") { "node" }
      else { "edge" }
    where not style_rules.supported(target, property))
    diagnostic.for_value("structurizr.unsupported-style", "warning",
      "Style property '" ++ string(property.name) ++ "' cannot be lowered safely",
      "style:" ++ string(style.tag) ++ "." ++ string(property.name), style)
]

pub fn validate(workspace) {
  if (not (workspace is element) or graph_model.tag(workspace) != "c4-workspace") { [
    diagnostic.make("structurizr.invalid-root", "error",
      "Canonical Structurizr root must be <c4-workspace>", "workspace", null)
  ] } else {
    let elements = model_values(workspace, "c4-element");
    let relationships = model_values(workspace, "c4-relationship");
    let views = view_values(workspace);
    [
      *duplicate_diagnostics(elements, "element"),
      *duplicate_diagnostics(relationships, "relationship"),
      *containment_diagnostics(elements),
      *model_reference_diagnostics(elements),
      *deployment_group_diagnostics(elements),
      *relationship_diagnostics(elements, relationships),
      *view_diagnostics(elements, views),
      *expression_diagnostics(views),
      *style_diagnostics(workspace)
    ]
  }
}

fn source_field(value, name) =>
  if (value.source != null) value.source[name] else null

fn diagnostic_element(value) =>
  <diagnostic code: value.code, severity: value.severity,
    message: value.message, path: value.path,
    'source-start': source_field(value, "start"),
    'source-end': source_field(value, "end"),
    'source-line': source_field(value, "line"),
    'source-column': source_field(value, "column")>

pub fn attach(workspace, values) {
  let attrs = map(workspace);
  let existing = [for (block in children(workspace, "diagnostics"))
    for (value in children(block, "diagnostic")) value];
  <'c4-workspace' *:attrs;
    for (child in children(workspace) where graph_model.tag(child) != "diagnostics") child
    <diagnostics;
      for (value in existing) value
      for (value in values) diagnostic_element(value)
    >
  >
}
