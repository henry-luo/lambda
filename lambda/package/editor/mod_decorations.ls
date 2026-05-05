// mod_decorations.ls — non-document overlay set (Phase R5).
//
// Decorations let UI affordances (find/replace highlights, spellcheck squiggles,
// collaborator carets) be painted over the rendered doc without entering the
// source tree or undo history. They are pure values — a session keeps the
// current set in its state and renders it on top of the view tree.
//
// Decoration shape:
//   { kind: 'inline', from: SourcePos, to: SourcePos, attrs: map }   // span
//   { kind: 'node',   path: SourcePath,                attrs: map }   // wrap one node
//   { kind: 'widget', at:   SourcePos, render: any,    attrs: map }   // injected glyph
//
// `attrs` is a map intended for the renderer (e.g. {class: "find-hit"}).

import .mod_doc
import .mod_source_pos
import .mod_step

// ---------------------------------------------------------------------------
// Constructors
// ---------------------------------------------------------------------------

pub fn deco_inline(from, to, attrs) =>
  {kind: 'inline', from: from, to: to, attrs: attrs}

pub fn deco_node(path, attrs) =>
  {kind: 'node', path: path, attrs: attrs}

pub fn deco_widget(at, render, attrs) =>
  {kind: 'widget', at: at, render: render, attrs: attrs}

// A decoration set is just an immutable list of decorations. We keep a tag so
// callers can store multiple sets keyed by source (e.g. spellcheck vs. search).
pub fn deco_set(items) => {kind: 'deco_set', items: items}

pub fn deco_empty() => deco_set([])

// ---------------------------------------------------------------------------
// Mutation (returns a NEW set)
// ---------------------------------------------------------------------------

pub fn deco_add(set, d) => deco_set([*set.items, d])

fn keep_not_class(d, cls) => d.attrs.class != cls

fn filter_class_at(items, cls, i, n, acc) {
  if (i >= n) { acc }
  else if (keep_not_class(items[i], cls)) {
    filter_class_at(items, cls, i + 1, n, [*acc, items[i]])
  } else { filter_class_at(items, cls, i + 1, n, acc) }
}

pub fn deco_remove_class(set, cls) =>
  deco_set(filter_class_at(set.items, cls, 0, len(set.items), []))

pub fn deco_concat(a, b) =>
  deco_set([*a.items, *b.items])

// ---------------------------------------------------------------------------
// Query — decorations touching a position or a path
// ---------------------------------------------------------------------------

fn d_covers_pos(d, p) {
  if (d.kind == 'inline') {
    (pos_compare(d.from, p) <= 0) and (pos_compare(p, d.to) <= 0)
  }
  else if (d.kind == 'widget') { pos_equal(d.at, p) }
  else if (d.kind == 'node')   { path_is_prefix(d.path, p.path) }
  else { false }
}

fn at_pos_at(items, p, i, n, acc) {
  if (i >= n) { acc }
  else if (d_covers_pos(items[i], p)) { at_pos_at(items, p, i + 1, n, [*acc, items[i]]) }
  else { at_pos_at(items, p, i + 1, n, acc) }
}

pub fn deco_at(set, p) => at_pos_at(set.items, p, 0, len(set.items), [])

// ---------------------------------------------------------------------------
// Step-driven mapping — translate the whole set through a Step (so that an
// edit shifts every overlay correctly without rebuilding from scratch).
// ---------------------------------------------------------------------------

fn map_one_deco(d, map_pos_fn) {
  if (d.kind == 'inline') { deco_inline(map_pos_fn(d.from), map_pos_fn(d.to), d.attrs) }
  else if (d.kind == 'widget') { deco_widget(map_pos_fn(d.at), d.render, d.attrs) }
  else { d }
}

fn map_items_at(items, map_pos_fn, i, n, acc) {
  if (i >= n) { acc }
  else { map_items_at(items, map_pos_fn, i + 1, n, [*acc, map_one_deco(items[i], map_pos_fn)]) }
}

pub fn deco_map(set, map_pos_fn) =>
  deco_set(map_items_at(set.items, map_pos_fn, 0, len(set.items), []))

fn map_pos_through_steps(steps, p, i, n) {
  if (i >= n) { p }
  else { map_pos_through_steps(steps, step_map(steps[i], p), i + 1, n) }
}

fn map_path_through_steps(steps, path) =>
  map_pos_through_steps(steps, pos(path, 0), 0, len(steps)).path

fn map_one_deco_tx(d, tx) {
  if (d.kind == 'inline') {
    deco_inline(map_pos_through_steps(tx.steps, d.from, 0, len(tx.steps)),
                map_pos_through_steps(tx.steps, d.to, 0, len(tx.steps)), d.attrs)
  }
  else if (d.kind == 'widget') {
    deco_widget(map_pos_through_steps(tx.steps, d.at, 0, len(tx.steps)), d.render, d.attrs)
  }
  else if (d.kind == 'node') { deco_node(map_path_through_steps(tx.steps, d.path), d.attrs) }
  else { d }
}

fn map_items_tx_at(items, tx, i, n, acc) {
  if (i >= n) { acc }
  else { map_items_tx_at(items, tx, i + 1, n, [*acc, map_one_deco_tx(items[i], tx)]) }
}

pub fn deco_map_tx(set, tx) =>
  deco_set(map_items_tx_at(set.items, tx, 0, len(set.items), []))

// ---------------------------------------------------------------------------
// Find — produce a deco_set highlighting every occurrence of `needle` in a
// single text leaf at `path`. (Multi-leaf find is the same idea applied to
// each leaf and is left to the caller / renderer.)
// ---------------------------------------------------------------------------

fn find_all_in(text, needle, start, n, hits) {
  if (start > n - len(needle)) { hits }
  else {
    let chunk = slice(text, start, start + len(needle))
    if (chunk == needle) { find_all_in(text, needle, start + len(needle), n, [*hits, start]) }
    else { find_all_in(text, needle, start + 1, n, hits) }
  }
}

pub fn find_in_leaf(text, needle) {
  if (len(needle) == 0) { [] }
  else { find_all_in(text, needle, 0, len(text), []) }
}

pub fn find_decorations(path, text, needle, attrs) {
  let hits = find_in_leaf(text, needle)
  let n = len(needle)
  let items = [for (off in hits) deco_inline(pos(path, off), pos(path, off + n), attrs)]
  deco_set(items)
}

fn child_path(path, i) => [*path, i]

fn find_decorations_children(children, path, needle, attrs, i, n, acc) {
  if (i >= n) { acc }
  else {
    let child_set = find_decorations_in_node(children[i], child_path(path, i), needle, attrs)
    find_decorations_children(children, path, needle, attrs, i + 1, n, deco_concat(acc, child_set))
  }
}

fn find_decorations_in_node(n, path, needle, attrs) {
  if (is_text(n)) { find_decorations(path, n.text, needle, attrs) }
  else if (is_node(n)) { find_decorations_children(n.content, path, needle, attrs, 0, len(n.content), deco_empty()) }
  else { deco_empty() }
}

pub fn find_decorations_in_doc(doc, needle, attrs) =>
  find_decorations_in_node(doc, [], needle, attrs)
