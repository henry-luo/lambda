// INI and Java properties retain source spans, including continued properties.
import source_text: lambda.editor.format.source_text
import text_policy: lambda.dom.edit_text_policy
fn raw(source, first, end_pos) => slice(source, first, end_pos) ^ { "" }
fn valid_key(key) any^ => not contains(key, "\u0000") and not starts_with(key, "#") and not starts_with(key, "!") and
  not any([for (ch in split(key, "")^) contains(["=", ":", " ", "\t", "\r", "\n"], ch)^])
fn valid_value(value) any^ => not contains(value, "\u0000") and not contains(value, "\n") and not contains(value, "\r") and not ends_with(value, "\\")
fn valid_key_for(key, kind) any^ => valid_key(key) and
  (kind != 'ini' or (key != "" and not starts_with(key, ";")))
fn property_key_text(key) any^ => join([for (ch in split(key, "")^)
  if (ch == "\t") "\\t" else if (ch == "\f") "\\f"
  else if (ch in ["\\", "=", ":", " ", "#", "!"]) "\\" ++ ch else ch], "")
fn property_key_valid(key) any^ => not contains(key, "\u0000") and not contains(key, "\n") and not contains(key, "\r")
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
fn property_skip_space(line, first, limit, spaces = [" ", "\t", "\f"]) any^ {
  var i = first
  while ((i < limit and raw(line, i, i + 1) in spaces)^) { i = i + 1 }
  return i
}
// A continuation removes its line ending and the following horizontal space.
fn property_escape_end(text, at, limit) any^ {
  let next = raw(text, at + 1, at + 2)
  if (next == "\r" or next == "\n") {
    let after = if (next == "\r" and raw(text, at + 2, at + 3) == "\n") at + 3 else at + 2
    property_skip_space(text, after, limit)^
  } else min(at + 2, limit)
}
fn value_text(text, kind) any^ {
  if (kind == 'ini') { return text_policy.sanitize(text, true)^ }
  if (not contains(text, "\n") and not contains(text, "\r")) { return text }
  var i = 0, first = 0, pieces = []
  while (i < len(text)) {
    let c = raw(text, i, i + 1)
    let next = raw(text, i + 1, i + 2)
    if (c == "\\" and next in ["\r", "\n"]) {
      pieces = [*pieces, raw(text, first, i)]
      i = property_escape_end(text, i, len(text))^
      first = i
    }
    else if (c == "\\" and i + 1 < len(text)) { i = i + 2 }
    else { i = i + 1 }
  }
  join([*pieces, raw(text, first, len(text))], "")
}
fn property_entry(line, offset, line_end, terminal_continuation = false, final_line_first = 0) any^ {
  let limit = len(line)
  // Trim only the last natural line, not a continuation ending before it.
  let value_limit = if (terminal_continuation) limit else
    final_line_first + len(trim_end(raw(line, final_line_first, limit)))
  let first = property_skip_space(line, 0, limit)^
  var end_pos = first
  while ((end_pos < limit and not (raw(line, end_pos, end_pos + 1) in ["=", ":", " ", "\t", "\f"]))^) {
    if (raw(line, end_pos, end_pos + 1) == "\\") { end_pos = property_escape_end(line, end_pos, limit)^ }
    else { end_pos = end_pos + 1 }
  }
  let needs_separator = end_pos >= limit
  // A bare key is itself a complete properties record with an empty value.
  let key_source = raw(line, first, end_pos)
  let parsed = parse(if (needs_separator) key_source else key_source ++ "=x", 'properties')^
  let keys = [for (key, value at parsed) string(key)]
  let key = (if (len(keys) != 1) raise error("a property name needs source mode") else keys[0])^
  if ((contains(key, "\u0000") or contains(key, "\n") or contains(key, "\r"))^) {
    raise error("property names with NUL or line breaks require source mode")
  }
  let after_key = property_skip_space(line, end_pos, limit)^
  let explicit_separator = raw(line, after_key, after_key + 1) in ["=", ":"]
  let after_separator = if (explicit_separator) after_key + 1 else after_key
  // Keep spaces after an explicit empty value outside its replacement span.
  let value_first = if (not explicit_separator and after_key >= value_limit) after_key
    else property_skip_space(line, after_separator, max(after_separator, value_limit))^;
  return {name: key, group: "", first: offset + first, key_end: offset + end_pos, line_first: offset, line_last: line_end,
   terminal_continuation: terminal_continuation, needs_separator: needs_separator,
   node: {kind: 'scalar', first: offset + value_first, last: offset + max(value_first, value_limit),
          text: value_text(raw(line, value_first, max(value_first, value_limit)), 'properties')^}}
}
fn ini_line_entry(line, offset, line_end, section) any^ {
  let trimmed = trim(line)
  if (trimmed == "" or starts_with(trimmed, "#") or starts_with(trimmed, ";")) null
  else {
    let sep = index_of(line, "=")
    if (sep == null) { raise error("a property has no separator") }
    else {
      let before = raw(line, 0, sep)
      let key = trim(before)
      if (not valid_key(key)) { raise error("a property name needs source mode") }
      else {
        let key_first = offset + len(before) - len(trim_start(before))
        // INI skips only space and tab here; other whitespace is value data.
        let value_first = offset + property_skip_space(line, sep + 1, len(line), [" ", "\t"])^
        let value_end = offset + max(value_first - offset, ini_end(line, value_first - offset)^)
        let value = value_text(raw(line, value_first - offset, max(value_first, value_end) - offset), 'ini')^;
        let literal = source_text.has_controls(value, ["\t", "\n"])^
        let display = if (literal) replace(format(value, 'json')^, chr(127), "\\u007f")^ else value;
        {name: key, group: section, first: key_first, key_end: key_first + len(key), line_first: offset, line_last: line_end,
         node: {kind: 'scalar', first: value_first, last: max(value_first, value_end), text: value,
                value_literal: literal, display_text: display,
                multiline: contains(value, "\n"), multiline_capable: raw(value, 0, 1) in ["\"", "'"]}}
      }
    }
  }
}
fn trailing_backslashes(source, first, end_pos) {
  var i = end_pos
  while (i > first and raw(source, i - 1, i) == "\\") { i = i - 1 }
  end_pos - i
}
// Comment lines never continue; only an odd run immediately before a line ending does.
fn property_record(source, first) any^ {
  var line = source_text.natural_line(source, first)^
  let content_first = property_skip_space(source, first, line.end)^
  let comment = raw(source, content_first, content_first + 1) in ["#", "!"]
  var terminal = false
  while ((not comment and line.next > line.end and trailing_backslashes(source, first, line.end)^ % 2 == 1)^) {
    if (line.next == len(source)) { terminal = true; break }
    line = source_text.natural_line(source, line.next)^
  }
  {end: if (terminal) line.next else line.end, next: line.next, terminal: terminal, final_line_first: line.first - first}
}
fn import_properties(source) map^ {
  let parsed = parse(source, 'properties')^
  var offset = 0, entries = []
  while (offset < len(source)) {
    let record = property_record(source, offset)^
    let line = raw(source, offset, record.end)
    let content_first = property_skip_space(line, 0, len(line))^
    let entry = if (content_first == len(line) or raw(line, content_first, content_first + 1) in ["#", "!"]) null
      else property_entry(line, offset, record.next, record.terminal, record.final_line_first)^
    if ((entry != null)^) { entries = [*entries, entry] }
    offset = record.next
  }
  {doc: {text: source, kind: 'properties', tree: {kind: 'object', first: 0, last: len(source), children: entries}}, envelope: null}
}
// The Input reader permits a quoted INI value to cross natural lines.
fn ini_record(source, first) any^ {
  let line = source_text.natural_line(source, first)^
  let initial = raw(source, first, line.end)
  let stripped = trim_start(initial)
  let ignored = starts_with(stripped, ";") or starts_with(stripped, "#") or starts_with(stripped, "[")
  let sep = if (ignored) null else index_of(initial, "=")
  let value_first = if (sep == null) line.end else first + property_skip_space(initial, sep + 1, len(initial), [" ", "\t"])^
  let quote = raw(source, value_first, value_first + 1)
  if (quote == "\"" or quote == "'") {
    let after_quote = ini_end(source, value_first)^
    let final_line = source_text.natural_line(source, after_quote)^;
    return {end:final_line.end, next:final_line.next}
  }
  return {end:line.end, next:line.next}
}
fn import_as(source, kind) map^ {
  if (contains(source, "\u0000")) { raise error("NUL bytes require source mode") }
  if (kind == 'properties') { return import_properties(source)^ }
  let parsed = parse(source, kind)^
  var offset = 0, entries = [], section = "global"
  while (offset < len(source)) {
    let record = ini_record(source, offset)^
    let line = raw(source, offset, record.end)
    let stripped = trim(line)
    let heading = starts_with(stripped, "[")
    let close = if (heading) index_of(stripped, "]") else null
    if ((heading and (close == null or close <= 1))^) { raise error("invalid INI section") }
    if (heading) { section = raw(stripped, 1, close) }
    let entry = if (heading) null else ini_line_entry(line, offset, record.next, section)^
    if ((entry != null)^) { entries = [*entries, entry] }
    offset = record.next
  }
  return {doc: {text: source, kind: kind, tree: {kind: 'object', first: 0, last: len(source), children: entries}}, envelope: null}
}
pub fn import_text(source) map^ => import_as(source, 'properties')^
fn import_ini(source) map^ => import_as(source, 'ini')^
fn replace_span(doc, first, end_pos, text) map^ =>
  import_as(raw(doc.text, 0, first) ++ text ++ raw(doc.text, end_pos, len(doc.text)), doc.kind)^
// Map an offset in the displayed value back to its source, retaining continuation spelling.
fn value_source_offset(text, offset, right, kind) any^ {
  if (kind == 'ini') { return source_text.normalized_offset(text, offset)^ }
  var raw_pos = 0, logical = 0
  while (raw_pos < len(text)) {
    let c = raw(text, raw_pos, raw_pos + 1)
    let next = raw(text, raw_pos + 1, raw_pos + 2)
    if (kind == 'properties' and c == "\\" and next in ["\r", "\n"]) {
      if (logical == offset and not right) { return raw_pos }
      raw_pos = property_escape_end(text, raw_pos, len(text))^
    } else {
      if (logical == offset) { return raw_pos }
      raw_pos = raw_pos + (if (kind == 'ini' and c == "\r" and next == "\n") 2 else 1)
      logical = logical + 1
      // The second backslash in an escape pair is not a continuation.
      if (kind == 'properties' and c == "\\" and raw_pos < len(text)) {
        if (logical == offset) { return raw_pos }
        raw_pos = raw_pos + 1; logical = logical + 1
      }
    }
  }
  raw_pos
}
fn value_edit(original, value, kind, newline) any^ {
  let old = value_text(original, kind)^
  let changed = source_text.changed_span(old, value)^
  let prefix = changed.prefix, suffix = changed.suffix
  let first = value_source_offset(original, prefix, true, kind)^
  let end_pos = max(first, value_source_offset(original, len(old) - suffix, false, kind)^)
  let inserted = raw(value, prefix, len(value) - suffix)
  raw(original, 0, first) ++ (if (kind == 'ini') replace(inserted, "\n", newline)^ else inserted) ++ raw(original, end_pos, len(original))
}
fn valid_property_value(value) any^ => not contains(value, "\u0000") and not contains(value, "\n") and not contains(value, "\r") and
  trailing_backslashes(value, 0, len(value)) % 2 == 0
fn valid_ini_value(value) any^ {
  if (contains(value, "\u0000") or contains(value, "\r")) false
  else if (not contains(value, "\n")) valid_value(value)^
  else if (raw(value, 0, 1) in ["\"", "'"]) (ini_end(value, 0) ^ { -1 }) == len(value)
  else false
}
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
  else if (value == entries[0].node.text) { {doc:doc, envelope:null} }
  else if (not (if (doc.kind == 'properties') valid_property_value(value) else valid_ini_value(value))) { raise error("invalid property value") }
  else {
    // An empty value can end at the comment marker; retain a separating space.
    let next = raw(doc.text, end_pos, end_pos + 1)
    let suffix = if (first == end_pos and value != "" and doc.kind != 'properties' and (next == "#" or next == ";")) " " else ""
    if (doc.kind == 'properties') {
      let key_end = entries[0].key_end
      let dangling_key = (entries[0].needs_separator == true and key_end == len(doc.text) and
        trailing_backslashes(doc.text, entries[0].first, key_end)^ % 2 == 1)^
      let marker = if (dangling_key) source_text.trailing_line_ending(doc.text)^ ++ "="
        else if (entries[0].needs_separator == true) "=" else ""
      let ending = if (entries[0].needs_separator == true and entries[0].terminal_continuation == true)
        source_text.trailing_line_ending(doc.text)^ else ""
      let replacement = marker ++ value_edit(raw(doc.text, first, end_pos), value, 'properties', "")^ ++ ending
      let candidate = replace_span(doc, first, end_pos, replacement) ^ { null }
      let retained = if (candidate == null) null else (checked_entry(candidate, entries[0].line_first, entries[0].name, value) ^ { null })
      // A changed escape can make a formerly retained continuation invalid.
      if (retained != null) retained else {
        let fallback_ending = if (entries[0].terminal_continuation == true) source_text.trailing_line_ending(doc.text)^ else ""
        checked_entry(replace_span(doc, first, end_pos, marker ++ value ++ fallback_ending)^, entries[0].line_first, entries[0].name, value)^
      }
    }
    else {
      // Added lines follow this record's ending, even when later records differ.
      let record = raw(doc.text, entries[0].line_first, entries[0].line_last)
      let newline = source_text.trailing_line_ending(if (contains(record, "\r") or contains(record, "\n")) record else doc.text)^
      let replacement = value_edit(raw(doc.text, first, end_pos), value, 'ini', newline)^
      let candidate = replace_span(doc, first, end_pos, replacement ++ suffix) ^ { null }
      let retained = if (candidate == null) null else (checked_entry(candidate, entries[0].line_first, entries[0].name, value) ^ { null })
      if (retained != null) retained else
        checked_entry(replace_span(doc, first, end_pos, replace(value, "\n", newline)^ ++ suffix)^, entries[0].line_first, entries[0].name, value)^
    }
  }
}
// A JSON string keeps otherwise invisible INI control characters editable.
pub fn replace_scalar_literal(doc, first, end_pos, literal) map^ {
  let value = parse(literal, 'json')^
  if (not (value is string)) raise error("enter a quoted JSON string for this value")
  else replace_scalar(doc, first, end_pos, value)^
}
pub fn rename_key(doc, first, end_pos, key) map^ {
  let entries = [for (entry in doc.tree.children where entry.first == first and entry.key_end == end_pos) entry]
  if (len(entries) == 0) raise error("property no longer exists")
  else if (not (if (doc.kind == 'properties') property_key_valid(key) else valid_key_for(key, doc.kind))) raise error("invalid property name")
  else checked_entry(replace_span(doc, first, end_pos, if (doc.kind == 'properties') property_key_text(key)^ else key)^, entries[0].line_first, key, entries[0].node.text)^
}
pub fn insert_child(doc, first, key, value) map^ {
  if (first != 0) { raise error("properties can only be added at the root") }
  else if (not (if (doc.kind == 'properties') property_key_valid(key) else valid_key_for(key, doc.kind)) or
           not (if (doc.kind == 'properties') valid_property_value(value) else valid_ini_value(value))) { raise error("invalid property name or value") }
  else {
    let newline = source_text.trailing_line_ending(doc.text)^
    // A final escaped line ending still belongs to the previous property.
    let tail = if (doc.kind == 'properties' and len(doc.tree.children) > 0) doc.tree.children[last] else null
    let continued = tail != null and tail.terminal_continuation == true
    // A trailing escape at EOF would escape the first appended line ending.
    let dangling = (tail != null and tail.line_last == len(doc.text) and ends_with(doc.text, "\\") and
      trailing_backslashes(doc.text, tail.line_first, len(doc.text))^ % 2 == 1)^
    let prefix = if (dangling) newline ++ newline else if (continued) newline
      else if (doc.text == "" or ends_with(doc.text, "\n") or ends_with(doc.text, "\r")) "" else newline
    let line_first = len(doc.text) + len(prefix)
    let written_key = if (doc.kind == 'properties') property_key_text(key)^ else key
    let written_value = if (doc.kind == 'ini') replace(value, "\n", newline)^ else value
    checked_entry(import_as(doc.text ++ prefix ++ written_key ++ "=" ++ written_value ++ newline, doc.kind)^, line_first, key, value)^
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
  replace_scalar_literal: replace_scalar_literal,
  value_help: "Enter a value. New properties are added to the last section."}
