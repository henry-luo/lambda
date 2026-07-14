// Declarative structural contract for canonical and source-compatible Graph IR.

import model: .model
import diagnostic: .diagnostics

fn attr(name, kind, required = false, values = null, code = null, message = null) => {
  name: name, kind: kind, required: required, values: values, code: code, message: message
}

fn common_attrs() => [
  attr("source-start", "int"),
  attr("source-end", "int"),
  attr("source-line", "int"),
  attr("source-column", "int")
]

fn graph_spec() => {
  attrs: [
    *common_attrs(),
    attr("id", "text"), attr("version", "text"), attr("kind", "text"),
    attr("diagram-type", "text"), attr("directed", "boolish"),
    attr("direction", "text"), attr("rank-dir", "text"),
    attr("layout", "text"), attr("status", "text"),
    attr("rank-sep", "number"), attr("node-sep", "number"),
    attr("edge-sep", "number")
  ],
  children: ["meta", "styles", "defs", "constraints", "node", "edge", "subgraph",
    "style-rule", "class-assignment", "style-assignment", "diagnostics", "diagnostic"],
  open_children: false, scalar_children: false
}

fn node_spec() => {
  attrs: [
    *common_attrs(),
    attr("id", "text", true, null, "graph.node.missing-id",
      "Graph node requires a stable id"),
    attr("label", "text"),
    attr("label-format", "text", false, ["text", "markdown", "html"]),
    attr("shape", "text"),
    attr("width", "number"), attr("height", "number"), attr("fill", "text"),
    attr("stroke", "text"), attr("stroke-width", "number"),
    attr("opacity", "number"), attr("class", "text")
  ],
  children: ["label", "content"],
  open_children: true, scalar_children: true
}

fn edge_spec() => {
  attrs: [
    *common_attrs(),
    attr("id", "text"),
    attr("from", "text", true, null, "graph.edge.missing-endpoint",
      "Graph edge requires a 'from' endpoint"),
    attr("to", "text", true, null, "graph.edge.missing-endpoint",
      "Graph edge requires a 'to' endpoint"),
    attr("label", "text"),
    attr("label-format", "text", false, ["text", "markdown", "html"]),
    attr("directed", "boolish"), attr("arrow-head", "text"),
    attr("arrow-tail", "text"), attr("min-length", "integerish"),
    attr("weight", "number"), attr("constraint", "boolish"),
    attr("stroke", "text"), attr("stroke-width", "number"),
    attr("stroke-dasharray", "text"), attr("opacity", "number")
  ],
  children: ["label", "content"],
  open_children: true, scalar_children: false
}

fn subgraph_spec() => {
  attrs: [
    *common_attrs(),
    attr("id", "text", true, null, "graph.subgraph.missing-id",
      "Graph subgraph requires a stable id"),
    attr("label", "text"),
    attr("label-format", "text", false, ["text", "markdown", "html"]),
    attr("direction", "text"), attr("rank", "text"), attr("class", "text"),
    attr("fill", "text"), attr("stroke", "text"), attr("stroke-width", "number")
  ],
  children: ["label", "content", "node", "edge", "subgraph", "style-rule",
    "class-assignment", "style-assignment", "diagnostics", "diagnostic"],
  open_children: false, scalar_children: false
}

fn label_spec() => {
  attrs: [*common_attrs(), attr("format", "text", false, ["text", "markdown", "html"])],
  children: [],
  open_children: true, scalar_children: true
}

fn metadata_spec(value_tag) {
  if (value_tag == "style-rule") {
    {attrs: [*common_attrs(), attr("class", "text", true),
      attr("declarations", "text", true)], children: [], open_children: false,
      scalar_children: false}
  }
  else if (value_tag == "class-assignment") {
    {attrs: [*common_attrs(), attr("targets", "text", true),
      attr("class", "text", true)], children: [], open_children: false,
      scalar_children: false}
  }
  else if (value_tag == "style-assignment") {
    {attrs: [*common_attrs(), attr("target-kind", "text", true, ["node", "edge"]),
      attr("targets", "text", true), attr("declarations", "text", true)],
      children: [], open_children: false, scalar_children: false}
  }
  else if (value_tag == "diagnostic") {
    {attrs: [*common_attrs(), attr("code", "text", true),
      attr("severity", "text", true, ["error", "warning", "note"]),
      attr("message", "text", true), attr("context", "text"), attr("hint", "text")],
      children: [], open_children: false, scalar_children: false}
  }
  else { null }
}

fn spec_for(value_tag) {
  if (value_tag == "graph") graph_spec()
  else if (value_tag == "node") node_spec()
  else if (value_tag == "edge") edge_spec()
  else if (value_tag == "subgraph") subgraph_spec()
  else if (value_tag == "label") label_spec()
  else if (value_tag == "content" or value_tag == "meta" or value_tag == "defs" or
      value_tag == "constraints") {
    {attrs: common_attrs(), children: [], open_children: true, scalar_children: true}
  }
  else if (value_tag == "styles") {
    {attrs: common_attrs(), children: ["style-rule", "class-assignment", "style-assignment"],
      open_children: false, scalar_children: false}
  }
  else if (value_tag == "diagnostics") {
    {attrs: common_attrs(), children: ["diagnostic"], open_children: false,
      scalar_children: false}
  }
  else metadata_spec(value_tag)
}

fn type_ok(value, kind) {
  if (kind == null) true
  else if (kind == "text") value is string or value is symbol
  else if (kind == "int") value is int
  else if (kind == "number") value is number
  else if (kind == "boolish") value is bool or
    ((value is string or value is symbol) and contains(["true", "false"], string(value)))
  else if (kind == "integerish") value is int or value is string or value is symbol
  else true
}

fn spec_attr(spec, name) {
  let matches = [for (entry in spec.attrs where entry.name == name) entry];
  if (len(matches) > 0) matches[0] else null
}

fn attr_path(path, name) => path ++ "." ++ name

fn required_attr_missing(value, entry) =>
  value == null or (entry.kind == "text" and string(value) == "")

fn required_attr_diagnostics(value, path, spec) => [
  for (entry in spec.attrs
    // Stable identities and endpoints cannot use an empty string as a present value.
    where entry.required == true and required_attr_missing(value[entry.name], entry))
    diagnostic.for_value(
      if (entry.code != null) entry.code else "graph.schema.required-attribute",
      "error",
      if (entry.message != null) entry.message
        else "Graph <" ++ model.tag(value) ++ "> requires attribute '" ++ entry.name ++ "'",
      attr_path(path, entry.name), value)
]

fn present_attr_diagnostics(value, path, spec) => [
  for (key, attr_value in map(value), let name = string(key),
    let entry = spec_attr(spec, name)
    where entry != null and (not type_ok(attr_value, entry.kind) or
      (entry.values != null and not contains(entry.values, string(attr_value)))))
    diagnostic.for_value(
      if (not type_ok(attr_value, entry.kind)) "graph.schema.attribute-type"
        else "graph.schema.attribute-value",
      "error",
      if (not type_ok(attr_value, entry.kind))
        "Attribute '" ++ name ++ "' on <" ++ model.tag(value) ++ "> must be " ++ entry.kind
      else "Attribute '" ++ name ++ "' on <" ++ model.tag(value) ++
        "> has unsupported value '" ++ string(attr_value) ++ "'",
      attr_path(path, name), value)
]

fn child_allowed(spec, child_tag) =>
  contains(spec.children, child_tag) or
    (spec.open_children == true and spec_for(child_tag) == null)

fn label_content_diagnostics(value, path) {
  let children = model.child_items(value);
  let labels = len([for (child in children
    where child is element and model.tag(child) == "label") child]);
  let contents = len([for (child in children
    where child is element and model.tag(child) == "content") child]);
  let duplicate = [
    for (entry in [{tag: "label", count: labels}, {tag: "content", count: contents}]
      where entry.count > 1) diagnostic.for_value(
      "graph.schema.child-cardinality", "error",
      "Graph <" ++ model.tag(value) ++ "> permits at most one <" ++ entry.tag ++ "> child",
      path ++ "." ++ entry.tag, value)
  ];
  let unpaired = if ((labels == 1 and contents == 0) or (labels == 0 and contents == 1)) [
    diagnostic.for_value(
      "graph.schema.label-content-pair", "error",
      "Canonical <label> and <content> children must occur as a pair",
      path, value)
  ] else [];
  [*duplicate, *unpaired]
}

fn label_source_diagnostics(value, path) {
  let items = model.child_items(value);
  if (len(items) == 1 and (items[0] is string or items[0] is symbol)) { [] }
  else [diagnostic.for_value(
    "graph.schema.label-source", "error",
    "Graph <label> requires exactly one scalar source value", path, value)]
}

fn child_diagnostics(value, path, spec) => [
  for (i, child in model.child_items(value)
    where (not (child is element) and spec.scalar_children != true) or
      (child is element and not child_allowed(spec, model.tag(child))))
    diagnostic.for_value(
      "graph.schema.child", "error",
      if (child is element)
        "Element <" ++ model.tag(child) ++ "> is not permitted under <" ++ model.tag(value) ++ ">"
      else "Scalar content is not permitted directly under <" ++ model.tag(value) ++ ">",
      path ++ "[" ++ string(i) ++ "]", value)
]

fn nested_diagnostics(value, path, spec) {
  let value_tag = model.tag(value);
  if (value_tag == "content" or value_tag == "label" or value_tag == "meta" or
      value_tag == "defs" or value_tag == "constraints") { [] }
  else [
    for (i, child in model.child_items(value),
      // Open node children are authored measured content, not nested Graph IR structure.
      nested in if (child is element and child_allowed(spec, model.tag(child)) and
          spec_for(model.tag(child)) != null)
        validate_element(child,
          path ++ "." ++ model.tag(child) ++ "[" ++ string(i) ++ "]")
        else []) nested
  ]
}

fn validate_element(value, path) {
  let value_tag = model.tag(value);
  let spec = spec_for(value_tag);
  if (spec == null) { [diagnostic.for_value(
    "graph.schema.element", "error",
    "Unknown Graph IR element <" ++ value_tag ++ ">", path, value)] }
  else {
    let pair_values = if (value_tag == "node" or value_tag == "edge" or value_tag == "subgraph")
      label_content_diagnostics(value, path) else [];
    let label_values = if (value_tag == "label") label_source_diagnostics(value, path) else [];
    [
      *required_attr_diagnostics(value, path, spec),
      *present_attr_diagnostics(value, path, spec),
      *child_diagnostics(value, path, spec),
      *pair_values,
      *label_values,
      *nested_diagnostics(value, path, spec)
    ]
  }
}

pub fn validate(graph) => validate_element(graph, "graph")
