// Pure graph data to semantic HTML element transformation.

import theme_defaults: .theme
import graph_content: .content
import model: lambda.package.graph.model
import graph_style: lambda.package.graph.style
import mermaid_config: lambda.package.graph.mermaid.config

fn opt(opts, key, fallback) {
  if (opts != null and opts[key] != null) opts[key] else fallback
}

fn source_attr(source, key, fallback) {
  if (source != null and source[key] != null) source[key] else fallback
}

fn label_source(value, fallback = null) {
  if (value is element) model.label_source(value, fallback)
  else source_attr(value, "label", fallback)
}

fn label_format(value) {
  if (value is element) model.label_format(value)
  else string(source_attr(value, "label-format", "text"))
}

fn canonical_content(value) {
  if (value is element and model.content_element(value) != null) model.content_items(value)
  else null
}

fn source_children(graph, tag) {
  if (graph is element) {
    if (len(graph) == 0) { [] }
    else {
      [for (i in 0 to (len(graph) - 1), let child = graph[i]
        where child is element and string(name(child)) == tag) child]
    }
  }
  else if (tag == "node" and graph.nodes != null) graph.nodes
  else if (tag == "edge" and graph.edges != null) graph.edges
  else []
}

fn node_id(node, index) {
  if (node.id != null and node.id != "") string(node.id)
  else "n" ++ string(index)
}

fn node_class(node, assigned_classes) {
  let source = if (node.class != null and node.class != "") [string(node.class)] else [];
  let classes = [*source, *assigned_classes];
  if (len(classes) > 0) "graph-node " ++ join(classes, " ") else "graph-node"
}

fn edge_class(assigned_classes) {
  if (len(assigned_classes) > 0) "graph-edge " ++ join(assigned_classes, " ")
  else "graph-edge"
}

fn shape_css(shape, palette) {
  if (shape == "circle" or shape == "ellipse" or shape == "f-circ") "border-radius:50%;"
  else if (shape == "doublecircle")
    "border-radius:50%;box-shadow:inset 0 0 0 3px " ++ palette.node_background ++
      ",inset 0 0 0 4px " ++ palette.node_border ++ ";"
  else if (shape == "stadium" or shape == "round" or shape == "rounded")
    "border-radius:999px;"
  else if (shape == "cylinder") "border-radius:50% / 12px;"
  else if (shape == "diamond") "clip-path:polygon(50% 0,100% 50%,50% 100%,0 50%);"
  else if (shape == "hexagon")
    "clip-path:polygon(25% 0,75% 0,100% 50%,75% 100%,25% 100%,0 50%);"
  else if (shape == "octagon")
    "clip-path:polygon(25% 0,75% 0,100% 25%,100% 75%,75% 100%,25% 100%,0 75%,0 25%);"
  else if (shape == "house")
    "clip-path:polygon(50% 0,100% 50%,100% 100%,0 100%,0 50%);"
  else if (shape == "invhouse")
    "clip-path:polygon(0 0,100% 0,100% 50%,50% 100%,0 50%);"
  else if (shape == "trapezoid")
    "clip-path:polygon(20% 0,80% 0,100% 100%,0 100%);"
  else if (shape == "trapezoid-alt")
    "clip-path:polygon(0 0,100% 0,80% 100%,20% 100%);"
  else if (shape == "asymmetric")
    "clip-path:polygon(0 0,85% 0,100% 50%,85% 100%,0 100%,15% 50%);"
  else if (shape == "lean-r")
    "clip-path:polygon(15% 0,100% 0,85% 100%,0 100%);"
  else if (shape == "lean-l" or shape == "sl-rect")
    "clip-path:polygon(0 0,85% 0,100% 100%,15% 100%);"
  else if (shape == "tri")
    "clip-path:polygon(50% 0,100% 100%,0 100%);"
  else if (shape == "flip-tri")
    "clip-path:polygon(0 0,100% 0,50% 100%);"
  else if (shape == "hourglass")
    "clip-path:polygon(0 0,100% 0,65% 50%,100% 100%,0 100%,35% 50%);"
  else if (shape == "bolt")
    "clip-path:polygon(42% 0,100% 0,62% 42%,88% 42%,30% 100%,43% 58%,12% 58%);"
  else if (shape == "notch-rect" or shape == "card")
    "clip-path:polygon(12% 0,100% 0,100% 100%,0 100%,0 12%);"
  else if (shape == "notch-pent")
    "clip-path:polygon(12% 0,100% 0,100% 80%,50% 100%,0 80%,0 12%);"
  else if (shape == "tag-rect" or shape == "tag-doc")
    "clip-path:polygon(0 0,85% 0,100% 50%,85% 100%,0 100%);"
  else if (shape == "bang")
    "clip-path:polygon(18% 0,82% 0,100% 18%,86% 50%,100% 82%,82% 100%,18% 100%,0 82%,14% 50%,0 18%);"
  else if (shape == "cloud") "border-radius:45% 55% 48% 52% / 55% 45% 55% 45%;"
  else if (shape == "delay") "border-radius:4px 50% 50% 4px;"
  else if (shape == "h-cyl") "border-radius:18px / 50%;"
  else if (shape == "curv-trap") "border-radius:45% 45% 8px 8px / 20% 20% 8px 8px;"
  else if (shape == "doc" or shape == "lin-doc") "border-radius:4px 4px 35% 12%;"
  else if (shape == "brace") "border-width:0 0 0 3px;border-radius:50% 0 0 50%;"
  else if (shape == "brace-r") "border-width:0 3px 0 0;border-radius:0 50% 50% 0;"
  else if (shape == "braces") "border-width:0 3px;border-radius:45%;"
  else if (shape == "fork") "height:8px;padding:0;border-width:4px 0 0;background:transparent;"
  else if (shape == "win-pane" or shape == "div-rect")
    "box-shadow:inset 0 1px 0 " ++ palette.node_border ++
      ",inset 1px 0 0 " ++ palette.node_border ++ ";"
  else if (shape == "lin-rect") "border-width:3px 1px;"
  else if (shape == "lin-cyl") "border-radius:50% / 12px;border-width:3px 1px;"
  else if (shape == "text") "border-color:transparent;background:transparent;"
  else if (shape == "subroutine")
    "box-shadow:inset 4px 0 0 -3px " ++ palette.node_border ++
      ",inset -4px 0 0 -3px " ++ palette.node_border ++ ";"
  else "border-radius:4px;"
}

fn resolved_node_style(node, style_declarations) {
  let source = if (node.style != null) string(node.style) else "";
  graph_style.parse(source ++ ";" ++ style_declarations)
}

fn node_style(node, parsed, palette) {
  let shape = string(source_attr(node, "shape", "box"));
  "display:inline-block;box-sizing:border-box;padding:10px 14px;" ++
    "border:1px solid " ++ palette.node_border ++ ";" ++
    "background:" ++ palette.node_background ++ ";" ++ shape_css(shape, palette) ++
    "color:" ++ palette.node_text ++
    ";white-space:" ++
    (if (graph_content.is_rich(label_format(node))) "normal" else "nowrap") ++
    ";" ++ graph_style.node_css(parsed)
}

fn html_port(entry, index) {
  let port = entry.value;
  <port 'data-node-id': entry.node,
      'data-subgraph-id': entry.group,
      'data-port-id': string(source_attr(port, "id", "p" ++ string(index))),
      'data-port-side': string(source_attr(port, "side", "auto")),
      'data-port-offset': string(source_attr(port, "offset", 0.5)),
      'data-z': string(source_attr(port, "z", 0)),
      style: "display:block;width:0;height:0;overflow:hidden;visibility:hidden;pointer-events:none;">
}

fn node_content(node, id) {
  let content = canonical_content(node);
  if (content != null) content
  else if (node.content is array) { [for (child in node.content) child] }
  else if (node.content is list) { [for (child in node.content) child] }
  else if (node.content != null) { [node.content] }
  else if (label_source(node) != null) {
    graph_content.lower(label_source(node), label_format(node))
  }
  else { [id] }
}

fn html_node(node, index, group, assigned_classes, style_declarations, interaction, palette) {
  let id = node_id(node, index);
  let content = node_content(node, id);
  let parsed_style = resolved_node_style(node, style_declarations);
  <node class: node_class(node, assigned_classes),
      'data-graph-role': "node", 'data-node-id': id,
      'data-subgraph-id': group,
      'data-shape': source_attr(node, "shape", "box"),
      'data-shape-family': source_attr(node, "shape-family", null),
      'data-graphviz-shape': source_attr(node, "graphviz-shape", null),
      'data-label': label_source(node, id),
      'data-label-format': label_format(node),
      'data-style-declarations': style_declarations,
      'data-fill': if (parsed_style.fill != null) parsed_style.fill else palette.node_background,
      'data-stroke': if (parsed_style.stroke != null) parsed_style.stroke else palette.node_border,
      'data-stroke-width': if (parsed_style.stroke_width != null) parsed_style.stroke_width else 1,
      'data-color': if (parsed_style.color != null) parsed_style.color else palette.node_text,
      'data-opacity': parsed_style.opacity,
      'data-dash-array': parsed_style.dash_array,
      'data-interaction-action': source_attr(interaction, "action", null),
      'data-href': source_attr(interaction, "href", null),
      'data-callback': source_attr(interaction, "callback", null),
      'data-tooltip': source_attr(interaction, "tooltip", null),
      'data-link-target': source_attr(interaction, "target-window", null),
      'data-z': string(source_attr(node, "z", 0)),
      style: node_style(node, parsed_style, palette);
    for (child in content) child
  >
}

fn bool_text(value, fallback) {
  let actual = if (value == null) fallback else value;
  if (actual == null or actual == false or actual == "false" or
      actual == "none" or actual == "no") "false" else "true"
}

fn marker_text(value, enabled) {
  if (value == null or value == "") {
    if (bool_text(enabled, false) == "true") "normal" else "none"
  }
  else if (value == true or value == "true") "normal"
  else if (value == false or value == "false") "none"
  else string(value)
}

fn html_edge(edge, index, group, graph_directed, assigned_classes,
    style_declarations, interaction, graph) {
  let id = string(source_attr(edge, "id", "e" ++ string(index)));
  let source_id = source_attr(edge, "source-id", null);
  let directed = bool_text(source_attr(edge, "directed", null), graph_directed);
  let marker_start = marker_text(source_attr(edge, "arrow-tail", source_attr(edge, "marker-start", null)),
    source_attr(edge, "arrow-start", source_attr(edge, "arrow_start", false)));
  let explicit_end = source_attr(edge, "arrow-end", source_attr(edge, "arrow_end", null));
  let marker_end = marker_text(source_attr(edge, "arrow-head", source_attr(edge, "marker-end", null)),
    if (explicit_end != null) explicit_end else directed == "true");
  <edge class: edge_class(assigned_classes), 'data-edge-id': id,
      'data-from': string(source_attr(edge, "from", source_attr(edge, "from_id", ""))),
      'data-to': string(source_attr(edge, "to", source_attr(edge, "to_id", ""))),
      'data-from-port': source_attr(edge, "from-port", source_attr(edge, "from_port", null)),
      'data-to-port': source_attr(edge, "to-port", source_attr(edge, "to_port", null)),
      'data-subgraph-id': group,
      'data-label': label_source(edge),
      'data-label-format': label_format(edge),
      'data-directed': directed,
      'data-arrow-start': bool_text(marker_start != "none", false),
      'data-arrow-end': bool_text(marker_end != "none", false),
      'data-marker-start': marker_start, 'data-marker-end': marker_end,
      'data-style': string(source_attr(edge, "style", "solid")),
      'data-style-declarations': style_declarations,
      'data-interaction-action': source_attr(interaction, "action", null),
      'data-href': source_attr(interaction, "href", null),
      'data-callback': source_attr(interaction, "callback", null),
      'data-tooltip': source_attr(interaction, "tooltip", null),
      'data-link-target': source_attr(interaction, "target-window", null),
      'data-animate': string(model.edge_property(graph, id, source_id, "animate", "false")),
      'data-animation': model.edge_property(graph, id, source_id, "animation", null),
      'data-curve': model.edge_property(graph, id, source_id, "curve", null),
      'data-min-length': string(source_attr(edge, "min-length", source_attr(edge, "min_length", 1))),
      'data-weight': string(source_attr(edge, "weight", 1)),
      'data-constraint': bool_text(source_attr(edge, "constraint", true), true),
      'data-z': string(source_attr(edge, "z", -1)),
      style: "display:block;width:0;height:0;overflow:hidden;visibility:hidden;pointer-events:none;">
}

fn html_cluster(entry, index, palette) {
  let group = entry.value;
  <cluster 'data-cluster-id': entry.group,
      'data-parent-cluster-id': entry.parent,
      'data-cluster-padding': string(source_attr(group, "padding", 16)),
      'data-cluster-label-gap': string(source_attr(group, "label-gap", 8)),
      'data-cluster-fill': string(source_attr(group, "fill", "none")),
      'data-cluster-stroke': string(source_attr(group, "stroke", palette.node_border)),
      'data-cluster-stroke-width': string(source_attr(group, "stroke-width", 1)),
      'data-cluster-radius': string(source_attr(group, "radius", 6)),
      'data-z': string(source_attr(group, "z", -2)),
      style: "display:block;width:0;height:0;overflow:hidden;visibility:hidden;pointer-events:none;">
}

fn html_cluster_label(entry, index, palette) {
  let group = entry.value;
  let label = label_source(group, entry.group);
  let format = label_format(group);
  let content = canonical_content(group);
  if (label == null or label == "") null
  else {
    <'cluster-label' class: "graph-cluster-label", 'data-graph-role': "cluster-label",
        'data-cluster-id': entry.group,
        'data-parent-cluster-id': entry.parent,
        'data-label': label,
        'data-label-format': format,
        'data-z': string(source_attr(group, "label-z", 0)),
        style: "display:inline-block;box-sizing:border-box;padding:2px 5px;" ++
          "background:" ++ palette.graph_background ++ ";color:" ++ palette.node_text ++
          ";white-space:" ++ (if (graph_content.is_rich(format)) "normal" else "nowrap") ++
          ";pointer-events:none;";
      for (child in if (content != null) content else graph_content.lower(label, format)) child
    >
  }
}

fn html_edge_label(edge, index, group, palette) {
  let label = label_source(edge);
  let format = label_format(edge);
  let content = canonical_content(edge);
  if (label == null or label == "") null
  else {
    <'edge-label' class: "graph-edge-label", 'data-graph-role': "edge-label",
        'data-edge-id': string(source_attr(edge, "id", "e" ++ string(index))),
        'data-subgraph-id': group, 'data-label': label, 'data-label-format': format,
        'data-z': string(source_attr(edge, "label-z", 0)),
        style: "display:inline-block;box-sizing:border-box;padding:2px 5px;" ++
          "background:" ++ palette.graph_background ++ ";color:" ++ palette.node_text ++
          ";white-space:" ++ (if (graph_content.is_rich(format)) "normal" else "nowrap") ++
          ";pointer-events:none;";
      for (child in if (content != null) content else graph_content.lower(label, format)) child
    >
  }
}

fn html_annotation(annotation, owner_kind, owner_id, index, palette) {
  let label = label_source(annotation);
  let format = label_format(annotation);
  if (label == null or label == "") null
  else {
    <annotation class: "graph-annotation", 'data-graph-role': "annotation",
        'data-owner-kind': owner_kind, 'data-owner-id': owner_id,
        'data-annotation-kind': string(source_attr(annotation, "kind", "external")),
        'data-label': label, 'data-label-format': format,
        'data-z': string(source_attr(annotation, "z", 0)),
        style: "display:inline-block;box-sizing:border-box;padding:2px 5px;" ++
          "background:" ++ palette.graph_background ++ ";color:" ++ palette.node_text ++
          ";white-space:" ++ (if (graph_content.is_rich(format)) "normal" else "nowrap") ++
          ";pointer-events:none;";
      for (child in graph_content.lower(label, format)) child
    >
  }
}

fn html_constraints(graph) => [
  for (i, constraint in model.constraints(graph), member in model.element_children(constraint)
    where model.tag(member) == "member")
    <constraint 'data-constraint-kind': string(constraint.kind),
      'data-constraint-value': string(constraint.value),
      'data-constraint-scope': string(if (constraint.scope != null) constraint.scope else "c" ++ string(i)),
      'data-constraint-member': string(member.node),
      style: "display:block;width:0;height:0;overflow:hidden;visibility:hidden;pointer-events:none;">
]

fn graph_directed(graph) {
  if (source_attr(graph, "directed", null) != null) source_attr(graph, "directed", true)
  else source_attr(graph, "type", "directed") != "undirected"
}

pub fn to_html(graph, opts = null) {
  let node_entries = if (graph is element) model.node_entries(graph)
    else [for (node in source_children(graph, "node")) {value: node, group: null}];
  let edge_entries = if (graph is element) model.edge_entries(graph)
    else [for (edge in source_children(graph, "edge")) {value: edge, group: null}];
  let subgraph_entries = if (graph is element) model.visual_subgraph_entries(graph) else [];
  let port_entries = if (graph is element) model.port_entries(graph) else [];
  let direction = string(opt(opts, "direction",
    if (graph is element) model.direction(graph)
    else source_attr(graph, "direction", source_attr(graph, "rank-dir", "TB"))));
  let config = if (graph is element and graph.flavor == "mermaid")
    mermaid_config.options(graph) else {};
  let node_sep = string(opt(opts, "node_sep", source_attr(graph, "node-sep",
    if (config.node_sep != null) config.node_sep else 60)));
  let rank_sep = string(opt(opts, "rank_sep", source_attr(graph, "rank-sep",
    if (config.rank_sep != null) config.rank_sep else 80)));
  let edge_sep = string(opt(opts, "edge_sep", source_attr(graph, "edge-sep", 10)));
  let directed = graph_directed(graph);
  let theme = string(opt(opts, "theme", "light"));
  let palette = theme_defaults.palette(theme);
  let title = if (graph is element and model.title(graph) != null) model.title(graph)
    else if (config.title != null) config.title else source_attr(graph, "title", null);
  let description = if (graph is element) model.description(graph)
    else source_attr(graph, "description", null);
  let children = [
    *(if (graph is element) html_constraints(graph) else []),
    for (i, annotation in if (graph is element) model.annotations(graph) else [],
      let value = html_annotation(annotation,
        string(source_attr(annotation, "owner-kind", "edge")),
        string(source_attr(annotation, "owner-id", "")), i, palette)
      where value != null) value,
    for (i, entry in subgraph_entries) html_cluster(entry, i, palette),
    for (i, entry in subgraph_entries,
      let label = html_cluster_label(entry, i, palette)
      where label != null) label,
    for (i, entry in node_entries) html_node(entry.value, i, entry.group,
      if (graph is element) model.classes_for(graph, node_id(entry.value, i)) else [],
      if (graph is element) model.node_style_declarations_for(
        graph, node_id(entry.value, i)) else "",
      if (graph is element) model.interaction_for(graph, node_id(entry.value, i)) else null,
      palette),
    for (i, entry in port_entries) html_port(entry, i),
    for (i, entry in edge_entries,
      let label = html_edge_label(entry.value, i, entry.group, palette)
      where label != null) label,
    for (i, entry in edge_entries,
      let edge_id = string(source_attr(entry.value, "id", "e" ++ string(i))),
      let source_id = source_attr(entry.value, "source-id", null))
      html_edge(entry.value, i, entry.group, directed,
        if (graph is element) model.edge_classes_for(graph, edge_id, source_id) else [],
        if (graph is element) model.edge_style_declarations_for(
          graph, edge_id, source_id, i) else "",
        if (graph is element) model.interaction_for(graph, edge_id) else null,
        graph)
  ];
  <'graph' class: "lambda-graph lambda-graph-theme-" ++ theme,
      'data-graph-role': "graph", 'data-radiant-layout': "lambda-graph", 'data-theme': theme,
      role: "group", 'aria-label': title, 'aria-description': description,
      'data-graph-title': title,
      'data-edge-color': palette.edge,
      'data-direction': direction, 'data-node-sep': node_sep,
      'data-rank-sep': rank_sep, 'data-edge-sep': edge_sep,
      'data-curve': config.curve,
      'data-use-splines': if (config.use_splines == true) "true" else "false",
      style: "position:relative;background:" ++ palette.graph_background ++ ";";
    for child in children { child }>
}
