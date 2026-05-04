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
//
// Out of scope (deferred): operational-transform conflict resolution
// inside an overlapping range, network transport, presence/cursor sync.

import .mod_doc
import .mod_source_pos
import .mod_step

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
    if (path_equal(p_from.path, p_to.path) and (p_from.offset <= p_to.offset)) {
      step_replace_text(p_from.path, p_from.offset, p_to.offset, step.text)
    } else { null }
  }
  else if (step.kind == 'replace') {
    let p_from = mapping_map(mapping, pos(step.parent, step.from))
    let p_to   = mapping_map(mapping, pos(step.parent, step.to))
    if (path_equal(p_from.path, p_to.path) and (p_from.offset <= p_to.offset)) {
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
