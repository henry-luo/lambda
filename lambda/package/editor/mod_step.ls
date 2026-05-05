// mod_step.ls — typed, invertible edit Steps for the rich-text engine
// (Radiant Rich Text Editing, Phase R3 — §5.1).
//
// A Step is a small, JSON-serialisable map describing one atomic edit. Every
// step kind supports three operations:
//
//   step_apply(step, doc)    -> new_doc  (immutable; doc is unchanged)
//   step_invert(step, doc)   -> inverse_step  (such that apply(inv) undoes apply(step))
//   step_map(step, pos)      -> new_pos       (translate a SourcePos through the step)
//
// The step kinds implemented here are a pure-Lambda subset of the spec:
//
//   'replace_text'   {path, from, to, text}      replace a slice of one text leaf
//   'replace'        {parent, from, to, slice}   replace children[from..to] of a node
//   'replace_around' {parent, from, to, gap_from, gap_to, slice, insert}
//                                                   replace outer range while preserving an inner gap
//   'add_mark'       {path, mark}                add `mark` to a text leaf
//   'remove_mark'    {path, mark}                remove `mark` from a text leaf
//   'set_attr'       {path, name, value}         set one attribute
//   'set_node_type'  {path, tag}                 retag a node

import .mod_doc
import .mod_source_pos

// ---------------------------------------------------------------------------
// Constructors
// ---------------------------------------------------------------------------

pub fn step_replace_text(path, from, to, text) =>
  {kind: 'replace_text', path: path, from: from, to: to, text: text}

pub fn step_replace(parent, from, to, slice) =>
  {kind: 'replace', parent: parent, from: from, to: to, slice: slice}

pub fn step_replace_around(parent, from, to, gap_from, gap_to, slice, insert) =>
  {kind: 'replace_around', parent: parent, from: from, to: to,
   gap_from: gap_from, gap_to: gap_to, slice: slice, insert: insert}

pub fn step_add_mark(path, mark) =>
  {kind: 'add_mark', path: path, mark: mark}

pub fn step_remove_mark(path, mark) =>
  {kind: 'remove_mark', path: path, mark: mark}

pub fn step_set_attr(path, name, value) =>
  {kind: 'set_attr', path: path, name: name, value: value}

pub fn step_set_node_type(path, tag) =>
  {kind: 'set_node_type', path: path, tag: tag}

// ---------------------------------------------------------------------------
// Mark helpers (pure list ops over [symbol...])
// ---------------------------------------------------------------------------

fn has_mark_at(marks, m, i, n) {
  if (i >= n) { false }
  else if (marks[i] == m) { true }
  else { has_mark_at(marks, m, i + 1, n) }
}

pub fn has_mark(marks, m) => has_mark_at(marks, m, 0, len(marks))

fn without_mark_at(marks, m, i, n, acc) {
  if (i >= n) { acc }
  else if (marks[i] == m) { without_mark_at(marks, m, i + 1, n, acc) }
  else { without_mark_at(marks, m, i + 1, n, [*acc, marks[i]]) }
}

pub fn without_mark(marks, m) => without_mark_at(marks, m, 0, len(marks), [])

pub fn with_mark(marks, m) =>
  if (has_mark(marks, m)) { marks } else { [*marks, m] }

// ---------------------------------------------------------------------------
// step_apply
// ---------------------------------------------------------------------------

fn apply_replace_text(step, doc) {
  let leaf = node_at(doc, step.path)
  let pre  = slice(leaf.text, 0, step.from)
  let post = slice(leaf.text, step.to, len(leaf.text))
  let new_text = pre ++ step.text ++ post
  let new_leaf = {kind: 'text', text: new_text, marks: leaf.marks}
  replace_node_at(doc, step.path, new_leaf)
}

fn apply_replace(step, doc) =>
  splice_children_at(doc, step.parent, step.from, step.to - step.from, step.slice)

fn apply_replace_around(step, doc) {
  let parent = node_at(doc, step.parent)
  let gap = list_take(list_drop(parent.content, step.gap_from), step.gap_to - step.gap_from)
  let slice2 = list_splice(step.slice, step.insert, 0, gap)
  splice_children_at(doc, step.parent, step.from, step.to - step.from, slice2)
}

fn apply_add_mark(step, doc) {
  let leaf = node_at(doc, step.path)
  let new_leaf = {kind: 'text', text: leaf.text, marks: with_mark(leaf.marks, step.mark)}
  replace_node_at(doc, step.path, new_leaf)
}

fn apply_remove_mark(step, doc) {
  let leaf = node_at(doc, step.path)
  let new_leaf = {kind: 'text', text: leaf.text, marks: without_mark(leaf.marks, step.mark)}
  replace_node_at(doc, step.path, new_leaf)
}

fn replace_attr_at(attrs, name, value, i, n, found, acc) {
  if (i >= n) {
    if (found or value == null) { acc } else { [*acc, {name: name, value: value}] }
  }
  else if (attrs[i].name == name) {
    if (value == null) { replace_attr_at(attrs, name, value, i + 1, n, true, acc) }
    else { replace_attr_at(attrs, name, value, i + 1, n, true, [*acc, {name: name, value: value}]) }
  }
  else {
    replace_attr_at(attrs, name, value, i + 1, n, found, [*acc, attrs[i]])
  }
}
pub fn attrs_set(attrs, name, value) =>
  replace_attr_at(attrs, name, value, 0, len(attrs), false, [])

fn find_attr_at(attrs, name, i, n) {
  if (i >= n) { null }
  else if (attrs[i].name == name) { attrs[i].value }
  else { find_attr_at(attrs, name, i + 1, n) }
}
pub fn attrs_get(attrs, name) => find_attr_at(attrs, name, 0, len(attrs))

fn apply_set_attr(step, doc) {
  let n = node_at(doc, step.path)
  let new_attrs = attrs_set(n.attrs, step.name, step.value)
  let new_node = {kind: n.kind, tag: n.tag, attrs: new_attrs, content: n.content}
  replace_node_at(doc, step.path, new_node)
}

fn apply_set_node_type(step, doc) {
  let n = node_at(doc, step.path)
  let new_node = {kind: n.kind, tag: step.tag, attrs: n.attrs, content: n.content}
  replace_node_at(doc, step.path, new_node)
}

pub fn step_apply(step, doc) {
  if (step.kind == 'replace_text')      { apply_replace_text(step, doc) }
  else if (step.kind == 'replace')      { apply_replace(step, doc) }
  else if (step.kind == 'replace_around'){ apply_replace_around(step, doc) }
  else if (step.kind == 'add_mark')     { apply_add_mark(step, doc) }
  else if (step.kind == 'remove_mark')  { apply_remove_mark(step, doc) }
  else if (step.kind == 'set_attr')     { apply_set_attr(step, doc) }
  else if (step.kind == 'set_node_type'){ apply_set_node_type(step, doc) }
  else { doc }
}

// ---------------------------------------------------------------------------
// step_invert — produces the step that undoes `step`, given the doc BEFORE
// `step` was applied.
// ---------------------------------------------------------------------------

fn invert_replace_text(step, doc_before) {
  let leaf = node_at(doc_before, step.path)
  let old_text = slice(leaf.text, step.from, step.to)
  let new_to = step.from + len(step.text)
  step_replace_text(step.path, step.from, new_to, old_text)
}

fn invert_replace(step, doc_before) {
  let parent = node_at(doc_before, step.parent)
  let old_slice = list_take(list_drop(parent.content, step.from), step.to - step.from)
  let new_to = step.from + len(step.slice)
  step_replace(step.parent, step.from, new_to, old_slice)
}

fn invert_replace_around(step, doc_before) {
  let parent = node_at(doc_before, step.parent)
  let old_slice = list_take(list_drop(parent.content, step.from), step.to - step.from)
  let new_to = step.from + len(step.slice) + (step.gap_to - step.gap_from)
  step_replace(step.parent, step.from, new_to, old_slice)
}

fn invert_set_attr(step, doc_before) {
  let n = node_at(doc_before, step.path)
  step_set_attr(step.path, step.name, attrs_get(n.attrs, step.name))
}

fn invert_set_node_type(step, doc_before) {
  let n = node_at(doc_before, step.path)
  step_set_node_type(step.path, n.tag)
}

pub fn step_invert(step, doc_before) {
  if (step.kind == 'replace_text')      { invert_replace_text(step, doc_before) }
  else if (step.kind == 'replace')      { invert_replace(step, doc_before) }
  else if (step.kind == 'replace_around'){ invert_replace_around(step, doc_before) }
  else if (step.kind == 'add_mark')     { step_remove_mark(step.path, step.mark) }
  else if (step.kind == 'remove_mark')  { step_add_mark(step.path, step.mark) }
  else if (step.kind == 'set_attr')     { invert_set_attr(step, doc_before) }
  else if (step.kind == 'set_node_type'){ invert_set_node_type(step, doc_before) }
  else { step }
}

// ---------------------------------------------------------------------------
// step_map — translate a SourcePos through this step.
//
// Bias rule: positions strictly after the affected range shift; positions at
// the boundary are pushed forward (post-bias, like ProseMirror's default).
// ---------------------------------------------------------------------------

fn map_replace_text(step, p) {
  if (not path_equal(p.path, step.path)) { p }
  else if (p.offset < step.from) { p }
  else if (p.offset >= step.to) {
    pos(p.path, p.offset + len(step.text) - (step.to - step.from))
  } else {
    pos(p.path, step.from + len(step.text))
  }
}

// True iff p sits strictly inside the parent affected by `step` (one level deep).
fn under_replace_parent(p, step) {
  if (len(p.path) <= len(step.parent)) { false }
  else { path_is_prefix(step.parent, p.path) }
}

fn map_replace(step, p) {
  let pp = step.parent
  let depth = len(pp)
  let same_parent_pos = (len(p.path) == depth and path_equal(p.path, pp))
  let inside = under_replace_parent(p, step)
  if (not same_parent_pos and not inside) { p }
  else if (same_parent_pos) {
    // p is on the parent itself; offset is a child index
    if (p.offset < step.from) { p }
    else if (p.offset >= step.to) {
      pos(p.path, p.offset + len(step.slice) - (step.to - step.from))
    } else {
      pos(p.path, step.from + len(step.slice))
    }
  } else {
    // p is inside one of the parent's children; check which one
    let ci = p.path[depth]
    if (ci < step.from) { p }
    else if (ci >= step.to) {
      // shift the child index
      let new_ci = ci + len(step.slice) - (step.to - step.from)
      let new_path = [*list_take(p.path, depth), new_ci, *list_drop(p.path, depth + 1)]
      pos(new_path, p.offset)
    } else {
      // p was inside a deleted child — collapse to the boundary
      pos(pp, step.from + len(step.slice))
    }
  }
}

fn replace_around_new_size(step) => len(step.slice) + (step.gap_to - step.gap_from)

fn map_replace_around(step, p) {
  let pp = step.parent
  let depth = len(pp)
  let same_parent_pos = (len(p.path) == depth and path_equal(p.path, pp))
  let inside = under_replace_parent(p, step)
  if (not same_parent_pos and not inside) { p }
  else if (same_parent_pos) {
    if (p.offset < step.from) { p }
    else if (p.offset >= step.gap_from and p.offset <= step.gap_to) {
      pos(p.path, step.from + step.insert + (p.offset - step.gap_from))
    }
    else if (p.offset >= step.to) {
      pos(p.path, p.offset + replace_around_new_size(step) - (step.to - step.from))
    } else {
      pos(p.path, step.from + step.insert)
    }
  } else {
    let ci = p.path[depth]
    if (ci < step.from) { p }
    else if (ci >= step.to) {
      let new_ci = ci + replace_around_new_size(step) - (step.to - step.from)
      let new_path = [*list_take(p.path, depth), new_ci, *list_drop(p.path, depth + 1)]
      pos(new_path, p.offset)
    }
    else if (ci >= step.gap_from and ci < step.gap_to) {
      let new_ci = step.from + step.insert + (ci - step.gap_from)
      let new_path = [*list_take(p.path, depth), new_ci, *list_drop(p.path, depth + 1)]
      pos(new_path, p.offset)
    } else {
      pos(pp, step.from + step.insert)
    }
  }
}

pub fn step_map(step, p) {
  if (step.kind == 'replace_text') { map_replace_text(step, p) }
  else if (step.kind == 'replace') { map_replace(step, p) }
  else if (step.kind == 'replace_around') { map_replace_around(step, p) }
  else { p }  // mark / attr / node-type changes don't move positions
}

// Map a list of positions through one step.
pub fn step_map_all(step, ps) =>
  [for (p in ps) step_map(step, p)]
