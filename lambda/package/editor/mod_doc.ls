// mod_doc.ls — rich-text document representation for the editing pipeline
// (Radiant Rich Text Editing, Phase R3 — internal model used by Step/Tx).
//
// Document shape (plain maps; independent of Mark elements):
//
//   TextLeaf = { kind: 'text', text: string, marks: [symbol...] }
//   Node     = { kind: 'node', tag: symbol, attrs: [{name, value}...], content: [child...] }
//
// SourcePath / SourcePos semantics:
//   path = chain of `content` indices from the root node.
//   offset on a text leaf  -> byte index in `text`.
//   offset on a non-leaf   -> child index in [0 .. len(content)].

// ---------------------------------------------------------------------------
// Constructors
// ---------------------------------------------------------------------------

pub fn text(s) => {kind: 'text', text: s, marks: []}

pub fn text_marked(s, marks) => {kind: 'text', text: s, marks: marks}

pub fn node(tag, content) =>
  {kind: 'node', tag: tag, attrs: [], content: content}

pub fn node_attrs(tag, attrs, content) =>
  {kind: 'node', tag: tag, attrs: attrs, content: content}

// ---------------------------------------------------------------------------
// Predicates
// ---------------------------------------------------------------------------

pub fn is_text(n)  => type(n) == map and n.kind == 'text'
pub fn is_node(n)  => type(n) == map and n.kind == 'node'

// ---------------------------------------------------------------------------
// Pure list ops
// ---------------------------------------------------------------------------

fn build_at(arr, i, n, acc) {
  if (i >= n) { acc }
  else { build_at(arr, i + 1, n, [*acc, arr[i]]) }
}

pub fn list_take(arr, k) => build_at(arr, 0, k, [])
pub fn list_drop(arr, k) => build_at(arr, k, len(arr), [])

fn concat_at(a, b, i, n, acc) {
  if (i >= n) { acc }
  else { concat_at(a, b, i + 1, n, [*acc, b[i]]) }
}
pub fn list_concat(a, b) => concat_at(a, b, 0, len(b), a)

fn set_at(arr, i, x, j, n, acc) {
  if (j >= n) { acc }
  else if (j == i) { set_at(arr, i, x, j + 1, n, [*acc, x]) }
  else { set_at(arr, i, x, j + 1, n, [*acc, arr[j]]) }
}
pub fn list_set(arr, i, x) => set_at(arr, i, x, 0, len(arr), [])

pub fn list_splice(arr, start, delete_count, items) {
  let head = list_take(arr, start)
  let tail = list_drop(arr, start + delete_count)
  list_concat(list_concat(head, items), tail)
}

// ---------------------------------------------------------------------------
// Tree navigation
// ---------------------------------------------------------------------------

fn at_step(node, path, i) {
  if (i >= len(path)) { node }
  else if (not is_node(node)) { null }
  else if (path[i] >= len(node.content)) { null }
  else { at_step(node.content[path[i]], path, i + 1) }
}
pub fn node_at(doc, path) => at_step(doc, path, 0)

pub fn parent_path(path) =>
  if (len(path) == 0) { [] } else { list_take(path, len(path) - 1) }

pub fn last_index(path) =>
  if (len(path) == 0) { -1 } else { path[len(path) - 1] }

// ---------------------------------------------------------------------------
// Tree updates (immutable)
// ---------------------------------------------------------------------------

pub fn with_content(n, new_content) =>
  {kind: n.kind, tag: n.tag, attrs: n.attrs, content: new_content}

fn replace_at_step(node, path, i, new_node) {
  if (i >= len(path)) { new_node }
  else {
    let idx = path[i]
    let child = node.content[idx]
    let child2 = replace_at_step(child, path, i + 1, new_node)
    with_content(node, list_set(node.content, idx, child2))
  }
}
pub fn replace_node_at(doc, path, new_node) =>
  replace_at_step(doc, path, 0, new_node)

pub fn splice_children_at(doc, parent_path, start, delete_count, items) {
  let parent = node_at(doc, parent_path)
  let new_content = list_splice(parent.content, start, delete_count, items)
  replace_node_at(doc, parent_path, with_content(parent, new_content))
}

// ---------------------------------------------------------------------------
// Text content extraction
// ---------------------------------------------------------------------------

fn concat_strings_at(arr, i, n, acc) {
  if (i >= n) { acc }
  else { concat_strings_at(arr, i + 1, n, acc ++ arr[i]) }
}

pub fn doc_text(node) {
  if (is_text(node)) { node.text }
  else if (is_node(node)) {
    let parts = [for (c in node.content) doc_text(c)]
    concat_strings_at(parts, 0, len(parts), "")
  }
  else { "" }
}
