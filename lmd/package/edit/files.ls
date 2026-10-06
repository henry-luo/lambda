// files.ls — the file half of every `lambda edit` application: the file
// label, the window's dirty state, Save, and the file dialogs (close, changed
// on disk, file exists, Save As) with their actions
// (vibe/radiant/Radiant_Design_Edit_Mode.md §8). The rich-text and drawing
// shell (shell.ls) and the source surface (source.ls) share this module;
// format-specific dialogs stay with their surface and reuse dialog_frame.
//
// The procedures return new session-state records; the calling template's
// `on` handler assigns them (S12.1.3).

import dom
import sess: lambda.edit.session

// ---------------------------------------------------------------------------
// Toolbar and window
// ---------------------------------------------------------------------------

pub fn file_label(session, dirty) =>
  <div class: "edit-file",
    <span class: "edit-name", session.name>
    <span class: "edit-dirty", title: if (dirty) "Unsaved changes" else "Saved", if (dirty) "*" else "">
  >

// Publish the dirty state to the window: the title marks it, and an armed
// close guard routes a close request through the Save / Discard dialog.
pub pn sync_window(node, session, doc) {
  let dirty = sess.is_dirty(session, doc)
  dom.set_close_guard(node, dirty)
  dom.set_window_title(node, sess.window_title(session, dirty))
}

// A closed dialog hands focus back to the document: the button that closed it
// is gone, and with nothing focused the next key would reach <body>, outside
// the shell that owns the shortcuts.
pub pn focus_surface(node) {
  let surface = dom.get_element_by_id(dom.root_node(node), "edit-surface")
  if (surface != null) { dom.focus_set(surface, false) }
}

// The live text of a dialog field. A click on a label targets its text node;
// `root_node` answers for any connected node.
pub pn field_value(node, id) {
  let input_node = dom.get_element_by_id(dom.root_node(node), id)
  if (input_node == null) "" else string(dom.get_state(input_node, "value") or "")
}

// ---------------------------------------------------------------------------
// Dialogs
// ---------------------------------------------------------------------------

view <edit_dialog_button> {
  <button class: "edit-dialog-btn edit-dialog-" ++ ~.action ++ (if (~.primary) " primary" else ""), ~.label>
}
on click(evt) {
  emit("edit_dialog", {action: ~.action, node: evt.target})
}

pub fn dialog_button(action, label, primary) =>
  apply(<edit_dialog_button action: action, label: label, primary: primary>)

pub fn field(id, value, placeholder) =>
  <input id: id, class: "edit-dialog-field", type: "text", value: value, placeholder: placeholder>

pub fn dialog_frame(kind, title, text, fields, buttons) =>
  <div class: "edit-dialog-layer",
    <div class: "edit-dialog-backdrop">
    <div class: "edit-dialog edit-dialog-" ++ kind, role: "dialog", ["aria-label"]: title,
      <div class: "edit-dialog-title", title>
      <div class: "edit-dialog-text", text>
      <div class: "edit-dialog-fields", *fields>
      <div class: "edit-dialog-actions", *buttons>
    >
  >

pub let dialog_kinds = ['close', 'changed', 'exists', 'save_as']

// The file dialogs; null for no dialog or another kind.
pub fn dialog_view(dialog, session) {
  if (dialog == null) null
  else if (dialog.kind == 'close') {
    dialog_frame("close", "Unsaved changes",
      "Save the changes to " ++ session.name ++ " before closing?", [],
      [dialog_button("cancel", "Cancel", false), dialog_button("discard", "Don't Save", false),
       dialog_button("save", "Save", true)])
  }
  else if (dialog.kind == 'changed') {
    dialog_frame("changed", "File changed on disk",
      session.name ++ " was changed by another program. Overwrite it with your version, " ++
      "reload the file and lose your edits, or save your version under another name.", [],
      [dialog_button("cancel", "Cancel", false), dialog_button("reload", "Reload", false),
       dialog_button("save_as", "Save As", false), dialog_button("overwrite", "Overwrite", true)])
  }
  else if (dialog.kind == 'exists') {
    dialog_frame("exists", "File exists",
      dialog.path ++ " already exists. Replace it?", [],
      [dialog_button("cancel", "Cancel", false), dialog_button("overwrite", "Replace", true)])
  }
  else if (dialog.kind == 'save_as') {
    dialog_frame("save_as", "Save As", "Save a copy of the document to a new file.",
      [field("edit-dialog-path", dialog.path, "path/to/file")],
      [dialog_button("cancel", "Cancel", false), dialog_button("save_as_ok", "Save", true)])
  }
  else null
}

// ---------------------------------------------------------------------------
// Save and the file dialog actions
// ---------------------------------------------------------------------------

// Save to the session's own file: the new session and status, and the
// conflict dialog a refused save opens (or null).
pub pn save_now(session, doc, node) {
  let result = sess.save(session, doc, session.path, false)
  sync_window(node, result.session, doc)
  {session: result.session, status: result.status,
   dialog: if (result.conflict != null) {kind: result.conflict, path: result.session.path} else null}
}

// Run one file dialog action against `st` = {session, doc, status, dialog,
// after_save}. Returns `st` updated, with `handled` (false for an action that
// belongs to the surface) and `reloaded` (the document read back from disk,
// or null).
pub pn dialog_action(action, node, st) {
  let session = st.session
  let dialog = st.dialog
  if (action == "cancel") {
    return {*: st, dialog: null, after_save: null, handled: true, reloaded: null}
  }
  if (action == "discard") {
    dom.request_window_close(node)
    return {*: st, dialog: null, handled: true, reloaded: null}
  }
  if (action == "save_as") {
    return {*: st, dialog: {kind: 'save_as', path: session.path}, handled: true, reloaded: null}
  }
  if (action == "reload") {
    let result = sess.reload(session)
    if (not result.ok) { return {*: st, status: result.status, handled: true, reloaded: null} }
    sync_window(node, result.session, result.doc)
    return {*: st, session: result.session, status: result.status, dialog: null, handled: true,
            reloaded: result.doc}
  }
  if (action != "save" and action != "overwrite" and action != "save_as_ok") {
    return {*: st, handled: false, reloaded: null}
  }
  let typed = if (action == "save_as_ok") trim(field_value(node, "edit-dialog-path")) else null
  let target = if (typed != null) (if (typed == "") "" else sess.resolve_path(sess.dirname(session.path), typed))
               else if (dialog != null and dialog.path != null) dialog.path
               else session.path
  let closing = (dialog != null and dialog.kind == 'close') or st.after_save == 'close'
  if (target == "") { return {*: st, status: "Choose a file name.", handled: true, reloaded: null} }
  let result = sess.save(session, st.doc, target, action == "overwrite")
  sync_window(node, result.session, st.doc)
  let saved = {*: st, session: result.session, status: result.status, handled: true, reloaded: null}
  if (result.ok) {
    if (closing) { dom.request_window_close(node) }
    return {*: saved, dialog: null, after_save: null}
  }
  if (result.conflict != null) {
    return {*: saved, dialog: {kind: result.conflict, path: target}, after_save: if (closing) 'close' else null}
  }
  // a refused Save As keeps its dialog, showing the path it tried
  if (typed != null) { return {*: saved, dialog: {kind: 'save_as', path: target}} }
  saved
}

pub let css = "
  * { box-sizing: border-box; }
  html { height: 100%; }
  .edit-dialog-backdrop { position: fixed; left: 0; top: 0; right: 0; bottom: 0;
                          background: rgba(31, 35, 40, 0.3); z-index: 20; }
  .edit-dialog { position: fixed; left: 50%; top: 96px; width: 440px; margin-left: -220px;
                 z-index: 21; background: #ffffff; border: 1px solid #d0d7de;
                 border-radius: 8px; padding: 16px; box-shadow: 0 8px 24px rgba(0,0,0,0.2); }
  .edit-dialog-title { font-weight: 600; margin-bottom: 8px; }
  .edit-dialog-text { font-size: 14px; color: #424a53; margin-bottom: 12px; }
  .edit-dialog-field { display: block; width: 100%; margin-bottom: 10px; padding: 5px 8px;
                       border: 1px solid #d0d7de; border-radius: 6px; font-size: 14px; }
  .edit-dialog-actions { display: flex; justify-content: flex-end; gap: 8px; }
  .edit-dialog-btn { height: 30px; padding: 0 12px; border: 1px solid #d0d7de; border-radius: 6px;
                     background: #f6f8fa; font-size: 13px; cursor: pointer; }
  .edit-dialog-btn.primary { background: #1f883d; border-color: #1a7f37; color: #ffffff; }
"
