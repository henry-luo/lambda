// Graphviz record fields lowered to measured table content and canonical ports.

fn leaf_at(text, close) => if (close > 1)
  {text: trim(slice(text, close + 1, len(text))), port: slice(text, 1, close), fields: null}
else {text: text, port: null, fields: null}

fn leaf(raw) => leaf_at(trim(raw),
  if (starts_with(trim(raw), "<")) index_of(trim(raw), ">") else -1)

fn append_leaf(fields, text) => [*fields, leaf(text)]

fn scan_result(fields, next, valid) => map(["fields", fields, "next", next, "valid", valid])

fn parse_fields(text, i, escaped, current, fields, nested) =>
  if (i >= len(text)) scan_result(append_leaf(fields, current), i, nested == false)
  else if (escaped) parse_fields(text, i + 1, false,
    current ++ slice(text, i, i + 1), fields, nested)
  else if (slice(text, i, i + 1) == "\\")
    parse_fields(text, i + 1, true, current, fields, nested)
  else if (slice(text, i, i + 1) == "|")
    parse_fields(text, i + 1, false, "", append_leaf(fields, current), nested)
  else if (slice(text, i, i + 1) == "}" and nested)
    scan_result(append_leaf(fields, current), i + 1, true)
  else parse_fields(text, i + 1, false,
    current ++ slice(text, i, i + 1), fields, nested)

fn record_cell(field) =>
  <td 'data-record-port': field.port,
      style: "border:1px solid currentColor;padding:4px 6px;vertical-align:middle;";
    field.text
  >

fn record_table(fields) =>
  <table class: "graphviz-record-table", style: "border-collapse:collapse;border-spacing:0;";
    <tbody;
      <tr; for field in fields { record_cell(field) }>
    >
  >

fn port_names(fields) => [
  for (field in fields,
    port in if (field.fields != null) port_names(field.fields)
      else if (field.port != null and field.port != "") [field.port] else []) port
]

fn lowered(parsed, names) => {
  valid: parsed.valid,
  content: record_table(parsed.fields),
  ports: [for (i, port_name in names) <port id: port_name, side: "auto",
    offset: (float(i) + 0.5) / float(len(names))>]
}

fn lowered_parse(parsed) => lowered(parsed, port_names(parsed.fields))

pub fn lower(raw) => lowered_parse(parse_fields(string(raw), 0, false, "", [], false))
