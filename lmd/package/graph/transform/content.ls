// Safe rich-label lowering for measured graph content.

import model: lambda.graph.model
import graph_style: lambda.graph.style

fn sanitized_children(value, dot = false, cell_border = "1", cell_padding = "2") =>
  [for (item in ((content(value)) ^ { [] }), child in ((sanitize_value(item, dot, cell_border, cell_padding)) ^ { [] })) child]

fn safe_enum(value, allowed) any^ {
  // this module also exports lower(), so method syntax must select string.lower().
  let text = string(if (value != null) value else "").lower();
  if (contains(allowed, text)) text else null
}

fn safe_span(value) any^ {
  let text = graph_style.unsigned_number_text(value, false);
  let span = if (text != null and not contains(text, ".")) int(text) else null;
  if (span != null and span > 0 and span <= 1000) span else null
}

fn safe_port(value) {
  let text = trim(string(if (value != null) value else ""));
  if (text == "") null else text
}

fn alignment_style(align, valign) =>
  (if (align != null) "text-align:" ++ align ++ ";" else "") ++
  (if (valign != null) "vertical-align:" ++ valign ++ ";" else "")

fn table_number(value, fallback) any^ {
  let numeric = graph_style.unsigned_number_text(value, false);
  if (numeric == null or float(numeric) > 1000.0) fallback else numeric
}

fn table_color_style(value) any^ {
  let color = graph_style.safe_color(value.bgcolor);
  if (color == null) "" else "background-color:" ++ color ++ ";"
}

fn sanitized_element(value, value_tag, dot, cell_border, cell_padding) {
  if (value_tag == "strong" or value_tag == "b") {
      [<strong for child in sanitized_children(value, dot, cell_border, cell_padding) { child }>]
    }
    else if (value_tag == "em" or value_tag == "i") {
      [<em for child in sanitized_children(value, dot, cell_border, cell_padding) { child }>]
    }
    else if (value_tag == "code") { [<code for child in sanitized_children(value, dot, cell_border, cell_padding) { child }>] }
    else if (value_tag == "u") { [<u for child in sanitized_children(value, dot, cell_border, cell_padding) { child }>] }
    else if (value_tag == "sub") { [<sub for child in sanitized_children(value, dot, cell_border, cell_padding) { child }>] }
    else if (value_tag == "sup") { [<sup for child in sanitized_children(value, dot, cell_border, cell_padding) { child }>] }
    else if (value_tag == "br") { [<br>] }
    else if (value_tag == "hr") { [<hr>] }
    else if (value_tag == "table") {
      let border = table_number(value.border, "1");
      let child_border = table_number(value.cellborder, "1");
      let padding = table_number(value.cellpadding, "2");
      [<table class: "graph-label-table",
          align: safe_enum(value.align, ["left", "center", "right"]),
          // Collapse shared cell borders into a single line.
          style: if (dot) "border:" ++ border ++ "px solid currentColor;border-collapse:collapse;" ++ table_color_style(value) else null,
        for child in ((sanitized_children(value, dot, child_border, padding)) ^ { [] }) { child }
      >]
    }
    else if (value_tag == "tbody") {
      [<tbody for child in sanitized_children(value, dot, cell_border, cell_padding) { child }>]
    }
    else if (value_tag == "tr") {
      let align = safe_enum(value.align, ["left", "center", "right"]);
      let valign = safe_enum(value.valign, ["top", "middle", "bottom"]);
      [<tr align: align, valign: valign, style: alignment_style(align, valign),
        for child in sanitized_children(value, dot, cell_border, cell_padding) { child }>]
    }
    else if (value_tag == "td") {
      let align = safe_enum(value.align, ["left", "center", "right"]);
      let valign = safe_enum(value.valign, ["top", "middle", "bottom"]);
      // cell metadata is emitted before recursion because MIR currently reuses recursive locals.
      [<td align: align, valign: valign, colspan: safe_span(value.colspan),
          rowspan: safe_span(value.rowspan), 'data-record-port': safe_port(value.port),
          style: alignment_style(align, valign) ++ (if (dot)
            "border:" ++ table_number(value.border, cell_border) ++ "px solid currentColor;padding:" ++ cell_padding ++ "px;" ++
            (if (value.width != null) "min-width:" ++ table_number(value.width, "0") ++ "px;" else "") ++
            (if (value.height != null) "min-height:" ++ table_number(value.height, "0") ++ "px;" else "") ++
            table_color_style(value) else ""),
        for child in sanitized_children(value, dot, cell_border, cell_padding) { child }>]
    }
    else if (value_tag == "font") {
      [<span for child in sanitized_children(value, dot, cell_border, cell_padding) { child }>]
    }
    // Unknown wrappers are removed while their sanitized text and inline children survive.
    else { sanitized_children(value, dot, cell_border, cell_padding) }
}

fn sanitize_value(value, dot = false, cell_border = "1", cell_padding = "2") any^ {
  if (value is string) { [value] }
  else if (value is element) {
    let value_tag = model.tag(value);
    if (value_tag == "script" or value_tag == "style" or value_tag == "template") { [] }
    else sanitized_element(value, value_tag, dot, cell_border, cell_padding)
  }
  else { [] }
}

fn parsed_label(source, label_format) any^ {
  if (label_format == "markdown") {
    parse(source, "markdown") ^ { null }
  }
  else if (label_format == "html") { parse_html_fragment(source) }
  else { null }
}

pub fn lower(source, label_format = "text", dot = false) any^ {
  let text = string(source);
  if (label_format == "markdown" or label_format == "html") {
    let parsed = parsed_label(text, label_format);
    let safe = if (parsed == null) [] else sanitize_value(parsed, dot);
    // Parse failure must preserve visible source text rather than erase a measured label.
    if (parsed == null) { [text] } else { safe }
  }
  else { [text] }
}

pub fn is_rich(label_format) => label_format == "markdown" or label_format == "html"

fn port_names_at(stack, result) any^ {
  if (len(stack) == 0) result
  else {
    let value = stack[len(stack) - 1];
    let remaining = slice(stack, 0, len(stack) - 1);
    let children = if (value is element) model.child_items(value) else [];
    let port_name = if (value is element and model.tag(value) == "td" and
        value["data-record-port"] != null) string(value["data-record-port"]) else null;
    // reverse child push order so LIFO traversal preserves source order directly.
    port_names_at([*remaining, *reverse(children)],
      if (port_name != null and port_name != "") [*result, port_name] else result)
  }
}

pub fn port_names(values) any^ => port_names_at(values, [])
