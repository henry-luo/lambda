// Structured Mermaid front-matter and initialization option lowering.

import model: lambda.package.graph.model

fn metadata_values(graph, wanted_tag) => [
  for (child in model.element_children(graph)
    where model.tag(child) == wanted_tag and child.value != null) string(child.value)
]

fn last_or(values, fallback = null) =>
  if (len(values) > 0) values[len(values) - 1] else fallback

fn parsed(raw) {
  if (raw == null or trim(raw) == "") null
  else {
    let value^err = parse(string(raw), {type: "yaml"});
    if (value is error) null else value
  }
}

fn flowchart_config(value, wrapped_key) {
  if (value == null) {}
  else {
    let wrapped = if (wrapped_key != null and value[wrapped_key] != null)
      value[wrapped_key] else value;
    if (wrapped.flowchart != null) wrapped.flowchart else {}
  }
}

fn bool_option(value, fallback = false) {
  if (value == null) fallback
  else not (value == false or value == "false" or value == "off" or value == "none")
}

pub fn options(graph) {
  let front = parsed(last_or(metadata_values(graph, "front-matter")));
  let init = parsed(last_or(metadata_values(graph, "init")));
  let front_flowchart = flowchart_config(front, "config");
  let init_flowchart = flowchart_config(init, "init");
  let combined = {*:front_flowchart, *:init_flowchart};
  let curve = if (combined.curve != null) string(combined.curve) else null;
  {
    title: if (front != null and front.title != null) string(front.title) else null,
    node_sep: if (combined.nodeSpacing != null) float(combined.nodeSpacing) else null,
    rank_sep: if (combined.rankSpacing != null) float(combined.rankSpacing) else null,
    curve: curve,
    use_splines: curve != null and curve != "linear" and curve != "step",
    html_labels: bool_option(combined.htmlLabels, true)
  }
}
