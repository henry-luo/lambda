// mod_query.ls — model-facet command state for toolbar UI.
//
// Lambda DOM Editable §8.3: toolbar active/enabled state is a query over the
// model's schema, selection, and stored marks — never a second UI switch.
// These are pure reads of an editor value; they apply no transaction.

import .mod_doc
import .mod_source_pos
import .mod_step
import .mod_history

// ---------------------------------------------------------------------------
// Text leaves covered by a selection
// ---------------------------------------------------------------------------

fn sel_lo(sel) => pos_min(sel.anchor, sel.head)
fn sel_hi(sel) => pos_max(sel.anchor, sel.head)

// A leaf at `path` contributes when the selected part of it is non-empty.
fn leaf_selected(path, leaf, lo, hi) {
  let after_lo = path_compare(path, lo.path) > 0 or
                 (path_equal(path, lo.path) and lo.offset < len(leaf.text))
  let before_hi = path_compare(path, hi.path) < 0 or
                  (path_equal(path, hi.path) and hi.offset > 0)
  after_lo and before_hi
}

fn collect_leaves(n, path, lo, hi) {
  if (is_text(n)) { if (leaf_selected(path, n, lo, hi)) [n] else [] }
  else if (is_node(n)) {
    [for (i in 0 to len(n.content) - 1)
       for (leaf in collect_leaves(n.content[i], [*path, i], lo, hi)) leaf]
  }
  else []
}

// Every non-empty text leaf the selection spans, in document order.
pub fn selected_leaves(doc, sel) {
  if (sel == null or sel.kind != 'text') []
  else [for (leaf in collect_leaves(doc, [], sel_lo(sel), sel_hi(sel)) where len(leaf.text) > 0) leaf]
}

fn caret_leaf(doc, sel) {
  let leaf = if (sel == null or sel.kind != 'text') null else node_at(doc, sel.head.path)
  if (leaf != null and is_text(leaf)) leaf else null
}

// True when `mark` applies at the selection: every selected leaf carries it,
// or, at a caret, the stored marks (else the caret leaf's marks) do.
pub fn mark_active(editor, mark) {
  let sel = editor.selection
  if (sel == null or sel.kind != 'text') { false }
  else if (pos_equal(sel.anchor, sel.head)) {
    let leaf = caret_leaf(editor.doc, sel)
    let marks = if (editor.stored_marks != null) editor.stored_marks
                else if (leaf != null) leaf.marks else []
    has_mark(marks, mark)
  }
  else {
    let leaves = selected_leaves(editor.doc, sel)
    len(leaves) > 0 and all([for (leaf in leaves) has_mark(leaf.marks, mark)])
  }
}

// ---------------------------------------------------------------------------
// Enclosing nodes
// ---------------------------------------------------------------------------

fn ancestor_tags_at(doc, path, acc) {
  if (len(path) == 0) acc
  else {
    let n = node_at(doc, path)
    let next = if (n != null and is_node(n)) [*acc, n.tag] else acc
    ancestor_tags_at(doc, parent_path(path), next)
  }
}

// Tags enclosing the selection head, innermost first (the head node included).
pub fn enclosing_tags(editor) {
  let sel = editor.selection
  if (sel == null) []
  else if (sel.kind == 'text') ancestor_tags_at(editor.doc, sel.head.path, [])
  else if (sel.kind == 'node') ancestor_tags_at(editor.doc, sel.path, [])
  else []
}

// The innermost enclosing tag that is one of `tags`, or null.
pub fn enclosing_tag_in(editor, tags) {
  let found = [for (tag in enclosing_tags(editor) where contains(tags, tag)) tag]
  if (len(found) == 0) null else found[0]
}

pub fn inside_tag(editor, tag) => contains(enclosing_tags(editor), tag) or false

// ---------------------------------------------------------------------------
// History
// ---------------------------------------------------------------------------

pub fn history_can_undo(editor) => editor.history != null and can_undo(editor.history) or false
pub fn history_can_redo(editor) => editor.history != null and can_redo(editor.history) or false
