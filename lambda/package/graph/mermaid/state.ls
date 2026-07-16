// Mermaid state descriptions to measured canonical state content.

import compartment: .compartment

fn description_row(description) =>
  <div class: "graph-state-description",
      'data-label-format': description["label-format"],
      style: "display:block;text-align:left;white-space:normal;padding:1px 2px;";
    string(description.value)
  >

fn state_content(node, descriptions) =>
  if (len(descriptions) > 0) compartment.content(node, {
      root_class: "graph-state-content", title_class: "graph-state-title",
      body_class: "graph-state-descriptions", min_width: 120
    }, [for (description in descriptions) description_row(description)])
  else if (contains(["start", "end", "choice", "fork", "join"], node["state-kind"]))
    <div class: "graph-state-marker graph-state-" ++ string(node["state-kind"]),
        style: if (node["state-kind"] == "fork" or node["state-kind"] == "join")
          "display:block;width:42px;height:4px;" else
          "display:block;width:16px;height:16px;";
      " "
    >
  else null

pub fn adapt(graph) => compartment.adapt_all(
  graph, "state-description", state_content)
