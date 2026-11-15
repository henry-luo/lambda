// Source-backed configuration fields share flow syntax and guarded mutations.
import source: lambda.editor.format.source_text
import text_policy: lambda.dom.edit_text_policy

pub fn skip_layout(text: string, first: int) int^ {
  var i = first
  while (i < len(text)) {
    if (source.char_at(text, i) == "#") {
      let line = source.natural_line(text, i)^
      i = int(line.next)^
    } else if (source.char_at(text, i) in [" ", "\t", "\r", "\n"]) { i = i + 1 }
    else { break }
  }
  i
}
pub fn key_name(text: string, kind: symbol) any^ {
  if (kind == 'yaml') {
    if (source.char_at(text, 0) in ["*", "&", "!"]) { raise error("YAML references and tags require source mode") }
    let checked = parse("[" ++ text ++ "]", 'yaml')^
    if (len(checked) != 1 or not (checked[0] is string)) { raise error("non-string YAML keys require source mode") }
    if (source.has_controls(checked[0])^) { raise error("control characters in keys require source mode") }
    return checked[0]
  }
  let checked = parse(text ++ "=0", 'toml')^
  let keys = [for (key, value at checked) string(key)]
  if (len(keys) != 1) { raise error("invalid TOML key") }
  if (source.has_controls(keys[0])^) { raise error("control characters in keys require source mode") }
  return if (contains(text, ".") and not (source.char_at(text, 0) in ["\"", "'"])) text else keys[0]
}
pub fn separator(text: string, first: int, end_pos: int, kind: symbol) int^ {
  var i = first, depth = 0
  while (i < end_pos) {
    let ch = source.char_at(text, i)
    if (ch in ["\"", "'"]) { i = source.quoted_end(text, i, kind)^ }
    else if (ch in ["[", "{"]) { depth = depth + 1; i = i + 1 }
    else if (ch in ["]", "}"]) { depth = depth - 1; i = i + 1 }
    else if (depth == 0 and (ch == "=" and kind == 'toml' or ch == ":" and kind == 'yaml' and
      (i + 1 == end_pos or source.char_at(text, i + 1) in [" ", "\t", "\r", "\n"]))) { return i }
    else { i = i + 1 }
  }
  -1
}
pub fn value_node(text: string, first: int, kind: symbol, in_flow = true) any^ {
  let ch = source.char_at(text, first)
  if (ch in ["[", "{"]) { return flow_node(text, first, kind)^ }
  var end_pos = first
  if (ch in ["\"", "'"]) { end_pos = source.quoted_end(text, first, kind)^ }
  else {
    while (end_pos < len(text)) {
      let next = source.char_at(text, end_pos)
      if (next in ["\r", "\n"] or in_flow and next in [",", "]", "}"] or next == "#" and
        (kind == 'toml' or end_pos == first or source.char_at(text, end_pos - 1) in [" ", "\t"])) { break }
      end_pos = end_pos + 1
    }
    end_pos = first + len(trim_end(source.raw(text, first, end_pos)))
  }
  let value = source.raw(text, first, end_pos)
  if (kind == 'yaml' and ch in ["*", "&", "!"]) { raise error("YAML references and tags require source mode") }
  return {kind: 'scalar', first: first, last: end_pos, text: text_policy.sanitize(value, true)^, multiline: contains(value, "\n"), multiline_capable: true}
}
fn flow_node(text: string, first: int, kind: symbol) any^ {
  let is_object = source.char_at(text, first) == "{"
  let close = if (is_object) "}" else "]"
  var i = skip_layout(text, first + 1)^, children = []
  while (i < len(text) and source.char_at(text, i) != close) {
    let entry_first = i
    var key_end = i, field_name = string(len(children))
    if (is_object) {
      while (i < len(text)) {
        let ch = source.char_at(text, i)
        if (ch in ["\"", "'"]) { i = source.quoted_end(text, i, kind)^ }
        else if (ch == (if (kind == 'toml') "=" else ":")) { break }
        else { i = i + 1 }
      }
      if (i == len(text)) { raise error("missing flow property separator") }
      let key_source = trim_end(source.raw(text, entry_first, i))
      key_end = entry_first + len(key_source)
      field_name = key_name(key_source, kind)^
      i = skip_layout(text, i + 1)^
    }
    let node = value_node(text, i, kind)^
    if (node.last == i) { raise error("empty flow value requires source mode") }
    children = [*children, {name: field_name, first: entry_first, key_end: key_end, node: node}]
    let after = skip_layout(text, node.last)^
    let delimiter = source.char_at(text, after)
    i = (if (delimiter == ",") skip_layout(text, after + 1)^
      else if (delimiter == close) after else raise error("invalid flow collection delimiter"))^
  }
  if (source.char_at(text, i) != close) { raise error("unclosed flow collection") }
  return {kind: if (is_object) 'object' else 'array', first: first, last: i + 1, flow: true, children: children}
}
fn validate_value(value: string, kind: symbol) any^ {
  let checked = parse("editor_value" ++ (if (kind == 'toml') "=" else ": ") ++ value, kind)^
  let keys = [for (key, child at checked) string(key)]
  if (len(keys) != 1 or keys[0] != "editor_value") { raise error("enter one configuration value") }
  if (kind == 'yaml' and source.char_at(value, 0) in ["|", ">"]) { return true }
  let node = value_node(value, 0, kind, false)^
  if (source.skip_space(value, node.last) != len(value)) { raise error("value contains neighboring source") }
  return true
}
fn splice_span(doc, first, end_pos, text, importer) map^ => importer(source.raw(doc.text, 0, first) ++ text ++ source.raw(doc.text, end_pos, len(doc.text)))^
pub fn replace_scalar(doc, first, end_pos, value, importer) map^ {
  let node = source.find_node(doc.tree, first)^
  if (node == null or node.kind != 'scalar' or node.last != end_pos) { raise error("configuration value no longer exists") }
  if (value == node.text) { return {doc: doc, envelope: null} }
  let checked = validate_value(value, doc.kind)^
  let separator = if (first == end_pos and source.char_at(doc.text, end_pos)^ == "#" and value != "") " " else ""
  let original = source.raw(doc.text, first, end_pos)^
  let newline = source.trailing_line_ending(source.raw(doc.text, 0, end_pos)^)^
  let written = source.replace_normalized(original, value, newline)^
  let result = splice_span(doc, first, end_pos, written ++ separator, importer)^
  let replaced = source.find_node(result.doc.tree, first)^
  // reparsing must keep the complete replacement inside the selected value.
  if (replaced == null or text_policy.sanitize(source.raw(result.doc.text, replaced.first, replaced.last)^, true)^ != value) {
    raise error("value would change neighboring configuration fields")
  }
  return result
}
pub fn rename_key(doc, first, end_pos, key, importer) map^ {
  let entry = source.find_member(doc.tree, first)^
  if (entry == null or entry.key_end != end_pos or entry.node.can_rename == false) { raise error("configuration name no longer exists") }
  if (key == entry.name) { return {doc: doc, envelope: null} }
  if (source.has_controls(key)^) { raise error("control characters in keys require source mode") }
  return splice_span(doc, first, end_pos, format(key, 'json')^, importer)^
}
pub fn insert_child(doc, first, key, value, importer) map^ {
  let parent = source.find_node(doc.tree, first)^
  if (parent == null or parent.kind == 'scalar' or parent.can_add == false) { raise error("configuration container no longer exists") }
  let checked = validate_value(value, doc.kind)^
  let member = if (parent.kind == 'object') format(key, 'json')^ ++ (if (doc.kind == 'toml') " = " else ": ") ++ value else value
  if (parent.flow == true) {
    let at = if (len(parent.children) == 0) first + 1 else parent.children[last].node.last
    return splice_span(doc, at, at, (if (len(parent.children) == 0) "" else ", ") ++ member, importer)^
  }
  let at = parent.content_end
  let newline = source.trailing_line_ending(source.raw(doc.text, 0, at))^
  let prefix = if (at == 0 or source.char_at(doc.text, at - 1) in ["\n", "\r"]) "" else newline
  let indent = join([for (i in 1 to parent.child_indent) " "], "")
  let written = replace((if (parent.kind == 'array') "- " else "") ++ member, "\n", newline ++ indent)^
  return splice_span(doc, at, at, prefix ++ indent ++ written ++ newline, importer)^
}
pub fn delete_child(doc, first, importer) map^ {
  let parent = source.find_parent(doc.tree, first)^
  if (parent == null) { raise error("configuration value no longer exists") }
  let indexes = [for (i, child in parent.children where child.node.first == first) i]
  let at = indexes[0]
  let entry = parent.children[at]
  if (entry.node.can_delete == false) { raise error("this container requires source mode") }
  if (entry.inline_first == true) {
    // keep the sequence marker; a final deleted field leaves an empty mapping.
    let line = source.natural_line(doc.text, entry.first)^
    let newline = source.raw(doc.text, line.end, line.next)^
    let replacement = (if (len(parent.children) == 1) "{}" else "") ++ newline
    return splice_span(doc, entry.first, entry.node.row_last, replacement, importer)^
  }
  if (parent.flow == true) {
    let begin = if (at == len(parent.children) - 1 and at > 0) parent.children[at - 1].node.last else entry.first
    let end_pos = if (at < len(parent.children) - 1) parent.children[at + 1].first else entry.node.last
    return splice_span(doc, begin, end_pos, "", importer)^
  }
  if (doc.kind == 'yaml' and len(parent.children) == 1) {
    // a block collection must retain its kind when its last child is removed.
    let ending = source.natural_line(doc.text, entry.node.last)^
    let newline = source.raw(doc.text, ending.end, ending.next)^
    let indent = source.raw(doc.text, entry.node.row_first, entry.first)^
    let empty = if (parent.kind == 'object') "{}" else "[]"
    return splice_span(doc, entry.node.row_first, entry.node.row_last, indent ++ empty ++ newline, importer)^
  }
  return splice_span(doc, entry.node.row_first, entry.node.row_last, "", importer)^
}
pub fn export_text(doc, envelope) => doc.text
pub fn check_roundtrip(doc, envelope) => null
