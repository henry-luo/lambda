// JSON property projection: validate with Input and retain each authored span.
import source: lambda.editor.format.source_text

fn string_end(text: string, first: int) int^ {
  var i = first + 1
  while (i < len(text)) {
    let ch = source.char_at(text, i)
    if (ch == "\\") { i = i + 2 }
    else if (ch == "\"") { return i + 1 }
    else { i = i + 1 }
  }
  raise error("unterminated JSON string")
}

fn value_node(text: string, first: int) any^ {
  let ch = source.char_at(text, first)
  if (ch == "{" or ch == "[") { return container_node(text, first)^ }
  var end_pos = first
  if (ch == "\"") { end_pos = string_end(text, first)^ }
  else {
    while (end_pos < len(text) and not (contains([" ", "\t", "\r", "\n", ",", "]", "}"], source.char_at(text, end_pos)) ^ { false })) {
      end_pos = end_pos + 1
    }
  }
  return {kind: 'scalar', first: first, last: end_pos, text: source.raw(text, first, end_pos)}
}

fn container_node(text: string, first: int) any^ {
  let is_object = source.char_at(text, first) == "{"
  let close = if (is_object) "}" else "]"
  var i = source.skip_space(text, first + 1), children = []
  while ((i < len(text) and source.char_at(text, i) != close)^) {
    let entry_first = i
    let key_end = if (is_object) string_end(text, i)^ else i
    let key_source = if (is_object) source.raw(text, i, key_end) else ""
    let key = if (is_object) parse(key_source, 'json')^ else string(len(children))
    // Control characters need their JSON spelling in single-line DOM controls.
    let key_literal = is_object and any([for (ch in split(key, "")^) ord(ch) < 32 or ord(ch) == 127])
    let display_key = if (key_literal) replace(key_source, chr(127), "\\u007f")^ else key
    let colon = if (is_object) source.skip_space(text, key_end) else i
    if ((is_object and source.char_at(text, colon) != ":")^) { raise error("invalid JSON object member") }
    let value_first = if (is_object) source.skip_space(text, colon + 1) else i
    let node = value_node(text, value_first)^
    children = [*children, {name: display_key, key_value: key, key_literal: key_literal,
      first: entry_first, key_end: key_end, node: node}]
    let after = source.skip_space(text, node.last)
    let delimiter = source.char_at(text, after)
    i = (if (delimiter == ",") source.skip_space(text, after + 1)
         else if (delimiter == close) after else raise error("invalid JSON container delimiter"))^
  }
  if (source.char_at(text, i) != close) { raise error("unterminated JSON container") }
  else { {kind: if (is_object) 'object' else 'array', first: first, last: i + 1, children: children} }
}

pub fn import_text(text: string) map^ {
  let checked = parse(text, 'json')^
  let first = source.skip_space(text, 0)
  let tree = value_node(text, first)^
  if (source.skip_space(text, tree.last) != len(text)) raise error("unexpected content after JSON value")
  else {doc: {text: text, tree: tree}, envelope: null}
}
fn replace_span(doc, first, end_pos, value) map^ =>
  import_text(source.raw(doc.text, 0, first) ++ value ++ source.raw(doc.text, end_pos, len(doc.text)))^

pub fn replace_scalar(doc, first, end_pos, value) map^ {
  let node = source.find_node(doc.tree, first)^
  if (node == null or node.kind != 'scalar' or node.last != end_pos) raise error("JSON value no longer exists")
  else if (value == node.text) {doc: doc, envelope: null}
  else {
    // Validate the fragment separately so it cannot inject neighboring members.
    let checked = parse(value, 'json')^
    replace_span(doc, first, end_pos, value)^
  }
}

fn find_member(node, first) any^ {
  if (node.kind == 'scalar') null
  else {
    let direct = if (node.kind == 'object') [for (child in node.children where child.first == first) child] else []
    let nested = [for (child in node.children, let found = find_member(child.node, first)^ where found != null) found]
    if (len(direct) > 0) direct[0] else if (len(nested) > 0) nested[0] else null
  }
}
pub fn rename_key(doc, first, end_pos, key) map^ {
  let member = find_member(doc.tree, first)^
  if (member == null or member.key_end != end_pos) raise error("JSON name no longer exists")
  else if (member.key_value == key) {doc: doc, envelope: null}
  else replace_span(doc, first, end_pos, format(key, 'json')^)^
}
pub fn rename_key_literal(doc, first, end_pos, literal) map^ {
  let key = parse(literal, 'json')^
  if (not (key is string)) raise error("enter a quoted JSON string for this name")
  else rename_key(doc, first, end_pos, key)^
}
pub fn insert_child(doc, first, key, value) map^ {
  let parent = source.find_node(doc.tree, first)^
  if (parent == null or parent.kind == 'scalar') raise error("JSON container no longer exists")
  else {
    let checked = parse(value, 'json')^
    let final_index = len(parent.children) - 1
    let at = if (final_index < 0) parent.first + 1 else parent.children[final_index].node.last
    let prefix = if (final_index < 0) "" else ","
    let member = if (parent.kind == 'object') format(key, 'json')^ ++ ":" ++ value else value
    replace_span(doc, at, at, prefix ++ member)^
  }
}
fn find_parent(node, first) any^ {
  if (node.kind == 'scalar') null
  else {
    let direct = [for (child in node.children where child.node.first == first) child]
    if (len(direct) > 0) node
    else {
      let nested = [for (child in node.children, let found = find_parent(child.node, first)^ where found != null) found]
      if (len(nested) > 0) nested[0] else null
    }
  }
}
pub fn delete_child(doc, first) map^ {
  let parent = find_parent(doc.tree, first)^
  if (parent == null) raise error("JSON value no longer exists")
  else {
    let indexes = [for (i, child in parent.children where child.node.first == first) i]
    let index = indexes[0]
    let children = parent.children
    let begin = if (index == len(children) - 1 and index > 0) children[index - 1].node.last else children[index].first
    let end_pos = if (index < len(children) - 1) children[index + 1].first else children[index].node.last
    replace_span(doc, begin, end_pos, "")^
  }
}
fn export_text(doc, envelope) => doc.text
fn check_roundtrip(doc, envelope) => null
pub let descriptor = {id: 'json', name: "JSON", suffixes: [".json"], surface: 'property', fallback_source: true,
  multiline_keys: true,
  import_text: import_text, export_text: export_text, check_roundtrip: check_roundtrip,
  replace_scalar: replace_scalar, rename_key: rename_key, rename_key_literal: rename_key_literal,
  insert_child: insert_child, delete_child: delete_child,
  value_help: "Enter a JSON value, such as a quoted string, number, boolean, null, array or object."}
