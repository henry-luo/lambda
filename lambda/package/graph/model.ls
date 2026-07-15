// Recursive queries shared by source-stage and canonical Mark Graph IR.

pub fn element_children(value) {
  if (value is element and len(value) > 0) {
    [for (i in 0 to (len(value) - 1), let child = value[i]
      where child is element) child]
  } else []
}

pub fn tag(value) => if (value is element) string(name(value)) else ""

fn first_direct_child(value, wanted_tag) {
  let matches = [for (child in element_children(value) where tag(child) == wanted_tag) child];
  if (len(matches) > 0) matches[0] else null
}

pub fn child_items(value) {
  if (value is element and len(value) > 0) {
    [for (i in 0 to (len(value) - 1)) value[i]]
  } else []
}

pub fn label_element(value) => first_direct_child(value, "label")

pub fn content_element(value) => first_direct_child(value, "content")

pub fn label_source(value, fallback = null) {
  let label = label_element(value);
  if (label != null and len(label) > 0) label[0]
  else if (value.label != null) value.label
  else fallback
}

pub fn label_format(value) {
  let label = label_element(value);
  if (label != null and label.format != null) string(label.format)
  else if (value["label-format"] != null) string(value["label-format"])
  else "text"
}

pub fn content_items(value) {
  let content = content_element(value);
  if (content != null) child_items(content) else []
}

fn group_id(group, fallback) {
  if (group.id != null and group.id != "") string(group.id) else fallback
}

fn nested_entries(container, wanted_tag, parent_group, prefix) {
  [for (i, child in element_children(container),
    entry in if (tag(child) == wanted_tag) [{value: child, group: parent_group}]
      else if (tag(child) == "subgraph")
        nested_entries(child, wanted_tag, group_id(child, prefix ++ string(i)),
                       prefix ++ string(i) ++ ".")
      else []) entry]
}

fn metadata_entries(container, wanted_tag) {
  [for (child in element_children(container),
    entry in if (tag(child) == wanted_tag) [child]
      else if (tag(child) == "subgraph") metadata_entries(child, wanted_tag)
      else []) entry]
}

pub fn node_entries(graph) => nested_entries(graph, "node", null, "g")

pub fn edge_entries(graph) => nested_entries(graph, "edge", null, "g")

fn nested_subgraph_entries(container, parent_group, prefix) {
  [for (i, child in element_children(container),
    entry in if (tag(child) == "subgraph") {
      let id = group_id(child, prefix ++ string(i));
      [
        {value: child, group: id, parent: parent_group},
        *nested_subgraph_entries(child, id, prefix ++ string(i) ++ ".")
      ]
    } else []) entry]
}

pub fn subgraph_entries(graph) => nested_subgraph_entries(graph, null, "g")

pub fn visual_subgraph_entries(graph) => [
  for (entry in subgraph_entries(graph) where entry.value.role != "scope") entry
]

pub fn ports(node) => [for (child in element_children(node) where tag(child) == "port") child]

pub fn port_entries(graph) => [
  for (entry in node_entries(graph), port in ports(entry.value)) {
    value: port,
    node: string(entry.value.id),
    group: entry.group
  }
]

pub fn nodes(graph) => [for (entry in node_entries(graph)) entry.value]

pub fn edges(graph) => [for (entry in edge_entries(graph)) entry.value]

pub fn subgraphs(graph) => [for (entry in subgraph_entries(graph)) entry.value]

pub fn style_rules(graph) => metadata_entries(graph, "style-rule")

pub fn class_assignments(graph) => metadata_entries(graph, "class-assignment")

pub fn style_assignments(graph) => metadata_entries(graph, "style-assignment")

pub fn interactions(graph) => metadata_entries(graph, "interaction")

pub fn edge_properties(graph) => metadata_entries(graph, "edge-property")

pub fn constraints(graph) => [
  for (group in element_children(graph), value in element_children(group)
    where tag(group) == "constraints" and tag(value) == "constraint") value
]

fn meta_values(graph, wanted_tag) {
  [for (child in element_children(graph),
    meta_child in if (tag(child) == "meta") element_children(child) else []
    where tag(meta_child) == wanted_tag and meta_child.value != null)
    string(meta_child.value)]
}

fn last_or(values, fallback) =>
  if (len(values) > 0) values[len(values) - 1] else fallback

pub fn title(graph) => last_or(meta_values(graph, "title"), null)

pub fn description(graph) => last_or(meta_values(graph, "description"), null)

fn assignment_targets(assignment) => [
  for (target in split(
    string(if (assignment.targets != null) assignment.targets else ""), ","))
  trim(string(target))
]

fn assignment_matches(assignment, target_kind, targets) {
  let assigned_targets = assignment_targets(assignment);
  assignment["target-kind"] == target_kind and
    (contains(assigned_targets, "default") or
     len([for (target in targets where contains(assigned_targets, string(target))) target]) > 0)
}

pub fn style_declarations_for(graph, target_kind, targets) => join([
  for (assignment in style_assignments(graph)
    where assignment_matches(assignment, target_kind, targets) and
      assignment.declarations != null) string(assignment.declarations)
], ";")

fn diagnostic_entries(container) {
  [for (child in element_children(container),
    entry in if (tag(child) == "diagnostic") [child]
      else if (tag(child) == "diagnostics" or tag(child) == "subgraph")
        diagnostic_entries(child)
      else []) entry]
}

pub fn diagnostics(graph) => diagnostic_entries(graph)

fn classes_for_kind(graph, target_kind, target_ids) {
  [for (assignment in class_assignments(graph),
    let targets = assignment_targets(assignment)
    where (assignment["target-kind"] == null or
      assignment["target-kind"] == target_kind) and
      len([for (target_id in target_ids where contains(targets, string(target_id))) target_id]) > 0 and
      assignment.class != null)
    string(assignment.class)]
}

pub fn classes_for(graph, node_id) => classes_for_kind(graph, "node", [node_id])

pub fn edge_classes_for(graph, edge_id, source_id = null) =>
  classes_for_kind(graph, "edge", [edge_id,
    for (value in [source_id] where value != null and value != edge_id) value])

fn class_style_declarations_for(graph, target_kind, target_id) {
  let classes = classes_for_kind(graph, target_kind, [target_id]);
  [for (rule in style_rules(graph)
    where rule.class != null and contains(classes, string(rule.class)) and
      rule.declarations != null) string(rule.declarations)]
}

pub fn node_style_declarations_for(graph, node_id) {
  let assigned = style_declarations_for(graph, "node", [string(node_id)]);
  join([
    *class_style_declarations_for(graph, "node", node_id),
    for (value in [assigned] where value != "") value
  ], ";")
}

pub fn edge_style_declarations_for(graph, edge_id, source_id, edge_index) {
  let assigned = style_declarations_for(graph, "edge", [string(edge_index), string(edge_id)]);
  let classes = edge_classes_for(graph, edge_id, source_id);
  join([
    for (rule in style_rules(graph)
      where rule.class != null and contains(classes, string(rule.class)) and
        rule.declarations != null) string(rule.declarations),
    for (value in [assigned] where value != "") value
  ], ";")
}

pub fn interaction_for(graph, target_id) {
  let matches = [for (interaction in interactions(graph)
    where interaction.target != null and string(interaction.target) == string(target_id))
    interaction];
  if (len(matches) > 0) matches[len(matches) - 1] else null
}

pub fn edge_property(graph, edge_id, source_id, key, fallback = null) {
  let target_ids = [edge_id,
    for (value in [source_id] where value != null and value != edge_id) value];
  let matches = [for (property in edge_properties(graph)
    where property.target != null and property.key != null and
      contains(target_ids, string(property.target)) and string(property.key) == string(key))
    property.value];
  if (len(matches) > 0) matches[len(matches) - 1] else fallback
}

pub fn direction(graph) {
  let value = if (graph.direction != null) graph.direction
    else if (graph["rank-dir"] != null) graph["rank-dir"]
    else "TB";
  if (value == "TD") "TB" else string(value)
}
