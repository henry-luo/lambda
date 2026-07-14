// Safe rich-label lowering for measured graph content.

import model: lambda.package.graph.model

fn sanitize_value(value) {
  if (value is string) { [value] }
  else if (value is element) {
    let value_tag = model.tag(value);
    let children = if (len(value) > 0) [
      for (i in 0 to (len(value) - 1), child in sanitize_value(value[i])) child
    ] else [];
    if (value_tag == "script" or value_tag == "style" or value_tag == "template") { [] }
    else if (value_tag == "strong" or value_tag == "b") {
      [<strong; for child in children { child }>]
    }
    else if (value_tag == "em" or value_tag == "i") {
      [<em; for child in children { child }>]
    }
    else if (value_tag == "code") { [<code; for child in children { child }>] }
    else if (value_tag == "u") { [<u; for child in children { child }>] }
    else if (value_tag == "sub") { [<sub; for child in children { child }>] }
    else if (value_tag == "sup") { [<sup; for child in children { child }>] }
    else if (value_tag == "br") { [<br>] }
    // Unknown wrappers are removed while their sanitized text and inline children survive.
    else { children }
  }
  else { [] }
}

fn parsed_label(source, label_format) {
  if (label_format == "markdown") {
    let parsed^error = parse(source, "markdown");
    if (^error) { null } else { parsed }
  }
  else if (label_format == "html") { parse_html_fragment(source) }
  else { null }
}

pub fn lower(source, label_format = "text") {
  let text = string(source);
  if (label_format == "markdown" or label_format == "html") {
    let parsed = parsed_label(text, label_format);
    let safe = if (parsed == null) [] else sanitize_value(parsed);
    // Parse failure must preserve visible source text rather than erase a measured label.
    if (parsed == null) { [text] } else { safe }
  }
  else { [text] }
}

pub fn is_rich(label_format) => label_format == "markdown" or label_format == "html"
