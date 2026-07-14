// Recursive queries over the canonical Mark Graph IR.

fn element_children(value) {
  if (value is element and len(value) > 0) {
    [for (i in 0 to (len(value) - 1), let child = value[i]
      where child is element) child]
  } else []
}

fn tag(value) => if (value is element) string(name(value)) else ""

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

pub fn nodes(graph) => [for (entry in node_entries(graph)) entry.value]

pub fn edges(graph) => [for (entry in edge_entries(graph)) entry.value]

pub fn subgraphs(graph) => [for (child in element_children(graph),
  group in if (tag(child) == "subgraph") [child, *subgraphs(child)] else []) group]

pub fn style_rules(graph) => metadata_entries(graph, "style-rule")

pub fn class_assignments(graph) => metadata_entries(graph, "class-assignment")

pub fn classes_for(graph, node_id) {
  [for (assignment in class_assignments(graph),
    let targets = split(string(if (assignment.targets != null) assignment.targets else ""), ",")
    where contains(targets, string(node_id)) and assignment.class != null)
    string(assignment.class)]
}

pub fn direction(graph) {
  let value = if (graph.direction != null) graph.direction
    else if (graph["rank-dir"] != null) graph["rank-dir"]
    else "TB";
  if (value == "TD") "TB" else string(value)
}
