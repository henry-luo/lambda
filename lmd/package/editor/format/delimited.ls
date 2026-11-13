// Source-backed CSV/TSV rows. The scanner keeps quoting and line endings;
// changing a field replaces only its original encoded span.
import source_text: lambda.editor.format.source_text
fn raw(chars, first, end_pos) => join(slice(chars, first, end_pos) ^ { [] }, "")
fn at(chars, i) => if (i < len(chars)) chars[i] else ""
// The tree edits field values; quoting belongs to the file representation.
fn decode_field(value) any^ =>
  if (starts_with(value, "\"") ^ { false }) replace(slice(value, 1, len(value) - 1)^, "\"\"", "\"")^
  else value
fn encode_field(value, separator, quoted) any^ {
  let needs_quote = quoted or contains(value, separator) or contains(value, "\"") or
                    contains(value, "\n") or contains(value, "\r")
  if (needs_quote) "\"" ++ replace(value, "\"", "\"\"")^ ++ "\"" else value
}
fn field_end(chars, first, separator) any^ {
  var i = first
  if (at(chars, i) == "\"") {
    i = i + 1
    while (i < len(chars)) {
      if (at(chars, i) == "\"" and at(chars, i + 1) == "\"") { i = i + 2 }
      else if (at(chars, i) == "\"") { return i + 1 }
      else { i = i + 1 }
    }
    raise error("unterminated quoted field")
  }
  else {
    while (i < len(chars) and at(chars, i) != separator and at(chars, i) != "\r" and at(chars, i) != "\n") {
      if (at(chars, i) == "\"") { raise error("quote in an unquoted field") }
      i = i + 1
    }
    return i
  }
}
fn row(chars, first, separator, row_index) any^ {
  var i = first, cells = [], col = 0, done = false
  while (not done) {
    let end_pos = field_end(chars, i, separator)^
    let value = raw(chars, i, end_pos)
    let multiline = contains(value, "\n") or contains(value, "\r")
    cells = [*cells, {name: string(col), first: i, node: {kind: 'scalar', first: i, last: end_pos,
      text: decode_field(value)^, quoted: starts_with(value, "\""), multiline: multiline}}]
    let following = at(chars, end_pos)
    if (following != separator and following != "" and following != "\n" and following != "\r") {
      raise error("text follows a closing quote")
    }
    if (following == separator) { i = end_pos + 1; col = col + 1 }
    else { i = end_pos; done = true }
  }
  let end_pos = i
  let next = if (at(chars, i) == "\r" and at(chars, i + 1) == "\n") i + 2
             else if (at(chars, i) == "\r" or at(chars, i) == "\n") i + 1 else i;
  {name: string(row_index), first: first, line_last: next,
   node: {kind: 'array', first: 0 - row_index - 2, pos: first, last: end_pos, children: cells}, next: next}
}
fn import_as(source, separator, kind) map^ {
  let chars = split(source, "")^
  var rows = [], i = 0, n = 0
  while (i < len(chars)) {
    let parsed = row(chars, i, separator, n)^
    rows = [*rows, parsed]
    i = parsed.next
    n = n + 1
  }
  {doc: {text: source, separator: separator, kind: kind,
         tree: {kind: 'array', first: -1, last: len(chars), children: rows}}, envelope: null}
}
fn import_csv(source) map^ {
  // Match the viewer's delimiter detection on the first line.
  let first = split(source, "\n")[0]
  let separator = if (len(split(first, "\t")) > len(split(first, ","))) "\t" else ","
  import_as(source, separator, 'csv')^
}
fn import_tsv(source) map^ => import_as(source, "\t", 'tsv')^
fn replace_span(doc, first, end_pos, value) map^ {
  let chars = split(doc.text, "")^
  import_as(raw(chars, 0, first) ++ value ++ raw(chars, end_pos, len(chars)), doc.separator, doc.kind)^
}
pub fn replace_scalar(doc, first, end_pos, value) map^ {
  let matches = [for (row in doc.tree.children) for (cell in row.node.children
    where (cell.node.first == first and cell.node.last == end_pos)) cell.node]
  if (len(matches) == 0) { raise error("field no longer exists") }
  else replace_span(doc, first, end_pos, encode_field(value, doc.separator, matches[0].quoted)^)^
}
fn no_key(doc, first, end_pos, value) map^ => raise error("table fields have no property name")
pub fn insert_child(doc, first, key, value) map^ {
  if (first == -1) {
    let chars = split(value, "")^
    let parsed = row(chars, 0, doc.separator, 0)^
    if (parsed.next != len(chars) or parsed.node.last != len(chars)) { raise error("enter one row") }
    else {
      let newline = source_text.trailing_line_ending(doc.text)^
      let prefix = if (doc.text == "" or ends_with(doc.text, "\n") or ends_with(doc.text, "\r")) "" else newline
      import_as(doc.text ++ prefix ++ value ++ newline, doc.separator, doc.kind)^
    }
  }
  else {
    let found = [for (entry in doc.tree.children where entry.node.first == first) entry.node]
    if (len(found) == 0) { raise error("row no longer exists") }
    else replace_span(doc, found[0].last, found[0].last, doc.separator ++ encode_field(value, doc.separator, false)^)^
  }
}
pub fn delete_child(doc, first) map^ {
  let rows = [for (entry in doc.tree.children where entry.node.first == first) entry]
  if (len(rows) > 0) replace_span(doc, rows[0].first, rows[0].line_last, "")^
  else {
    let found = [for (entry in doc.tree.children) for (i, cell in entry.node.children where cell.node.first == first)
      {row: entry.node, first: entry.first, line_last: entry.line_last, index: i}]
    if (len(found) == 0) { raise error("field no longer exists") }
    else {
      let cells = found[0].row.children
      let i = found[0].index
      // CSV has no row with zero fields; deleting its last field removes the row.
      if (len(cells) == 1) replace_span(doc, found[0].first, found[0].line_last, "")^
      else {
        let begin = if (i == len(cells) - 1 and i > 0) cells[i - 1].node.last else cells[i].node.first
        let end_pos = if (i < len(cells) - 1) cells[i + 1].node.first else cells[i].node.last
        replace_span(doc, begin, end_pos, "")^
      }
    }
  }
}
fn export_text(doc, envelope) => doc.text
fn check_roundtrip(doc, envelope) => null
pub let csv_descriptor = {id: 'csv', name: "CSV", suffixes: [".csv"], surface: 'property', fallback_source: true,
  multiline_values: true,
  import_text: import_csv, export_text: export_text, check_roundtrip: check_roundtrip,
  replace_scalar: replace_scalar, rename_key: no_key, insert_child: insert_child, delete_child: delete_child,
  value_help: "Enter a field value, or a CSV row at the root."}
pub let tsv_descriptor = {*: csv_descriptor, id: 'tsv', name: "TSV", suffixes: [".tsv"], import_text: import_tsv,
  value_help: "Enter a field value, or a TSV row at the root."}
