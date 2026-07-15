// Typed lowering for the first Graphviz attribute conformance slice.

import shapes: .shapes

fn property_matches(entry, name) => lower(string(entry.name)) == name

pub fn last_entry(properties, name) {
  let matches = [for (entry in properties where property_matches(entry, name)) entry];
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

fn bool_value(properties, name) {
  let raw = lower(string(value(properties, name, "")));
  if (raw == "true" or raw == "yes" or raw == "1") true
  else if (raw == "false" or raw == "no" or raw == "0") false
  else null
}

fn attr(name, value) => if (value == null) [] else [name, value]

fn label_attrs(properties) {
  let entry = last_entry(properties, "label");
  if (entry == null) { [] }
  else { ["label", string(entry.value), "label-format",
    if (entry.kind == "html") "html" else "text"] }
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

fn graph_attrs(properties) {
  let rankdir = upper(string(value(properties, "rankdir", "")));
  map([
    *attr("direction", if (contains(["TB", "BT", "LR", "RL"], rankdir)) rankdir else null),
    *attr("node-sep", number_value(properties, "nodesep", 96.0)),
    *attr("rank-sep", number_value(properties, "ranksep", 96.0)),
    *attr("layout", value(properties, "layout")),
    *attr("fill", value(properties, "bgcolor")),
    *label_attrs(properties)
  ])
}

fn node_attrs(properties) {
  let raw_shape = value(properties, "shape");
  map([
    *label_attrs(properties),
    *attr("shape", shapes.role(raw_shape)),
    *attr("shape-family", shapes.family(raw_shape)),
    *attr("graphviz-shape", shapes.source_name(raw_shape)),
    *attr("width", number_value(properties, "width", 96.0)),
    *attr("height", number_value(properties, "height", 96.0)),
    *attr("fixed-size", bool_value(properties, "fixedsize")),
    *attr("fill", value(properties, "fillcolor")),
    *attr("stroke", value(properties, "color")),
    *attr("stroke-width", number_value(properties, "penwidth", 96.0 / 72.0)),
    *attr("group", value(properties, "group")),
    *style_attrs(properties)
  ])
}

fn edge_attrs(properties) {
  map([
    *label_attrs(properties),
    *attr("arrow-head", value(properties, "arrowhead")),
    *attr("arrow-tail", value(properties, "arrowtail")),
    *attr("arrow-direction", value(properties, "dir")),
    *attr("min-length", number_value(properties, "minlen")),
    *attr("weight", number_value(properties, "weight")),
    *attr("constraint", bool_value(properties, "constraint")),
    *attr("stroke", value(properties, "color")),
    *attr("stroke-width", number_value(properties, "penwidth", 96.0 / 72.0)),
    *attr("head-cluster", value(properties, "lhead")),
    *attr("tail-cluster", value(properties, "ltail")),
    *style_attrs(properties)
  ])
}

pub fn canonical(kind, properties) {
  if (kind == "graph" or kind == "cluster") graph_attrs(properties)
  else if (kind == "node") node_attrs(properties)
  else if (kind == "edge") edge_attrs(properties)
  else {}
}
