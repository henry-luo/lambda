// Pure allowlisted Structurizr view-expression evaluation.

import graph_model: lambda.package.graph.model

fn children(value, wanted) => [
  for (child in graph_model.element_children(value) where graph_model.tag(child) == wanted) child
]

fn tags(value) => [for (tag in children(value, "tag")) string(tag.name)]

fn ordered_ids(elements, ids) => [
  for (entry in elements where contains(ids, string(entry.id))) string(entry.id)
]

fn value_after(expression, prefix) =>
  trim(slice(expression, len(prefix), len(expression)))

fn selected_tags(expression, prefix) => [
  for (tag in split(value_after(expression, prefix), ",") where trim(tag) != "") trim(tag)
]

fn issue(code, message) => {code: code, message: message}

fn paren_balance(value, index = 0, depth = 0) {
  if (index >= len(value)) { depth }
  else {
    let ch = slice(value, index, index + 1);
    let next = if (ch == "(") depth + 1 else if (ch == ")") depth - 1 else depth;
    if (next < 0) { -1 } else { paren_balance(value, index + 1, next) }
  }
}

fn matching_close(value, index = 1, depth = 1) {
  if (index >= len(value)) { -1 }
  else {
    let ch = slice(value, index, index + 1);
    let next = if (ch == "(") depth + 1 else if (ch == ")") depth - 1 else depth;
    if (next == 0) { index } else { matching_close(value, index + 1, next) }
  }
}

fn unwrapped(expression) {
  let value = trim(expression);
  if (starts_with(value, "(") and matching_close(value) == len(value) - 1) {
    unwrapped(slice(value, 1, len(value) - 1))
  } else { value }
}

fn top_operator(value, operator, index = 0, depth = 0) {
  if (index > len(value) - len(operator)) { -1 }
  else {
    let ch = slice(value, index, index + 1);
    let next = if (ch == "(") depth + 1 else if (ch == ")") depth - 1 else depth;
    if (depth == 0 and starts_with(slice(value, index, len(value)), operator)) { index }
    else { top_operator(value, operator, index + 1, next) }
  }
}

fn validate_pair(value, operator, index) {
  let left = validate(slice(value, 0, index));
  if (left != null) { left }
  else { validate(slice(value, index + len(operator), len(value))) }
}

fn required_value_issue(value, prefix) =>
  if (trim(value_after(value, prefix)) == "")
    issue("structurizr.invalid-expression", "Expression predicate requires a value")
  else null

fn property_issue(value, prefix) {
  let parts = split(value_after(value, prefix), "]==");
  if (len(parts) != 2 or trim(parts[0]) == "" or trim(parts[1]) == "")
    issue("structurizr.invalid-expression", "Property expression requires [name]==value")
  else null
}

fn endpoint_issue(value) {
  let endpoints = split(value, "->");
  if (len(endpoints) != 2 or trim(endpoints[0]) == "" or trim(endpoints[1]) == "")
    issue("structurizr.invalid-expression", "Relationship expression requires source->destination")
  else null
}

fn atomic_issue(expression) {
  let value = trim(expression);
  let relation = trim(if (starts_with(value, "relationship=="))
    value_after(value, "relationship==") else value);
  let element_value = trim(if (starts_with(value, "element=="))
    value_after(value, "element==") else value);
  if (value == "") issue("structurizr.invalid-expression", "Expression must not be empty")
  else if (value == "*" or value == "*?") null
  else if (starts_with(value, "relationship.properties["))
    property_issue(value, "relationship.properties[")
  else if (starts_with(value, "relationship.tag=="))
    required_value_issue(value, "relationship.tag==")
  else if (starts_with(value, "relationship.tag!="))
    required_value_issue(value, "relationship.tag!=")
  else if (starts_with(value, "relationship.source=="))
    required_value_issue(value, "relationship.source==")
  else if (starts_with(value, "relationship.destination=="))
    required_value_issue(value, "relationship.destination==")
  else if (starts_with(value, "relationship.tag") or
      starts_with(value, "relationship.source") or
      starts_with(value, "relationship.destination"))
    issue("structurizr.invalid-expression", "Relationship predicate has invalid syntax")
  else if (starts_with(value, "relationship."))
    issue("structurizr.unsupported-expression", "Relationship predicate is not supported")
  else if (starts_with(value, "relationship==")) endpoint_issue(relation)
  else if (value == "*->*" or
      (index_of(value, "->") > 0 and not ends_with(value, "->"))) endpoint_issue(value)
  else if (starts_with(value, "element.properties["))
    property_issue(value, "element.properties[")
  else if (starts_with(value, "element.type=="))
    required_value_issue(value, "element.type==")
  else if (starts_with(value, "element.parent=="))
    required_value_issue(value, "element.parent==")
  else if (starts_with(value, "element.group=="))
    required_value_issue(value, "element.group==")
  else if (starts_with(value, "element.tag=="))
    required_value_issue(value, "element.tag==")
  else if (starts_with(value, "element.tag!="))
    required_value_issue(value, "element.tag!=")
  else if (starts_with(value, "element.technology=="))
    required_value_issue(value, "element.technology==")
  else if (starts_with(value, "element.technology!="))
    required_value_issue(value, "element.technology!=")
  else if (starts_with(value, "element.type") or starts_with(value, "element.parent") or
      starts_with(value, "element.group") or starts_with(value, "element.tag") or
      starts_with(value, "element.technology"))
    issue("structurizr.invalid-expression", "Element predicate has invalid syntax")
  else if (starts_with(value, "element."))
    issue("structurizr.unsupported-expression", "Element predicate is not supported")
  else if (starts_with(element_value, "->") or ends_with(element_value, "->")) {
    let middle = slice(element_value, if (starts_with(element_value, "->")) 2 else 0,
      len(element_value) - (if (ends_with(element_value, "->")) 2 else 0));
    if (trim(middle) == "" or contains(middle, "->"))
      issue("structurizr.invalid-expression", "Coupling expression requires an element")
    else null
  }
  else if (contains(value, "==") or contains(value, "!=") or
      contains(value, "[") or contains(value, "]"))
    issue("structurizr.invalid-expression", "Expression has invalid predicate syntax")
  else null
}

pub fn validate(expression) {
  let raw = trim(expression);
  let balance = paren_balance(raw);
  let value = unwrapped(raw);
  let alternative = top_operator(value, "||");
  let term = top_operator(value, "&&");
  if (balance != 0)
    issue("structurizr.invalid-expression", "Expression has unbalanced parentheses")
  else if (alternative >= 0) validate_pair(value, "||", alternative)
  else if (term >= 0) validate_pair(value, "&&", term)
  else atomic_issue(value)
}

fn has_all_tags(value, wanted) => len([
  for (tag in wanted where contains(tags(value), tag)) tag
]) == len(wanted)

fn property_matches(value, expression, prefix) {
  let parts = split(value_after(expression, prefix), "]==");
  if (len(parts) != 2 or trim(parts[0]) == "") false
  else len([for (property in children(value, "property")
    where string(property.name) == trim(parts[0]) and
      string(property.value) == trim(parts[1])) property]) > 0
}

fn canonical_kind(value) {
  let kind = lower(trim(value));
  if (kind == "softwaresystem" or kind == "software system") "software-system"
  else if (kind == "deploymentnode" or kind == "deployment node") "deployment-node"
  else if (kind == "infrastructurenode" or kind == "infrastructure node")
    "infrastructure-node"
  else if (kind == "softwaresysteminstance" or kind == "software system instance")
    "software-system-instance"
  else if (kind == "containerinstance" or kind == "container instance")
    "container-instance"
  else kind
}

fn reference_matches(entry, value) {
  let wanted = trim(value);
  string(entry.id) == wanted or string(entry.identifier) == wanted or
    string(graph_model.optional(entry, "local-identifier")) == wanted or
    ends_with(string(entry.identifier), "." ++ wanted)
}

fn reference_ids(elements, value) => [
  for (entry in elements where reference_matches(entry, value)) string(entry.id)
]

fn atomic_ids(elements, expression) {
  let value = trim(if (starts_with(expression, "element=="))
    value_after(expression, "element==") else expression);
  if (value == "*") ordered_ids(elements, [for (entry in elements) string(entry.id)])
  else if (starts_with(value, "element.type==")) [
    for (entry in elements
      where string(entry.kind) == canonical_kind(value_after(value, "element.type==")))
      string(entry.id)
  ]
  else if (starts_with(value, "element.parent==")) {
    let parents = reference_ids(elements, value_after(value, "element.parent=="));
    [for (entry in elements where contains(parents, string(entry.parent))) string(entry.id)]
  }
  else if (starts_with(value, "element.group==")) [
    for (entry in elements where string(entry.group) == value_after(value, "element.group=="))
      string(entry.id)
  ]
  else if (starts_with(value, "element.tag==")) {
    let wanted = selected_tags(value, "element.tag==");
    [for (entry in elements where has_all_tags(entry, wanted)) string(entry.id)]
  }
  else if (starts_with(value, "element.tag!=")) {
    let wanted = selected_tags(value, "element.tag!=");
    [for (entry in elements where not has_all_tags(entry, wanted)) string(entry.id)]
  }
  else if (starts_with(value, "element.technology==")) [
    for (entry in elements where string(entry.technology) ==
      value_after(value, "element.technology==")) string(entry.id)
  ]
  else if (starts_with(value, "element.technology!=")) [
    for (entry in elements where string(entry.technology) !=
      value_after(value, "element.technology!=")) string(entry.id)
  ]
  else if (starts_with(value, "element.properties[")) [
    for (entry in elements
      where property_matches(entry, value, "element.properties[")) string(entry.id)
  ]
  else if (starts_with(value, "element.")) []
  else reference_ids(elements, value)
}

fn coupled_ids(elements, relationships, expression) {
  let value = trim(if (starts_with(expression, "element=="))
    value_after(expression, "element==") else expression);
  let inbound = starts_with(value, "->");
  let outbound = ends_with(value, "->");
  let start = if (inbound) 2 else 0;
  let finish = len(value) - (if (outbound) 2 else 0);
  let base = atomic_ids(elements, slice(value, start, finish));
  let connected = [
    for (relation in relationships)
      for (id in if (inbound and contains(base, string(relation.destination)))
          [string(relation.source)]
        else if (outbound and contains(base, string(relation.source)))
          [string(relation.destination)]
        else []) id
  ];
  ordered_ids(elements, unique([*base, *connected]))
}

pub fn element_ids(elements, relationships, expression) {
  let value = unwrapped(expression);
  let alternative = top_operator(value, "||");
  let term = top_operator(value, "&&");
  if (alternative >= 0) ordered_ids(elements, unique([
    *element_ids(elements, relationships, slice(value, 0, alternative)),
    *element_ids(elements, relationships,
      slice(value, alternative + 2, len(value)))
  ]))
  else if (term >= 0) {
    let left = element_ids(elements, relationships, slice(value, 0, term));
    let right = element_ids(elements, relationships, slice(value, term + 2, len(value)));
    [for (id in left where contains(right, id)) id]
  }
  else if (starts_with(value, "->") or ends_with(value, "->") or
      starts_with(value, "element==->")) coupled_ids(elements, relationships, value)
  else atomic_ids(elements, value)
}

fn endpoint_matches(elements, endpoint, expression) =>
  expression == "*" or contains(reference_ids(elements, expression), string(endpoint))

fn relation_atomic_matches(elements, relation, expression) {
  let value = trim(if (starts_with(expression, "relationship=="))
    value_after(expression, "relationship==") else expression);
  if (value == "*" or value == "*->*") true
  else if (starts_with(value, "relationship.tag=="))
    has_all_tags(relation, selected_tags(value, "relationship.tag=="))
  else if (starts_with(value, "relationship.tag!="))
    not has_all_tags(relation, selected_tags(value, "relationship.tag!="))
  else if (starts_with(value, "relationship.source=="))
    endpoint_matches(elements, relation.source, value_after(value, "relationship.source=="))
  else if (starts_with(value, "relationship.destination=="))
    endpoint_matches(elements, relation.destination,
      value_after(value, "relationship.destination=="))
  else if (starts_with(value, "relationship.properties["))
    property_matches(relation, value, "relationship.properties[")
  else {
    let endpoints = split(value, "->");
    len(endpoints) == 2 and endpoint_matches(elements, relation.source, trim(endpoints[0])) and
      endpoint_matches(elements, relation.destination, trim(endpoints[1]))
  }
}

pub fn relationship_expression(expression) {
  let value = unwrapped(expression);
  let alternative = top_operator(value, "||");
  let term = top_operator(value, "&&");
  if (alternative >= 0) {
    relationship_expression(slice(value, 0, alternative)) or
      relationship_expression(slice(value, alternative + 2, len(value)))
  }
  else if (term >= 0) {
    relationship_expression(slice(value, 0, term)) or
      relationship_expression(slice(value, term + 2, len(value)))
  }
  else { starts_with(value, "relationship.") or starts_with(value, "relationship==") or
    value == "*->*" or (index_of(value, "->") > 0 and not ends_with(value, "->")) }
}

pub fn relationship_matches(elements, relation, expression) {
  let value = unwrapped(expression);
  let alternative = top_operator(value, "||");
  let term = top_operator(value, "&&");
  if (alternative >= 0) {
    relationship_matches(elements, relation, slice(value, 0, alternative)) or
      relationship_matches(elements, relation, slice(value, alternative + 2, len(value)))
  }
  else if (term >= 0) {
    relationship_matches(elements, relation, slice(value, 0, term)) and
      relationship_matches(elements, relation, slice(value, term + 2, len(value)))
  }
  else { relation_atomic_matches(elements, relation, value) }
}
