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
import sess: lambda.editor.session
import controller: lambda.editor.controller
import focus_policy: lambda.dom.focus_order
import chrome: lambda.doc.document_chrome

// ---------------------------------------------------------------------------
// Toolbar and window
// ---------------------------------------------------------------------------

fn viewer_tabs(modes, session, source, source_intent) {
  let current = if (source and session.rich_format != null) "Source" else "Edit";
  chrome.mode_tabs([for (mode in modes)
    (let intent = if (mode == "Edit") {event:"edit_cmd", detail:{cmd:"view_rich"}}
      else if (mode == "Source" and source_intent != null) source_intent
      else {event:"viewer_preview", detail:{mode:lower(mode)}},
     chrome.mode_button(mode, mode == current, intent))])
}

pub fn file_label(session, dirty, source = false, modes = null, source_intent = null) =>
  <div class: "edit-file",
    chrome.title(session.path, session.name, "document-name edit-name");
    <span class: "edit-dirty", title: if (dirty) "Unsaved changes" else "Saved", if (dirty) "*" else "">
    if (modes == null) chrome.badge(if (source) "Source" else "Edit", if (source) "source" else "editing")
    else viewer_tabs(modes, session, source, source_intent)
  >

// An embedded editor needs space for the document at rest. Show feedback
// from file and editing actions below its toolbar only when there is a message.
pub fn status_notice(status) =>
  if (status == null or status == "") null else <div class:"edit-notice", role:"status", status>

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
  let host = controller.surface(node)
  let target = if (host != null) host else dom.query_selector(dom.root_node(node), "body.edit-format-property")
  if (target == null) false else dom.focus_set(target, false)
}

// Dialog autofocus needs both the active editing surface and any focused
// toolbar/control released, including a field that triggered window close.
pub pn release_focus(node) {
  let host = controller.surface(node)
  if (host != null) { dom.clear_editing_focus(host) }
  for (field in dom.focus_candidates(dom.root_node(node)) where dom.focused(field.node)) {
    dom.clear_editing_focus(field.node)
  }
}

pub pn save_as_dialog(session, node) {
  release_focus(node)^;
  {kind: 'save_as', path: session.path}
}

// The live text of a dialog field. A click on a label targets its text node;
// `root_node` answers for any connected node.
pub fn field_value(node, id) any^ {
  let input_node = dom.get_element_by_id(dom.root_node(node), id)
  if (input_node == null) "" else string(dom.get_state(input_node, "value") or "")
}

// ---------------------------------------------------------------------------
// Dialogs
// ---------------------------------------------------------------------------

view <edit_dialog_button> {
  <button class: "edit-dialog-btn edit-dialog-" ++ ~.action ++ (if (~.primary) " primary" else ""),
    autofocus: if (~.autofocus == true) "autofocus" else null, ~.label>
}
on click(evt) {
  emit("edit_dialog", {action: ~.action, node: evt.target})
}

pub fn dialog_button(action, label, primary, autofocus = false) =>
  apply(<edit_dialog_button action: action, label: label, primary: primary, autofocus: autofocus>)

pub fn field(id, value, placeholder, autofocus = false) =>
  <input id: id, class: "edit-dialog-field", type: "text", value: value, placeholder: placeholder,
    autofocus: if (autofocus) "autofocus" else null>

pub fn dialog_frame(kind, title, text, fields, buttons) =>
  <div class: "edit-dialog-layer",
    <div class: "edit-dialog-backdrop">
    <div class: "edit-dialog edit-dialog-" ++ kind, role: "dialog", ["aria-label"]: title, ["aria-modal"]: "true",
      <div class: "edit-dialog-title", title>
      <div class: "edit-dialog-text", text>
      <div class: "edit-dialog-fields", *fields>
      <div class: "edit-dialog-actions", *buttons>
    >
  >

pub let dialog_kinds = ['close', 'navigate', 'changed', 'exists', 'save_as']

pub pn navigation_decision(node, session, doc, entry) {
  if (sess.is_dirty(session, doc)^) {
    release_focus(node)^;
    return {navigate:false, dialog:{kind:'navigate', entry:entry}}
  }
  return {navigate:true, dialog:null}
}

pub fn navigation_result(entry, node, session) =>
  {entry:entry, node:node, edit_path:session.path}

// The file dialogs; null for no dialog or another kind.
pub fn dialog_view(dialog, session) any^ {
  if (dialog == null) null
  else if (dialog.kind == 'close') {
    dialog_frame("close", "Unsaved changes",
      "Save the changes to " ++ session.name ++ " before closing?", [],
      [dialog_button("cancel", "Cancel", false, true), dialog_button("discard", "Don't Save", false),
       dialog_button("save", "Save", true)])
  }
  else if (dialog.kind == 'navigate') {
    dialog_frame("navigate", "Unsaved changes",
      "Save the changes to " ++ session.name ++
        (if (dialog.entry.preview_mode != null) " before leaving the editor?" else " before opening another file?"), [],
      [dialog_button("cancel", "Cancel", false, true), dialog_button("discard", "Don't Save", false),
       dialog_button("save", "Save", true)])
  }
  else if (dialog.kind == 'changed') {
    dialog_frame("changed", "File changed on disk",
      session.name ++ " was changed by another program. Overwrite it with your version, " ++
      "reload the file and lose your edits, or save your version under another name.", [],
      [dialog_button("cancel", "Cancel", false, true), dialog_button("reload", "Reload", false),
       dialog_button("save_as", "Save As", false), dialog_button("overwrite", "Overwrite", true)])
  }
  else if (dialog.kind == 'exists') {
    dialog_frame("exists", "File exists",
      dialog.path ++ " already exists. Replace it?", [],
      [dialog_button("cancel", "Cancel", false, true), dialog_button("overwrite", "Replace", true)])
  }
  else if (dialog.kind == 'save_as') {
    dialog_frame("save_as", "Save As", "Save a copy of the document to a new file.",
      [field("edit-dialog-path", dialog.path, "path/to/file", true)],
      [dialog_button("cancel", "Cancel", false), dialog_button("save_as_ok", "Save", true)])
  }
  else null
}

// Modal keys stay on the dialog's action path, including an uncommitted
// property or link draft. Document shortcuts must not escape that scope.
pub pn dialog_keydown(dialog, evt) {
  let root = dom.root_node(evt.target)
  let modal = dom.query_selector(root, ".edit-dialog")
  if (evt.key == "Tab" and evt.metaKey != true and evt.ctrlKey != true and evt.altKey != true) {
    let fields = if (modal == null) [] else dom.focus_candidates(modal)
    let next = focus_policy.next_candidate(fields, evt.shiftKey != true)^
    if (next != null) { dom.focus_set(next.node, true) }
    return 'prevent-default'
  }
  let save = (evt.metaKey == true or evt.ctrlKey == true) and lower(string(evt.key)) == "s"
  let confirm = evt.key == "Enter" and evt.shiftKey != true and evt.altKey != true and
    evt.metaKey != true and evt.ctrlKey != true and dom.matches(evt.target, ".edit-dialog-field")
  let selector = if (evt.key == "Escape") ".edit-dialog-cancel"
    else if (confirm or (dialog.kind == 'save_as' and save)) ".edit-dialog-btn.primary"
    else null
  if (selector != null) {
    let button = dom.query_selector(root, selector)
    if (button != null) { dom.keyboard_click(button) }
    return 'prevent-default'
  }
  if (save) { return 'prevent-default' }
  if (modal == null or not dom.contains(modal, evt.target)) { return 'prevent-default' }
  'pass'
}

// ---------------------------------------------------------------------------
// Save and the file dialog actions
// ---------------------------------------------------------------------------

// Save to the session's own file: the new session and status, and the
// conflict dialog a refused save opens (or null).
pub pn save_now(session, doc, node) {
  let result = sess.save(session, doc, session.path, false)
  sync_window(node, result.session, doc)^;
  if ((result.conflict != null)^) { release_focus(node)^ }
  {session: result.session, status: result.status,
   dialog: if (result.conflict != null) {kind: result.conflict, path: result.session.path} else null}
}

// Run one file dialog action against `st` = {session, doc, status, dialog,
// after_save}. Returns `st` updated, with `handled` (false for an action that
// belongs to the surface) and `reloaded` (the document read back from disk,
// or null).
pub pn dialog_action(action, node, before) {
  let st = {*: before, source_text: null, navigation: null}
  let session = st.session
  let dialog = st.dialog
  let navigation = if (dialog != null and dialog.kind == 'navigate') dialog.entry
    else if (type(st.after_save) == map and st.after_save.kind == 'navigate') st.after_save.entry
    else null
  if (action == "cancel") {
    return {*: st, dialog: null, after_save: null, handled: true, reloaded: null}
  }
  if (action == "discard") {
    if (navigation != null) { return {*:st, dialog:null, after_save:null, handled:true, reloaded:null, navigation:navigation} }
    dom.request_window_close(node)
    return {*: st, dialog: null, handled: true, reloaded: null}
  }
  if (action == "save_as") {
    return {*: st, dialog: save_as_dialog(session, node)^, handled: true, reloaded: null}
  }
  if (action == "reload") {
    if (navigation != null) { return {*:st, dialog:null, after_save:null, handled:true, reloaded:null, navigation:navigation} }
    let result = sess.reload(session)
    if ((not result.ok)^) { return {*: st, status: result.status, handled: true, reloaded: null} }
    if ((result.source_text != null)^) {
      dom.set_close_guard(node, false)
      dom.set_window_title(node, sess.window_title(session, false)^)
      return {*: st, status: result.status, dialog: null, handled: true, reloaded: null,
              source_text: result.source_text}
    }
    sync_window(node, result.session, result.doc)^
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
  if ((target == "")^) { return {*: st, status: "Choose a file name.", handled: true, reloaded: null} }
  let result = sess.save(session, st.doc, target, action == "overwrite")
  sync_window(node, result.session, st.doc)^
  let saved = {*: st, session: result.session, status: result.status, handled: true, reloaded: null}
  if (result.ok^) {
    if (closing) { dom.request_window_close(node) }
    return {*: saved, dialog: null, after_save: null, navigation:navigation}
  }
  if ((result.conflict != null)^) {
    return {*: saved, dialog: {kind: result.conflict, path: target},
      after_save: if (closing) 'close' else if (navigation != null) {kind:'navigate', entry:navigation} else null}
  }
  // a refused Save As keeps its dialog, showing the path it tried
  if (typed != null) { return {*: saved, dialog: {kind: 'save_as', path: target}} }
  saved
}

pub let css = "
  .edit-notice { flex: none; padding: 4px 12px; font-size: 12px; color: var(--document-muted);
                 background: var(--document-status); border-bottom: 1px solid var(--document-rule); }
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
