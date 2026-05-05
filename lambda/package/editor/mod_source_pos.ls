// mod_source_pos.ls — SourcePath / SourcePos / SourceSelection helpers
// (Radiant Rich Text Editing, Phase R2 — source-side position model)
//
// Position model (mirrors §4.3 of doc/dev/Radiant_Rich_Text_Editing.md):
//
//   SourcePath = [int, ...]                 // child indices from the doc root
//   SourcePos  = { path: SourcePath,        // the containing node
//                  offset: int }            //   text leaf  -> UTF-8 byte offset
//                                           //   non-leaf   -> child index in [0, len]
//   TextSelection = { kind: 'text', anchor: SourcePos, head: SourcePos }
//   NodeSelection = { kind: 'node', path:   SourcePath }
//   AllSelection  = { kind: 'all' }
//
// All operations are pure functional and do not mutate the document.

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

pub fn pos(path, offset) => {path: path, offset: offset}

pub fn text_selection(anchor, head) =>
  {kind: 'text', anchor: anchor, head: head}

pub fn node_selection(path) =>
  {kind: 'node', path: path}

pub fn all_selection() => {kind: 'all'}

// ---------------------------------------------------------------------------
// Path comparison
// ---------------------------------------------------------------------------

// Lexicographic compare of two integer paths.
//   -1  if a < b   (a appears earlier in document order)
//    0  if a == b
//   +1  if a > b
fn cmp_path_at(a, b, i, n) {
  if (i >= n) {
    if (len(a) == len(b)) { 0 }
    else if (len(a) < len(b)) { -1 }
    else { 1 }
  } else if (a[i] < b[i]) { -1 }
  else if (a[i] > b[i]) { 1 }
  else { cmp_path_at(a, b, i + 1, n) }
}

pub fn path_compare(a, b) =>
  cmp_path_at(a, b, 0, if (len(a) < len(b)) len(a) else len(b))

pub fn path_equal(a, b) => path_compare(a, b) == 0

// True iff `prefix` is a (possibly equal) prefix of `path`.
fn is_prefix_at(prefix, path, i) {
  if (i >= len(prefix)) { true }
  else if (i >= len(path)) { false }
  else if (prefix[i] != path[i]) { false }
  else { is_prefix_at(prefix, path, i + 1) }
}

pub fn path_is_prefix(prefix, path) => is_prefix_at(prefix, path, 0)

// ---------------------------------------------------------------------------
// Position comparison
// ---------------------------------------------------------------------------

pub fn pos_compare(a, b) {
  let pc = path_compare(a.path, b.path)
  if (pc != 0) { pc }
  else if (a.offset < b.offset) { -1 }
  else if (a.offset > b.offset) { 1 }
  else { 0 }
}

pub fn pos_equal(a, b) => pos_compare(a, b) == 0

pub fn pos_min(a, b) => if (pos_compare(a, b) <= 0) a else b
pub fn pos_max(a, b) => if (pos_compare(a, b) >= 0) a else b

// ---------------------------------------------------------------------------
// Resolution — walk a path against a document tree
// ---------------------------------------------------------------------------

// Walk `path` from `node`; return the descendant node, or null if the path
// runs off the tree or hits a non-container.
fn resolve_at(node, path, i) {
  if (i >= len(path)) { node }
  else if (type(node) != element and type(node) != array) { null }
  else if (path[i] >= len(node)) { null }
  else { resolve_at(node[path[i]], path, i + 1) }
}

// resolve_pos(doc, pos) -> { node, parent, parent_index, depth, found }
//   node         the node at pos.path
//   parent       the immediate ancestor (null for root)
//   parent_index the child index of `node` within `parent`
//   depth        len(pos.path)
//   found        false if the path was invalid (other fields null)
pub fn resolve_pos(doc, p) {
  let node = resolve_at(doc, p.path, 0)
  if (node == null) {
    {node: null, parent: null, parent_index: -1, depth: len(p.path), found: false}
  } else if (len(p.path) == 0) {
    {node: doc, parent: null, parent_index: -1, depth: 0, found: true}
  } else {
    let parent_path = slice(p.path, 0, len(p.path) - 1)
    let parent = resolve_at(doc, parent_path, 0)
    let pi = p.path[len(p.path) - 1]
    {node: node, parent: parent, parent_index: pi, depth: len(p.path), found: true}
  }
}

// ---------------------------------------------------------------------------
// Selection text extraction
// ---------------------------------------------------------------------------

// Concat all string elements in `arr` (recursive — reduce() is not yet
// available in MIR Direct).
fn concat_at(arr, i, n, acc) {
  if (i >= n) { acc }
  else { concat_at(arr, i + 1, n, acc ++ arr[i]) }
}

pub fn concat_strings(arr) => concat_at(arr, 0, len(arr), "")

// Extract the text content of a node and all its element descendants in
// document order. Used as the building block for selection_to_string when
// the selection spans multiple nodes.
pub fn node_text(node) {
  if (type(node) == string) { node }
  else if (type(node) == element) {
    let parts = [for (c in node) node_text(c)]
    concat_strings(parts)
  }
  else { "" }
}

// Enumerate string leaves in document order, preserving their source paths.
fn text_leaves_at(node, path) {
  if (type(node) == string) { [{path: path, text: node}] }
  else if (type(node) == element or type(node) == array) {
    [for (i in 0 to len(node) - 1) for (leaf in text_leaves_at(node[i], [*path, i])) leaf]
  }
  else { [] }
}

fn text_leaves(doc) => text_leaves_at(doc, [])

fn selection_leaf_text(leaf, lo, hi) {
  if (path_equal(leaf.path, lo.path)) { slice(leaf.text, lo.offset, len(leaf.text)) }
  else if (path_equal(leaf.path, hi.path)) { slice(leaf.text, 0, hi.offset) }
  else { leaf.text }
}

fn selection_parts_at(leaves, lo, hi, i, n, acc) {
  if (i >= n) { acc }
  else {
    let leaf = leaves[i]
    if (path_compare(leaf.path, lo.path) < 0) {
      selection_parts_at(leaves, lo, hi, i + 1, n, acc)
    } else if (path_compare(leaf.path, hi.path) > 0) {
      acc
    } else {
      selection_parts_at(leaves, lo, hi, i + 1, n, [*acc, selection_leaf_text(leaf, lo, hi)])
    }
  }
}

fn selection_text_across_leaves(doc, lo, hi) {
  let leaves = text_leaves(doc)
  concat_strings(selection_parts_at(leaves, lo, hi, 0, len(leaves), []))
}

// Text extraction for a TextSelection. Same-leaf selections return the direct
// substring; cross-leaf selections concatenate all string leaves in document
// order between the endpoints.
pub fn selection_to_string(doc, sel) {
  if (sel.kind == 'all') { node_text(doc) }
  else if (sel.kind == 'node') {
    let r = resolve_pos(doc, pos(sel.path, 0))
    if (not r.found) { "" } else { node_text(r.node) }
  }
  else if (sel.kind == 'text') {
    let lo = pos_min(sel.anchor, sel.head)
    let hi = pos_max(sel.anchor, sel.head)
    if (path_equal(lo.path, hi.path)) {
      let r = resolve_pos(doc, lo)
      if (not r.found or type(r.node) != string) { "" }
      else { slice(r.node, lo.offset, hi.offset) }
    } else { selection_text_across_leaves(doc, lo, hi) }
  }
  else { null }
}
