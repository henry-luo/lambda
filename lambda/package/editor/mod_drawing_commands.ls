// mod_drawing_commands.ls — drawing-layer commands
// (Radiant Rich Editor, Stage 4 — §5, §13.1).
//
// Lambda port of test/editor-js/src/drawing/commands.ts. Every command compiles
// down to the existing mod_step steps (`set_attr`, `replace`) — there are NO new
// step kinds (Stage-4 §5.1). A command is (state, args) -> Transaction | null,
// where null means "the command does not apply".
//
//   state = {doc: Doc, selection: Selection|null}
//
// Note on ids: the JS reference uses a mutable module-level counter inside the
// shape constructors. Lambda is pure functional, so the constructors here take an
// explicit `id` argument — the tool/caller allocates ids deterministically.

import .mod_doc
import .mod_step
import .mod_source_pos
import .mod_transaction
import .mod_geom

// ---------------------------------------------------------------------------
// cmd_insert_shape — append a drawing-object to a layer
//   args = {drawing_path, layer_index (default 0), shape}
// ---------------------------------------------------------------------------

pub fn cmd_insert_shape(state, args) {
  let drawing = node_at(state.doc, args.drawing_path)
  if (drawing == null or not is_node(drawing) or drawing.tag != 'drawing') { null }
  else {
    let layer_idx = if (args.layer_index == null) { 0 } else { args.layer_index }
    if (layer_idx < 0 or layer_idx >= len(drawing.content)) { null }
    else {
      let layer = drawing.content[layer_idx]
      if (not is_node(layer) or layer.tag != 'layer') { null }
      else {
        let layer_path = [*args.drawing_path, layer_idx]
        let insert_at = len(layer.content)
        tx_step(tx_begin(state.doc, state.selection),
                step_replace(layer_path, insert_at, insert_at, [args.shape]))
      }
    }
  }
}

// ---------------------------------------------------------------------------
// cmd_move_shapes — delta-translate one or many shapes
//   args = {shape_paths, dx, dy}
// ---------------------------------------------------------------------------

fn move_one(tx, p, dx, dy) {
  let n = node_at(tx.doc_after, p)
  if (n == null or not is_node(n)) { tx }
  else {
    let tx1 = if (dx != 0.0) {
        let cur = get_shape_attr_number(n, 'x', 0.0)
        tx_step(tx, step_set_attr(p, 'x', cur + dx))
      } else { tx }
    if (dy != 0.0) {
      let after = node_at(tx1.doc_after, p)
      let cur = get_shape_attr_number(after, 'y', 0.0)
      tx_step(tx1, step_set_attr(p, 'y', cur + dy))
    } else { tx1 }
  }
}

fn move_at(tx, paths, dx, dy, i, n) {
  if (i >= n) { tx }
  else { move_at(move_one(tx, paths[i], dx, dy), paths, dx, dy, i + 1, n) }
}

pub fn cmd_move_shapes(state, args) {
  if (len(args.shape_paths) == 0) { null }
  else if (args.dx == 0.0 and args.dy == 0.0) { null }
  else {
    let tx = move_at(tx_begin(state.doc, state.selection),
                     args.shape_paths, args.dx, args.dy, 0, len(args.shape_paths))
    if (len(tx.steps) == 0) { null } else { tx }
  }
}

// ---------------------------------------------------------------------------
// cmd_resize_shape — set new geometry on one shape
//   args = {shape_path, x?, y?, width?, height?}   (absent fields are skipped)
// ---------------------------------------------------------------------------

fn resize_step(tx, p, name, val) {
  if (val == null) { tx }
  else {
    let n = node_at(tx.doc_after, p)
    let cur = get_shape_attr_number(n, name, 0.0)
    if (val == cur) { tx } else { tx_step(tx, step_set_attr(p, name, val)) }
  }
}

pub fn cmd_resize_shape(state, args) {
  let n = node_at(state.doc, args.shape_path)
  if (n == null or not is_node(n)) { null }
  else {
    let tx0 = tx_begin(state.doc, state.selection)
    let tx1 = resize_step(tx0, args.shape_path, 'x', args.x)
    let tx2 = resize_step(tx1, args.shape_path, 'y', args.y)
    let tx3 = resize_step(tx2, args.shape_path, 'width', args.width)
    let tx4 = resize_step(tx3, args.shape_path, 'height', args.height)
    if (len(tx4.steps) == 0) { null } else { tx4 }
  }
}

// ---------------------------------------------------------------------------
// cmd_rotate_shape / cmd_set_shape_attr
// ---------------------------------------------------------------------------

pub fn cmd_rotate_shape(state, shape_path, angle_deg) {
  let n = node_at(state.doc, shape_path)
  if (n == null or not is_node(n)) { null }
  else if (get_shape_attr_number(n, 'rotate', 0.0) == angle_deg) { null }
  else { tx_step(tx_begin(state.doc, state.selection), step_set_attr(shape_path, 'rotate', angle_deg)) }
}

pub fn cmd_set_shape_attr(state, shape_path, name, value) {
  let n = node_at(state.doc, shape_path)
  if (n == null or not is_node(n)) { null }
  else { tx_step(tx_begin(state.doc, state.selection), step_set_attr(shape_path, name, value)) }
}

// ---------------------------------------------------------------------------
// cmd_delete_shapes — delete one or many shapes
// Sort paths in descending document order so later deletes don't shift the
// indices of earlier ones.
// ---------------------------------------------------------------------------

fn path_seen(acc, p, i, n) {
  if (i >= n) { false }
  else if (path_equal(acc[i], p)) { true }
  else { path_seen(acc, p, i + 1, n) }
}

fn unique_paths_at(ps, i, n, acc) {
  if (i >= n) { acc }
  else if (path_seen(acc, ps[i], 0, len(acc))) { unique_paths_at(ps, i + 1, n, acc) }
  else { unique_paths_at(ps, i + 1, n, [*acc, ps[i]]) }
}

// insertion sort, descending document order (path_compare(b, a) ascending => desc)
fn insert_desc(sorted, p, i, n, acc) {
  if (i >= n) { [*acc, p] }
  else if (path_compare(p, sorted[i]) > 0) { list_concat([*acc, p], list_drop(sorted, i)) }
  else { insert_desc(sorted, p, i + 1, n, [*acc, sorted[i]]) }
}

fn sort_desc_at(ps, i, n, acc) {
  if (i >= n) { acc }
  else { sort_desc_at(ps, i + 1, n, insert_desc(acc, ps[i], 0, len(acc), [])) }
}

fn delete_at(tx, paths, i, n) {
  if (i >= n) { tx }
  else {
    let p = paths[i]
    let node = node_at(tx.doc_after, p)
    if (node == null) { delete_at(tx, paths, i + 1, n) }
    else {
      let parent = parent_path(p)
      let idx = last_index(p)
      if (idx < 0) { delete_at(tx, paths, i + 1, n) }
      else { delete_at(tx_step(tx, step_replace(parent, idx, idx + 1, [])), paths, i + 1, n) }
    }
  }
}

pub fn cmd_delete_shapes(state, shape_paths) {
  if (len(shape_paths) == 0) { null }
  else {
    let uniq = unique_paths_at(shape_paths, 0, len(shape_paths), [])
    let sorted = sort_desc_at(uniq, 0, len(uniq), [])
    let tx = delete_at(tx_begin(state.doc, state.selection), sorted, 0, len(sorted))
    if (len(tx.steps) == 0) { null } else { tx }
  }
}

// ---------------------------------------------------------------------------
// cmd_bring_to_front / cmd_send_to_back — reorder z within the parent
// ---------------------------------------------------------------------------

pub fn cmd_bring_to_front(state, shape_path) {
  let n = node_at(state.doc, shape_path)
  if (n == null or not is_node(n)) { null }
  else {
    let parent = parent_path(shape_path)
    let parent_node = node_at(state.doc, parent)
    if (parent_node == null or not is_node(parent_node)) { null }
    else {
      let idx = last_index(shape_path)
      if (idx < 0 or idx == len(parent_node.content) - 1) { null }
      else {
        let tx0 = tx_begin(state.doc, state.selection)
        let tx1 = tx_step(tx0, step_replace(parent, idx, idx + 1, []))
        let end_idx = len(parent_node.content) - 1
        tx_step(tx1, step_replace(parent, end_idx, end_idx, [n]))
      }
    }
  }
}

pub fn cmd_send_to_back(state, shape_path) {
  let n = node_at(state.doc, shape_path)
  if (n == null or not is_node(n)) { null }
  else {
    let parent = parent_path(shape_path)
    let parent_node = node_at(state.doc, parent)
    if (parent_node == null or not is_node(parent_node)) { null }
    else {
      let idx = last_index(shape_path)
      if (idx < 0 or idx == 0) { null }
      else {
        let tx0 = tx_begin(state.doc, state.selection)
        let tx1 = tx_step(tx0, step_replace(parent, idx, idx + 1, []))
        tx_step(tx1, step_replace(parent, 0, 0, [n]))
      }
    }
  }
}

// ---------------------------------------------------------------------------
// Shape constructors (convenience for tools and tests)
//   geom  = {x, y, width, height}
//   style = {fill?, stroke?, stroke-width?}   (absent fields fall back to schema defaults)
// ---------------------------------------------------------------------------

fn def(v, fallback) => if (v == null) { fallback } else { v }

pub fn make_rect_shape(id, geom, style) =>
  node_attrs('shape', [
    {name: 'id',           value: id},
    {name: 'kind',         value: 'rect'},
    {name: 'x',            value: geom.x},
    {name: 'y',            value: geom.y},
    {name: 'width',        value: geom.width},
    {name: 'height',       value: geom.height},
    {name: 'fill',         value: def(style.fill, "transparent")},
    {name: 'stroke',       value: def(style.stroke, "#000")},
    {name: 'stroke-width', value: def(style['stroke-width'], 1.0)}
  ], [])

pub fn make_ellipse_shape(id, geom, style) =>
  node_attrs('shape', [
    {name: 'id',     value: id},
    {name: 'kind',   value: 'ellipse'},
    {name: 'x',      value: geom.x},
    {name: 'y',      value: geom.y},
    {name: 'width',  value: geom.width},
    {name: 'height', value: geom.height},
    {name: 'fill',   value: def(style.fill, "transparent")},
    {name: 'stroke', value: def(style.stroke, "#000")}
  ], [])

pub fn make_line_shape(id, geom, style) =>
  node_attrs('shape', [
    {name: 'id',           value: id},
    {name: 'kind',         value: 'line'},
    {name: 'x',            value: geom.x},
    {name: 'y',            value: geom.y},
    {name: 'width',        value: geom.width},
    {name: 'height',       value: geom.height},
    {name: 'stroke',       value: def(style.stroke, "#000")},
    {name: 'stroke-width', value: def(style['stroke-width'], 1.0)}
  ], [])

fn opt_attr(acc, name, val) => if (val == null) { acc } else { [*acc, {name: name, value: val}] }

// args = {from_shape?, to_shape?, from_x?, from_y?, to_x?, to_y?, routing?}
pub fn make_connector(id, args) {
  let base = [{name: 'id', value: id}, {name: 'routing', value: def(args.routing, 'orthogonal')}]
  let a1 = opt_attr(base, 'from-shape', args.from_shape)
  let a2 = opt_attr(a1, 'to-shape', args.to_shape)
  let a3 = opt_attr(a2, 'from-x', args.from_x)
  let a4 = opt_attr(a3, 'from-y', args.from_y)
  let a5 = opt_attr(a4, 'to-x', args.to_x)
  let a6 = opt_attr(a5, 'to-y', args.to_y)
  node_attrs('connector', a6, [])
}
