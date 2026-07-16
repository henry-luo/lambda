// Mermaid class source metadata to measured canonical node content.

import model: lambda.package.graph.model

fn class_members(node) => [
  for (child in model.element_children(node)
    where model.tag(child) == "class-member") child
]

fn member_row(member) =>
  <div class: "graph-class-member graph-class-" ++ string(member.kind),
      'data-member-kind': member.kind,
      'data-visibility': member.visibility,
      style: "display:block;text-align:left;white-space:nowrap;padding:1px 2px;";
    string(member.value)
  >

fn class_content(node, members) =>
  <div class: "graph-class-content", style: "display:block;min-width:120px;";
    <div class: "graph-class-title",
        style: "display:block;font-weight:600;text-align:center;padding:2px 4px;";
      string(model.label_source(node, node.id))
    >
    <div class: "graph-class-members",
        style: "display:block;border-top:1px solid currentColor;margin-top:4px;padding-top:3px;";
      for (member in members) member_row(member)
    >
  >

fn adapt_node(node) {
  let attrs = map(node);
  let members = class_members(node);
  <node *:attrs;
    if (len(members) > 0) { class_content(node, members) }
    for (child in model.child_items(node)
      where not (child is element and model.tag(child) == "class-member")) child
  >
}

fn adapt_child(child) {
  if (not (child is element)) child
  else if (model.tag(child) == "node") adapt_node(child)
  else if (model.tag(child) == "subgraph") {
    let attrs = map(child);
    <subgraph *:attrs;
      for (nested in model.child_items(child)) adapt_child(nested)
    >
  }
  else child
}

pub fn adapt(graph) {
  let attrs = map(graph);
  <graph *:attrs;
    for (child in model.child_items(graph)) adapt_child(child)
  >
}
