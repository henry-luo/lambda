// session.ls — the edit session's file lifecycle
// (vibe/radiant/Radiant_Design_Edit_Mode.md §8).
//
// A session record is plain data owned by the shell template's instance state
// (S1.4, D7.2.1): the file identity, its format adapter, the envelope the
// adapter keeps beside the model, the bytes last read from or written to disk
// (the overwrite check), and the saved checkpoint (dirty state). Reads and
// writes happen only in the procedures below, called from `on` handlers
// (S12.1.1v2, S12.1.3); everything else here is pure.

import .model

pub fn basename(path) {
  let parts = split(path, "/")
  parts[len(parts) - 1]
}

pub fn dirname(path) {
  let parts = split(path, "/")
  let folder = join(take(parts, len(parts) - 1), "/")
  if (len(parts) <= 1) "." else if (folder == "") "/" else folder
}

// A path with "." and ".." folded, so one file compares equal however it was
// spelled. A relative path keeps the ".." it cannot fold; a rooted one drops
// any ".." above the root.
pub fn normal_path(path) {
  let rooted = starts_with(path, "/")
  let folded = join(fold_segments(split(path, "/"), 0, [], rooted), "/")
  if (rooted) "/" ++ folded else if (folded == "") "." else folded
}

fn fold_segments(segs, i, acc, rooted) {
  if (i >= len(segs)) acc
  else {
    let s = segs[i]
    let can_pop = len(acc) > 0 and acc[len(acc) - 1] != ".."
    let next = if (s == "" or s == ".") acc
               else if (s == ".." and can_pop) take(acc, len(acc) - 1)
               else if (s == ".." and rooted) acc
               else [*acc, s]
    fold_segments(segs, i + 1, next, rooted)
  }
}

// A typed Save As path: absolute, or relative to the document's folder (where
// a native save panel opens).
pub fn resolve_path(base_dir, typed) =>
  normal_path(if (starts_with(typed, "/")) typed else base_dir ++ "/" ++ typed)

fn same_path(a, b) => normal_path(a) == normal_path(b)

pub fn new_session(path, format, source, loaded) =>
  {path: path, name: basename(path), format: format, disk_text: source,
   envelope: loaded.envelope, saved_doc: loaded.doc}

// Dirty compares content with the saved checkpoint, so undoing back to the
// saved document is clean again and selection or view changes never dirty it.
pub fn is_dirty(session, doc) => doc != session.saved_doc

pub fn window_title(session, dirty) =>
  (if (dirty) "* " else "") ++ session.name ++ " - Lambda Edit"

// ---------------------------------------------------------------------------
// Save
// ---------------------------------------------------------------------------

fn outcome(ok, session, status, conflict) =>
  {ok: ok, session: session, status: status, conflict: conflict}

// Why a Save As target cannot take this document, or null. Save As keeps the
// format (proposal §8); a new folder would need its relative references
// rebased, which is not implemented yet, and copying assets is never implicit.
fn new_target_problem(session, doc, target) {
  let format = session.format
  let folder = dirname(target)
  let named = any([for (s in format.suffixes) ends_with(lower(target), s)]) or false
  if (not named)
    basename(target) ++ " is not a " ++ format.name ++ " file name (" ++ join(format.suffixes, ", ") ++ ")"
  else if (not exists(folder)) "the folder " ++ folder ++ " does not exist"
  else if (same_path(folder, dirname(session.path))) null
  else {
    let reference = first_relative_reference(doc)
    if (reference == null) null else reference ++ " would break in another folder"
  }
}

// Export, check, and write `doc` to `target`. `overwrite` confirms a target
// that changed on disk since it was read (or, for Save As, already exists).
// A failed save leaves both the file and the in-memory session untouched; the
// checkpoint moves only after the write succeeded.
pub pn save(session, doc, target, overwrite) {
  let format = session.format
  let text = format.export_text(doc, session.envelope)
  let mismatch = format.check_roundtrip(doc, session.envelope)
  if (mismatch != null) {
    return outcome(false, session, "Not saved: " ++ mismatch ++ " when written as " ++
                   format.name ++ ".", null)
  }
  let same_file = same_path(target, session.path)
  let problem = if (same_file) null else new_target_problem(session, doc, target)
  if (problem != null) {
    return outcome(false, session, "Not saved: " ++ problem ++ ".", null)
  }
  // probe first: reading a missing Save As target is the normal case, not a failure
  let on_disk = if (exists(target)) (input(target, 'text') ^ { null }) else null
  let conflict = if (overwrite) null
                 else if (same_file and on_disk != session.disk_text) 'changed'
                 else if (not same_file and on_disk != null) 'exists'
                 else null
  if (conflict == 'changed') {
    return outcome(false, session, basename(target) ++ " changed on disk since it was opened.", 'changed')
  }
  if (conflict == 'exists') {
    return outcome(false, session, basename(target) ++ " already exists.", 'exists')
  }
  // `output` is a procedure: its failure is an error value to test, since a
  // braced handler on a pn call is a statement and yields no binding value
  let written = output(text, target, {format: 'text', atomic: true})
  if (written is error) {
    return outcome(false, session, "Not saved: could not write " ++ target ++ ".", null)
  }
  let saved_path = if (same_file) session.path else target
  outcome(true, {*: session, path: saved_path, name: basename(saved_path), disk_text: text, saved_doc: doc},
          "Saved " ++ basename(saved_path) ++ ".", null)
}

// Re-read the file, discarding in-memory edits (the conflict dialog's Reload).
pub pn reload(session) {
  let source = input(session.path, 'text') ^ { null }
  if (source == null) {
    return {ok: false, status: "Could not read " ++ session.path ++ "."}
  }
  let loaded = session.format.import_text(source) ^ { {error: ^.message} }
  if (loaded.error != null) {
    return {ok: false, status: "Could not reload: " ++ loaded.error}
  }
  {ok: true, session: new_session(session.path, session.format, source, loaded),
   doc: loaded.doc, status: "Reloaded " ++ session.name ++ " from disk."}
}
