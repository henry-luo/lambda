// INI and Java properties retain each original line and replace only the edited span.
// Continued values use source mode until they have a multi-line span model.
import source_text: lambda.editor.format.source_text
fn raw(source, first, end_pos) => slice(source, first, end_pos) ^ { "" }
fn valid_key(key) any^ => not starts_with(key, "#") and not starts_with(key, "!") and
  not any([for (ch in split(key, "")^) contains(["=", ":", " ", "\t", "\r", "\n"], ch)^])
fn valid_value(value) any^ => not contains(value, "\n") and not contains(value, "\r") and not ends_with(value, "\\")
fn valid_key_for(key, kind) any^ => valid_key(key) and
  (kind != 'ini' or (key != "" and not starts_with(key, ";")))
fn property_key_text(key) any^ => join([for (ch in split(key, "")^)
  if (ch == "\t") "\\t" else if (ch == "\f") "\\f"
  else if (ch in ["\\", "=", ":", " ", "#", "!"]) "\\" ++ ch else ch], "")
fn property_key_valid(key) any^ => not contains(key, "\n") and not contains(key, "\r")
fn separator(line, kind) {
  let a = index_of(line, "=")
  let b = index_of(line, ":")
  if (kind == 'ini') a else if (a == null) b else if (b == null) a else min(a, b)
}
// INI comments are outside quoted values; the closing quote belongs to the
// value span and comments after it must remain untouched.
fn ini_end(line, first) any^ {
  let quote = raw(line, first, first + 1)
  var i = first
  if (quote == "\"" or quote == "'") {
    i = i + 1
    while (i < len(line)) {
      let c = raw(line, i, i + 1)
      if (c == "\\" and raw(line, i + 1, i + 2) == quote) { i = i + 2 }
      else if (c == quote) { return i + 1 }
      else { i = i + 1 }
    }
    raise error("unterminated INI value")
  }
  else {
    while (i < len(line) and raw(line, i, i + 1) != "#" and raw(line, i, i + 1) != ";" and raw(line, i, i + 1) != "\r") { i = i + 1 }
    return len(trim_end(raw(line, 0, i)))
  }
}
// Locate unescaped delimiters while leaving decoding to the properties parser.
fn property_skip_space(line, first, limit) any^ {
  var i = first
  while ((i < limit and raw(line, i, i + 1) in [" ", "\t", "\f"])^) { i = i + 1 }
  return i
}
fn property_entry(line, offset, line_end) any^ {
  let limit = len(trim_end(line))
  let first = property_skip_space(line, 0, limit)^
  var end_pos = first
  while ((end_pos < limit and not (raw(line, end_pos, end_pos + 1) in ["=", ":", " ", "\t", "\f"]))^) {
    if (raw(line, end_pos, end_pos + 1) == "\\") { end_pos = end_pos + 2 }
    else { end_pos = end_pos + 1 }
  }
  let parsed = (if (end_pos >= limit) raise error("a property has no separator")
                else parse(raw(line, first, end_pos) ++ "=x", 'properties'))^
  let keys = [for (key, value at parsed) string(key)]
  let key = (if (len(keys) != 1) raise error("a property name needs source mode") else keys[0])^
  var value_first = property_skip_space(line, end_pos, limit)^
  if (raw(line, value_first, value_first + 1) in ["=", ":"]) { value_first = value_first + 1 }
  value_first = property_skip_space(line, value_first, limit)^;
  {name: key, group: "", first: offset + first, key_end: offset + end_pos, line_first: offset, line_last: line_end,
   node: {kind: 'scalar', first: offset + value_first, last: offset + max(value_first, limit),
          text: raw(line, value_first, max(value_first, limit))}}
}
fn line_entry(line, offset, line_end, kind, section) any^ {
  let ini = kind == 'ini'
  let trimmed = trim(line)
  if (trimmed == "" or starts_with(trimmed, "#") or (not ini and starts_with(trimmed, "!")) or
      (ini and starts_with(trimmed, ";"))) null
  else if (not ini and ends_with(trim_end(line), "\\")) { raise error("continued property values require source mode") }
  else if (not ini) { property_entry(line, offset, line_end)^ }
  else {
    let sep = separator(line, kind)
    if (sep == null) { raise error("a property has no separator") }
    else {
      let before = raw(line, 0, sep)
      let key = trim(before)
      if (not valid_key(key)) { raise error("a property name needs source mode") }
      else {
        let key_first = offset + len(before) - len(trim_start(before))
        let rest = raw(line, sep + 1, max(sep + 1, len(trim_end(line))))
        let value_first = offset + sep + 1 + len(rest) - len(trim_start(rest))
        let value_end = if (ini) offset + max(value_first - offset, ini_end(line, value_first - offset)^)
                        else offset + max(value_first - offset, len(trim_end(line)))
        let value = raw(line, value_first - offset, max(value_first, value_end) - offset);
        {name: key, group: section, first: key_first, key_end: key_first + len(key), line_first: offset, line_last: line_end,
         node: {kind: 'scalar', first: value_first, last: max(value_first, value_end), text: value}}
      }
    }
  }
}
fn import_as(source, kind) map^ {
  let parsed = parse(source, kind)^
  let lines = split(source, "\n")^
  let ini = kind == 'ini'
  var offset = 0, entries = [], i = 0, section = if (ini) "global" else ""
  while (i < len(lines)) {
    let line = lines[i]
    let line_end = min(len(source), offset + len(line) + 1)
    let stripped = trim(line)
    let heading = ini and starts_with(stripped, "[")
    if ((heading and not ends_with(stripped, "]"))^) { raise error("invalid INI section") }
    if (heading) { section = raw(stripped, 1, len(stripped) - 1) }
    let entry = if (heading) null else line_entry(line, offset, line_end, kind, section)^
    if ((entry != null)^) { entries = [*entries, entry] }
    offset = line_end
    i = i + 1
  }
  {doc: {text: source, kind: kind, tree: {kind: 'object', first: 0, last: len(source), children: entries}}, envelope: null}
}
pub fn import_text(source) map^ => import_as(source, 'properties')^
fn import_ini(source) map^ => import_as(source, 'ini')^
fn replace_span(doc, first, end_pos, text) map^ =>
  import_as(raw(doc.text, 0, first) ++ text ++ raw(doc.text, end_pos, len(doc.text)), doc.kind)^
// Names and values are source text; reject changes the line parser would alter.
fn checked_entry(result, line_first, key, value) map^ {
  let entries = [for (entry in result.doc.tree.children where entry.line_first == line_first) entry]
  if (len(entries) != 1 or entries[0].name != key)
    raise error("property name would be changed by the file syntax")
  else if (entries[0].node.text != value)
    raise error("value would be changed by the file syntax; quote INI comments and avoid unquoted edge whitespace")
  else result
}
pub fn replace_scalar(doc, first, end_pos, value) map^ {
  let entries = [for (entry in doc.tree.children where entry.node.first == first and entry.node.last == end_pos) entry]
  if (len(entries) == 0) { raise error("property no longer exists") }
  else if (not valid_value(value)) { raise error("use source mode for a multi-line value") }
  else {
    // An empty value can end at the comment marker; retain a separating space.
    let next = raw(doc.text, end_pos, end_pos + 1)
    let suffix = if (first == end_pos and value != "" and doc.kind != 'properties' and (next == "#" or next == ";")) " " else ""
    checked_entry(replace_span(doc, first, end_pos, value ++ suffix)^, entries[0].line_first, entries[0].name, value)^
  }
}
pub fn rename_key(doc, first, end_pos, key) map^ {
  let entries = [for (entry in doc.tree.children where entry.first == first and entry.key_end == end_pos) entry]
  if (len(entries) == 0) raise error("property no longer exists")
  else if (not (if (doc.kind == 'properties') property_key_valid(key) else valid_key_for(key, doc.kind))) raise error("invalid property name")
  else checked_entry(replace_span(doc, first, end_pos, if (doc.kind == 'properties') property_key_text(key)^ else key)^, entries[0].line_first, key, entries[0].node.text)^
}
pub fn insert_child(doc, first, key, value) map^ {
  if (first != 0) { raise error("properties can only be added at the root") }
  else if (not (if (doc.kind == 'properties') property_key_valid(key) else valid_key_for(key, doc.kind)) or not valid_value(value)) { raise error("invalid property name or value") }
  else {
    let newline = source_text.trailing_line_ending(doc.text)^
    let prefix = if (doc.text == "" or ends_with(doc.text, "\n")) "" else newline
    let line_first = len(doc.text) + len(prefix)
    let written_key = if (doc.kind == 'properties') property_key_text(key)^ else key
    checked_entry(import_as(doc.text ++ prefix ++ written_key ++ "=" ++ value ++ newline, doc.kind)^, line_first, key, value)^
  }
}
pub fn delete_child(doc, first) map^ {
  let found = [for (entry in doc.tree.children where entry.node.first == first) entry]
  if (len(found) == 0) { raise error("property no longer exists") }
  else replace_span(doc, found[0].line_first, found[0].line_last, "")^
}
fn export_text(doc, envelope) => doc.text
fn check_roundtrip(doc, envelope) => null
pub let descriptor = {id: 'properties', name: "Properties", suffixes: [".properties", ".props"],
  surface: 'property', fallback_source: true, import_text: import_text, export_text: export_text,
  check_roundtrip: check_roundtrip, replace_scalar: replace_scalar, rename_key: rename_key,
  insert_child: insert_child, delete_child: delete_child, value_help: "Enter a property value."}
pub let ini_descriptor = {*: descriptor, id: 'ini', name: "INI", suffixes: [".ini"], import_text: import_ini,
  value_help: "Enter a value. New properties are added to the last section."}
