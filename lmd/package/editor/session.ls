// session.ls — the edit session's file lifecycle
// (vibe/radiant/Radiant_Design_Edit_Mode.md §8).
//
// A session record is plain data owned by the shell template's instance state
// (S1.4, D9.3.1): the file identity, its format adapter, the envelope the
// adapter keeps beside the model, the bytes last read from or written to disk
// (the overwrite check), and the saved checkpoint (dirty state). Reads and
// writes happen only in the procedures below, called from `on` handlers
// (S12.1.1v2, S12.1.3); everything else here is pure.

import lambda.editor.format.model
import paths: lambda.io.path
import utf8: lambda.io.utf8

pub let basename = paths.basename
pub let dirname = paths.dirname

// A path with "." and ".." folded, so one file compares equal however it was
// spelled. A relative path keeps the ".." it cannot fold; a rooted one drops
// any ".." above the root.
pub let normal_path = paths.normal_path

// A typed Save As path: absolute, or relative to the document's folder (where
// a native save panel opens).
pub let resolve_path = paths.resolve_path

// Plain Netpbm images can be UTF-8, so inspect their dimensions as well.
fn is_netpbm(source) any^ {
  let prefix = slice(source, 0, min(len(source), 4096))^
  if (not (slice(prefix, 0, min(len(prefix), 2))^ in ["P1", "P2", "P3", "P4", "P5", "P6"])) { return false }
  let lines = split(prefix, "\n")^
  let uncommented = join([for (line in lines) {
    let at = index_of(line, "#")
    if (at == null) line else slice(line, 0, at)^
  }], " ")^
  let words = [for (word in split(replace(replace(uncommented, "\t", " ")^, "\r", " ")^, " ")^ where word != "") word]
  if (len(words) < 3 or not (words[0] in ["P1", "P2", "P3", "P4", "P5", "P6"])) false
  else {
    let width = int(words[1]) ^ { null }
    let height = int(words[2]) ^ { null }
    width != null and height != null and width > 0 and height > 0
  }
}
fn is_non_text_content(source) any^ =>
  starts_with(source, "%PDF-") or starts_with(source, "GIF87a") or starts_with(source, "GIF89a") or is_netpbm(source)^

// Opening and reloading must apply the same byte and content checks.
pub pn read_text(path) string^ {
  let source = input(path, 'text') ^ { raise error("could not read " ++ path, ^) }
  if (is_non_text_content(source)^) {
    raise error("'" ++ basename(path) ++ "' is a non-text document and cannot be edited")
  }
  let bytes = input(path, 'binary') ^ { raise error("could not read " ++ path, ^) }
  // D10.2.6 classifies embedded NULs as binary for editor routing.
  if (index_of(source, "\u0000") != null) {
    raise error("the file contains NUL bytes and is treated as binary. Binary file editing is not yet supported")
  }
  if ((bytes != null and (binary(source)^ != bytes or not utf8.is_valid(bytes)^))^) {
    raise error("the file is not safely readable as text")
  }
  return source
}
pub pn read_snapshot(format, path) any^ {
  return if (format.binary == true) input(path, 'binary')^ else read_text(path)^
}

fn same_path(a, b) any^ => normal_path(a) == normal_path(b)
fn source_suffix(path) any^ {
  let parts = split(lower(basename(path)^), ".")^;
  if (len(parts) <= 1) "" else "." ++ parts[len(parts) - 1]
}

pub fn new_session(path, format, source, loaded) =>
  {path: path, name: basename(path), format: format, disk_text: source,
   envelope: loaded.envelope, saved_doc: loaded.doc}

// Some formats derive editor capabilities from the opened document envelope.
pub fn edit_schema(format, envelope) any^ =>
  if (format.document_schema == null) format.schema else format.document_schema(envelope)^

// Dirty compares content with the saved checkpoint, so undoing back to the
// saved document is clean again and selection or view changes never dirty it.
// A format whose documents are too large to compare whole supplies `same_doc`
// (the source buffer compares versions, Radiant_Design_Source_Editor §5.1).
pub fn is_dirty(session, doc) any^ =>
  if (session.format.same_doc != null) not session.format.same_doc(doc, session.saved_doc)
  else doc != session.saved_doc

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
fn new_target_problem(session, doc, target) any^ {
  let format = session.format
  let folder = dirname(target)
  let named = (any([for (s in format.suffixes) ends_with(lower(target), s)]) or false) or
    (format.id == 'source' and source_suffix(target)^ == source_suffix(session.path)^)
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
  let prepared: any^ = if (format.binary == true) format.export_tree(doc, session.envelope)
    else format.export_text(doc, session.envelope)
  if (prepared is error) { return outcome(false, session, "Not saved: " ++ prepared.message, null) }
  let mismatch = if (format.binary == true) null else format.check_roundtrip(doc, session.envelope)
  if ((mismatch != null)^) {
    return outcome(false, session, "Not saved: " ++ mismatch ++ " when written as " ++
                   format.name ++ ".", null)
  }
  let same_file = same_path(target, session.path)
  let problem = if (same_file) null else new_target_problem(session, doc, target)
  if (not (problem is null)) {
    return outcome(false, session, "Not saved: " ++ problem ++ ".", null)
  }
  // probe first: reading a missing Save As target is the normal case, not a failure
  let on_disk = if (exists(target)) (input(target, if (format.binary == true) 'binary' else 'text') ^ { null }) else null
  let conflict = if (overwrite) null
                 else if (same_file and on_disk != session.disk_text) 'changed'
                 else if (not same_file and on_disk != null) 'exists'
                 else null
  if ((conflict == 'changed')^) {
    return outcome(false, session, basename(target) ++ " changed on disk since it was opened.", 'changed')
  }
  if ((conflict == 'exists')^) {
    return outcome(false, session, basename(target) ++ " already exists.", 'exists')
  }
  // S7.5.1v6: retain output's ordinary outcome to report a failed save.
  // An unchanged package must not be reconstructed just to save it.
  let same_snapshot = format.binary == true and same_file and doc == session.saved_doc and on_disk == session.disk_text
  let written: any^ = if (same_snapshot) null else if (format.binary == true and prepared.changed)
    output(prepared.tree, target, {format:'zip', atomic:true})
    else if (format.binary == true) output(session.envelope.snapshot, target, {format:'binary', atomic:true})
    else output(prepared, target, {format: 'text', atomic: true})
  if (written is error) {
    return outcome(false, session, "Not saved: could not write " ++ target ++ ".", null)
  }
  let saved_path = if (same_file) session.path else target
  let saved = if (format.binary == true) (input(saved_path, 'binary') ^ { null }) else prepared
  if ((format.binary == true and saved == null)^) {
    return outcome(false, session, "Saved " ++ basename(saved_path) ++ ", but could not verify the saved package.", null)
  }
  outcome(true, {*: session, path: saved_path, name: basename(saved_path), disk_text: saved, saved_doc: doc},
          "Saved " ++ basename(saved_path) ++ ".", null)
}

// Re-read the file, discarding in-memory edits (the conflict dialog's Reload).
pub pn reload(session) {
  let source = read_snapshot(session.format, session.path) ^ { ^ }
  if (source is error) { return {ok: false, status: "Could not reload: " ++ source.message} }
  let loaded = session.format.import_text(source) ^ { {error: ^.message} }
  if (not (loaded.error is null)) {
    return {ok: false, status: "Could not reload: " ++ loaded.error}
  }
  let mismatch = session.format.check_roundtrip(loaded.doc, loaded.envelope) ^ { ^.message }
  if ((mismatch != null)^) { return {ok: false, status: "Could not reload: " ++ mismatch} }
  // Keep view-switch metadata while replacing the saved checkpoint.
  {ok: true, session: {*: session, disk_text: source, envelope: loaded.envelope, saved_doc: loaded.doc},
   doc: loaded.doc, status: "Reloaded " ++ session.name ++ " from disk."}
}
