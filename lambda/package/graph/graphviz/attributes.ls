// Typed lowering for the first Graphviz attribute conformance slice.

import shapes: .shapes

fn property_matches(entry, name) => lower(string(entry.name)) == name

pub fn last_entry(properties, name) {
  let matches = [for (entry in properties where property_matches(entry, name)) entry];
  if (len(matches) > 0) matches[len(matches) - 1] else null
}

fn last_named_entry(properties, names) {
  let matches = [for (entry in properties
    where contains(names, lower(string(entry.name)))) entry];
  if (len(matches) > 0) matches[len(matches) - 1] else null
}

pub fn value(properties, name, fallback = null) {
  let entry = last_entry(properties, name);
  if (entry != null) string(entry.value) else fallback
}

fn number_chars(text, i, dots, digits) {
  if (i >= len(text)) digits > 0
  else {
    let ch = slice(text, i, i + 1);
    if (contains("0123456789", ch)) number_chars(text, i + 1, dots, digits + 1)
    else if (ch == "." and dots == 0) number_chars(text, i + 1, 1, digits)
    else false
  }
}

fn number_text(raw) {
  if (raw == null) null
  else {
    let text = trim(string(raw));
    let unsigned = if (starts_with(text, "-") or starts_with(text, "+"))
      slice(text, 1, len(text)) else text;
    if (len(unsigned) > 0 and number_chars(unsigned, 0, 0, 0)) text else null
  }
}

fn number_value(properties, name, scale = 1.0) {
  let text = number_text(value(properties, name));
  if (text == null) null else float(text) * scale
}

fn number_pair_result(x, y) => {x: x, y: y}

fn number_pair(properties, name, scale = 1.0) {
  let raw = value(properties, name);
  if (raw == null) number_pair_result(null, null)
  else {
    let parts = split(string(raw), ",");
    let first = number_text(parts[0]);
    let second = if (len(parts) > 1) number_text(parts[1]) else first;
    if (first == null or second == null) number_pair_result(null, null)
    else number_pair_result(float(first) * scale, float(second) * scale)
  }
}

fn bounded_integer(properties, name, low, high, fallback = null) {
  let numeric = number_value(properties, name);
  if (numeric == null) fallback else min([high, max([low, int(numeric)])])
}

fn bool_value(properties, name) {
  let raw = lower(string(value(properties, name, "")));
  if (raw == "true" or raw == "yes" or raw == "1") true
  else if (raw == "false" or raw == "no" or raw == "0") false
  else null
}

fn attr(name, value) => if (value == null) [] else [name, value]

fn label_attrs(properties, name = "label", target = "label") {
  let entry = last_entry(properties, name);
  if (entry == null) { [] }
  else {
    let raw = string(entry.value);
    // DOT HTML IDs retain their outer grammar delimiters; the fragment parser needs the payload.
    let source = if (entry.kind == "html" and len(raw) >= 2 and
        starts_with(raw, "<") and ends_with(raw, ">")) slice(raw, 1, len(raw) - 1)
      else raw;
    [target, source, target ++ "-format", if (entry.kind == "html") "html" else "text"]
  }
}

fn interaction_attrs(properties) {
  let link = last_named_entry(properties, ["url", "href"]);
  let tooltip = last_entry(properties, "tooltip");
  let target = last_entry(properties, "target");
  let present = link != null or tooltip != null or target != null;
  [
    *attr("interaction-action", if (present) "link" else null),
    *attr("href", if (link != null) string(link.value) else null),
    *attr("tooltip", if (tooltip != null) string(tooltip.value) else null),
    *attr("target-window", if (target != null) string(target.value) else null)
  ]
}

fn style_attrs(properties) {
  let style = lower(string(value(properties, "style", "")));
  [
    *attr("style", if (style != "") style else null),
    *attr("stroke-dasharray", if (contains(style, "dashed")) "6,4"
      else if (contains(style, "dotted")) "2,3" else null),
    *attr("opacity", if (contains(style, "invis")) 0.0 else null),
    *attr("radius", if (contains(style, "rounded")) 6.0 else null)
  ]
}

fn font_attrs(properties) => [
  *attr("font-name", value(properties, "fontname")),
  *attr("font-size", number_value(properties, "fontsize", 96.0 / 72.0)),
  *attr("font-color", value(properties, "fontcolor"))
]

pub fn route_mode(raw) {
  if (raw == null) null
  else {
    let value = lower(trim(string(raw)));
    if (value == "none" or value == "") "none"
    else if (value == "line" or value == "false") "line"
    else if (value == "polyline") "polyline"
    else if (value == "ortho") "orthogonal"
    else if (value == "curved" or value == "spline" or value == "true") "curved"
    else null
  }
}

pub fn ordering(raw) {
  if (raw == null) null
  else {
    let value = lower(trim(string(raw)));
    if (value == "in" or value == "out") value else null
  }
}

fn graph_attrs(properties) {
  let rankdir = upper(string(value(properties, "rankdir", "")));
  map([
    *attr("direction", if (contains(["TB", "BT", "LR", "RL"], rankdir)) rankdir else null),
    *attr("node-sep", number_value(properties, "nodesep", 96.0)),
    *attr("rank-sep", number_value(properties, "ranksep", 96.0)),
    *attr("route-mode", route_mode(value(properties, "splines"))),
    *attr("ordering", ordering(value(properties, "ordering"))),
    *attr("new-rank", bool_value(properties, "newrank")),
    *attr("compound", bool_value(properties, "compound")),
    *attr("layout", value(properties, "layout")),
    *attr("fill", value(properties, "bgcolor")),
    *attr("gradient-angle", number_value(properties, "gradientangle")),
    *label_attrs(properties),
    *font_attrs(properties),
    *style_attrs(properties)
  ])
}

fn node_attrs(properties) {
  let raw_shape = value(properties, "shape");
  let sides = bounded_integer(properties, "sides", 3, 64,
    shapes.default_sides(raw_shape));
  let peripheries = bounded_integer(properties, "peripheries", 0, 10,
    shapes.default_peripheries(raw_shape));
  let margin = number_pair(properties, "margin", 96.0);
  map([
    *label_attrs(properties),
    *label_attrs(properties, "xlabel", "external-label"),
    *attr("shape", shapes.role(raw_shape)),
    *attr("shape-family", shapes.family(raw_shape)),
    *attr("graphviz-shape", shapes.source_name(raw_shape)),
    *attr("polygon-sides", sides),
    *attr("polygon-orientation", number_value(properties, "orientation")),
    *attr("polygon-skew", number_value(properties, "skew")),
    *attr("polygon-distortion", number_value(properties, "distortion")),
    *attr("regular", bool_value(properties, "regular")),
    *attr("peripheries", peripheries),
    *attr("width", number_value(properties, "width", 96.0)),
    *attr("height", number_value(properties, "height", 96.0)),
    *attr("fixed-size", bool_value(properties, "fixedsize")),
    *attr("margin-x", margin.x),
    *attr("margin-y", margin.y),
    *attr("fill", value(properties, "fillcolor")),
    *attr("gradient-angle", number_value(properties, "gradientangle")),
    *attr("stroke", value(properties, "color")),
    *attr("stroke-width", number_value(properties, "penwidth", 96.0 / 72.0)),
    *attr("group", value(properties, "group")),
    *attr("ordering", ordering(value(properties, "ordering"))),
    *interaction_attrs(properties),
    *font_attrs(properties),
    *style_attrs(properties)
  ])
}

fn edge_attrs(properties) {
  map([
    *label_attrs(properties),
    *label_attrs(properties, "xlabel", "external-label"),
    *label_attrs(properties, "headlabel", "head-label"),
    *label_attrs(properties, "taillabel", "tail-label"),
    *attr("arrow-head", value(properties, "arrowhead")),
    *attr("arrow-tail", value(properties, "arrowtail")),
    *attr("arrow-direction", value(properties, "dir")),
    *attr("arrow-size", number_value(properties, "arrowsize")),
    *attr("min-length", number_value(properties, "minlen")),
    *attr("weight", number_value(properties, "weight")),
    *attr("constraint", bool_value(properties, "constraint")),
    *attr("stroke", value(properties, "color")),
    *attr("stroke-width", number_value(properties, "penwidth", 96.0 / 72.0)),
    *attr("head-cluster", value(properties, "lhead")),
    *attr("tail-cluster", value(properties, "ltail")),
    *interaction_attrs(properties),
    *font_attrs(properties),
    *style_attrs(properties)
  ])
}

pub fn canonical(kind, properties) {
  if (kind == "graph" or kind == "cluster") graph_attrs(properties)
  else if (kind == "node") node_attrs(properties)
  else if (kind == "edge") edge_attrs(properties)
  else {}
}
