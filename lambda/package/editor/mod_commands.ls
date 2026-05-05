// mod_commands.ls — high-level editing commands (Phase R4, Lambda side).
//
// A command is a pure function (state, ...args) -> Transaction | null.
//
//   state = {doc, selection}      (selection may be null for whole-doc cmds)
//
// Commands return a Transaction that the caller can either dispatch
// (mutating an EditorState) or inspect. They never mutate `state`.
//
// The goals for this layer mirror ProseMirror's `commands` module:
//   * one place per intent,
//   * selection-aware,
//   * composable (return null when the command is not applicable so callers
//     can chain alternatives).

import .mod_doc
import .mod_step
import .mod_transaction
import .mod_source_pos
import .mod_html_paste
import .mod_paste
import .mod_md_schema

// ---------------------------------------------------------------------------
// Selection helpers
// ---------------------------------------------------------------------------

fn sel_collapsed(sel) =>
  sel.kind == 'text' and pos_equal(sel.anchor, sel.head)

fn sel_lo(sel) => pos_min(sel.anchor, sel.head)
fn sel_hi(sel) => pos_max(sel.anchor, sel.head)

// Build a collapsed text selection at `p`.
fn caret(p) => text_selection(p, p)

// True iff `sel` is a text selection both endpoints of which lie in the same
// text leaf. Most commands here only handle this simple case; cross-leaf
// editing is deferred to the same layer that handles HTML paste.
fn sel_single_leaf(sel) =>
  sel.kind == 'text' and path_equal(sel.anchor.path, sel.head.path)

// ---------------------------------------------------------------------------
// cmd_insert_text — replace the current selection with `txt`
// ---------------------------------------------------------------------------
//
// Returns a Transaction containing one `replace_text` step and a collapsed
// caret immediately after the inserted text. Returns null if the selection
// straddles multiple leaves (caller should split it first).

pub fn cmd_insert_text(state, txt) {
  let sel = state.selection
  if (sel == null) { null }
  else if (not sel_single_leaf(sel)) { null }
  else {
    let lo = sel_lo(sel)
    let hi = sel_hi(sel)
    let stored_marks = state.stored_marks
    let leaf = node_at(state.doc, lo.path)
    if (stored_marks != null and len(txt) > 0 and leaf != null and is_text(leaf) and len(lo.path) > 0) {
      let leaf_parent = parent_path(lo.path)
      let leaf_index = last_index(lo.path)
      let before = nonempty_text_leaf(slice(leaf.text, 0, lo.offset), leaf.marks)
      let inserted = text_marked(txt, stored_marks)
      let after = nonempty_text_leaf(slice(leaf.text, hi.offset, len(leaf.text)), leaf.marks)
      let slice_nodes = list_concat(list_concat(before, [inserted]), after)
      let tx0 = tx_begin(state.doc, sel)
      let tx1 = tx_step(tx0, step_replace(leaf_parent, leaf_index, leaf_index + 1, slice_nodes))
      tx_set_selection(tx1, caret(pos([*leaf_parent, leaf_index + len(before)], len(txt))))
    } else {
      let step = step_replace_text(lo.path, lo.offset, hi.offset, txt)
      let tx0  = tx_begin(state.doc, sel)
      let tx1  = tx_step(tx0, step)
      let new_caret = caret(pos(lo.path, lo.offset + len(txt)))
      tx_set_selection(tx1, new_caret)
    }
  }
}

pub fn cmd_paste_text(state, txt) => cmd_insert_text(state, txt)

fn nonempty_text_leaf(s, marks) =>
  if (len(s) == 0) { [] } else { [text_marked(s, marks)] }

fn last_text_offset_in(node) {
  if (is_text(node)) { len(node.text) }
  else if (not is_node(node) or len(node.content) == 0) { 0 }
  else { last_text_offset_in(node.content[len(node.content) - 1]) }
}

pub fn cmd_paste_fragment(state, fragment) {
  let sel = state.selection
  if (sel == null or not sel_single_leaf(sel)) { null }
  else {
    let lo = sel_lo(sel)
    let hi = sel_hi(sel)
    let leaf = node_at(state.doc, lo.path)
    if (leaf == null or not is_text(leaf) or len(lo.path) < 2) { null }
    else {
      let block_path = parent_path(lo.path)
      let block = node_at(state.doc, block_path)
      if (block == null or not is_node(block)) { null }
      else {
        let blocks = coerce_for_md_block(fragment)
        if (len(blocks) == 0) { null }
        else if (len(blocks) == 1 and blocks[0].tag == 'paragraph') {
          let before = nonempty_text_leaf(slice(leaf.text, 0, lo.offset), leaf.marks)
          let after = nonempty_text_leaf(slice(leaf.text, hi.offset, len(leaf.text)), leaf.marks)
          let new_block = node_attrs(block.tag, block.attrs, list_concat(list_concat(before, blocks[0].content), after))
          let tx0 = tx_begin(state.doc, sel)
          let tx1 = tx_step(tx0, step_replace(parent_path(block_path), last_index(block_path), last_index(block_path) + 1, [new_block]))
          let caret_path = [*block_path, len(before) + len(blocks[0].content) - 1]
          let caret_off = if (len(blocks[0].content) == 0) { 0 } else { last_text_offset_in(blocks[0].content[len(blocks[0].content) - 1]) }
          tx_set_selection(tx1, caret(pos(caret_path, caret_off)))
        } else { cmd_paste_text(state, doc_text(node('fragment', blocks))) }
      }
    }
  }
}

pub fn cmd_paste_html(state, html, fallback_text) =>
  if (type(html) == string) {
    let parsed = parse_html_fragment(html)
    if (parsed == null) { cmd_paste_text(state, fallback_text) }
    else { cmd_paste_fragment(state, html_to_editor_fragment(parsed)) }
  }
  else { cmd_paste_fragment(state, html_to_editor_fragment(html)) }

// ---------------------------------------------------------------------------
// cmd_insert_at / cmd_move_node — structural drop commands
// ---------------------------------------------------------------------------

fn valid_insert_index(parent, index) =>
  parent != null and is_node(parent) and index >= 0 and index <= len(parent.content)

fn insert_selection(parent_path, index, count) =>
  if (count == 1) { node_selection([*parent_path, index]) }
  else { text_selection(pos(parent_path, index + count), pos(parent_path, index + count)) }

pub fn cmd_insert_at(state, target_parent_path, target_index, slice) {
  let parent = node_at(state.doc, target_parent_path)
  if (not valid_insert_index(parent, target_index)) { null }
  else if (len(slice) == 0) { null }
  else {
    let tx0 = tx_begin(state.doc, state.selection)
    let tx1 = tx_step(tx0, step_replace(target_parent_path, target_index, target_index, slice))
    tx_set_selection(tx1, insert_selection(target_parent_path, target_index, len(slice)))
  }
}

fn same_parent_move_is_noop(source_index, target_index) =>
  target_index == source_index or target_index == source_index + 1

fn adjusted_drop_index(source_parent_path, source_index, target_parent_path, target_index) =>
  if (path_equal(source_parent_path, target_parent_path) and source_index < target_index) { target_index - 1 }
  else { target_index }

pub fn cmd_move_node(state, source_path, target_parent_path, target_index) {
  if (len(source_path) == 0) { null }
  else if (path_is_prefix(source_path, target_parent_path)) { null }
  else {
    let source_parent_path = parent_path(source_path)
    let source_index = last_index(source_path)
    let source_parent = node_at(state.doc, source_parent_path)
    let target_parent = node_at(state.doc, target_parent_path)
    let moved = node_at(state.doc, source_path)
    if (moved == null or not valid_insert_index(source_parent, source_index) or source_index >= len(source_parent.content)) { null }
    else if (not valid_insert_index(target_parent, target_index)) { null }
    else if (path_equal(source_parent_path, target_parent_path) and same_parent_move_is_noop(source_index, target_index)) { null }
    else {
      let insert_index = adjusted_drop_index(source_parent_path, source_index, target_parent_path, target_index)
      let tx0 = tx_begin(state.doc, state.selection)
      let tx1 = tx_step(tx0, step_replace(source_parent_path, source_index, source_index + 1, []))
      let tx2 = tx_step(tx1, step_replace(target_parent_path, insert_index, insert_index, [moved]))
      tx_set_selection(tx2, node_selection([*target_parent_path, insert_index]))
    }
  }
}

// ---------------------------------------------------------------------------
// cmd_delete_backward / cmd_delete_forward
// ---------------------------------------------------------------------------
//
// If the selection is non-empty, delete it.  Otherwise delete one character
// to the left/right of the caret within the same text leaf. At block
// boundaries, merge with the adjacent sibling block when the caret is in the
// edge text child.

fn delete_range(state, lo, hi) {
  let step = step_replace_text(lo.path, lo.offset, hi.offset, "")
  let tx0  = tx_begin(state.doc, state.selection)
  let tx1  = tx_step(tx0, step)
  tx_set_selection(tx1, caret(lo))
}

fn last_text_pos_at(n, path) {
  if (is_text(n)) { pos(path, len(n.text)) }
  else if (not is_node(n) or len(n.content) == 0) { null }
  else { last_text_pos_at(n.content[len(n.content) - 1], [*path, len(n.content) - 1]) }
}

fn block_merge_allowed(a, b) =>
  is_node(a) and is_node(b) and a.tag == b.tag

fn merge_blocks_backward(state, p) {
  if (len(p.path) < 2 or last_index(p.path) != 0) { null }
  else {
    let block_path = parent_path(p.path)
    let block_index = last_index(block_path)
    let parent = node_at(state.doc, parent_path(block_path))
    let block = node_at(state.doc, block_path)
    if (parent == null or block == null or not is_node(parent) or not is_node(block) or block_index <= 0) { null }
    else {
      let prev = parent.content[block_index - 1]
      if (not block_merge_allowed(prev, block)) { null }
      else {
        let caret_pos = last_text_pos_at(prev, [*parent_path(block_path), block_index - 1])
        if (caret_pos == null) { null }
        else {
          let merged = node_attrs(prev.tag, prev.attrs, list_concat(prev.content, block.content))
          let tx0 = tx_begin(state.doc, state.selection)
          let tx1 = tx_step(tx0, step_replace(parent_path(block_path), block_index - 1, block_index + 1, [merged]))
          tx_set_selection(tx1, caret(caret_pos))
        }
      }
    }
  }
}

fn merge_blocks_forward(state, p) {
  let block_path = parent_path(p.path)
  let block = node_at(state.doc, block_path)
  if (len(p.path) < 2 or block == null or not is_node(block) or last_index(p.path) != len(block.content) - 1) { null }
  else {
    let block_index = last_index(block_path)
    let parent = node_at(state.doc, parent_path(block_path))
    if (parent == null or block == null or not is_node(parent) or not is_node(block) or block_index + 1 >= len(parent.content)) { null }
    else {
      let next = parent.content[block_index + 1]
      if (not block_merge_allowed(block, next)) { null }
      else {
        let merged = node_attrs(block.tag, block.attrs, list_concat(block.content, next.content))
        let tx0 = tx_begin(state.doc, state.selection)
        let tx1 = tx_step(tx0, step_replace(parent_path(block_path), block_index, block_index + 2, [merged]))
        tx_set_selection(tx1, caret(p))
      }
    }
  }
}

pub fn cmd_delete_backward(state) {
  let sel = state.selection
  if (sel == null) { null }
  else if (not sel_single_leaf(sel)) { null }
  else if (not sel_collapsed(sel)) { delete_range(state, sel_lo(sel), sel_hi(sel)) }
  else {
    let p = sel.anchor
    if (p.offset <= 0) { merge_blocks_backward(state, p) }
    else { delete_range(state, pos(p.path, p.offset - 1), p) }
  }
}

pub fn cmd_delete_forward(state) {
  let sel = state.selection
  if (sel == null) { null }
  else if (not sel_single_leaf(sel)) { null }
  else if (not sel_collapsed(sel)) { delete_range(state, sel_lo(sel), sel_hi(sel)) }
  else {
    let p = sel.anchor
    let leaf = node_at(state.doc, p.path)
    if (leaf == null or not is_text(leaf)) { null }
    else if (p.offset >= len(leaf.text)) { merge_blocks_forward(state, p) }
    else { delete_range(state, p, pos(p.path, p.offset + 1)) }
  }
}

// ---------------------------------------------------------------------------
// cmd_delete_word_backward / cmd_insert_line_break
// ---------------------------------------------------------------------------

fn is_space_char(ch) => ch == " " or ch == "\n" or ch == "\t" or ch == "\r"

fn skip_space_left(text, i) {
  if (i <= 0) { 0 }
  else if (is_space_char(slice(text, i - 1, i))) { skip_space_left(text, i - 1) }
  else { i }
}

fn scan_word_left(text, i) {
  if (i <= 0) { 0 }
  else if (is_space_char(slice(text, i - 1, i))) { i }
  else { scan_word_left(text, i - 1) }
}

fn word_start_left(text, i) => scan_word_left(text, skip_space_left(text, i))

pub fn cmd_delete_word_backward(state) {
  let sel = state.selection
  if (sel == null) { null }
  else if (not sel_single_leaf(sel)) { null }
  else if (not sel_collapsed(sel)) { delete_range(state, sel_lo(sel), sel_hi(sel)) }
  else {
    let p = sel.anchor
    let leaf = node_at(state.doc, p.path)
    if (leaf == null or not is_text(leaf) or p.offset <= 0) { null }
    else {
      let start = word_start_left(leaf.text, p.offset)
      if (start == p.offset) { null }
      else { delete_range(state, pos(p.path, start), p) }
    }
  }
}

pub fn cmd_insert_line_break(state) {
  let sel = state.selection
  if (sel == null or not sel_single_leaf(sel)) { null }
  else {
    let lo = sel_lo(sel)
    let hi = sel_hi(sel)
    let leaf_path = lo.path
    let leaf = node_at(state.doc, leaf_path)
    if (leaf == null or not is_text(leaf) or len(leaf_path) < 2) { null }
    else {
      let block_path = parent_path(leaf_path)
      let leaf_index = last_index(leaf_path)
      let before = nonempty_text_leaf(slice(leaf.text, 0, lo.offset), leaf.marks)
      let after = text_marked(slice(leaf.text, hi.offset, len(leaf.text)), leaf.marks)
      let slice_nodes = [*before, node('hard_break', []), after]
      let tx0 = tx_begin(state.doc, sel)
      let tx1 = tx_step(tx0, step_replace(block_path, leaf_index, leaf_index + 1, slice_nodes))
      tx_set_selection(tx1, caret(pos([*block_path, leaf_index + len(before) + 1], 0)))
    }
  }
}

// ---------------------------------------------------------------------------
// cmd_indent_list_item / cmd_outdent_list_item
// ---------------------------------------------------------------------------

fn ancestor_tag_at(doc, path, tag, depth) {
  if (depth < 0) { null }
  else {
    let p = list_take(path, depth)
    let n = node_at(doc, p)
    if (n != null and is_node(n) and n.tag == tag) { p }
    else { ancestor_tag_at(doc, path, tag, depth - 1) }
  }
}

fn ancestor_tag(doc, path, tag) => ancestor_tag_at(doc, path, tag, len(path))

fn same_list_kind(a, b) => is_node(a) and is_node(b) and a.tag == 'list' and b.tag == 'list'

fn append_to_sublist(prev_item, list_node, item) {
  let kids = prev_item.content
  if (len(kids) > 0 and same_list_kind(kids[len(kids) - 1], list_node)) {
    let sub = kids[len(kids) - 1]
    let sub2 = with_content(sub, [*sub.content, item])
    node_attrs(prev_item.tag, prev_item.attrs, list_set(kids, len(kids) - 1, sub2))
  } else {
    node_attrs(prev_item.tag, prev_item.attrs, [*kids, node_attrs('list', list_node.attrs, [item])])
  }
}

pub fn cmd_indent_list_item(state) {
  let sel = state.selection
  if (sel == null) { null }
  else {
    let base_path = if (sel.kind == 'node') { sel.path } else { sel.anchor.path }
    let item_path = ancestor_tag(state.doc, base_path, 'list_item')
    if (item_path == null) { null }
    else {
      let list_path = parent_path(item_path)
      let item_index = last_index(item_path)
      let list_node = node_at(state.doc, list_path)
      if (list_node == null or not is_node(list_node) or list_node.tag != 'list' or item_index <= 0) { null }
      else {
        let item = list_node.content[item_index]
        let prev_item = list_node.content[item_index - 1]
        let prev2 = append_to_sublist(prev_item, list_node, item)
        let new_items = list_splice(list_set(list_node.content, item_index - 1, prev2), item_index, 1, [])
        let list2 = node_attrs(list_node.tag, list_node.attrs, new_items)
        let tx0 = tx_begin(state.doc, sel)
        let tx1 = tx_step(tx0, step_replace(parent_path(list_path), last_index(list_path), last_index(list_path) + 1, [list2]))
        tx_set_selection(tx1, node_selection([*list_path, item_index - 1]))
      }
    }
  }
}

fn replace_or_remove_sublist(parent_item, sub_index, sublist) {
  if (len(sublist.content) == 0) { list_splice(parent_item.content, sub_index, 1, []) }
  else { list_set(parent_item.content, sub_index, sublist) }
}

pub fn cmd_outdent_list_item(state) {
  let sel = state.selection
  if (sel == null) { null }
  else {
    let base_path = if (sel.kind == 'node') { sel.path } else { sel.anchor.path }
    let item_path = ancestor_tag(state.doc, base_path, 'list_item')
    if (item_path == null) { null }
    else {
      let list_path = parent_path(item_path)
      let item_index = last_index(item_path)
      let parent_item_path = parent_path(list_path)
      let grand_list_path = parent_path(parent_item_path)
      let parent_item_index = last_index(parent_item_path)
      let list_child_index = last_index(list_path)
      let list_node = node_at(state.doc, list_path)
      let parent_item = node_at(state.doc, parent_item_path)
      let grand_list = node_at(state.doc, grand_list_path)
      if (list_node == null or parent_item == null or grand_list == null or
          not is_node(list_node) or not is_node(parent_item) or not is_node(grand_list) or
          list_node.tag != 'list' or parent_item.tag != 'list_item' or grand_list.tag != 'list') { null }
      else {
        let item = list_node.content[item_index]
        let list2 = with_content(list_node, list_splice(list_node.content, item_index, 1, []))
        let parent2 = node_attrs(parent_item.tag, parent_item.attrs, replace_or_remove_sublist(parent_item, list_child_index, list2))
        let tx0 = tx_begin(state.doc, sel)
        let tx1 = tx_step(tx0, step_replace(grand_list_path, parent_item_index, parent_item_index + 1, [parent2, item]))
        tx_set_selection(tx1, node_selection([*grand_list_path, parent_item_index + 1]))
      }
    }
  }
}

// ---------------------------------------------------------------------------
// cmd_toggle_mark — add/remove a mark or toggle a stored mark at a caret
// ---------------------------------------------------------------------------
//
// If the text selection is collapsed, update stored marks so the next inserted
// text carries the mark without rewriting existing content. Otherwise, if the
// addressed leaf already has the mark, remove it; if not, add it. Selection is
// preserved.
//
// (Multi-leaf selections will gain a true range-based add/remove pair once
// SourcePos resolution across leaves is wired through; this scaffolding holds.)

fn state_marks_at(state, path) {
  if (state.stored_marks != null) { state.stored_marks }
  else {
    let leaf = node_at(state.doc, path)
    if (leaf == null or not is_text(leaf)) { [] } else { leaf.marks }
  }
}

pub fn cmd_toggle_stored_mark(state, mark) {
  let sel = state.selection
  if (sel == null or not sel_collapsed(sel)) { null }
  else {
    let base = state_marks_at(state, sel.anchor.path)
    let next = if (has_mark(base, mark)) { without_mark(base, mark) } else { with_mark(base, mark) }
    tx_set_meta(tx_set_meta(tx_begin(state.doc, sel), "storedMarks", next), "addToHistory", false)
  }
}

pub fn cmd_toggle_mark(state, mark) {
  let sel = state.selection
  if (sel == null) { null }
  else if (not sel_single_leaf(sel)) { null }
  else if (sel_collapsed(sel)) { cmd_toggle_stored_mark(state, mark) }
  else {
    let leaf = node_at(state.doc, sel.anchor.path)
    if (leaf == null or not is_text(leaf)) { null }
    else {
      let step = if (has_mark(leaf.marks, mark)) {
        step_remove_mark(sel.anchor.path, mark)
      } else {
        step_add_mark(sel.anchor.path, mark)
      }
      let tx0 = tx_begin(state.doc, sel)
      tx_step(tx0, step)
    }
  }
}

// ---------------------------------------------------------------------------
// cmd_replace_all — find/replace as source-tree transactions
// ---------------------------------------------------------------------------
//
// Replaces every non-overlapping occurrence of `needle` in text leaves with
// `replacement`. Each changed leaf becomes one replace_text step, keeping the
// edit invertible and mappable through the normal transaction machinery.

fn replace_all_text_at(txt, needle, replacement, i, n, acc) {
  if (len(needle) == 0) { txt }
  else if (i > n - len(needle)) { acc ++ slice(txt, i, n) }
  else {
    let chunk = slice(txt, i, i + len(needle))
    if (chunk == needle) {
      replace_all_text_at(txt, needle, replacement, i + len(needle), n, acc ++ replacement)
    } else {
      replace_all_text_at(txt, needle, replacement, i + 1, n, acc ++ slice(txt, i, i + 1))
    }
  }
}

fn replace_all_text(txt, needle, replacement) =>
  replace_all_text_at(txt, needle, replacement, 0, len(txt), "")

fn collect_replace_steps_children(children, path, needle, replacement, i, n, acc) {
  if (i >= n) { acc }
  else {
    let child_steps = collect_replace_steps(children[i], [*path, i], needle, replacement, acc)
    collect_replace_steps_children(children, path, needle, replacement, i + 1, n, child_steps)
  }
}

fn collect_replace_steps(n, path, needle, replacement, acc) {
  if (is_text(n)) {
    let next = replace_all_text(n.text, needle, replacement)
    if (next == n.text) { acc }
    else { [*acc, step_replace_text(path, 0, len(n.text), next)] }
  }
  else if (is_node(n)) { collect_replace_steps_children(n.content, path, needle, replacement, 0, len(n.content), acc) }
  else { acc }
}

fn tx_apply_steps(tx, steps, i, n) {
  if (i >= n) { tx }
  else { tx_apply_steps(tx_step(tx, steps[i]), steps, i + 1, n) }
}

pub fn cmd_replace_all(state, needle, replacement) {
  if (needle == null or len(needle) == 0) { null }
  else {
    let steps = collect_replace_steps(state.doc, [], needle, replacement, [])
    if (len(steps) == 0) { null }
    else { tx_apply_steps(tx_begin(state.doc, state.selection), steps, 0, len(steps)) }
  }
}

pub fn cmd_format_bold(state) {
  let mark = 'strong'
  cmd_toggle_mark(state, mark)
}

pub fn cmd_format_italic(state) {
  let mark = 'em'
  cmd_toggle_mark(state, mark)
}

pub fn cmd_format_underline(state) {
  let mark = 'underline'
  cmd_toggle_mark(state, mark)
}

// ---------------------------------------------------------------------------
// cmd_set_block_type — re-tag the immediate block ancestor of the caret
// ---------------------------------------------------------------------------
//
// `state.selection` must address a position inside (at least) one node — the
// command targets the parent path of the caret leaf. Use this for
// "Heading 1", "Paragraph", etc.

pub fn cmd_set_block_type(state, tag) {
  let sel = state.selection
  if (sel == null) { null }
  else if (not sel_single_leaf(sel)) { null }
  else {
    let block_path = parent_path(sel.anchor.path)
    let n = node_at(state.doc, block_path)
    if (n == null or not is_node(n)) { null }
    else {
      let step = step_set_node_type(block_path, tag)
      let tx0  = tx_begin(state.doc, sel)
      tx_step(tx0, step)
    }
  }
}

// ---------------------------------------------------------------------------
// cmd_split_block — split the block containing the caret into two siblings
// ---------------------------------------------------------------------------
//
// Pre-condition: caret is collapsed inside a text leaf whose parent block
// holds exactly that one leaf (the common Markdown paragraph case). The
// command emits a single `replace` step on the grand-parent that swaps one
// child for two, plus a caret pointing at offset 0 of the new block's leaf.

fn state_default_block(state) =>
  if (state.default_block == null) md_default_block else state.default_block

fn split_right_tag(state, block, cut, text_len) {
  let default_block = state_default_block(state)
  if (cut == text_len and block.tag != default_block) { default_block } else { block.tag }
}

fn split_right_attrs(block, right_tag) =>
  if (right_tag == block.tag) { block.attrs } else { [] }

pub fn cmd_split_block(state) {
  let sel = state.selection
  if (sel == null or not sel_collapsed(sel)) { null }
  else if (not sel_single_leaf(sel)) { null }
  else {
    let leaf_path = sel.anchor.path
    if (len(leaf_path) < 2) { null }
    else {
      let block_path = parent_path(leaf_path)
      let block = node_at(state.doc, block_path)
      if (block == null or not is_node(block) or len(block.content) != 1) { null }
      else {
        let leaf = block.content[0]
        if (not is_text(leaf)) { null }
        else {
          let cut = sel.anchor.offset
          let left_text  = slice(leaf.text, 0, cut)
          let right_text = slice(leaf.text, cut, len(leaf.text))
          let right_tag = split_right_tag(state, block, cut, len(leaf.text))
          let left_block  = node_attrs(block.tag, block.attrs, [text_marked(left_text,  leaf.marks)])
          let right_block = node_attrs(right_tag, split_right_attrs(block, right_tag), [text_marked(right_text, leaf.marks)])
          let grand_path = parent_path(block_path)
          let block_idx  = last_index(block_path)
          let step = step_replace(grand_path, block_idx, block_idx + 1, [left_block, right_block])
          let tx0 = tx_begin(state.doc, sel)
          let tx1 = tx_step(tx0, step)
          let new_caret_path = [*grand_path, block_idx + 1, 0]
          tx_set_selection(tx1, caret(pos(new_caret_path, 0)))
        }
      }
    }
  }
}

// ---------------------------------------------------------------------------
// chain — try each command in turn; return the first non-null transaction
// ---------------------------------------------------------------------------
//
// Mirrors PM's `chainCommands`. Used by callers to fall back across handlers
// (e.g. Backspace tries delete-selection, then merge-blocks, then no-op).

fn chain_at(state, cmds, i, n) {
  if (i >= n) { null }
  else {
    let r = cmds[i](state)
    if (r != null) { r } else { chain_at(state, cmds, i + 1, n) }
  }
}

pub fn chain(state, cmds) => chain_at(state, cmds, 0, len(cmds))
