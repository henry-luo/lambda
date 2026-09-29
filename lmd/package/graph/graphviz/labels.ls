// Graphviz plain-label escape substitution after object identity is resolved.

let ESCAPED_SLASH = "__LAMBDA_GRAPHVIZ_ESCAPED_SLASH__"

fn line_breaks(text) => replace(replace(replace(text,
  "\\n", "\n"), "\\l", "\n"), "\\r", "\n")

fn common(raw, graph_id) {
  if (raw == null) null
  else {
    // Protect authored double slashes so they cannot become Graphviz escapes.
    let text = replace(string(raw), "\\\\", ESCAPED_SLASH);
    line_breaks(replace(replace(text, "\\G", graph_id), "\\L", "L"))
  }
}

fn finish(text) => if (text == null) null else replace(text, ESCAPED_SLASH, "\\")

pub fn graph(raw, graph_id) => finish(common(raw, graph_id))

pub fn node(raw, node_id, graph_id) {
  let source = if (raw == null) node_id else string(raw);
  let resolved = common(source, graph_id);
  finish(replace(resolved, "\\N", node_id))
}

pub fn edge(raw, from, to, directed, graph_id) {
  if (raw == null) null
  else {
    let operator = if (directed) "->" else "--";
    let identity = from ++ operator ++ to;
    let resolved = common(raw, graph_id);
    finish(replace(replace(replace(resolved, "\\E", identity), "\\T", from), "\\H", to))
  }
}
