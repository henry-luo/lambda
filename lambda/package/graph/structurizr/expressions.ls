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

fn has_all_tags(value, wanted) => len([
  for (tag in wanted where contains(tags(value), tag)) tag
]) == len(wanted)

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
  let value = trim(expression);
  let alternatives = split(value, "||");
  let terms = split(value, "&&");
  if (len(alternatives) > 1) ordered_ids(elements, unique([
    for (part in alternatives) for (id in element_ids(elements, relationships, part)) id
  ]))
  else if (len(terms) > 1) {
    let selected = element_ids(elements, relationships, terms[0]);
    [for (id in selected where len([
      for (part in slice(terms, 1, len(terms))
        where contains(element_ids(elements, relationships, part), id)) part
    ]) == len(terms) - 1) id]
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
  else {
    let endpoints = split(value, "->");
    len(endpoints) == 2 and endpoint_matches(elements, relation.source, trim(endpoints[0])) and
      endpoint_matches(elements, relation.destination, trim(endpoints[1]))
  }
}

pub fn relationship_expression(expression) {
  let value = trim(expression);
  starts_with(value, "relationship.") or starts_with(value, "relationship==") or
    value == "*->*" or (index_of(value, "->") > 0 and not ends_with(value, "->"))
}

pub fn relationship_matches(elements, relation, expression) {
  let value = trim(expression);
  let alternatives = split(value, "||");
  let terms = split(value, "&&");
  if (len(alternatives) > 1) len([
    for (part in alternatives where relationship_matches(elements, relation, part)) part
  ]) > 0
  else if (len(terms) > 1) len([
    for (part in terms where relationship_matches(elements, relation, part)) part
  ]) == len(terms)
  else relation_atomic_matches(elements, relation, value)
}
