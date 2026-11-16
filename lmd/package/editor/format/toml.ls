// TOML section headings retain their source while fields use the property tree.
import source: lambda.editor.format.source_text
import config: lambda.editor.format.config

fn header(text: string, first: int) any^ {
  let is_array = source.char_at(text, first + 1) == "["
  let path_start = first + (if (is_array) 2 else 1)
  let key_first = source.skip_horizontal(text, path_start)
  var i = key_first
  while (i < len(text)) {
    let ch = source.char_at(text, i)
    if (ch in ["\"", "'"]) { i = source.quoted_end(text, i, 'toml')^ }
    else if (ch == "]") { break }
    else { i = i + 1 }
  }
  if (i == len(text)) { raise error("unterminated TOML table header") }
  let line = source.natural_line(text, i)^
  let path = trim_end(source.raw(text, key_first, i))
  return {first: key_first, key_end: key_first + len(path), label: path, next: line.next}
}
pub fn import_text(text: string) map^ {
  let checked = parse(text, 'toml')^
  var i = 0, root_fields = [], sections = [], current = {first: 0, row_first: 0, children: []}
  while (i < len(text)) {
    i = config.skip_layout(text, i)^
    if (i == len(text)) { break }
    if (source.char_at(text, i) == "[") {
      if (current.first == 0) { root_fields = current.children }
      else {
        sections = [*sections, {name: current.label, first: current.first, key_end: current.key_end,
          node: {*: current, kind: 'object', last: i, content_end: i, child_indent: 0, table_header: true, can_delete: false}}]
      }
      let heading = header(text, i)^
      current = {*: heading, row_first: i, children: []}
      i = int(heading.next)^
    } else {
      let line = source.natural_line(text, i)^
      let sep = config.separator(text, i, line.end, 'toml')^
      if (sep < 0) { raise error("invalid TOML assignment") }
      let key_source = trim_end(source.raw(text, i, sep))
      let field_name = config.key_name(key_source, 'toml')^
      let value_first = source.skip_space(text, sep + 1)
      let value = config.value_node(text, value_first, 'toml')^
      let ending = source.natural_line(text, value.last)^
      let tail = trim(source.raw(text, value.last, ending.end)^)
      if (tail != "" and not starts_with(tail, "#")) { raise error("unexpected content after TOML value") }
      let node = {*: value, row_first: i, row_last: ending.next}
      current = {*: current, children: [*current.children, {name: field_name, first: i, key_end: i + len(key_source), node: node}]}
      i = int(ending.next)^
    }
  }
  if (current.first == 0) { root_fields = current.children }
  else {
    sections = [*sections, {name: current.label, first: current.first, key_end: current.key_end,
      node: {*: current, kind: 'object', last: len(text), content_end: len(text), child_indent: 0, table_header: true, can_delete: false}}]
  }
  let root_end = if (len(sections) == 0) len(text) else sections[0].node.row_first
  return {doc: {text: text, kind: 'toml', tree: {kind: 'object', first: 0, last: len(text), content_end: root_end,
    child_indent: 0, children: [*root_fields, *sections]}}, envelope: null}
}
pub fn replace_scalar(doc, first, end_pos, value) map^ => config.replace_scalar(doc, first, end_pos, value, import_text)^
// compare decoded path segments so quoted and bare spellings name the same table.
fn path_parts(path: string) any^ {
  var i = 0, parts = []
  while (i < len(path)) {
    i = source.skip_horizontal(path, i)
    let first = i
    if (source.char_at(path, i) in ["\"", "'"]) { i = source.quoted_end(path, i, 'toml')^ }
    else { while (i < len(path) and not (source.char_at(path, i) in [".", " ", "\t"])) { i = i + 1 } }
    if (first == i) { raise error("empty TOML table path") }
    parts = [*parts, {name: config.key_name(source.raw(path, first, i), 'toml')^, end: i}]
    i = source.skip_horizontal(path, i)
    if (i < len(path)) {
      if (source.char_at(path, i) != ".") { raise error("invalid TOML table path") }
      i = i + 1
      if (source.skip_horizontal(path, i) == len(path)) { raise error("empty TOML table path") }
    }
  }
  if (len(parts) == 0) { raise error("empty TOML table path") }
  return parts
}
pub fn rename_key(doc, first, end_pos, key) map^ {
  let entry = source.find_member(doc.tree, first)^
  if (entry == null or entry.node.table_header != true) { return config.rename_key(doc, first, end_pos, key, import_text)^ }
  if (entry.key_end != end_pos) { raise error("table heading no longer exists") }
  let original = path_parts(entry.name)^
  let replacement = path_parts(key)^
  let old_names = [for (part in original) part.name]
  var changed = doc.text
  // update every declaration of the table and its descendants, from right to left.
  let candidates = reverse(doc.tree.children)
  var index = 0
  while ((index < len(candidates))^) {
    let candidate = candidates[index]
    if (candidate.node.table_header == true) {
      let parts = path_parts(candidate.name)^
      let names = [for (part in parts) part.name]
      if ((len(names) >= len(old_names) and take(names, len(old_names)) == old_names)^) {
        let stop = candidate.first + parts[len(old_names) - 1].end
        changed = source.raw(changed, 0, candidate.first)^ ++ key ++ source.raw(changed, stop, len(changed))^
      }
    }
    index = index + 1
  }
  return import_text(changed)^
}
pub fn insert_child(doc, first, key, value) map^ => config.insert_child(doc, first, key, value, import_text)^
pub fn delete_child(doc, first) map^ => config.delete_child(doc, first, import_text)^
pub let descriptor = {id: 'toml', name: "TOML", suffixes: [".toml"], surface: 'property', fallback_source: true,
  import_text: import_text, export_text: config.export_text, check_roundtrip: config.check_roundtrip,
  replace_scalar: replace_scalar, rename_key: rename_key, insert_child: insert_child, delete_child: delete_child,
  value_help: "Enter a TOML value, including quotes around strings. New properties belong to the selected table."}
