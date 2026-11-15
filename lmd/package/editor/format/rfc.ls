// RFC content lines retain authored folding, parameters and repeated properties.
import source: lambda.editor.format.source_text

fn token(text) any^ => text != "" and all([for (ch in split(text, "")^) contains("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-.", ch)])
fn record(text: string, first: int) any^ {
  var line = source.natural_line(text, first)^, logical = "", parts = [], part_first = first
  while (true) {
    let piece = source.raw(text, part_first, line.end)
    parts = [*parts, {first: part_first, last: line.end, logical_first: len(logical), logical_last: len(logical) + len(piece)}]
    logical = logical ++ piece
    if ((line.next >= len(text) or not (source.char_at(text, line.next) in [" ", "\t"]))^) { break }
    part_first = line.next + 1
    line = source.natural_line(text, part_first)^
  }
  {first: first, end: line.end, next: line.next, text: logical, parts: parts}
}
fn raw_offset(parts, at, right) any^ {
  let matches = [for (part in parts where at >= part.logical_first and at <= part.logical_last) part]
  if (len(matches) == 0) raise error("content-line offset no longer exists")
  else {
    let part = if (right) matches[last] else matches[0]
    part.first + at - part.logical_first
  }
}
fn content_line(text: string, first: int) any^ {
  let rec = record(text, first)^
  var i = 0, quoted = false, key_end = -1, colon = -1
  while ((i < len(rec.text))^) {
    let ch = source.char_at(rec.text, i)^
    if (ch == "\"") { quoted = not quoted }
    if (not quoted and (ch == ";" or ch == ":") and key_end < 0) { key_end = i }
    if (not quoted and ch == ":") { colon = i; break }
    i = i + 1
  }
  if (colon < 0 or quoted or key_end < 0) { raise error("invalid RFC content line") }
  let property_name = source.raw(rec.text, 0, key_end)
  if (not token(property_name)) { raise error("unsupported content-line name") }
  return {record: rec, name: property_name, key_end: raw_offset(rec.parts, key_end, false)^,
   value_first: raw_offset(rec.parts, colon + 1, true)^, logical_first: colon + 1,
   value: source.raw(rec.text, colon + 1, len(rec.text))}
}
fn component(text, line) any^ {
  let component_name = upper(line.value)
  var i = line.record.next, children = []
  while (i < len(text)) {
    let natural = source.natural_line(text, i)^
    if (natural.end == i) { i = natural.next; continue }
    let next = content_line(text, i)^
    if (upper(next.name) == "END") {
      if (upper(next.value) != component_name) { raise error("mismatched component boundary") }
      return {kind: 'object', first: line.value_first, last: next.record.next,
        row_first: line.record.first, row_last: next.record.next, content_end: next.record.first,
        can_rename: false, children: children}
    }
    if (upper(next.name) == "BEGIN") {
      let child = component(text, next)^
      children = [*children, {name: upper(next.value), first: next.value_first, key_end: next.key_end, node: child}]
      i = child.last
    } else {
      let node = {kind: 'scalar', first: next.value_first, last: next.record.end,
        row_first: i, row_last: next.record.next, record: next.record, logical_first: next.logical_first, text: next.value}
      children = [*children, {name: next.name, first: i, key_end: next.key_end, node: node}]
      i = next.record.next
    }
  }
  raise error("unclosed component")
}
fn import_as(text, kind) map^ {
  let checked = parse(text, kind)^
  if (contains(text, "\u0000")) { raise error("NUL bytes require source mode") }
  var i = 0, children = []
  while (i < len(text)) {
    let natural = source.natural_line(text, i)^
    if (natural.end == i) { i = natural.next; continue }
    let line = content_line(text, i)^
    if (upper(line.name) != "BEGIN") { raise error("property outside a component") }
    let child = component(text, line)^
    children = [*children, {name: upper(line.value), node: child}]
    i = child.last
  }
  return {doc: {text: text, kind: kind, tree: {kind: 'array', first: 0, last: len(text), can_add: false, children: children}}, envelope: null}
}
pub fn import_text(text) map^ => import_as(text, 'ics')^
fn import_vcf(text) map^ => import_as(text, 'vcf')^
fn replace_span(doc, first, end_pos, text) map^ => import_as(source.raw(doc.text, 0, first) ++ text ++ source.raw(doc.text, end_pos, len(doc.text)), doc.kind)^
fn valid_value(text) any^ => not source.has_controls(text, ["\t"])^
pub fn replace_scalar(doc, first, end_pos, value) map^ {
  let node = source.find_node(doc.tree, first)^
  if (node == null or node.kind != 'scalar' or node.last != end_pos) { raise error("property no longer exists") }
  if (not valid_value(value)) { raise error("use RFC escapes for line breaks and control characters") }
  if (value == node.text) { return {doc: doc, envelope: null} }
  let changed = source.changed_span(node.text, value)^
  let prefix = changed.prefix, suffix = changed.suffix
  let begin = raw_offset(node.record.parts, node.logical_first + prefix, true)^
  let changed_end = max(begin, raw_offset(node.record.parts, node.logical_first + len(node.text) - suffix, false)^)
  return replace_span(doc, begin, changed_end, source.raw(value, prefix, len(value) - suffix))^
}
pub fn rename_key(doc, first, end_pos, key) map^ {
  let member = source.find_member(doc.tree, first)^
  if (member == null or member.key_end != end_pos or member.node.can_rename == false) { raise error("property name no longer exists") }
  if (not token(key) or upper(key) in ["BEGIN", "END"]) { raise error("enter a property name without parameters") }
  return if (key == member.name) {doc: doc, envelope: null} else replace_span(doc, first, end_pos, key)^
}
pub fn insert_child(doc, first, key, value) map^ {
  let parent = source.find_node(doc.tree, first)^
  if (parent == null or parent.kind != 'object' or not token(key) or upper(key) in ["BEGIN", "END"] or not valid_value(value)) { raise error("invalid component property") }
  let newline = source.trailing_line_ending(source.raw(doc.text, 0, parent.content_end))^
  return replace_span(doc, parent.content_end, parent.content_end, key ++ ":" ++ value ++ newline)^
}
pub fn delete_child(doc, first) map^ {
  let node = source.find_node(doc.tree, first)^
  if (node == null or first == 0 or node.row_first == null) { raise error("property no longer exists") }
  return replace_span(doc, node.row_first, node.row_last, "")^
}
fn export_text(doc, envelope) => doc.text
fn check_roundtrip(doc, envelope) => null
pub let descriptor = {id: 'ics', name: "iCalendar", suffixes: [".ics", ".ical"], surface: 'property', fallback_source: true,
  import_text: import_text, export_text: export_text, check_roundtrip: check_roundtrip,
  replace_scalar: replace_scalar, rename_key: rename_key, insert_child: insert_child, delete_child: delete_child,
  value_help: "Enter a content-line value. Keep RFC escapes such as \\n for line breaks."}
pub let vcf_descriptor = {*: descriptor, id: 'vcf', name: "vCard", suffixes: [".vcf", ".vcard"], import_text: import_vcf}
