// YAML block collections retain indentation, comments and each authored value.
import source: lambda.editor.format.source_text
import config: lambda.editor.format.config
import text_policy: lambda.dom.edit_text_policy

fn records(text: string) any^ {
  var i = 0, result = []
  while (i < len(text)) {
    let line = source.natural_line(text, i)^
    let content_first = source.skip_horizontal(text, i)
    let body = source.raw(text, content_first, line.end)^
    if (body != "" and not starts_with(body, "#")) {
      let boundary = content_first == i and (starts_with(body, "---") or starts_with(body, "...")) and
        source.char_at(body, 3) in ["", " ", "\t"]
      let after_marker = if (boundary) source.skip_horizontal(text, i + 3) else line.end
      let inline_start = after_marker < line.end and source.char_at(text, after_marker) != "#"
      // markers and directives are source records; content scanners may skip
      // them when they occur inside a multiline scalar.
      let marker = boundary and not inline_start
      let directive = content_first == i and starts_with(body, "%")
      let kind = if (marker) (if (starts_with(body, "---")) 'start' else 'end')
        else if (directive) 'directive' else 'content'
      if (inline_start) {
        result = [*result, {first: i, end: i + 3, next: after_marker, content_first: i, indent: 0, record_kind: 'start'},
          {first: after_marker, end: line.end, next: line.next, content_first: after_marker, indent: 0, record_kind: 'content'}]
      } else {
        result = [*result, {*: line, content_first: content_first, indent: content_first - i, record_kind: kind}]
      }
    }
    i = int(line.next)^
  }
  return result
}
fn after_value(lines, at: int, last_pos) int^ {
  var i = at + 1
  while ((i < len(lines) and lines[i].first < last_pos)^) { i = i + 1 }
  i
}
fn block_scalar_end(text: string, first: int, indent: int) int^ {
  var line = source.natural_line(text, first)^, last_pos = int(line.end)^
  var i = int(line.next)^
  while (i < len(text)) {
    line = source.natural_line(text, i)^
    let content_first = source.skip_horizontal(text, i)
    if (content_first < line.end and content_first - i <= indent) { break }
    last_pos = int(line.end)^
    i = int(line.next)^
  }
  return last_pos
}
fn array_marker(text: string, first: int, end_pos: int) bool =>
  source.char_at(text, first) == "-" and (first + 1 == end_pos or source.char_at(text, first + 1) in [" ", "\t"])
fn explicit_key_marker(text: string, first: int, end_pos: int) bool =>
  source.char_at(text, first) == "?" and (first + 1 == end_pos or source.char_at(text, first + 1) in [" ", "\t"])
fn empty_document(first, index) => {name: "Document " ++ string(index + 1),
  node: {kind: 'scalar', first: first, last: first, text: "", empty_doc: true, can_delete: false}}
fn block_node(text: string, lines, at: int, indent: int, first: int, inline_first: int = -1) any^ {
  let initial = at
  let first_content = if (inline_first >= 0) inline_first else int(lines[at].content_first)^
  let is_array = array_marker(text, first_content, lines[at].end)^
  if (not is_array and not explicit_key_marker(text, first_content, lines[at].end)^ and
      config.separator(text, first_content, lines[at].end, 'yaml')^ < 0) {
    let value = config.value_node(text, first_content, 'yaml', false)^
    return {next: after_value(lines, at, value.last)^, node: value}
  }
  var i = at, children = [], last_pos = first
  while (i < len(lines)) {
    var line = lines[i]
    let virtual_first = i == initial and inline_first >= 0
    if (not virtual_first and line.record_kind != 'content') { break }
    if (not virtual_first and line.indent < indent) { break }
    if (not virtual_first and line.indent != indent) { raise error("unexpected YAML indentation") }
    let content_first = if (virtual_first) inline_first else int(line.content_first)^
    let row_first = line.first
    if (array_marker(text, content_first, line.end)^ != is_array) { raise error("mixed YAML block collections require source mode") }
    var value_first = 0, field_name = string(len(children)), key_first = content_first, key_end = content_first, literal_key = false, empty_key = false
    if (is_array) { value_first = source.skip_horizontal(text, content_first + 1) }
    else if (explicit_key_marker(text, content_first, line.end)^) {
      key_first = source.skip_horizontal(text, content_first + 1)
      if (key_first == line.end and i + 1 < len(lines) and
          lines[i + 1].record_kind == 'content' and lines[i + 1].indent > indent) {
        // the key is an indented node; its source span excludes the indicator.
        i = i + 1
        key_first = int(lines[i].content_first)^
        let key_node = block_node(text, lines, i, lines[i].indent, key_first)^
        key_end = key_node.node.last
        i = int(key_node.next)^
      } else {
        let key_node = config.value_node(text, key_first, 'yaml', false)^
        key_end = key_node.last
        i = i + 1
      }
      let key_source = source.raw(text, key_first, key_end)^
      empty_key = key_source == ""
      field_name = if (empty_key) "null" else key_source
      literal_key = true
      if ((i >= len(lines) or lines[i].record_kind != 'content' or lines[i].indent != indent)^)
        { raise error("YAML explicit key has no value indicator") }
      line = lines[i]
      let colon = int(line.content_first)^
      if (source.char_at(text, colon) != ":" or not (source.char_at(text, colon + 1) in ["", " ", "\t", "\r", "\n"]))
        { raise error("YAML explicit key has no value indicator") }
      value_first = source.skip_horizontal(text, colon + 1)
    }
    else {
      let sep = config.separator(text, content_first, line.end, 'yaml')^
      if (sep < 0) { raise error("unsupported YAML block key") }
      let key_source = trim_end(source.raw(text, content_first, sep))
      field_name = config.key_name(key_source, 'yaml')^
      literal_key = config.yaml_key_is_literal(key_source)^
      key_end = content_first + len(key_source)
      value_first = source.skip_horizontal(text, sep + 1)
    }
    var next = i + 1, node = null
    let content_after_properties = config.yaml_properties_end(text, value_first)
    if ((content_after_properties == line.end or source.char_at(text, content_after_properties) == "#") and
        next < len(lines) and lines[next].record_kind == 'content' and lines[next].indent > indent) {
      let nested = block_node(text, lines, next, lines[next].indent, value_first)^
      node = nested.node
      next = int(nested.next)^
    } else if (is_array and config.separator(text, value_first, line.end, 'yaml')^ >= 0) {
      let nested = block_node(text, lines, i, indent + 2, value_first, value_first)^
      node = nested.node
      next = int(nested.next)^
    } else {
      node = config.value_node(text, value_first, 'yaml', false)^
      let ch = source.char_at(text, content_after_properties)
      if (ch in ["|", ">"]) {
        let end_pos = block_scalar_end(text, content_after_properties, indent)^
        node = {*: node, last: end_pos, text: text_policy.sanitize(source.raw(text, value_first, end_pos), true)^, multiline: true, block_scalar: true}
      }
      next = after_value(lines, i, node.last)^
      let ending = source.natural_line(text, node.last)^
      let tail = trim(source.raw(text, node.last, ending.end)^)
      if (tail != "" and not starts_with(tail, "#")) { raise error("unsupported content after YAML value") }
    }
    let ending = source.natural_line(text, node.last)^
    last_pos = int(node.last)^
    node = {*: node, row_first: row_first, row_last: ending.next}
    children = [*children, {name: field_name, first: key_first, key_end: key_end, literal_key: literal_key, empty_key: empty_key, inline_first: virtual_first, node: node}]
    i = next
  }
  let content_end = if (i < len(lines)) lines[i].first else len(text)
  return {next: i, node: {kind: if (is_array) 'array' else 'object', first: first, last: last_pos,
    child_indent: indent, content_end: content_end, children: children}}
}
pub fn import_text(text: string) map^ {
  let checked = parse(text, 'yaml')^
  let lines = records(text)^
  if (len(lines) == 0) {
    return {doc: {text: text, kind: 'yaml', tree: {kind: 'object', first: 0, last: len(text), child_indent: 0, content_end: len(text), children: []}}, envelope: null}
  }
  var i = 0, documents = [], pending = null
  while (i < len(lines)) {
    let line = lines[i]
    if (line.record_kind == 'start') {
      if (pending != null) { documents = [*documents, empty_document(pending, len(documents))] }
      pending = line.next
      i = i + 1
      continue
    }
    if (line.record_kind == 'end') {
      if (pending != null) { documents = [*documents, empty_document(pending, len(documents))] }
      pending = null
      i = i + 1
      continue
    }
    if (line.record_kind == 'directive') { i = i + 1; continue }
    let first = int(line.content_first)^
    var tree = null, next = i + 1
    if (array_marker(text, first, line.end)^ or explicit_key_marker(text, first, line.end)^ or
        config.separator(text, first, line.end, 'yaml')^ >= 0) {
      let result = block_node(text, lines, i, line.indent, first)^
      tree = result.node
      next = int(result.next)^
    } else {
      tree = config.value_node(text, first, 'yaml', false)^
      next = after_value(lines, i, tree.last)^
    }
    if ((next < len(lines) and lines[next].record_kind == 'content')^) {
      raise error("unsupported YAML scalar continuation")
    }
    documents = [*documents, {name: "Document " ++ string(len(documents) + 1),
      node: {*: tree, can_delete: false}}]
    pending = null
    i = next
  }
  if (pending != null) { documents = [*documents, empty_document(pending, len(documents))] }
  if (len(documents) == 0) { raise error("empty YAML document markers require source mode") }
  let tree = if (len(documents) == 1 and documents[0].node.kind != 'scalar' and documents[0].node.flow != true) {*: documents[0].node, first: 0}
    else if (len(documents) == 1) documents[0].node
    else {kind: 'array', first: -1, last: len(text), can_add: false, children: documents}
  return {doc: {text: text, kind: 'yaml', tree: tree}, envelope: null}
}
pub fn replace_scalar(doc, first, end_pos, value) map^ => config.replace_scalar(doc, first, end_pos, value, import_text)^
pub fn rename_key(doc, first, end_pos, key) map^ => config.rename_key(doc, first, end_pos, key, import_text)^
pub fn insert_child(doc, first, key, value) map^ => config.insert_child(doc, first, key, value, import_text)^
pub fn delete_child(doc, first) map^ => config.delete_child(doc, first, import_text)^
pub let descriptor = {id: 'yaml', name: "YAML", suffixes: [".yaml", ".yml"], surface: 'property', fallback_source: true,
  import_text: import_text, export_text: config.export_text, check_roundtrip: config.check_roundtrip,
  replace_scalar: replace_scalar, rename_key: rename_key, insert_child: insert_child, delete_child: delete_child,
  value_help: "Enter a YAML value. Use quotes to distinguish strings from numbers, booleans and null."}
