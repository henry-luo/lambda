// Mermaid ER source metadata to measured canonical entity content.

import model: lambda.package.graph.model

fn entity_attributes(node) => [
  for (child in model.element_children(node)
    where model.tag(child) == "er-attribute") child
]

fn attribute_text(attribute) {
  let key = if (attribute.key != null and attribute.key != "")
    " [" ++ string(attribute.key) ++ "]" else "";
  let comment = if (attribute.comment != null and attribute.comment != "")
    " - " ++ string(attribute.comment) else "";
  string(attribute["type"]) ++ " " ++ string(attribute.name) ++ key ++ comment
}

fn attribute_row(attribute) =>
  <div class: "graph-er-attribute", 'data-attribute-type': attribute["type"],
      'data-attribute-name': attribute.name, 'data-attribute-key': attribute.key,
      style: "display:block;text-align:left;white-space:nowrap;padding:1px 2px;";
    attribute_text(attribute)
  >

fn entity_content(node, attributes) =>
  <div class: "graph-er-content", style: "display:block;min-width:140px;";
    <div class: "graph-er-title",
        style: "display:block;font-weight:600;text-align:center;padding:2px 4px;";
      string(model.label_source(node, node.id))
    >
    <div class: "graph-er-attributes",
        style: "display:block;border-top:1px solid currentColor;margin-top:4px;padding-top:3px;";
      for (attribute in attributes) attribute_row(attribute)
    >
  >

fn adapt_node(node) {
  let attrs = map(node);
  let attributes = entity_attributes(node);
  <node *:attrs;
    if (len(attributes) > 0) { entity_content(node, attributes) }
    for (child in model.child_items(node)
      where not (child is element and model.tag(child) == "er-attribute")) child
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
