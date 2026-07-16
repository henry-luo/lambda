// Mermaid ER source metadata to measured canonical entity content.

import model: lambda.package.graph.model
import compartment: .compartment

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
  compartment.content(node, {
    root_class: "graph-er-content", title_class: "graph-er-title",
    body_class: "graph-er-attributes", min_width: 140
  }, [for (attribute in attributes) attribute_row(attribute)])

pub fn adapt(graph) => compartment.adapt(graph, "er-attribute", entity_content)
