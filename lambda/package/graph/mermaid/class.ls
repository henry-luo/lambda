// Mermaid class source metadata to measured canonical node content.

import model: lambda.package.graph.model
import compartment: .compartment
import config: .config

fn member_row(member) =>
  <div class: "graph-class-member graph-class-" ++ string(member.kind),
      'data-member-kind': member.kind,
      'data-visibility': member.visibility,
      'data-member-classifier': member.classifier,
      style: "display:block;text-align:left;white-space:nowrap;padding:1px 2px;" ++
        (if (member.classifier == "static") "text-decoration:underline;" else "") ++
        (if (member.classifier == "abstract") "font-style:italic;" else "");
    string(if (member.display != null) member.display else member.value)
  >

fn member_section(kind, members) =>
  <div class: "graph-class-" ++ kind ++ "s",
      'data-class-compartment': kind,
      style: "display:block;border-top:1px solid currentColor;min-height:12px;padding:3px 2px;";
    for (member in members where member.kind == kind) member_row(member)
  >

fn class_content(node, members) =>
  <div class: "graph-class-content", style: "display:block;min-width:120px;";
    <div class: "graph-class-title",
        style: "display:block;font-weight:600;text-align:center;padding:3px 4px;";
      for (member in members where member.kind == "stereotype")
        <div class: "graph-class-stereotype", 'data-member-kind': "stereotype",
            style: "display:block;font-weight:400;white-space:nowrap;";
          string(if (member.display != null) member.display else member.value)
        >
      string(model.label_source(node, node.id))
    >
    member_section("field", members)
    member_section("method", members)
  >

pub fn adapt(graph) {
  if (config.options(graph).hide_empty_members)
    compartment.adapt(graph, "class-member", class_content)
  else compartment.adapt_all(graph, "class-member", class_content)
}
