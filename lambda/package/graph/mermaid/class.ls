// Mermaid class source metadata to measured canonical node content.

import model: lambda.package.graph.model
import compartment: .compartment

fn member_row(member) =>
  <div class: "graph-class-member graph-class-" ++ string(member.kind),
      'data-member-kind': member.kind,
      'data-visibility': member.visibility,
      style: "display:block;text-align:left;white-space:nowrap;padding:1px 2px;";
    string(member.value)
  >

fn class_content(node, members) =>
  compartment.content(node, {
    root_class: "graph-class-content", title_class: "graph-class-title",
    body_class: "graph-class-members", min_width: 120
  }, [for (member in members) member_row(member)])

pub fn adapt(graph) => compartment.adapt(graph, "class-member", class_content)
