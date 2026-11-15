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
// Count only delimiters in the first logical record, as the input viewer does.
fn detected_separator(chars) any^ {
  var i = 0, commas = 0, tabs = 0, field_start = true
  while (i < len(chars) and at(chars, i) != "\r" and at(chars, i) != "\n") {
    let ch = at(chars, i)
    if (ch == "\"" and field_start) {
      i = field_end(chars, i, ",")^
      field_start = false
    }
    else {
      if (ch == ",") { commas = commas + 1 }
      if (ch == "\t") { tabs = tabs + 1 }
      field_start = ch == "," or ch == "\t"
      i = i + 1
    }
  }
  if (tabs > commas) "\t" else ","
}
fn import_csv(source) map^ {
  let separator = detected_separator(split(source, "")^)^
  import_as(source, separator, 'csv')^
}
fn import_tsv(source) map^ => import_as(source, "\t", 'tsv')^
fn row_values(doc) => [for (entry in doc.tree.children) [for (cell in entry.node.children) cell.node.text]]
// A .csv file is detected again when reopened. Never save an edit that would
// reinterpret its rows under the other delimiter.
fn import_edited(doc, text) map^ {
  let loaded = import_as(text, doc.separator, doc.kind)^
  let detected = if (doc.kind == 'csv') detected_separator(split(text, "")^)^ else doc.separator
  if (detected == doc.separator) loaded else {
    let reopened = import_as(text, detected, doc.kind) ^ { null }
    if (reopened == null or row_values(reopened.doc) != row_values(loaded.doc))
      raise error("this edit would change the detected CSV delimiter; adjust the first row in source view")
    else reopened
  }
}
// Quote the other detectable delimiter in a changed first-row field.
fn detection_quote(doc, first_row, value) any^ => doc.kind == 'csv' and first_row and
  contains(value, if (doc.separator == ",") "\t" else ",")^
fn replace_span(doc, first, end_pos, value) map^ {
  let chars = split(doc.text, "")^
  import_edited(doc, raw(chars, 0, first) ++ value ++ raw(chars, end_pos, len(chars)))^
}
pub fn replace_scalar(doc, first, end_pos, value) map^ {
  let matches = [for (row in doc.tree.children) for (cell in row.node.children
    where (cell.node.first == first and cell.node.last == end_pos)) {node:cell.node, first_row:row.first == 0}]
  if (len(matches) == 0) { raise error("field no longer exists") }
  else replace_span(doc, first, end_pos, encode_field(value, doc.separator,
    matches[0].node.quoted or detection_quote(doc, matches[0].first_row, value)^)^)^
}
fn no_key(doc, first, end_pos, value) map^ => raise error("table fields have no property name")
// The separator belongs to the deleted field on its right, except at row end.
fn field_cut(cells, index) =>
  {first: if (index == len(cells) - 1 and index > 0) cells[index - 1].node.last else cells[index].node.first,
   last: if (index < len(cells) - 1) cells[index + 1].node.first else cells[index].node.last}
pub fn insert_child(doc, first, key, value) map^ {
  if (first == -1) {
    let chars = split(value, "")^
    let parsed = row(chars, 0, doc.separator, 0)^
    if (parsed.next != len(chars) or parsed.node.last != len(chars)) { raise error("enter one row") }
    else {
      let newline = source_text.trailing_line_ending(doc.text)^
      let prefix = if (doc.text == "" or ends_with(doc.text, "\n") or ends_with(doc.text, "\r")) "" else newline
      import_edited(doc, doc.text ++ prefix ++ value ++ newline)^
    }
  }
  else {
    let found = [for (entry in doc.tree.children where entry.node.first == first) entry.node]
    if (len(found) == 0) { raise error("row no longer exists") }
    else replace_span(doc, found[0].last, found[0].last, doc.separator ++
      encode_field(value, doc.separator, detection_quote(doc, found[0].pos == 0, value)^)^)^
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
        let cut = field_cut(cells, i)
        replace_span(doc, cut.first, cut.last, "")^
      }
    }
  }
}
// Apply every row change as one candidate before validating delimiter detection.
fn column_change(doc, index: int, insert: bool) map^ {
  let rows = doc.tree.children
  let widths = [for (entry in rows) len(entry.node.children)]
  let columns = if (len(widths) == 0) 0 else max(widths)
  if (index < 0 or (if (insert) index > columns else index >= columns) or columns == 0) {
    raise error("column no longer exists")
  }
  if (not insert and any([for (entry in rows) index < len(entry.node.children) and len(entry.node.children) == 1])) {
    raise error("a row needs at least one field; remove the row instead")
  }
  let chars = split(doc.text, "")^
  let row_count = len(rows)^
  var cursor = 0, parts = [], row_index = 0
  while (row_index < row_count) {
    let entry = rows[row_index]
    let cells = entry.node.children
    let width = len(cells)^
    if (insert) {
      // Pad short rows so the new field occupies the same column everywhere.
      let at = if (index < width) cells[index].node.first else entry.node.last
      var remaining = if (index < width) 1 else index - width + 1
      var separators = ""
      while (remaining > 0) { separators = separators ++ doc.separator; remaining = remaining - 1 }
      parts = [*parts, raw(chars, cursor, at), separators]
      cursor = at
    }
    else if (index < width) {
      let cut = field_cut(cells, index)
      parts = [*parts, raw(chars, cursor, cut.first)]
      cursor = cut.last
    }
    row_index = row_index + 1
  }
  return import_edited(doc, join([*parts, raw(chars, cursor, len(chars))], ""))^
}
pub fn insert_column(doc, index) map^ => column_change(doc, index, true)^
pub fn delete_column(doc, index) map^ => column_change(doc, index, false)^
// Insert a full-width empty row without rewriting the neighboring records.
pub fn insert_row(doc, first) map^ {
  let found = [for (entry in doc.tree.children where entry.node.first == first) entry]
  if (len(found) == 0) { raise error("row no longer exists") }
  let entry = found[0]
  let widths = [for (item in doc.tree.children) len(item.node.children)]
  let columns = max(widths)^
  var fields = "", index = 1
  while (index < columns) { fields = fields ++ doc.separator; index = index + 1 }
  // A newline inside a quoted field is not a record ending.
  let previous_endings = [for (row in doc.tree.children where row.first < entry.first and row.line_last > row.node.last)
    source_text.raw(doc.text, row.node.last, row.line_last)]
  let ending = if (entry.line_last > entry.node.last)
    source_text.raw(doc.text, entry.node.last, entry.line_last)
    else if (len(previous_endings) > 0) previous_endings[len(previous_endings) - 1] else "\n"
  return replace_span(doc, entry.first, entry.first, fields ++ ending)^
}
fn export_text(doc, envelope) => doc.text
fn check_roundtrip(doc, envelope) => null
pub let csv_descriptor = {id: 'csv', name: "CSV", suffixes: [".csv"], surface: 'property', fallback_source: true,
  multiline_values: true,
  import_text: import_csv, export_text: export_text, check_roundtrip: check_roundtrip,
  replace_scalar: replace_scalar, rename_key: no_key, insert_child: insert_child, delete_child: delete_child,
  insert_column: insert_column, delete_column: delete_column, insert_row: insert_row,
  value_help: "Enter a field value, or a CSV row at the root."}
pub let tsv_descriptor = {*: csv_descriptor, id: 'tsv', name: "TSV", suffixes: [".tsv"], import_text: import_tsv,
  value_help: "Enter a field value, or a TSV row at the root."}
