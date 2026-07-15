// Graphviz record fields lowered to measured table content and canonical ports.

fn leaf_at(text, close) => if (close > 1)
  {text: trim(slice(text, close + 1, len(text))), port: slice(text, 1, close), fields: null}
else {text: text, port: null, fields: null}

fn leaf(raw) => leaf_at(trim(raw),
  if (starts_with(trim(raw), "<")) index_of(trim(raw), ">") else -1)

fn scan_result(fields, next, valid) => map(["fields", fields, "next", next, "valid", valid])

fn frame(fields, text, complete) => {fields: fields, text: text, complete: complete}

fn finish(value) => if (value.complete) value
  else frame([*value.fields, leaf(value.text)], "", true)

fn replace_last(stack, value) => [*slice(stack, 0, len(stack) - 1), value]

fn close_group(stack) {
  let nested = finish(stack[len(stack) - 1]);
  let parents = slice(stack, 0, len(stack) - 1);
  let parent = parents[len(parents) - 1];
  let group = {text: null, port: null, fields: nested.fields};
  replace_last(parents, frame([*parent.fields, group], "", true))
}

fn parse_fields(text, i, escaped, stack, valid) {
  let current = stack[len(stack) - 1];
  if (i >= len(text)) {
    if (len(stack) != 1) scan_result([], i, false)
    else {
      let result = finish(current);
      scan_result(result.fields, i, valid)
    }
  }
  else {
    let ch = slice(text, i, i + 1);
    if (escaped) parse_fields(text, i + 1, false,
      replace_last(stack, frame(current.fields, current.text ++ ch, false)), valid)
    else if (ch == "\\") parse_fields(text, i + 1, true, stack, valid)
    else if (ch == "{" and trim(current.text) == "" and not current.complete)
      parse_fields(text, i + 1, false, [*stack, frame([], "", false)], valid)
    else if (ch == "|" and len(stack) > 0) {
      let done = finish(current);
      parse_fields(text, i + 1, false,
        replace_last(stack, frame(done.fields, "", false)), valid)
    }
    else if (ch == "}" and len(stack) > 1)
      parse_fields(text, i + 1, false, close_group(stack), valid)
    else if (current.complete and trim(ch) != "")
      parse_fields(text, i + 1, false, stack, false)
    else parse_fields(text, i + 1, false,
      replace_last(stack, frame(current.fields, current.text ++ ch, false)),
      valid and ch != "}")
  }
}

fn record_cell(field, vertical) =>
  <td 'data-record-port': field.port,
      style: "border:1px solid currentColor;padding:4px 6px;vertical-align:middle;";
    if (field.fields != null) { record_table(field.fields, not vertical) }
    else { field.text }
  >

fn record_rows(fields, vertical) => if (vertical) [
  for (field in fields) <tr; record_cell(field, vertical)>
] else [<tr; for field in fields { record_cell(field, vertical) }>]

fn record_table(fields, vertical) =>
  <table class: "graphviz-record-table", 'data-record-axis': if (vertical) "vertical" else "horizontal",
      style: "border-collapse:collapse;border-spacing:0;";
    <tbody;
      for row in record_rows(fields, vertical) { row }
    >
  >

fn lowered(parsed, vertical) => {
  valid: parsed.valid,
  content: record_table(parsed.fields, vertical)
}

pub fn lower(raw, vertical = false) => lowered(
  parse_fields(string(raw), 0, false, [frame([], "", false)], true), vertical)
