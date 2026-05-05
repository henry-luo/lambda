// mod_collab.ls — collaboration-ready primitives (Phase R6).
//
// Steps in mod_step.ls are already plain Lambda data and therefore
// trivially serialisable across a transport. What this module adds:
//
//   * `Mapping`      — an ordered list of steps used purely for position
//                      translation. Compose by concatenation; map a
//                      position by piping it through every contained
//                      step's `step_map`.
//   * `step_rebase`  — translate a step's *anchor* positions through a
//                      Mapping so a step authored against an older
//                      revision can be reapplied on a doc that received
//                      concurrent remote edits. Returns null if the
//                      step's affected range was concurrently deleted
//                      (i.e. the rebased range collapsed across paths).
//   * `rebase_steps` — rebase a whole list of local steps over a list
//                      of remote steps, dropping invalidated ones.
//   * Serialisation thin-wrappers — identity today, but a stable seam
//                      for future schema-version migrations.
//   * CollabState   — minimal local-session state for unconfirmed steps:
//                      apply local steps immediately, then receive remote
//                      steps by rebasing pending local edits over them.
//   * Packets       — small plain-data messages for future transport:
//                      local changes, remote changes, and acknowledgements.
//   * Presence      — remote selection packets lowered into decorations
//                      for collaborator carets/ranges without doc mutation.
//
// Out of scope (deferred): operational-transform conflict resolution
// inside an overlapping range, network transport.

import .mod_doc
import .mod_source_pos
import .mod_step
import .mod_decorations

// ---------------------------------------------------------------------------
// Mapping
// ---------------------------------------------------------------------------

pub fn mapping_new() => {kind: 'mapping', steps: []}
pub fn mapping_from(steps) => {kind: 'mapping', steps: steps}
pub fn mapping_add(m, step) => {kind: 'mapping', steps: [*m.steps, step]}

fn mapping_concat_at(acc, b, i, n) {
  if (i >= n) { acc }
  else { mapping_concat_at(mapping_add(acc, b[i]), b, i + 1, n) }
}
pub fn mapping_concat(a, b) => mapping_concat_at(a, b.steps, 0, len(b.steps))

fn mapping_pos_at(steps, p, i, n) {
  if (i >= n) { p }
  else { mapping_pos_at(steps, step_map(steps[i], p), i + 1, n) }
}
pub fn mapping_map(m, p) => mapping_pos_at(m.steps, p, 0, len(m.steps))

pub fn mapping_size(m) => len(m.steps)

// ---------------------------------------------------------------------------
// Step rebase
// ---------------------------------------------------------------------------
// Returns the rebased step, or null if the original step's affected range
// was concurrently invalidated (its endpoints no longer share a single
// container after remapping).

fn rebase_path(mapping, path) {
  let q = mapping_map(mapping, pos(path, 0))
  q.path
}

pub fn step_rebase(step, mapping) {
  if (step.kind == 'replace_text') {
    let p_from = mapping_map(mapping, pos(step.path, step.from))
    let p_to   = mapping_map(mapping, pos(step.path, step.to))
    if (path_equal(p_from.path, p_to.path) and len(p_from.path) == len(step.path) and (p_from.offset <= p_to.offset)) {
      step_replace_text(p_from.path, p_from.offset, p_to.offset, step.text)
    } else { null }
  }
  else if (step.kind == 'replace') {
    let p_from = mapping_map(mapping, pos(step.parent, step.from))
    let p_to   = mapping_map(mapping, pos(step.parent, step.to))
    if (path_equal(p_from.path, p_to.path) and len(p_from.path) == len(step.parent) and (p_from.offset <= p_to.offset)) {
      step_replace(p_from.path, p_from.offset, p_to.offset, step.slice)
    } else { null }
  }
  else if (step.kind == 'add_mark') {
    step_add_mark(rebase_path(mapping, step.path), step.mark)
  }
  else if (step.kind == 'remove_mark') {
    step_remove_mark(rebase_path(mapping, step.path), step.mark)
  }
  else if (step.kind == 'set_attr') {
    step_set_attr(rebase_path(mapping, step.path), step.name, step.value)
  }
  else if (step.kind == 'set_node_type') {
    step_set_node_type(rebase_path(mapping, step.path), step.tag)
  }
  else { step }
}

fn rebase_steps_at(local, remote_mapping, i, n, acc, dropped) {
  if (i >= n) { {kept: acc, dropped: dropped} }
  else {
    let s = step_rebase(local[i], remote_mapping)
    if (s == null) {
      rebase_steps_at(local, remote_mapping, i + 1, n, acc, dropped + 1)
    } else {
      rebase_steps_at(local, remote_mapping, i + 1, n, [*acc, s], dropped)
    }
  }
}

// Rebase `local_steps` (authored against some doc D) over `remote_steps`
// (already committed against D). Returns {kept: [...], dropped: count}.
pub fn rebase_steps(local_steps, remote_steps) =>
  rebase_steps_at(local_steps, mapping_from(remote_steps), 0, len(local_steps), [], 0)

// ---------------------------------------------------------------------------
// Serialisation seams (identity today; a stable seam for migrations)
// ---------------------------------------------------------------------------

pub fn step_serialize(step) => step
pub fn step_deserialize(data) => data

fn ser_at(steps, i, n, acc) {
  if (i >= n) { acc }
  else { ser_at(steps, i + 1, n, [*acc, step_serialize(steps[i])]) }
}
pub fn steps_serialize(steps) => ser_at(steps, 0, len(steps), [])

fn deser_at(data, i, n, acc) {
  if (i >= n) { acc }
  else { deser_at(data, i + 1, n, [*acc, step_deserialize(data[i])]) }
}
pub fn steps_deserialize(data) => deser_at(data, 0, len(data), [])

// ---------------------------------------------------------------------------
// CollabState — local pending-step bookkeeping
// ---------------------------------------------------------------------------

fn apply_steps_at(doc, steps, i, n) {
  if (i >= n) { doc }
  else { apply_steps_at(step_apply(steps[i], doc), steps, i + 1, n) }
}
pub fn apply_steps(doc, steps) => apply_steps_at(doc, steps, 0, len(steps))

fn mapping_map_selection(m, sel) {
  if (sel == null) { null }
  else if (sel.kind == 'all') { sel }
  else if (sel.kind == 'node') { node_selection(rebase_path(m, sel.path)) }
  else if (sel.kind == 'text') { text_selection(mapping_map(m, sel.anchor), mapping_map(m, sel.head)) }
  else { sel }
}

pub fn collab_state(doc, version, selection) =>
  {kind: 'collab_state', base_doc: doc, doc: doc, version: version,
   unconfirmed: [], selection: selection, dropped: 0}

pub fn collab_local_step(state, step, selection_after) {
  let doc_after = step_apply(step, state.doc)
  let sel_after = if (selection_after == null) {
    mapping_map_selection(mapping_from([step]), state.selection)
  } else { selection_after }
  {kind: 'collab_state', base_doc: state.base_doc, doc: doc_after, version: state.version,
   unconfirmed: [*state.unconfirmed, step], selection: sel_after, dropped: state.dropped}
}

fn ack_count(state, count) =>
  if (count < 0) { 0 }
  else if (count > len(state.unconfirmed)) { len(state.unconfirmed) }
  else { count }

pub fn collab_ack(state, count, version) {
  let n = ack_count(state, count)
  let acked = list_take(state.unconfirmed, n)
  let pending = list_drop(state.unconfirmed, n)
  let new_base = apply_steps(state.base_doc, acked)
  {kind: 'collab_state', base_doc: new_base, doc: state.doc, version: version,
   unconfirmed: pending, selection: state.selection, dropped: state.dropped}
}

pub fn collab_receive_remote(state, remote_steps, remote_version) {
  let remote_mapping = mapping_from(remote_steps)
  let rebased = rebase_steps(state.unconfirmed, remote_steps)
  let remote_doc = apply_steps(state.base_doc, remote_steps)
  let doc_after = apply_steps(remote_doc, rebased.kept)
  {kind: 'collab_state', base_doc: remote_doc, doc: doc_after, version: remote_version,
   unconfirmed: rebased.kept,
   selection: mapping_map_selection(remote_mapping, state.selection),
   dropped: state.dropped + rebased.dropped}
}

// ---------------------------------------------------------------------------
// Collab packets — transport-neutral wire messages
// ---------------------------------------------------------------------------

pub fn collab_local_packet(state, client_id) =>
  {kind: 'collab_local_steps', client_id: client_id, base_version: state.version,
   steps: steps_serialize(state.unconfirmed)}

pub fn collab_remote_packet(version, steps) =>
  {kind: 'collab_remote_steps', version: version, steps: steps_serialize(steps)}

pub fn collab_ack_packet(version, count) =>
  {kind: 'collab_ack', version: version, count: count}

pub fn collab_apply_remote_packet(state, packet) =>
  collab_receive_remote(state, steps_deserialize(packet.steps), packet.version)

pub fn collab_apply_ack_packet(state, packet) =>
  collab_ack(state, packet.count, packet.version)

pub fn collab_apply_packet(state, packet) {
  if (packet.kind == 'collab_remote_steps') { collab_apply_remote_packet(state, packet) }
  else if (packet.kind == 'collab_ack') { collab_apply_ack_packet(state, packet) }
  else { state }
}

// ---------------------------------------------------------------------------
// Presence packets — collaborator cursors/ranges as decorations
// ---------------------------------------------------------------------------

pub fn collab_presence_packet(client_id, label, color, selection) =>
  {kind: 'collab_presence', client_id: client_id, label: label, color: color,
   selection: selection}

fn presence_attrs(packet, role) =>
  {class: "collab-" ++ role, client_id: packet.client_id,
   label: packet.label, color: packet.color}

fn presence_render(packet) =>
  {kind: 'collab_caret', client_id: packet.client_id,
   label: packet.label, color: packet.color}

pub fn collab_presence_decorations(packet) {
  let sel = packet.selection
  if (sel == null) { deco_empty() }
  else if (sel.kind == 'text' and pos_equal(sel.anchor, sel.head)) {
    deco_set([deco_widget(sel.anchor, presence_render(packet), presence_attrs(packet, "caret"))])
  }
  else if (sel.kind == 'text') {
    deco_set([deco_inline(pos_min(sel.anchor, sel.head), pos_max(sel.anchor, sel.head), presence_attrs(packet, "selection"))])
  }
  else if (sel.kind == 'node') {
    deco_set([deco_node(sel.path, presence_attrs(packet, "node"))])
  }
  else if (sel.kind == 'all') {
    deco_set([deco_node([], presence_attrs(packet, "selection"))])
  }
  else { deco_empty() }
}

fn presences_decorations_at(packets, i, n, acc) {
  if (i >= n) { acc }
  else { presences_decorations_at(packets, i + 1, n, deco_concat(acc, collab_presence_decorations(packets[i]))) }
}

pub fn collab_presences_decorations(packets) =>
  presences_decorations_at(packets, 0, len(packets), deco_empty())
