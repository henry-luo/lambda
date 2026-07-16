// Shared measured title/body reconstruction for Mermaid node families.

import model: lambda.package.graph.model

pub fn content(node, config, rows) =>
  <div class: config.root_class,
      style: "display:block;min-width:" ++ string(config.min_width) ++ "px;";
    <div class: config.title_class,
        style: "display:block;font-weight:600;text-align:center;padding:2px 4px;";
      string(model.label_source(node, node.id))
    >
    <div class: config.body_class,
        style: "display:block;border-top:1px solid currentColor;margin-top:4px;padding-top:3px;";
      for (row in rows) row
    >
  >

fn source_values(node, source_tag) => [
  for (child in model.element_children(node)
    where model.tag(child) == source_tag) child
]

fn adapt_node(node, source_tag, make_content, include_empty) {
  let attrs = map(node);
  let values = source_values(node, source_tag);
  <node *:attrs;
    if (include_empty or len(values) > 0) { make_content(node, values) }
    for (child in model.child_items(node)
      where not (child is element and model.tag(child) == source_tag)) child
  >
}

fn adapt_child(child, source_tag, make_content, include_empty) {
  if (not (child is element)) child
  else if (model.tag(child) == "node") adapt_node(child, source_tag, make_content, include_empty)
  else if (model.tag(child) == "subgraph") {
    let attrs = map(child);
    <subgraph *:attrs;
      for (nested in model.child_items(child))
        adapt_child(nested, source_tag, make_content, include_empty)
    >
  }
  else child
}

pub fn adapt(graph, source_tag, make_content) {
  let attrs = map(graph);
  <graph *:attrs;
    for (child in model.child_items(graph)) adapt_child(child, source_tag, make_content, false)
  >
}

pub fn adapt_all(graph, source_tag, make_content) {
  let attrs = map(graph);
  <graph *:attrs;
    for (child in model.child_items(graph)) adapt_child(child, source_tag, make_content, true)
  >
}
