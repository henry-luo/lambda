// Pure graph data to semantic HTML element transformation.

import theme_defaults: .theme

fn opt(opts, key, fallback) {
  if (opts != null and opts[key] != null) opts[key] else fallback
}

fn source_attr(source, key, fallback) {
  if (source != null and source[key] != null) source[key] else fallback
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

fn node_class(node) {
  if (node.class != null and node.class != "") "graph-node " ++ string(node.class)
  else "graph-node"
}

fn node_style(node, palette) {
  let source = if (node.style != null) string(node.style) else "";
  let shape = string(source_attr(node, "shape", "box"));
  let radius = if (shape == "round" or shape == "rounded" or shape == "stadium") "999px" else "4px";
  "display:inline-block;box-sizing:border-box;padding:10px 14px;" ++
    "border:1px solid " ++ palette.node_border ++ ";border-radius:" ++ radius ++ ";" ++
    "background:" ++ palette.node_background ++ ";color:" ++ palette.node_text ++
    ";white-space:nowrap;" ++ source
}

fn node_content(node, id) {
  if (node is element and len(node) > 0) {
    [for (i in 0 to (len(node) - 1)) node[i]]
  }
  else if (node.content is array) { [for (child in node.content) child] }
  else if (node.content is list) { [for (child in node.content) child] }
  else if (node.content != null) { [node.content] }
  else if (node.label != null) { [node.label] }
  else { [id] }
}

fn html_node(node, index, palette) {
  let id = node_id(node, index);
  let content = node_content(node, id);
  <node class: node_class(node), 'data-node-id': id,
      'data-shape': source_attr(node, "shape", "box"),
      'data-z': string(source_attr(node, "z", 0)),
      style: node_style(node, palette);
    for (child in content) child
  >
}

fn bool_text(value, fallback) {
  let actual = if (value == null) fallback else value;
  if (actual == null or actual == false or actual == "false" or
      actual == "none" or actual == "no") "false" else "true"
}

fn html_edge(edge, index, graph_directed) {
  let directed = bool_text(source_attr(edge, "directed", null), graph_directed);
  <edge 'data-edge-id': string(source_attr(edge, "id", "e" ++ string(index))),
      'data-from': string(source_attr(edge, "from", source_attr(edge, "from_id", ""))),
      'data-to': string(source_attr(edge, "to", source_attr(edge, "to_id", ""))),
      'data-directed': directed,
      'data-arrow-start': bool_text(source_attr(edge, "arrow-start", source_attr(edge, "arrow_start", false)), false),
      'data-arrow-end': bool_text(source_attr(edge, "arrow-end", source_attr(edge, "arrow_end", null)), directed == "true"),
      'data-style': string(source_attr(edge, "style", "solid")),
      'data-z': string(source_attr(edge, "z", -1)),
      style: "display:block;width:0;height:0;overflow:hidden;visibility:hidden;pointer-events:none;">
}

fn graph_directed(graph) {
  if (source_attr(graph, "directed", null) != null) source_attr(graph, "directed", true)
  else source_attr(graph, "type", "directed") != "undirected"
}

pub fn to_html(graph, opts = null) {
  let nodes = source_children(graph, "node");
  let edges = source_children(graph, "edge");
  let direction = string(opt(opts, "direction",
    source_attr(graph, "direction", source_attr(graph, "rank-dir", "TB"))));
  let node_sep = string(opt(opts, "node_sep", source_attr(graph, "node-sep", 60)));
  let rank_sep = string(opt(opts, "rank_sep", source_attr(graph, "rank-sep", 80)));
  let directed = graph_directed(graph);
  let theme = string(opt(opts, "theme", "light"));
  let palette = theme_defaults.palette(theme);
  let children = [
    for (i, node in nodes) html_node(node, i, palette),
    for (i, edge in edges) html_edge(edge, i, directed)
  ];
  <'graph' class: "lambda-graph lambda-graph-theme-" ++ theme,
      'data-radiant-layout': "lambda-graph", 'data-theme': theme,
      'data-edge-color': palette.edge,
      'data-direction': direction, 'data-node-sep': node_sep,
      'data-rank-sep': rank_sep,
      style: "position:relative;background:" ++ palette.graph_background ++ ";";
    for child in children { child }>
}
