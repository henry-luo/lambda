// drawing.ls — the SVG drawing surface of lambda.edit
// (vibe/radiant/Radiant_Design_Edit_Mode.md §6, §7).
//
// The surface projects the drawing model as inline SVG. Each top-level object
// carries its model index (`data-edit-path`), so a press on it — Radiant
// hit-tests SVG paint geometry — names the object. Scripts, event-handler
// attributes, and other namespaces' metadata are not projected. Selection
// frames and resize handles are an overlay drawn after the drawing from
// session state; they never enter the model.
//
// Object edits are Transactions of attribute and child Steps on the source
// tree (lambda.editor), so undo/redo, dirty state and saving work as they do
// for rich text. A gesture runs from press to release: release commits one
// transaction and Escape before it cancels. Tool, selection, and zoom are
// session state that never dirties the file; zoom never rewrites the viewBox.

import .model
import sv: lambda.edit.svg
import lambda.editor.mod_doc
import lambda.editor.mod_step
import lambda.editor.mod_transaction

// ---------------------------------------------------------------------------
// Session state
// ---------------------------------------------------------------------------

// A new drawing session state (`ds` below): the Select tool, nothing picked,
// at 100%. A picked entry is {index, box}: the object's position among the
// root's children and its frame in user units (null until measured).
pub fn new_state() =>
  {tool: 'select', picked: [], gesture: null, zoom: 1.0,
   style: {fill: "#dbe9f6", stroke: "#1f2328", width: "2"}}

pub fn picked_indices(ds) => [for (p in ds.picked) p.index]

// ---------------------------------------------------------------------------
// Numbers and boxes
// ---------------------------------------------------------------------------

// A number from an attribute ("12", "12.5", "12px"), or `fallback`.
pub fn num(v, fallback) any {
  if (v == null) fallback
  else {
    let s = trim(string(v))
    float(if (ends_with(s, "px")) slice(s, 0, len(s) - 2) else s) or fallback
  }
}

// Coordinates are written with at most two decimals.
pub fn fmt(v) => string(round(v * 100) / 100)

pub fn box(x, y, w, h) => {x: x, y: y, w: w, h: h}

fn numbers_in(s) => [for (p in split(replace(string(s), ",", " "), " ") where p != "") num(p, 0.0)]

// The drawing's user space: its viewBox, else its width and height.
pub fn view_box(doc) {
  let parts = if (attr_get(doc, 'viewBox') == null) [] else numbers_in(attr_get(doc, 'viewBox'))
  if (len(parts) == 4) box(parts[0], parts[1], parts[2], parts[3])
  else box(0.0, 0.0, num(attr_get(doc, 'width'), 300.0), num(attr_get(doc, 'height'), 150.0))
}

fn has(s, part) bool => contains(s, part) or false

// ---------------------------------------------------------------------------
// Objects and their geometry
// ---------------------------------------------------------------------------

let object_tags = ['rect', 'circle', 'ellipse', 'line', 'polyline', 'polygon', 'path', 'text',
                   'image', 'use', 'g', 'a', 'switch', 'foreignObject']

pub fn is_object(n) => (is_node(n) and member(object_tags, n.tag)) or false

fn attr_num(n, key) => num(attr_get(n, key), 0.0)

// The box an untransformed shape's own attributes define, or null when its
// geometry is not one the tools can resize.
fn geometry_box(n) {
  let tag = n.tag
  if (not is_node(n) or attr_get(n, 'transform') != null) null
  else if (tag == 'rect' or tag == 'image' or tag == 'foreignObject')
    box(attr_num(n, 'x'), attr_num(n, 'y'), attr_num(n, 'width'), attr_num(n, 'height'))
  else if (tag == 'circle')
    box(attr_num(n, 'cx') - attr_num(n, 'r'), attr_num(n, 'cy') - attr_num(n, 'r'), 2.0 * attr_num(n, 'r'), 2.0 * attr_num(n, 'r'))
  else if (tag == 'ellipse')
    box(attr_num(n, 'cx') - attr_num(n, 'rx'), attr_num(n, 'cy') - attr_num(n, 'ry'), 2.0 * attr_num(n, 'rx'), 2.0 * attr_num(n, 'ry'))
  else if (tag == 'line')
    box(min(attr_num(n, 'x1'), attr_num(n, 'x2')), min(attr_num(n, 'y1'), attr_num(n, 'y2')),
        abs(attr_num(n, 'x2') - attr_num(n, 'x1')), abs(attr_num(n, 'y2') - attr_num(n, 'y1')))
  else null
}

pub fn resizable(n) => geometry_box(n) != null

// The frame to draw for an object: its own geometry when the tools know it,
// else the rendered box the surface measured.
pub fn frame_box(n, measured) any {
  let own = geometry_box(n)
  if (own != null) own else measured
}

// ---------------------------------------------------------------------------
// Transactions
// ---------------------------------------------------------------------------

fn apply_steps(tx, steps, i) => if (i >= len(steps)) tx else apply_steps(tx_step(tx, steps[i]), steps, i + 1)

fn tx_of(doc, steps) => if (len(steps) == 0) null else apply_steps(tx_begin(doc, null), steps, 0)

// Move: an untransformed shape with plain coordinates moves by its own
// attributes; anything else by a translate in its parent's user space,
// merged into a leading translate so repeated moves stay one transform.
fn coord_pairs(n) {
  let tag = n.tag
  if (tag == 'rect' or tag == 'image' or tag == 'use' or tag == 'foreignObject' or tag == 'text') [['x', 'y']]
  else if (tag == 'circle' or tag == 'ellipse') [['cx', 'cy']]
  else if (tag == 'line') [['x1', 'y1'], ['x2', 'y2']]
  else []
}

fn plain_number(v) => v == null or num(v, null) != null

// a text's own x/y do not move spans that position themselves
fn positioned_spans(n) =>
  any([for (c in n.content) is_node(c) and (attr_get(c, 'x') != null or attr_get(c, 'y') != null or positioned_spans(c))]) or false

fn moves_by_coords(n) {
  let pairs = coord_pairs(n)
  len(pairs) > 0 and attr_get(n, 'transform') == null and
    (all([for (p in pairs) for (a in p) plain_number(attr_get(n, a))]) or false) and
    not (n.tag == 'text' and positioned_spans(n))
}

fn translated(t, dx, dy) {
  let s = if (t == null) "" else trim(string(t))
  let end_at = index_of(s, ")")
  if (s == "") "translate(" ++ fmt(dx) ++ "," ++ fmt(dy) ++ ")"
  else if (starts_with(s, "translate(") and end_at != null) {
    let args = numbers_in(slice(s, 10, end_at))
    let tx = if (len(args) > 0) args[0] else 0.0
    let ty = if (len(args) > 1) args[1] else 0.0
    let rest = trim(slice(s, end_at + 1, len(s)))
    "translate(" ++ fmt(tx + dx) ++ "," ++ fmt(ty + dy) ++ ")" ++ (if (rest == "") "" else " " ++ rest)
  }
  else "translate(" ++ fmt(dx) ++ "," ++ fmt(dy) ++ ") " ++ s
}

fn move_steps(n, path, dx, dy) {
  if (moves_by_coords(n))
    [for (p in coord_pairs(n))
       for (s in [step_set_attr(path, p[0], fmt(attr_num(n, p[0]) + dx)),
                  step_set_attr(path, p[1], fmt(attr_num(n, p[1]) + dy))]) s]
  else [step_set_attr(path, 'transform', translated(attr_get(n, 'transform'), dx, dy))]
}

pub fn move_tx(doc, indices, dx, dy) =>
  if (dx == 0.0 and dy == 0.0) null
  else tx_of(doc, [for (i in indices) for (s in move_steps(doc.content[i], [i], dx, dy)) s])

// Resize: a handle ("nw", "n", ..., "w") moves the edges it names.
pub fn resized_box(b, handle, dx, dy) {
  let h = string(handle)
  let left = b.x + (if (has(h, "w")) dx else 0.0)
  let right = b.x + b.w + (if (has(h, "e")) dx else 0.0)
  let top = b.y + (if (has(h, "n")) dy else 0.0)
  let bottom = b.y + b.h + (if (has(h, "s")) dy else 0.0)
  box(min(left, right), min(top, bottom), max(abs(right - left), 1.0), max(abs(bottom - top), 1.0))
}

fn scale_to(v, from_start, from_size, to_start, to_size) =>
  if (from_size == 0.0) v + (to_start - from_start) else to_start + (v - from_start) * to_size / from_size

fn resize_steps(n, path, b) {
  let tag = n.tag
  let old = geometry_box(n)
  if (old == null) []
  else if (tag == 'circle')
    [step_set_attr(path, 'cx', fmt(b.x + b.w / 2.0)), step_set_attr(path, 'cy', fmt(b.y + b.h / 2.0)),
     step_set_attr(path, 'r', fmt(min(b.w, b.h) / 2.0))]
  else if (tag == 'ellipse')
    [step_set_attr(path, 'cx', fmt(b.x + b.w / 2.0)), step_set_attr(path, 'cy', fmt(b.y + b.h / 2.0)),
     step_set_attr(path, 'rx', fmt(b.w / 2.0)), step_set_attr(path, 'ry', fmt(b.h / 2.0))]
  else if (tag == 'line')
    [step_set_attr(path, 'x1', fmt(scale_to(attr_num(n, 'x1'), old.x, old.w, b.x, b.w))),
     step_set_attr(path, 'y1', fmt(scale_to(attr_num(n, 'y1'), old.y, old.h, b.y, b.h))),
     step_set_attr(path, 'x2', fmt(scale_to(attr_num(n, 'x2'), old.x, old.w, b.x, b.w))),
     step_set_attr(path, 'y2', fmt(scale_to(attr_num(n, 'y2'), old.y, old.h, b.y, b.h)))]
  else [step_set_attr(path, 'x', fmt(b.x)), step_set_attr(path, 'y', fmt(b.y)),
        step_set_attr(path, 'width', fmt(b.w)), step_set_attr(path, 'height', fmt(b.h))]
}

pub fn resize_tx(doc, index, b) => tx_of(doc, resize_steps(doc.content[index], [index], b))

// Ids: new objects and copies take ids no element of the drawing has.
fn own_id(n) => if (is_node(n) and attr_get(n, 'id') != null) [string(attr_get(n, 'id'))] else []
fn ids_in(n) => if (not is_node(n)) [] else [*own_id(n), *[for (c in n.content) for (i in ids_in(c)) i]]

fn fresh_id(taken, base, k) {
  let candidate = base ++ "-" ++ string(k)
  if (member(taken, candidate)) fresh_id(taken, base, k + 1) else candidate
}

pub fn insert_tx(doc, n) => tx_of(doc, [step_replace([], len(doc.content), len(doc.content), [n])])

fn paint(style, filled) =>
  [{name: 'fill', value: if (filled) style.fill else "none"}, {name: 'stroke', value: style.stroke},
   {name: 'stroke-width', value: style.width}]

// A shape drawn from press point `a` to release point `b`; a click without
// a drag places a default-sized shape.
pub fn new_shape(doc, tool, a, b, style) {
  let tag = if (tool == 'rect') 'rect' else if (tool == 'ellipse') 'ellipse' else 'line'
  let id = fresh_id(ids_in(doc), string(tag), 1)
  let clicked = abs(b.x - a.x) < 2.0 and abs(b.y - a.y) < 2.0
  let q = if (clicked) {x: a.x + 100.0, y: a.y + (if (tag == 'line') 0.0 else 60.0)} else b
  let r = box(min(a.x, q.x), min(a.y, q.y), abs(q.x - a.x), abs(q.y - a.y))
  let geometry = if (tag == 'rect')
      [{name: 'x', value: fmt(r.x)}, {name: 'y', value: fmt(r.y)},
       {name: 'width', value: fmt(r.w)}, {name: 'height', value: fmt(r.h)}]
    else if (tag == 'ellipse')
      [{name: 'cx', value: fmt(r.x + r.w / 2.0)}, {name: 'cy', value: fmt(r.y + r.h / 2.0)},
       {name: 'rx', value: fmt(r.w / 2.0)}, {name: 'ry', value: fmt(r.h / 2.0)}]
    else
      [{name: 'x1', value: fmt(a.x)}, {name: 'y1', value: fmt(a.y)},
       {name: 'x2', value: fmt(q.x)}, {name: 'y2', value: fmt(q.y)}]
  node_attrs(tag, [{name: 'id', value: id}, *geometry, *paint(style, tag != 'line')], [])
}

pub fn new_text(doc, p, words, style) =>
  node_attrs('text', [{name: 'id', value: fresh_id(ids_in(doc), "text", 1)},
                      {name: 'x', value: fmt(p.x)}, {name: 'y', value: fmt(p.y)},
                      {name: 'font-size', value: "20"}, {name: 'fill', value: style.stroke}], [text(words)])

pub fn delete_tx(doc, indices) =>
  tx_of(doc, [for (i in (sort(indices, 'desc') or [])) step_replace([], i, i + 1, [])])

// Duplicate: a copy's ids are fresh and every reference inside the copy
// (`url(#id)`, `#id` hrefs) follows them, so it never draws the original's
// gradient or clip twice under one id.
fn remap_value(v, pairs, i) {
  if (i >= len(pairs)) v
  else {
    let old_ref = "#" ++ pairs[i][0]
    let new_ref = "#" ++ pairs[i][1]
    let s = replace(v, "url(" ++ old_ref ++ ")", "url(" ++ new_ref ++ ")")
    remap_value(if (s == old_ref) new_ref else s, pairs, i + 1)
  }
}

fn remap_id(id, pairs) {
  let found = [for (p in pairs where p[0] == id) p[1]]
  if (len(found) > 0) found[0] else id
}

fn remap_node(n, pairs) {
  if (not is_node(n)) n
  else node_attrs(n.tag,
    [for (a in n.attrs)
       {name: a.name, value: if (a.name == 'id') remap_id(string(a.value), pairs) else remap_value(string(a.value), pairs, 0)}],
    [for (c in n.content) remap_node(c, pairs)])
}

fn copy_pairs(ids, taken, i, acc) {
  if (i >= len(ids)) acc
  else copy_pairs(ids, taken, i + 1, [*acc, [ids[i], fresh_id([*taken, *[for (p in acc) p[1]]], ids[i], 2)]])
}

fn offset_copy(doc, n) {
  let copy = remap_node(n, copy_pairs(ids_in(n), ids_in(doc), 0, []))
  apply_steps(tx_begin(copy, null), move_steps(copy, [], 10.0, 10.0), 0).doc_after
}

fn duplicate_at(tx, indices, k, j) {
  if (j >= len(indices)) tx
  else {
    let i = indices[j] + k
    let doc = tx.doc_after
    duplicate_at(tx_step(tx, step_replace([], i + 1, i + 1, [offset_copy(doc, doc.content[i])])), indices, k + 1, j + 1)
  }
}

// The transaction and the copies' indices (each lands right above its original).
pub fn duplicate_tx(doc, indices) {
  let ordered = sort(indices) or []
  if (len(ordered) == 0) null
  else {tx: duplicate_at(tx_begin(doc, null), ordered, 0, 0),
        picked: [for (j in 0 to len(ordered) - 1) ordered[j] + j + 1]}
}

// Paint order: to the front is the root's last child; to the back is before
// the first drawn object, so <defs> and metadata keep leading the file.
fn first_object(content, i) => if (i >= len(content) or is_object(content[i])) i else first_object(content, i + 1)

pub fn order_tx(doc, index, to_front) {
  let n = doc.content[index]
  let removed = tx_step(tx_begin(doc, null), step_replace([], index, index + 1, []))
  let at = if (to_front) len(removed.doc_after.content) else first_object(removed.doc_after.content, 0)
  if (at == index) null else {tx: tx_step(removed, step_replace([], at, at, [n])), index: at}
}

// Paint: a property set in the `style` attribute is changed there, since it
// overrides the presentation attribute; otherwise the attribute is set.
fn declarations(style) => [for (d in split(string(style), ";") where trim(d) != "") trim(d)]

fn declared_name(d) {
  let colon = index_of(d, ":")
  trim(if (colon == null) d else slice(d, 0, colon))
}

fn declared_value(d) {
  let colon = index_of(d, ":")
  if (colon == null) "" else trim(slice(d, colon + 1, len(d)))
}

fn style_value(style, prop) {
  let found = if (style == null) [] else [for (d in declarations(style) where declared_name(d) == prop) declared_value(d)]
  if (len(found) > 0) found[0] else null
}

// The paint an object shows for `prop` (a name string), or "".
pub fn paint_value(n, prop) {
  let styled = style_value(attr_get(n, 'style'), prop)
  let attr = [for (a in n.attrs where string(a.name) == prop) string(a.value)]
  if (styled != null) styled else if (len(attr) > 0) attr[0] else ""
}

fn restyled_declaration(d, changes) {
  let prop = declared_name(d)
  let found = [for (c in changes where c[0] == prop) c[1]]
  if (len(found) > 0) prop ++ ":" ++ found[0] else d
}

fn restyled(style, changes) => join([for (d in declarations(style)) restyled_declaration(d, changes)], ";")

// `changes` are [prop-name, value] pairs; empty values are left alone.
fn paint_steps(n, path, changes) {
  let style = attr_get(n, 'style')
  let wanted = [for (c in changes where c[1] != "") c]
  let in_style = [for (c in wanted where style_value(style, c[0]) != null) c]
  let as_attr = [for (c in wanted where style_value(style, c[0]) == null) c];
  [*(if (len(in_style) == 0) [] else [step_set_attr(path, 'style', restyled(style, in_style))]),
   *[for (c in as_attr) step_set_attr(path, symbol(c[0]), c[1])]]
}

pub fn paint_tx(doc, indices, changes) =>
  tx_of(doc, [for (i in indices) for (s in paint_steps(doc.content[i], [i], changes)) s])

// Text: a text object holding one run can be retyped as a whole.
pub fn text_editable(n) => (is_node(n) and n.tag == 'text' and len(n.content) <= 1 and
                            (all([for (c in n.content) is_text(c)]) or false)) or false

pub fn retext_tx(doc, index, words) =>
  tx_of(doc, [step_replace([index], 0, len(doc.content[index].content), [text(words)])])

// ---------------------------------------------------------------------------
// Projection
// ---------------------------------------------------------------------------

// Scripts stay inactive data; another namespace's metadata (sodipodi:,
// inkscape:) has no rendering.
fn projected(n) => not is_node(n) or not (n.tag == 'script' or has(string(n.tag), ":"))

fn is_handler(a) => starts_with(string(a.name), "on")

fn projected_attrs(n, path) {
  let own = [for (a in n.attrs where not is_handler(a)) a]
  if (len(path) == 1 and is_object(n)) [*own, {name: 'data-edit-path', value: string(path[0])}] else own
}

// The canvas shows the drawing's user space at the session zoom; the saved
// size and viewBox are untouched.
fn canvas_attrs(doc, vb, zoom) =>
  [*[for (a in doc.attrs where not (a.name == 'width' or a.name == 'height' or a.name == 'viewBox' or
                                    a.name == 'class' or a.name == 'style' or is_handler(a))) a],
   {name: 'class', value: "edit-canvas"}, {name: 'width', value: fmt(vb.w * zoom)},
   {name: 'height', value: fmt(vb.h * zoom)},
   {name: 'viewBox', value: fmt(vb.x) ++ " " ++ fmt(vb.y) ++ " " ++ fmt(vb.w) ++ " " ++ fmt(vb.h)}]

let handle_names = ["nw", "n", "ne", "e", "se", "s", "sw", "w"]

fn handle_x(b, h) => if (has(h, "w")) b.x else if (has(h, "e")) b.x + b.w else b.x + b.w / 2.0
fn handle_y(b, h) => if (has(h, "n")) b.y else if (has(h, "s")) b.y + b.h else b.y + b.h / 2.0

fn frame_xml(b, zoom) =>
  "<rect class=\"edit-frame\" x=\"" ++ fmt(b.x) ++ "\" y=\"" ++ fmt(b.y) ++ "\" width=\"" ++ fmt(b.w) ++
  "\" height=\"" ++ fmt(b.h) ++ "\" fill=\"none\" stroke=\"#0969da\" stroke-width=\"" ++ fmt(1.5 / zoom) ++
  "\" stroke-dasharray=\"" ++ fmt(4.0 / zoom) ++ " " ++ fmt(3.0 / zoom) ++ "\"/>"

fn handle_xml(b, h, zoom) {
  let s = 8.0 / zoom
  "<rect class=\"edit-handle\" data-edit-handle=\"" ++ h ++ "\" x=\"" ++ fmt(handle_x(b, h) - s / 2.0) ++
  "\" y=\"" ++ fmt(handle_y(b, h) - s / 2.0) ++ "\" width=\"" ++ fmt(s) ++ "\" height=\"" ++ fmt(s) ++
  "\" fill=\"#ffffff\" stroke=\"#0969da\" stroke-width=\"" ++ fmt(1.0 / zoom) ++ "\"/>"
}

fn overlay_xml(doc, ds) {
  let frames = [for (p in ds.picked where p.box != null) frame_xml(p.box, ds.zoom)]
  let single = if (len(ds.picked) == 1) ds.picked[0] else null
  let handles = if (single != null and single.box != null and resizable(doc.content[single.index]))
                  [for (h in handle_names) handle_xml(single.box, h, ds.zoom)] else []
  "<g class=\"edit-overlay\">" ++ join(frames, "") ++ join(handles, "") ++ "</g>"
}

pub fn projection_xml(doc, ds) {
  let kids = [for (i in 0 to len(doc.content) - 1) {child: doc.content[i], path: [i]}]
  "<svg" ++ sv.attrs_xml(canvas_attrs(doc, view_box(doc), ds.zoom)) ++ ">" ++
    join([for (k in kids where projected(k.child)) sv.node_xml(k.child, k.path, 1, projected_attrs, projected)], "") ++
    overlay_xml(doc, ds) ++ "</svg>"
}

fn canvas_of(parsed) {
  let found = if (parsed == null) [] else [for (c in content(parsed) where type(c) == element) c]
  if (len(found) > 0) found[0] else null
}

// The drawing surface: the projected canvas inside a focusable host.
pub fn surface(doc, ds) =>
  <div id: "edit-surface", class: "edit-drawing", tabindex: "0",
    canvas_of(parse(projection_xml(doc, ds), 'xml') or null)
  >

pub let css = "
  .edit-drawing { outline: none; padding: 24px; overflow: auto; background: #eef1f4; min-height: 100%; }
  .edit-canvas { background: #ffffff; box-shadow: 0 1px 3px rgba(31, 35, 40, 0.25); }
  .edit-zoom { min-width: 44px; text-align: center; font-size: 12px; color: #57606a; align-self: center; }
"
