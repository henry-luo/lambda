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
    let step = step_replace_text(lo.path, lo.offset, hi.offset, txt)
    let tx0  = tx_begin(state.doc, sel)
    let tx1  = tx_step(tx0, step)
    let new_caret = caret(pos(lo.path, lo.offset + len(txt)))
    tx_set_selection(tx1, new_caret)
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
// cmd_delete_backward / cmd_delete_forward
// ---------------------------------------------------------------------------
//
// If the selection is non-empty, delete it.  Otherwise delete one character
// to the left/right of the caret within the same text leaf. At leaf boundaries
// the command returns null (block-merge handled elsewhere).

fn delete_range(state, lo, hi) {
  let step = step_replace_text(lo.path, lo.offset, hi.offset, "")
  let tx0  = tx_begin(state.doc, state.selection)
  let tx1  = tx_step(tx0, step)
  tx_set_selection(tx1, caret(lo))
}

pub fn cmd_delete_backward(state) {
  let sel = state.selection
  if (sel == null) { null }
  else if (not sel_single_leaf(sel)) { null }
  else if (not sel_collapsed(sel)) { delete_range(state, sel_lo(sel), sel_hi(sel)) }
  else {
    let p = sel.anchor
    if (p.offset <= 0) { null }
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
    else if (p.offset >= len(leaf.text)) { null }
    else { delete_range(state, p, pos(p.path, p.offset + 1)) }
  }
}

// ---------------------------------------------------------------------------
// cmd_toggle_mark — add or remove a mark across the (single-leaf) selection
// ---------------------------------------------------------------------------
//
// If the addressed leaf already has the mark, the command removes it; otherwise
// it adds it. Selection is preserved.
//
// (Multi-leaf selections will gain a true range-based add/remove pair once
// SourcePos resolution across leaves is wired through; this scaffolding holds.)

pub fn cmd_toggle_mark(state, mark) {
  let sel = state.selection
  if (sel == null) { null }
  else if (not sel_single_leaf(sel)) { null }
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
          let left_block  = node_attrs(block.tag, block.attrs, [text_marked(left_text,  leaf.marks)])
          let right_block = node_attrs(block.tag, block.attrs, [text_marked(right_text, leaf.marks)])
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
