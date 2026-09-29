// mod_dom_bridge.ls — bidirectional bridge between an editor doc tree and
// the Radiant DOM/render-map (Phase R7, Radiant integration step 1).
//
// Design contract
// ---------------
// The Radiant side records, at render time, the *source path* for every
// rendered subtree (a list of child indices into the editor doc root).
// `render_map` is augmented to store this path alongside the source item;
// `render_map_reverse_lookup(dom_native_element)` then yields a stable
// `(source_path, source_item)` tuple even though Lambda map equality is
// structural and unreliable for identity tests.
//
// In the other direction, when a command computes a new SourcePos, the
// C++ side resolves it to a DOM boundary by walking the path from the
// recorded doc root and consulting `render_map_get_result` on each step.
//
// This module contains only the path-arithmetic helpers used by both
// directions. Everything is pure: no identity comparisons.

import .mod_doc
import .mod_source_pos

// ---------------------------------------------------------------------------
// DOM lookup payload (what the C++ side fills and passes in)
// ---------------------------------------------------------------------------
//
//   {path: <list-of-int>, dom_offset: <int>, hit_kind: 'text' | 'element'}
//
// `path`        — recorded source path of the subtree the DomNode renders
// `dom_offset`  — UTF-16 code-unit offset within a DomText, OR
//                 child index within a DomElement
// `hit_kind`    — which case applies

pub fn dom_lookup(path, dom_offset, hit_kind) =>
  {path: path, dom_offset: dom_offset, hit_kind: hit_kind}

// ---------------------------------------------------------------------------
// DOM lookup → SourcePos
// ---------------------------------------------------------------------------
// For text hits, the offset comes through unchanged (the C++ side has
// already converted UTF-16 ↔ UTF-8 to match the editor's storage). For
// element hits, the offset is the child index, which is exactly what
// `pos.offset` means at a non-text path. Returns null for an empty path
// (no rendered subtree could match) or unknown `hit_kind`.

pub fn source_pos_from_dom(lookup) {
  if (lookup.path == null) { null }
  else if (lookup.hit_kind == 'text' or lookup.hit_kind == 'element') {
    pos(lookup.path, lookup.dom_offset)
  }
  else { null }
}

// Drag selection: anchor + head DOM lookups → SourceSelection
pub fn source_selection_from_dom(anchor_lookup, head_lookup) {
  let a = source_pos_from_dom(anchor_lookup)
  let h = source_pos_from_dom(head_lookup)
  if (a == null or h == null) { null }
  else { text_selection(a, h) }
}

fn schema_entry_for_node(schema, node) =>
  if (not is_node(node)) { null } else { schema[node.tag] }

fn is_selectable_entry(entry) =>
  entry != null and (entry.selectable == true or entry.atomic == true) and entry.editable != true

fn nearest_selectable_at(schema, root, path, depth) {
  if (depth < 0) { null }
  else {
    let p = path_take(path, depth)
    let n = node_at(root, p)
    let e = schema_entry_for_node(schema, n)
    if (is_selectable_entry(e)) { p }
    else { nearest_selectable_at(schema, root, path, depth - 1) }
  }
}

pub fn nearest_selectable_path(schema, root, path) =>
  nearest_selectable_at(schema, root, path, len(path))

pub fn source_selection_from_dom_with_schema(schema, root, anchor_lookup, head_lookup) {
  let a = source_pos_from_dom(anchor_lookup)
  let h = source_pos_from_dom(head_lookup)
  if (a == null or h == null) { null }
  else {
    let ap = nearest_selectable_path(schema, root, a.path)
    let hp = nearest_selectable_path(schema, root, h.path)
    if (ap != null and hp != null and path_equal(ap, hp)) { node_selection(ap) }
    else { text_selection(a, h) }
  }
}

// ---------------------------------------------------------------------------
// SourcePos → DOM lookup descriptor
// ---------------------------------------------------------------------------
// Returns the subtree at `path` (so the C++ side can call
// `render_map_get_result` to obtain the rendered DomNode) plus the offset
// to apply within that DomNode and its hit_kind.
//
//   {subtree: <node|text>, path: <path>, dom_offset: <int>, hit_kind: ...}
//
// Returns null if the path is invalid against `root`.

pub fn dom_lookup_from_source_pos(root, p) {
  let n = node_at(root, p.path)
  if (n == null) { null }
  else {
    let kind = if (is_text(n)) { 'text' } else { 'element' }
    {subtree: n, path: p.path, dom_offset: p.offset, hit_kind: kind}
  }
}

pub fn dom_lookup_from_source_selection(root, sel) {
  if (sel.kind == 'node') {
    dom_lookup_from_source_pos(root, pos(sel.path, 0))
  } else if (sel.kind == 'text') {
    let a = dom_lookup_from_source_pos(root, sel.anchor)
    let h = dom_lookup_from_source_pos(root, sel.head)
    if (a == null or h == null) { null }
    else { {anchor: a, head: h} }
  } else { null }
}

// ---------------------------------------------------------------------------
// Ancestor walk — enumerate every (path, node) pair on the path from the
// doc root down to (and including) the resolved subtree. The C++ side
// uses this when an event bubbles: it can ask which ancestor in the
// editor source tree owns a handler. Returned in root→leaf order.
// ---------------------------------------------------------------------------

fn walk_at(root, path, i, n, acc) {
  if (i > n) { acc }
  else {
    let prefix = path_take(path, i)
    let sub = node_at(root, prefix)
    if (sub == null) { acc }
    else { walk_at(root, path, i + 1, n, [*acc, {path: prefix, node: sub}]) }
  }
}
fn path_take_at(p, k, i, acc) {
  if (i >= k) { acc } else { path_take_at(p, k, i + 1, [*acc, p[i]]) }
}
pub fn path_take(p, k) => path_take_at(p, k, 0, [])

pub fn ancestors_along(root, path) =>
  walk_at(root, path, 0, len(path), [])
