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

fn sel_same_parent_leaves(sel) =>
  sel.kind == 'text' and path_equal(parent_path(sel.anchor.path), parent_path(sel.head.path))

fn state_schema(state) =>
  if (state.schema == null) { md_schema } else { state.schema }

fn state_default_block(state) =>
  if (state.default_block != null) { state.default_block } else { schema_default_block(state_schema(state)) }

fn state_hard_break(state) => schema_hard_break(state_schema(state))

fn state_image_tag(state) =>
  if (state_schema(state).img != null and state_schema(state).image == null) { 'img' } else { 'image' }

fn state_link_tag(state) =>
  if (state_schema(state).a != null and state_schema(state).link == null) { 'a' } else { 'link' }

fn state_code_block_tag(state) =>
  if (state_schema(state).pre != null and state_schema(state).code_block == null) { 'pre' } else { 'code_block' }

fn replacement_marks(state) =>
  if (state.stored_marks == null) [] else state.stored_marks

fn insert_marks_for_leaf(state, leaf) =>
  if (state.stored_marks != null) { state.stored_marks }
  else if (leaf != null and is_text(leaf)) { leaf.marks }
  else { [] }

fn replacement_block(state, txt) =>
  node(state_default_block(state), [text_marked(txt, replacement_marks(state))])

fn replace_all_with_text(state, txt) {
  let tx0 = tx_begin(state.doc, state.selection)
  let tx1 = tx_step(tx0, step_replace([], 0, len(state.doc.content), [replacement_block(state, txt)]))
  tx_set_selection(tx1, caret(pos([0, 0], len(txt))))
}

fn last_caret_pos_in(n, path) {
  if (is_text(n)) { pos(path, len(n.text)) }
  else if (is_node(n) and len(n.content) > 0) { last_caret_pos_in(n.content[len(n.content) - 1], [*path, len(n.content) - 1]) }
  else { pos(path, 0) }
}

fn selection_after_blocks(blocks) =>
  if (len(blocks) == 0) { caret(pos([], 0)) }
  else { caret(last_caret_pos_in(blocks[len(blocks) - 1], [len(blocks) - 1])) }

fn selection_after_inserted_blocks(start_index, blocks) =>
  if (len(blocks) == 0) { caret(pos([], start_index)) }
  else { caret(last_caret_pos_in(blocks[len(blocks) - 1], [start_index + len(blocks) - 1])) }

fn replace_all_with_blocks(state, blocks) {
  if (len(blocks) == 0) { null }
  else {
    let tx0 = tx_begin(state.doc, state.selection)
    let tx1 = tx_step(tx0, step_replace([], 0, len(state.doc.content), blocks))
    tx_set_selection(tx1, selection_after_blocks(blocks))
  }
}

fn valid_selected_node(state) {
  let sel = state.selection
  if (sel == null or sel.kind != 'node' or len(sel.path) == 0) { false }
  else { node_at(state.doc, sel.path) != null }
}

fn replace_node_with_slice(state, slice_nodes, next_selection) {
  let sel = state.selection
  let selected_parent_path = parent_path(sel.path)
  let selected_index = last_index(sel.path)
  let tx0 = tx_begin(state.doc, sel)
  let tx1 = tx_step(tx0, step_replace(selected_parent_path, selected_index, selected_index + 1, slice_nodes))
  tx_set_selection(tx1, next_selection)
}

fn replace_node_with_text(state, txt) {
  if (not valid_selected_node(state)) { null }
  else {
    let selected_parent_path = parent_path(state.selection.path)
    let selected_index = last_index(state.selection.path)
    if (len(selected_parent_path) == 0) {
      replace_node_with_slice(state, [replacement_block(state, txt)], caret(pos([selected_index, 0], len(txt))))
    } else {
      replace_node_with_slice(state, [text_marked(txt, replacement_marks(state))], caret(pos([*selected_parent_path, selected_index], len(txt))))
    }
  }
}

fn delete_node_selection(state) {
  if (not valid_selected_node(state)) { null }
  else {
    let selected_parent_path = parent_path(state.selection.path)
    let selected_index = last_index(state.selection.path)
    let parent = node_at(state.doc, selected_parent_path)
    if (parent == null or not is_node(parent)) { null }
    else if (len(selected_parent_path) == 0 and len(parent.content) == 1) {
      replace_node_with_slice(state, [replacement_block(state, "")], caret(pos([0, 0], 0)))
    } else {
      replace_node_with_slice(state, [], caret(pos(selected_parent_path, selected_index)))
    }
  }
}

fn replace_node_with_blocks(state, blocks) {
  if (not valid_selected_node(state) or len(blocks) == 0) { null }
  else {
    let selected_parent_path = parent_path(state.selection.path)
    let selected_index = last_index(state.selection.path)
    if (len(selected_parent_path) == 0) { replace_node_with_slice(state, blocks, selection_after_inserted_blocks(selected_index, blocks)) }
    else { replace_node_with_text(state, doc_text(node('fragment', blocks))) }
  }
}

fn text_leaf_or_null(doc, path) {
  let leaf = node_at(doc, path)
  if (leaf != null and is_text(leaf)) { leaf } else { null }
}

fn replace_same_parent_text_selection(state, txt) {
  let sel = state.selection
  if (sel == null or not sel_same_parent_leaves(sel)) { null }
  else {
    let lo = sel_lo(sel)
    let hi = sel_hi(sel)
    let parent_path0 = parent_path(lo.path)
    let lo_index = last_index(lo.path)
    let hi_index = last_index(hi.path)
    let lo_leaf = text_leaf_or_null(state.doc, lo.path)
    let hi_leaf = text_leaf_or_null(state.doc, hi.path)
    if (lo_leaf == null or hi_leaf == null or lo_index > hi_index) { null }
    else {
      let before_text = slice(lo_leaf.text, 0, lo.offset)
      let after_text = slice(hi_leaf.text, hi.offset, len(hi_leaf.text))
      let before = nonempty_text_leaf(before_text, lo_leaf.marks)
      let inserted = if (len(txt) == 0) { [] } else { [text_marked(txt, insert_marks_for_leaf(state, lo_leaf))] }
      let after = nonempty_text_leaf(after_text, hi_leaf.marks)
      let slice_nodes0 = list_concat(list_concat(before, inserted), after)
      let slice_nodes = if (len(slice_nodes0) == 0) { [text_marked("", insert_marks_for_leaf(state, lo_leaf))] } else { slice_nodes0 }
      let tx0 = tx_begin(state.doc, sel)
      let tx1 = tx_step(tx0, step_replace(parent_path0, lo_index, hi_index + 1, slice_nodes))
      let caret_index = if (len(inserted) > 0) { lo_index + len(before) }
        else if (len(before) > 0) { lo_index + len(before) - 1 }
        else { lo_index }
      let caret_offset = if (len(inserted) > 0) { len(txt) }
        else if (len(before) > 0) { len(before_text) }
        else { 0 }
      tx_set_selection(tx1, caret(pos([*parent_path0, caret_index], caret_offset)))
    }
  }
}

fn list_take_range(arr, start, end, i, acc) {
  if (i >= end) { acc }
  else { list_take_range(arr, start, end, i + 1, [*acc, arr[i]]) }
}

fn list_slice(arr, start, end) => list_take_range(arr, start, end, start, [])

fn nonempty_or_empty_text(nodes, marks) =>
  if (len(nodes) == 0) { [text_marked("", marks)] } else { nodes }

fn same_parent_text_span_parts(state) {
  let sel = state.selection
  if (sel == null or not sel_same_parent_leaves(sel)) { null }
  else {
    let lo = sel_lo(sel)
    let hi = sel_hi(sel)
    let parent_path0 = parent_path(lo.path)
    let lo_index = last_index(lo.path)
    let hi_index = last_index(hi.path)
    let parent = node_at(state.doc, parent_path0)
    let lo_leaf = text_leaf_or_null(state.doc, lo.path)
    let hi_leaf = text_leaf_or_null(state.doc, hi.path)
    if (parent == null or not is_node(parent) or lo_leaf == null or hi_leaf == null or lo_index > hi_index) { null }
    else {
      let before_edge = nonempty_text_leaf(slice(lo_leaf.text, 0, lo.offset), lo_leaf.marks)
      let after_edge = nonempty_text_leaf(slice(hi_leaf.text, hi.offset, len(hi_leaf.text)), hi_leaf.marks)
      {parent_path: parent_path0, parent: parent, lo: lo, hi: hi, lo_index: lo_index, hi_index: hi_index,
       lo_leaf: lo_leaf, hi_leaf: hi_leaf, before_edge: before_edge, after_edge: after_edge}
    }
  }
}

fn insert_line_break_same_parent_selection(state) {
  let span = same_parent_text_span_parts(state)
  if (span == null or len(span.parent_path) < 1) { null }
  else {
    let slice_nodes0 = list_concat(list_concat(span.before_edge, [node(state_hard_break(state), [])]), span.after_edge)
    let tx0 = tx_begin(state.doc, state.selection)
    let tx1 = tx_step(tx0, step_replace(span.parent_path, span.lo_index, span.hi_index + 1, slice_nodes0))
    tx_set_selection(tx1, caret(pos([*span.parent_path, span.lo_index + len(span.before_edge) + 1], 0)))
  }
}

fn sel_top_block_range(sel) =>
  sel.kind == 'text' and not sel_single_leaf(sel) and len(sel_lo(sel).path) == 2 and len(sel_hi(sel).path) == 2 and sel_lo(sel).path[0] < sel_hi(sel).path[0]

fn selected_top_block_end(sel) {
  let lo = sel_lo(sel)
  let hi = sel_hi(sel)
  if (len(hi.path) == 2 and hi.offset == 0 and hi.path[0] > lo.path[0]) { hi.path[0] - 1 }
  else { hi.path[0] }
}

fn cross_block_text_span_parts(state) {
  let sel = state.selection
  if (sel == null or not sel_top_block_range(sel)) { null }
  else {
    let lo = sel_lo(sel)
    let hi = sel_hi(sel)
    let lo_block_index = lo.path[0]
    let hi_block_index = hi.path[0]
    let lo_leaf_index = lo.path[1]
    let hi_leaf_index = hi.path[1]
    let lo_block = node_at(state.doc, [lo_block_index])
    let hi_block = node_at(state.doc, [hi_block_index])
    let lo_leaf = text_leaf_or_null(state.doc, lo.path)
    let hi_leaf = text_leaf_or_null(state.doc, hi.path)
    if (lo_block == null or hi_block == null or lo_leaf == null or hi_leaf == null or not is_node(lo_block) or not is_node(hi_block)) { null }
    else {
      let before_leaf = nonempty_text_leaf(slice(lo_leaf.text, 0, lo.offset), lo_leaf.marks)
      let after_leaf = nonempty_text_leaf(slice(hi_leaf.text, hi.offset, len(hi_leaf.text)), hi_leaf.marks)
      let prefix = list_concat(list_slice(lo_block.content, 0, lo_leaf_index), before_leaf)
      let suffix = list_concat(after_leaf, list_slice(hi_block.content, hi_leaf_index + 1, len(hi_block.content)))
      {lo: lo, hi: hi, lo_block_index: lo_block_index, hi_block_index: hi_block_index,
       lo_block: lo_block, hi_block: hi_block, lo_leaf: lo_leaf, hi_leaf: hi_leaf,
       prefix: prefix, suffix: suffix}
    }
  }
}

fn replacement_nodes_or_empty(nodes, marks) =>
  if (len(nodes) == 0) { [text_marked("", marks)] } else { nodes }

fn replace_cross_block_text_selection_with_inline(state, inline_nodes, caret_kind) {
  let span = cross_block_text_span_parts(state)
  if (span == null) { null }
  else {
    let merged0 = list_concat(list_concat(span.prefix, inline_nodes), span.suffix)
    let merged = replacement_nodes_or_empty(merged0, insert_marks_for_leaf(state, span.lo_leaf))
    let new_block = node_attrs(span.lo_block.tag, span.lo_block.attrs, merged)
    let tx0 = tx_begin(state.doc, state.selection)
    let tx1 = tx_step(tx0, step_replace([], span.lo_block_index, span.hi_block_index + 1, [new_block]))
    let caret_index = if (caret_kind == 'after_break' and len(span.suffix) > 0) { len(span.prefix) + len(inline_nodes) }
      else if (caret_kind == 'after_insert') { len(span.prefix) + len(inline_nodes) - 1 }
      else if (len(span.prefix) > 0) { len(span.prefix) - 1 }
      else { 0 }
    let caret_offset = if (caret_kind == 'after_break') { 0 }
      else if (caret_kind == 'after_insert' and len(inline_nodes) > 0) { last_text_offset_in(inline_nodes[len(inline_nodes) - 1]) }
      else if (len(span.prefix) > 0) { last_text_offset_in(span.prefix[len(span.prefix) - 1]) }
      else { 0 }
    tx_set_selection(tx1, caret(pos([span.lo_block_index, caret_index], caret_offset)))
  }
}

fn prepend_content_to_block(block, prefix) =>
  if (len(prefix) == 0 or not is_node(block)) { block }
  else { node_attrs(block.tag, block.attrs, list_concat(prefix, block.content)) }

fn append_content_to_block(block, suffix) =>
  if (len(suffix) == 0 or not is_node(block)) { block }
  else { node_attrs(block.tag, block.attrs, list_concat(block.content, suffix)) }

fn apply_cross_block_edges(blocks, prefix, suffix) {
  if (len(blocks) == 0) { [] }
  else if (len(blocks) == 1) { [append_content_to_block(prepend_content_to_block(blocks[0], prefix), suffix)] }
  else {
    let first = prepend_content_to_block(blocks[0], prefix)
    let last = append_content_to_block(blocks[len(blocks) - 1], suffix)
    [first, *list_slice(blocks, 1, len(blocks) - 1), last]
  }
}

fn replace_cross_block_text_selection_with_blocks(state, blocks) {
  let span = cross_block_text_span_parts(state)
  if (span == null or len(blocks) == 0) { null }
  else if (len(blocks) == 1 and blocks[0].tag == state_default_block(state)) {
    replace_cross_block_text_selection_with_inline(state, blocks[0].content, if (len(blocks[0].content) == 0) { 'at_boundary' } else { 'after_insert' })
  }
  else {
    let slice_nodes = apply_cross_block_edges(blocks, span.prefix, span.suffix)
    let tx0 = tx_begin(state.doc, state.selection)
    let tx1 = tx_step(tx0, step_replace([], span.lo_block_index, span.hi_block_index + 1, slice_nodes))
    tx_set_selection(tx1, selection_after_inserted_blocks(span.lo_block_index, slice_nodes))
  }
}

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
  else if (sel.kind == 'all') { replace_all_with_text(state, txt) }
  else if (sel.kind == 'node') { replace_node_with_text(state, txt) }
  else if (not sel_single_leaf(sel) and sel_same_parent_leaves(sel)) { replace_same_parent_text_selection(state, txt) }
  else if (sel_top_block_range(sel)) {
    let inserted = if (len(txt) == 0) { [] } else { [text_marked(txt, insert_marks_for_leaf(state, text_leaf_or_null(state.doc, sel_lo(sel).path)))] }
    replace_cross_block_text_selection_with_inline(state, inserted, if (len(inserted) == 0) { 'at_boundary' } else { 'after_insert' })
  }
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

pub fn cmd_select_all(state) =>
  tx_set_meta(tx_set_selection(tx_begin(state.doc, state.selection), all_selection()), "addToHistory", false)

fn nonempty_text_leaf(s, marks) =>
  if (len(s) == 0) { [] } else { [text_marked(s, marks)] }

fn last_text_offset_in(node) {
  if (is_text(node)) { len(node.text) }
  else if (not is_node(node) or len(node.content) == 0) { 0 }
  else { last_text_offset_in(node.content[len(node.content) - 1]) }
}

fn insert_inline_nodes_select_first(state, inline_nodes) {
  let sel = state.selection
  if (sel == null or len(inline_nodes) == 0) { null }
  else if (sel.kind == 'all') {
    let block = node(state_default_block(state), inline_nodes)
    let tx0 = tx_begin(state.doc, sel)
    let tx1 = tx_step(tx0, step_replace([], 0, len(state.doc.content), [block]))
    tx_set_selection(tx1, node_selection([0, 0]))
  }
  else if (sel.kind == 'node') {
    if (not valid_selected_node(state)) { null }
    else {
      let selected_parent_path = parent_path(sel.path)
      let selected_index = last_index(sel.path)
      if (len(selected_parent_path) == 0) {
        let block = node(state_default_block(state), inline_nodes)
        replace_node_with_slice(state, [block], node_selection([selected_index, 0]))
      } else {
        replace_node_with_slice(state, inline_nodes, node_selection([*selected_parent_path, selected_index]))
      }
    }
  }
  else if (sel_top_block_range(sel)) {
    let span = cross_block_text_span_parts(state)
    if (span == null) { null }
    else {
      let merged0 = list_concat(list_concat(span.prefix, inline_nodes), span.suffix)
      let merged = replacement_nodes_or_empty(merged0, insert_marks_for_leaf(state, span.lo_leaf))
      let new_block = node_attrs(span.lo_block.tag, span.lo_block.attrs, merged)
      let tx0 = tx_begin(state.doc, sel)
      let tx1 = tx_step(tx0, step_replace([], span.lo_block_index, span.hi_block_index + 1, [new_block]))
      tx_set_selection(tx1, node_selection([span.lo_block_index, len(span.prefix)]))
    }
  }
  else if (sel_same_parent_leaves(sel)) {
    let span = same_parent_text_span_parts(state)
    if (span == null) { null }
    else {
      let slice_nodes = list_concat(list_concat(span.before_edge, inline_nodes), span.after_edge)
      let tx0 = tx_begin(state.doc, sel)
      let tx1 = tx_step(tx0, step_replace(span.parent_path, span.lo_index, span.hi_index + 1, slice_nodes))
      tx_set_selection(tx1, node_selection([*span.parent_path, span.lo_index + len(span.before_edge)]))
    }
  }
  else { null }
}

fn text_leaf_with_marks(s, marks) => text_marked(s, marks)

fn selected_inline_content(span, label, fallback_marks) {
  if (span.lo_index == span.hi_index) {
    let selected_text = slice(span.lo_leaf.text, span.lo.offset, span.hi.offset)
    if (len(selected_text) == 0) { [text_leaf_with_marks(label, fallback_marks)] }
    else { [text_leaf_with_marks(selected_text, span.lo_leaf.marks)] }
  } else {
    let first = nonempty_text_leaf(slice(span.lo_leaf.text, span.lo.offset, len(span.lo_leaf.text)), span.lo_leaf.marks)
    let middle = list_slice(span.parent.content, span.lo_index + 1, span.hi_index)
    let last = nonempty_text_leaf(slice(span.hi_leaf.text, 0, span.hi.offset), span.hi_leaf.marks)
    let selected = list_concat(list_concat(first, middle), last)
    if (len(selected) == 0) { [text_leaf_with_marks(label, fallback_marks)] } else { selected }
  }
}

fn link_label(label, href) => if (label == null or len(label) == 0) { href } else { label }

fn link_attrs(href, title) =>
  [{name: 'href', value: href}, {name: 'title', value: if (title == null) { "" } else { title }}]

fn image_attrs(src, alt) =>
  [{name: 'src', value: src}, {name: 'alt', value: if (alt == null) { "" } else { alt }}]

pub fn cmd_insert_image(state, src, alt) =>
  insert_inline_nodes_select_first(state, [node_attrs(state_image_tag(state), image_attrs(src, alt), [])])

pub fn cmd_insert_link(state, href, title, label) {
  let sel = state.selection
  let label2 = link_label(label, href)
  if (sel == null) { null }
  else if (sel.kind == 'all' or sel.kind == 'node' or sel_top_block_range(sel)) {
    insert_inline_nodes_select_first(state, [node_attrs(state_link_tag(state), link_attrs(href, title), [text_marked(label2, replacement_marks(state))])])
  }
  else if (sel_same_parent_leaves(sel)) {
    let span = same_parent_text_span_parts(state)
    if (span == null) { null }
    else {
      let kids = selected_inline_content(span, label2, insert_marks_for_leaf(state, span.lo_leaf))
      let link = node_attrs(state_link_tag(state), link_attrs(href, title), kids)
      let slice_nodes = list_concat(list_concat(span.before_edge, [link]), span.after_edge)
      let tx0 = tx_begin(state.doc, sel)
      let tx1 = tx_step(tx0, step_replace(span.parent_path, span.lo_index, span.hi_index + 1, slice_nodes))
      let link_path = [*span.parent_path, span.lo_index + len(span.before_edge)]
      tx_set_selection(tx1, caret(last_caret_pos_in(link, link_path)))
    }
  }
  else { null }
}

pub fn cmd_paste_fragment(state, fragment) {
  let sel = state.selection
  if (sel == null) { null }
  else if (sel.kind == 'all') { replace_all_with_blocks(state, coerce_children(state_schema(state), fragment, 'block')) }
  else if (sel.kind == 'node') { replace_node_with_blocks(state, coerce_children(state_schema(state), fragment, 'block')) }
  else if (not sel_single_leaf(sel) and sel_same_parent_leaves(sel)) { cmd_paste_text(state, doc_text(node('fragment', coerce_children(state_schema(state), fragment, 'block')))) }
  else if (sel_top_block_range(sel)) { replace_cross_block_text_selection_with_blocks(state, coerce_children(state_schema(state), fragment, 'block')) }
  else if (not sel_single_leaf(sel)) { null }
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
        let blocks = coerce_children(state_schema(state), fragment, 'block')
        if (len(blocks) == 0) { null }
        else if (len(blocks) == 1 and blocks[0].tag == state_default_block(state)) {
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
    else { cmd_paste_fragment(state, html_to_editor_fragment_for_schema(state_schema(state), parsed)) }
  }
  else { cmd_paste_fragment(state, html_to_editor_fragment_for_schema(state_schema(state), html)) }

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
    let coerced = coerce_children_for_parent(state_schema(state), parent.tag, slice)
    if (len(coerced) == 0) { null }
    else {
      let tx0 = tx_begin(state.doc, state.selection)
      let tx1 = tx_step(tx0, step_replace(target_parent_path, target_index, target_index, coerced))
      tx_set_selection(tx1, insert_selection(target_parent_path, target_index, len(coerced)))
    }
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

fn delete_all(state) => replace_all_with_text(state, "")

fn delete_selection(state) {
  let sel = state.selection
  if (sel == null) { null }
  else if (sel.kind == 'all') { delete_all(state) }
  else if (sel.kind == 'node') { delete_node_selection(state) }
  else if (sel_single_leaf(sel)) { delete_range(state, sel_lo(sel), sel_hi(sel)) }
  else if (sel_same_parent_leaves(sel)) { replace_same_parent_text_selection(state, "") }
  else if (sel_top_block_range(sel)) { replace_cross_block_text_selection_with_inline(state, [], 'at_boundary') }
  else { null }
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
  else if (sel.kind != 'text' or not sel_collapsed(sel)) { delete_selection(state) }
  else if (not sel_single_leaf(sel)) { null }
  else {
    let p = sel.anchor
    if (p.offset <= 0) { merge_blocks_backward(state, p) }
    else { delete_range(state, pos(p.path, p.offset - 1), p) }
  }
}

pub fn cmd_delete_forward(state) {
  let sel = state.selection
  if (sel == null) { null }
  else if (sel.kind != 'text' or not sel_collapsed(sel)) { delete_selection(state) }
  else if (not sel_single_leaf(sel)) { null }
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
  else if (sel.kind != 'text' or not sel_collapsed(sel)) { delete_selection(state) }
  else if (not sel_single_leaf(sel)) { null }
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
  if (sel == null) { null }
  else if (not sel_collapsed(sel) and sel_same_parent_leaves(sel)) { insert_line_break_same_parent_selection(state) }
  else if (not sel_collapsed(sel) and sel_top_block_range(sel)) { replace_cross_block_text_selection_with_inline(state, [node(state_hard_break(state), [])], 'after_break') }
  else if (not sel_single_leaf(sel)) { null }
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
      let slice_nodes = [*before, node(state_hard_break(state), []), after]
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

fn state_list_item_tag(state) =>
  if (state_schema(state).li != null and state_schema(state).list_item == null) { 'li' } else { 'list_item' }

fn is_list_tag(tag) => tag == 'list' or tag == 'ul' or tag == 'ol'
fn is_list_node(n) => is_node(n) and is_list_tag(n.tag)

fn same_list_kind(a, b) => is_list_node(a) and is_list_node(b) and a.tag == b.tag

fn append_to_sublist(prev_item, list_node, item) {
  let kids = prev_item.content
  if (len(kids) > 0 and same_list_kind(kids[len(kids) - 1], list_node)) {
    let sub = kids[len(kids) - 1]
    let sub2 = with_content(sub, [*sub.content, item])
    node_attrs(prev_item.tag, prev_item.attrs, list_set(kids, len(kids) - 1, sub2))
  } else {
    node_attrs(prev_item.tag, prev_item.attrs, [*kids, node_attrs(list_node.tag, list_node.attrs, [item])])
  }
}

pub fn cmd_indent_list_item(state) {
  let sel = state.selection
  if (sel == null) { null }
  else {
    let base_path = if (sel.kind == 'node') { sel.path } else { sel.anchor.path }
    let item_path = ancestor_tag(state.doc, base_path, state_list_item_tag(state))
    if (item_path == null) { null }
    else {
      let list_path = parent_path(item_path)
      let item_index = last_index(item_path)
      let list_node = node_at(state.doc, list_path)
      if (list_node == null or not is_list_node(list_node) or item_index <= 0) { null }
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
    let item_path = ancestor_tag(state.doc, base_path, state_list_item_tag(state))
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
        let item_tag = state_list_item_tag(state)
      if (list_node == null or parent_item == null or grand_list == null or
          not is_node(list_node) or not is_node(parent_item) or not is_node(grand_list) or
          not is_list_node(list_node) or parent_item.tag != item_tag or not is_list_node(grand_list)) { null }
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

fn has_text_leaf(n) {
  if (is_text(n)) { true }
  else if (is_node(n)) { has_text_leaf_children(n.content, 0, len(n.content)) }
  else { false }
}

fn has_text_leaf_children(children, i, n) {
  if (i >= n) { false }
  else if (has_text_leaf(children[i])) { true }
  else { has_text_leaf_children(children, i + 1, n) }
}

fn any_unmarked_text(n, mark) {
  if (is_text(n)) { not has_mark(n.marks, mark) }
  else if (is_node(n)) { any_unmarked_text_children(n.content, mark, 0, len(n.content)) }
  else { false }
}

fn any_unmarked_text_children(children, mark, i, n) {
  if (i >= n) { false }
  else if (any_unmarked_text(children[i], mark)) { true }
  else { any_unmarked_text_children(children, mark, i + 1, n) }
}

fn collect_mark_steps(n, path, mark, adding, acc) {
  if (is_text(n)) {
    if (adding and not has_mark(n.marks, mark)) { [*acc, step_add_mark(path, mark)] }
    else if (not adding and has_mark(n.marks, mark)) { [*acc, step_remove_mark(path, mark)] }
    else { acc }
  }
  else if (is_node(n)) { collect_mark_steps_children(n.content, path, mark, adding, 0, len(n.content), acc) }
  else { acc }
}

fn collect_mark_steps_children(children, path, mark, adding, i, n, acc) {
  if (i >= n) { acc }
  else {
    let next = collect_mark_steps(children[i], [*path, i], mark, adding, acc)
    collect_mark_steps_children(children, path, mark, adding, i + 1, n, next)
  }
}

fn range_any_unmarked_leaf(doc, parent_path0, mark, i, end_i) {
  if (i > end_i) { false }
  else {
    let leaf = text_leaf_or_null(doc, [*parent_path0, i])
    if (leaf != null and not has_mark(leaf.marks, mark)) { true }
    else { range_any_unmarked_leaf(doc, parent_path0, mark, i + 1, end_i) }
  }
}

fn collect_range_mark_steps(doc, parent_path0, mark, adding, i, end_i, acc) {
  if (i > end_i) { acc }
  else {
    let path = [*parent_path0, i]
    let leaf = text_leaf_or_null(doc, path)
    let next = if (leaf == null) { acc }
      else if (adding and not has_mark(leaf.marks, mark)) { [*acc, step_add_mark(path, mark)] }
      else if (not adding and has_mark(leaf.marks, mark)) { [*acc, step_remove_mark(path, mark)] }
      else { acc }
    collect_range_mark_steps(doc, parent_path0, mark, adding, i + 1, end_i, next)
  }
}

fn mark_list_for(marks, mark, adding) =>
  if (adding) { with_mark(marks, mark) } else { without_mark(marks, mark) }

fn mark_text_leaf_node(leaf, mark, adding) =>
  text_marked(leaf.text, mark_list_for(leaf.marks, mark, adding))

fn mark_text_leaf_range(leaf, from, to, mark, adding) {
  let before = nonempty_text_leaf(slice(leaf.text, 0, from), leaf.marks)
  let middle_text = slice(leaf.text, from, to)
  let middle = if (len(middle_text) == 0) { [] } else { [text_marked(middle_text, mark_list_for(leaf.marks, mark, adding))] }
  let after = nonempty_text_leaf(slice(leaf.text, to, len(leaf.text)), leaf.marks)
  list_concat(list_concat(before, middle), after)
}

fn mark_node_tree(n, mark, adding) {
  if (is_text(n)) { mark_text_leaf_node(n, mark, adding) }
  else if (is_node(n)) { node_attrs(n.tag, n.attrs, mark_node_children(n.content, mark, adding, 0, len(n.content), [])) }
  else { n }
}

fn mark_node_children(children, mark, adding, i, n, acc) {
  if (i >= n) { acc }
  else { mark_node_children(children, mark, adding, i + 1, n, [*acc, mark_node_tree(children[i], mark, adding)]) }
}

fn selected_leaf_range_any_unmarked(leaf, from, to, mark) =>
  leaf != null and is_text(leaf) and to > from and not has_mark(leaf.marks, mark)

fn selected_node_any_unmarked(n, mark) => any_unmarked_text(n, mark)

fn selected_children_any_unmarked(children, mark, i, end_i) {
  if (i > end_i) { false }
  else if (selected_node_any_unmarked(children[i], mark)) { true }
  else { selected_children_any_unmarked(children, mark, i + 1, end_i) }
}

fn same_parent_selection_any_unmarked(span, mark) {
  if (span.lo_index == span.hi_index) { selected_leaf_range_any_unmarked(span.lo_leaf, span.lo.offset, span.hi.offset, mark) }
  else if (selected_leaf_range_any_unmarked(span.lo_leaf, span.lo.offset, len(span.lo_leaf.text), mark)) { true }
  else if (selected_children_any_unmarked(span.parent.content, mark, span.lo_index + 1, span.hi_index - 1)) { true }
  else { selected_leaf_range_any_unmarked(span.hi_leaf, 0, span.hi.offset, mark) }
}

fn mark_selected_children(children, mark, adding, i, end_i, acc) {
  if (i > end_i) { acc }
  else { mark_selected_children(children, mark, adding, i + 1, end_i, [*acc, mark_node_tree(children[i], mark, adding)]) }
}

fn marked_same_parent_slice(span, mark, adding) {
  if (span.lo_index == span.hi_index) { mark_text_leaf_range(span.lo_leaf, span.lo.offset, span.hi.offset, mark, adding) }
  else {
    let first = mark_text_leaf_range(span.lo_leaf, span.lo.offset, len(span.lo_leaf.text), mark, adding)
    let middle = mark_selected_children(span.parent.content, mark, adding, span.lo_index + 1, span.hi_index - 1, [])
    let last = mark_text_leaf_range(span.hi_leaf, 0, span.hi.offset, mark, adding)
    list_concat(list_concat(first, middle), last)
  }
}

fn marked_same_parent_selection(span, slice_nodes) {
  let before_count = len(nonempty_text_leaf(slice(span.lo_leaf.text, 0, span.lo.offset), span.lo_leaf.marks))
  if (span.lo_index == span.hi_index) {
    let selected_text = slice(span.lo_leaf.text, span.lo.offset, span.hi.offset)
    text_selection(pos([*span.parent_path, span.lo_index + before_count], 0),
      pos([*span.parent_path, span.lo_index + before_count], len(selected_text)))
  } else {
    let after_count = len(nonempty_text_leaf(slice(span.hi_leaf.text, span.hi.offset, len(span.hi_leaf.text)), span.hi_leaf.marks))
    let last_selected_index = span.lo_index + len(slice_nodes) - after_count - 1
    let head_index = if (span.hi.offset == 0 and after_count > 0) { last_selected_index + 1 } else { last_selected_index }
    text_selection(pos([*span.parent_path, span.lo_index + before_count], 0),
      pos([*span.parent_path, head_index], span.hi.offset))
  }
}

fn toggle_mark_same_parent_range(state, mark) {
  let sel = state.selection
  if (sel == null or not sel_same_parent_leaves(sel) or sel_collapsed(sel)) { null }
  else {
    let span = same_parent_text_span_parts(state)
    if (span == null) { null }
    else {
      let adding = same_parent_selection_any_unmarked(span, mark)
      let slice_nodes = marked_same_parent_slice(span, mark, adding)
      let tx0 = tx_begin(state.doc, sel)
      let tx1 = tx_step(tx0, step_replace(span.parent_path, span.lo_index, span.hi_index + 1, slice_nodes))
      tx_set_selection(tx1, marked_same_parent_selection(span, slice_nodes))
    }
  }
}

fn block_range_any_unmarked_leaf(doc, mark, block_i, end_block_i) {
  if (block_i > end_block_i) { false }
  else {
    let block = node_at(doc, [block_i])
    if (block != null and any_unmarked_text(block, mark)) { true }
    else { block_range_any_unmarked_leaf(doc, mark, block_i + 1, end_block_i) }
  }
}

fn collect_block_range_mark_steps(doc, mark, adding, block_i, end_block_i, acc) {
  if (block_i > end_block_i) { acc }
  else {
    let block = node_at(doc, [block_i])
    let next = if (block == null) { acc } else { collect_mark_steps(block, [block_i], mark, adding, acc) }
    collect_block_range_mark_steps(doc, mark, adding, block_i + 1, end_block_i, next)
  }
}

fn toggle_mark_top_block_range(state, mark) {
  let sel = state.selection
  if (sel == null or not sel_top_block_range(sel) or sel_collapsed(sel)) { null }
  else {
    let lo = sel_lo(sel)
    let end_block = selected_top_block_end(sel)
    let adding = block_range_any_unmarked_leaf(state.doc, mark, lo.path[0], end_block)
    let steps = collect_block_range_mark_steps(state.doc, mark, adding, lo.path[0], end_block, [])
    if (len(steps) == 0) { null }
    else { tx_apply_steps(tx_begin(state.doc, sel), steps, 0, len(steps)) }
  }
}

fn toggle_mark_all(state, mark) {
  if (not has_text_leaf(state.doc)) { null }
  else {
    let adding = any_unmarked_text(state.doc, mark)
    let steps = collect_mark_steps(state.doc, [], mark, adding, [])
    if (len(steps) == 0) { null }
    else { tx_apply_steps(tx_begin(state.doc, state.selection), steps, 0, len(steps)) }
  }
}

fn toggle_mark_node(state, mark) {
  if (not valid_selected_node(state)) { null }
  else {
    let selected_node = node_at(state.doc, state.selection.path)
    if (selected_node == null or not has_text_leaf(selected_node)) { null }
    else {
      let adding = any_unmarked_text(selected_node, mark)
      let steps = collect_mark_steps(selected_node, state.selection.path, mark, adding, [])
      if (len(steps) == 0) { null }
      else { tx_apply_steps(tx_begin(state.doc, state.selection), steps, 0, len(steps)) }
    }
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
  else if (sel.kind == 'all') { toggle_mark_all(state, mark) }
  else if (sel.kind == 'node') { toggle_mark_node(state, mark) }
  else if (not sel_collapsed(sel) and sel_same_parent_leaves(sel)) { toggle_mark_same_parent_range(state, mark) }
  else if (sel_top_block_range(sel)) { toggle_mark_top_block_range(state, mark) }
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

fn block_path_from_path(doc, path) {
  if (len(path) == 0) { null }
  else {
    let n = node_at(doc, path)
    if (n != null and is_node(n) and len(parent_path(path)) == 0) { path }
    else { parent_path(path) }
  }
}

fn block_path_from_selection(state) {
  let sel = state.selection
  if (sel == null or sel.kind == 'all') { null }
  else if (sel.kind == 'node') { block_path_from_path(state.doc, sel.path) }
  else { block_path_from_path(state.doc, sel_lo(sel).path) }
}

fn collect_top_block_type_steps(children, tag, i, n, acc) {
  if (i >= n) { acc }
  else if (is_node(children[i])) { collect_top_block_type_steps(children, tag, i + 1, n, [*acc, step_set_node_type([i], tag)]) }
  else { collect_top_block_type_steps(children, tag, i + 1, n, acc) }
}

fn set_all_block_type(state, tag) {
  if (state.doc == null or not is_node(state.doc)) { null }
  else {
    let steps = collect_top_block_type_steps(state.doc.content, tag, 0, len(state.doc.content), [])
    if (len(steps) == 0) { null }
    else { tx_apply_steps(tx_begin(state.doc, state.selection), steps, 0, len(steps)) }
  }
}

fn collect_top_block_type_steps_between(doc, tag, i, end_i, acc) {
  if (i > end_i) { acc }
  else {
    let n = node_at(doc, [i])
    let next = if (n != null and is_node(n)) { [*acc, step_set_node_type([i], tag)] } else { acc }
    collect_top_block_type_steps_between(doc, tag, i + 1, end_i, next)
  }
}

fn set_top_block_range_type(state, tag) {
  let sel = state.selection
  if (sel == null or not sel_top_block_range(sel)) { null }
  else {
    let lo = sel_lo(sel)
    let hi = sel_hi(sel)
    let steps = collect_top_block_type_steps_between(state.doc, tag, lo.path[0], selected_top_block_end(sel), [])
    if (len(steps) == 0) { null }
    else { tx_apply_steps(tx_begin(state.doc, sel), steps, 0, len(steps)) }
  }
}

pub fn cmd_set_block_type(state, tag) {
  let sel = state.selection
  if (sel != null and sel.kind == 'all') { set_all_block_type(state, tag) }
  else if (sel != null and sel_top_block_range(sel)) { set_top_block_range_type(state, tag) }
  else {
    let block_path = block_path_from_selection(state)
    if (block_path == null) { null }
    else {
      let n = node_at(state.doc, block_path)
      if (n == null or not is_node(n)) { null }
      else {
        let step = step_set_node_type(block_path, tag)
        let tx0  = tx_begin(state.doc, state.selection)
        tx_step(tx0, step)
      }
    }
  }
}

// ---------------------------------------------------------------------------
// Block structure commands — atomic blocks, code blocks, and blockquotes
// ---------------------------------------------------------------------------

fn wrap_top_blocks(state, start_index, end_index) {
  if (start_index < 0 or end_index < start_index) { null }
  else {
    let wrapped = list_slice(state.doc.content, start_index, end_index + 1)
    let quote = node('blockquote', wrapped)
    let tx0 = tx_begin(state.doc, state.selection)
    let tx1 = tx_step(tx0, step_replace([], start_index, end_index + 1, [quote]))
    tx_set_selection(tx1, node_selection([start_index]))
  }
}

pub fn cmd_insert_horizontal_rule(state) {
  if (state_schema(state).hr == null) { null }
  else { insert_block_after_selection(state, node('hr', [])) }
}

pub fn cmd_insert_code_block(state, txt) {
  let tag = state_code_block_tag(state)
  if (state_schema(state)[tag] == null) { null }
  else {
    let text_value = if (txt == null) { "" } else { txt }
    let tx = insert_block_after_selection(state, node(tag, [text(text_value)]))
    if (tx == null or tx.sel_after.kind != 'node') { tx }
    else { tx_set_selection(tx, caret(pos([*tx.sel_after.path, 0], len(text_value)))) }
  }
}

pub fn cmd_wrap_blockquote(state) {
  if (state_schema(state).blockquote == null) { null }
  else {
    let sel = state.selection
    if (sel == null or state.doc == null or not is_node(state.doc) or len(state.doc.content) == 0) { null }
    else if (sel.kind == 'all') { wrap_top_blocks(state, 0, len(state.doc.content) - 1) }
    else if (sel.kind == 'node') {
      if (len(sel.path) == 0) { null } else { wrap_top_blocks(state, sel.path[0], sel.path[0]) }
    }
    else if (sel_top_block_range(sel)) { wrap_top_blocks(state, sel_lo(sel).path[0], selected_top_block_end(sel)) }
    else {
      let lo = sel_lo(sel)
      if (len(lo.path) == 0) { null } else { wrap_top_blocks(state, lo.path[0], lo.path[0]) }
    }
  }
}

pub fn cmd_lift_blockquote(state) {
  let sel = state.selection
  if (sel == null) { null }
  else {
    let base_path = if (sel.kind == 'node') { sel.path } else if (sel.kind == 'all') { [] } else { sel.anchor.path }
    let quote_path = ancestor_tag(state.doc, base_path, 'blockquote')
    if (quote_path == null) { null }
    else {
      let quote = node_at(state.doc, quote_path)
      if (quote == null or not is_node(quote) or len(quote_path) == 0) { null }
      else {
        let parent = parent_path(quote_path)
        let idx = last_index(quote_path)
        let tx0 = tx_begin(state.doc, state.selection)
        let tx1 = tx_step(tx0, step_replace(parent, idx, idx + 1, quote.content))
        tx_set_selection(tx1, node_selection([*parent, idx]))
      }
    }
  }
}

// ---------------------------------------------------------------------------
// Table commands — schema-gated block insertion and local row/column edits
// ---------------------------------------------------------------------------

fn table_supported(state) => state_schema(state).table != null

fn make_table_cells(cols, cell_tag, i, acc) {
  if (i >= cols) { acc }
  else { make_table_cells(cols, cell_tag, i + 1, [*acc, node(cell_tag, [text("")])]) }
}

fn make_table_row(cols, header) =>
  node('tr', make_table_cells(cols, if (header) { 'th' } else { 'td' }, 0, []))

fn make_table_rows(rows, cols, header, i, acc) {
  if (i >= rows) { acc }
  else { make_table_rows(rows, cols, header, i + 1, [*acc, make_table_row(cols, header and i == 0)]) }
}

fn make_table_node(rows, cols, header) =>
  node('table', make_table_rows(rows, cols, header, 0, []))

fn insert_block_after_selection(state, block) {
  let sel = state.selection
  if (sel == null or state.doc == null or not is_node(state.doc)) { null }
  else if (sel.kind == 'all') {
    let tx0 = tx_begin(state.doc, sel)
    let tx1 = tx_step(tx0, step_replace([], 0, len(state.doc.content), [block]))
    tx_set_selection(tx1, node_selection([0]))
  }
  else if (sel.kind == 'node') {
    if (len(sel.path) == 0 or len(parent_path(sel.path)) != 0) { null }
    else {
      let idx = last_index(sel.path)
      let tx0 = tx_begin(state.doc, sel)
      let tx1 = tx_step(tx0, step_replace([], idx, idx + 1, [block]))
      tx_set_selection(tx1, node_selection([idx]))
    }
  }
  else if (sel_top_block_range(sel)) {
    let lo = sel_lo(sel)
    let tx0 = tx_begin(state.doc, sel)
    let tx1 = tx_step(tx0, step_replace([], lo.path[0], selected_top_block_end(sel) + 1, [block]))
    tx_set_selection(tx1, node_selection([lo.path[0]]))
  }
  else {
    let lo = sel_lo(sel)
    if (len(lo.path) == 0) { null }
    else {
      let insert_at = lo.path[0] + 1
      let tx0 = tx_begin(state.doc, sel)
      let tx1 = tx_step(tx0, step_replace([], insert_at, insert_at, [block]))
      tx_set_selection(tx1, node_selection([insert_at]))
    }
  }
}

pub fn cmd_insert_table(state, rows, cols, header) {
  if (not table_supported(state) or rows <= 0 or cols <= 0) { null }
  else { insert_block_after_selection(state, make_table_node(rows, cols, header)) }
}

fn selected_table_cell_path(state) {
  let sel = state.selection
  if (sel == null or sel.kind == 'all') { null }
  else {
    let base_path = if (sel.kind == 'node') { sel.path } else { sel.anchor.path }
    let td = ancestor_tag(state.doc, base_path, 'td')
    if (td != null) { td } else { ancestor_tag(state.doc, base_path, 'th') }
  }
}

fn selected_table_context(state) {
  let cell_path = selected_table_cell_path(state)
  if (cell_path == null) { null }
  else {
    let row_path = parent_path(cell_path)
    let row_container_path = parent_path(row_path)
    let row_container = node_at(state.doc, row_container_path)
    let table_path = if (row_container != null and is_node(row_container) and row_container.tag == 'tr') { null }
      else if (row_container != null and is_node(row_container) and row_container.tag == 'table') { row_container_path }
      else { parent_path(row_container_path) }
    let table = if (table_path == null) { null } else { node_at(state.doc, table_path) }
    let row = node_at(state.doc, row_path)
    if (table == null or row_container == null or row == null or not is_node(table) or not is_node(row_container) or not is_node(row) or table.tag != 'table' or row.tag != 'tr') { null }
    else { {cell_path: cell_path, row_path: row_path, table_path: table_path, table: table, row: row,
            row_container_path: row_container_path, row_container: row_container,
            row_index: last_index(row_path), col_index: last_index(cell_path)} }
  }
}

fn empty_cell_like(cell) =>
  if (cell != null and is_node(cell) and cell.tag == 'th') { node('th', [text("")]) }
  else { node('td', [text("")]) }

pub fn cmd_add_table_row(state) {
  let ctx = selected_table_context(state)
  if (ctx == null) { null }
  else {
    let insert_at = ctx.row_index + 1
    let cols = len(ctx.row.content)
    let tx0 = tx_begin(state.doc, state.selection)
    let tx1 = tx_step(tx0, step_replace(ctx.row_container_path, insert_at, insert_at, [make_table_row(cols, false)]))
    tx_set_selection(tx1, node_selection([*ctx.row_container_path, insert_at]))
  }
}

pub fn cmd_delete_table_row(state) {
  let ctx = selected_table_context(state)
  if (ctx == null or len(ctx.row_container.content) <= 1) { null }
  else {
    let tx0 = tx_begin(state.doc, state.selection)
    let tx1 = tx_step(tx0, step_replace(ctx.row_container_path, ctx.row_index, ctx.row_index + 1, []))
    let next_row = if (ctx.row_index >= len(ctx.row_container.content) - 1) { ctx.row_index - 1 } else { ctx.row_index }
    tx_set_selection(tx1, node_selection([*ctx.row_container_path, next_row]))
  }
}

fn collect_add_col_steps_rows(rows_path, rows, col_index, row_i, row_count, acc) {
  if (row_i >= row_count) { acc }
  else {
    let row = rows.content[row_i]
    let next = if (row != null and is_node(row) and row.tag == 'tr') {
      let ref_cell = if (col_index < len(row.content)) { row.content[col_index] } else { null }
      [*acc, step_replace([*rows_path, row_i], col_index + 1, col_index + 1, [empty_cell_like(ref_cell)])]
    } else { acc }
    collect_add_col_steps_rows(rows_path, rows, col_index, row_i + 1, row_count, next)
  }
}

fn collect_add_col_steps_table(table_path, table, col_index, i, n, acc) {
  if (i >= n) { acc }
  else {
    let child = table.content[i]
    let next = if (child != null and is_node(child) and child.tag == 'tr') {
      let ref_cell = if (col_index < len(child.content)) { child.content[col_index] } else { null }
      [*acc, step_replace([*table_path, i], col_index + 1, col_index + 1, [empty_cell_like(ref_cell)])]
    } else if (child != null and is_node(child) and (child.tag == 'thead' or child.tag == 'tbody' or child.tag == 'tfoot')) {
      collect_add_col_steps_rows([*table_path, i], child, col_index, 0, len(child.content), acc)
    } else { acc }
    collect_add_col_steps_table(table_path, table, col_index, i + 1, n, next)
  }
}

pub fn cmd_add_table_column(state) {
  let ctx = selected_table_context(state)
  if (ctx == null) { null }
  else {
    let steps = collect_add_col_steps_table(ctx.table_path, ctx.table, ctx.col_index, 0, len(ctx.table.content), [])
    let tx1 = tx_apply_steps(tx_begin(state.doc, state.selection), steps, 0, len(steps))
    tx_set_selection(tx1, node_selection([*ctx.row_path, ctx.col_index + 1]))
  }
}

fn collect_delete_col_steps_rows(rows_path, rows, col_index, row_i, row_count, acc) {
  if (row_i >= row_count) { acc }
  else {
    let row = rows.content[row_i]
    let next = if (row != null and is_node(row) and row.tag == 'tr' and col_index < len(row.content)) {
      [*acc, step_replace([*rows_path, row_i], col_index, col_index + 1, [])]
    } else { acc }
    collect_delete_col_steps_rows(rows_path, rows, col_index, row_i + 1, row_count, next)
  }
}

fn collect_delete_col_steps_table(table_path, table, col_index, i, n, acc) {
  if (i >= n) { acc }
  else {
    let child = table.content[i]
    let next = if (child != null and is_node(child) and child.tag == 'tr' and col_index < len(child.content)) {
      [*acc, step_replace([*table_path, i], col_index, col_index + 1, [])]
    } else if (child != null and is_node(child) and (child.tag == 'thead' or child.tag == 'tbody' or child.tag == 'tfoot')) {
      collect_delete_col_steps_rows([*table_path, i], child, col_index, 0, len(child.content), acc)
    } else { acc }
    collect_delete_col_steps_table(table_path, table, col_index, i + 1, n, next)
  }
}

pub fn cmd_delete_table_column(state) {
  let ctx = selected_table_context(state)
  if (ctx == null or len(ctx.row.content) <= 1) { null }
  else {
    let steps = collect_delete_col_steps_table(ctx.table_path, ctx.table, ctx.col_index, 0, len(ctx.table.content), [])
    let tx1 = tx_apply_steps(tx_begin(state.doc, state.selection), steps, 0, len(steps))
    let next_col = if (ctx.col_index >= len(ctx.row.content) - 1) { ctx.col_index - 1 } else { ctx.col_index }
    tx_set_selection(tx1, node_selection([*ctx.row_path, next_col]))
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

fn split_right_tag(state, block, cut, text_len) {
  let default_block = state_default_block(state)
  if (cut == text_len and block.tag != default_block) { default_block } else { block.tag }
}

fn split_right_attrs(block, right_tag) =>
  if (right_tag == block.tag) { block.attrs } else { [] }

fn split_block_text_selection(state) {
  let span = same_parent_text_span_parts(state)
  if (span == null or len(span.parent_path) < 1) { null }
  else {
    let block_path = span.parent_path
    let block = span.parent
    let grand_path = parent_path(block_path)
    let block_idx = last_index(block_path)
    let left_prefix = list_slice(block.content, 0, span.lo_index)
    let right_suffix = list_slice(block.content, span.hi_index + 1, len(block.content))
    let left_content = nonempty_or_empty_text(list_concat(left_prefix, span.before_edge), span.lo_leaf.marks)
    let right_content = nonempty_or_empty_text(list_concat(span.after_edge, right_suffix), span.hi_leaf.marks)
    let right_tag = split_right_tag(state, block, if (len(span.after_edge) == 0 and len(right_suffix) == 0) { len(span.hi_leaf.text) } else { 0 }, len(span.hi_leaf.text))
    let left_block = node_attrs(block.tag, block.attrs, left_content)
    let right_block = node_attrs(right_tag, split_right_attrs(block, right_tag), right_content)
    let tx0 = tx_begin(state.doc, state.selection)
    let tx1 = tx_step(tx0, step_replace(grand_path, block_idx, block_idx + 1, [left_block, right_block]))
    tx_set_selection(tx1, caret(pos([*grand_path, block_idx + 1, 0], 0)))
  }
}

pub fn cmd_split_block(state) {
  let sel = state.selection
  if (sel == null) { null }
  else if (sel.kind == 'text' and not sel_collapsed(sel) and sel_same_parent_leaves(sel)) { split_block_text_selection(state) }
  else if (not sel_collapsed(sel)) { null }
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
