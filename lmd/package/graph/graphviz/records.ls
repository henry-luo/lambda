// Graphviz record fields lowered to measured table content and canonical ports.

fn leaf_at(text, close) => if (close != null and close > 1)
  {text: trim(slice(text, close + 1, len(text))), port: slice(text, 1, close), fields: null}
else {text: text, port: null, fields: null}

fn leaf(raw) any^ =>
  // An unreadable label contributes an empty record cell rather than aborting
  // the containing graph normalization.
  leaf_at(trim(raw),
    if (starts_with(trim(raw), "<")) index_of(trim(raw), ">") else null) or
  {text: "", port: null, fields: null}

fn scan_result(fields, next, valid) => map(["fields", fields, "next", next, "valid", valid])

fn frame(fields, text, complete) => {fields: fields, text: text, complete: complete}

fn finish(value) => if (value.complete) value
  else frame([*value.fields, leaf(value.text)], "", true)

fn replace_last(stack, value) => [*slice(stack, 0, len(stack) - 1), value]

fn close_group(stack) any^ {
  let nested = finish(stack[len(stack) - 1]);
  let parents = slice(stack, 0, len(stack) - 1);
  let parent = parents[len(parents) - 1];
  let group = {text: null, port: null, fields: nested.fields};
  replace_last(parents, frame([*parent.fields, group], "", true))
}

fn parse_fields(text, i, escaped, stack, valid) any^ {
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
    // Whitespace after a completed group is not a new empty field.
    else if (current.complete and trim(ch) == "")
      parse_fields(text, i + 1, false, stack, valid)
    else if (current.complete and trim(ch) != "")
      parse_fields(text, i + 1, false, stack, false)
    else parse_fields(text, i + 1, false,
      replace_last(stack, frame(current.fields, current.text ++ ch, false)),
      valid and ch != "}")
  }
}

fn record_cell(field, vertical, index, nested) =>
  <td 'data-record-port': field.port,
      // The containing cell owns a nested group's perimeter; only add its internal dividers.
      style: (if (not nested) "border:1px solid currentColor;" else
        if (index == 0) "border:0;" else if (vertical) "border:0;border-top:1px solid currentColor;"
        else "border:0;border-left:1px solid currentColor;") ++
        (if (field.fields != null) "padding:0;" else "padding:4px 6px;") ++
        "vertical-align:middle;text-align:center;",
    if (field.fields != null) { record_table(field.fields, not vertical, true) }
    else { field.text }
  >

fn record_rows(fields, vertical, nested) any^ => if (vertical) [
  for (i, field in fields) <tr record_cell(field, vertical, i, nested)>
] else [<tr for (i, field in fields) record_cell(field, vertical, i, nested)^>]

fn record_table(fields, vertical, nested = false, width = null, height = null) =>
  <table class: "graphviz-record-table", 'data-record-axis': if (vertical) "vertical" else "horizontal",
      style: "border-collapse:collapse;border-spacing:0;" ++ (if (nested) "width:100%;" else "") ++
        (if (width != null) "width:" ++ string(width) ++ "px;" else "") ++
        (if (height != null) "height:" ++ string(height) ++ "px;" else ""),
    <tbody
      for row in ((record_rows(fields, vertical, nested)) ^ { [] }) { row }
    >
  >

fn lowered(parsed, vertical, width, height) => {
  valid: parsed.valid,
  content: record_table(parsed.fields, vertical, false, width, height)
}

pub fn lower(raw, vertical = false, width = null, height = null) =>
  // Preserve a renderable, explicitly invalid record when dynamic label text
  // cannot be parsed; callers use valid to decide whether to trust its ports.
  (lowered(parse_fields(string(raw), 0, false, [frame([], "", false)], true), vertical, width, height) ^ { null }) or
  {valid: false, content: record_table([], vertical)}
