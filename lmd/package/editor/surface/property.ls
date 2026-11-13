// Structured data surface; the source-backed model keeps untouched tokens.
import dom
import sess: lambda.editor.session
import files: lambda.editor.app.files
import src: lambda.editor.surface.source

pub let css = "
  body.edit-format-property { margin: 0; font: 14px -apple-system, sans-serif; color: #24292f; }
  .property-toolbar { display: flex; gap: 12px; padding: 10px 16px; background: #f6f8fa; border-bottom: 1px solid #d0d7de; }
  .property-tree { padding: 16px; max-width: 1000px; }
  .property-children { padding-left: 22px; border-left: 1px solid #d0d7de; }
  .property-row { display: flex; align-items: center; min-height: 30px; gap: 8px; }
  .property-toggle { border: 0; background: transparent; min-width: 22px; cursor: pointer; }
  .property-name { min-width: 140px; color: #8250df; overflow-wrap: anywhere; }
  .property-key { width: 140px; border: 1px solid transparent; padding: 4px; color: #8250df; }
  .property-key:focus { border-color: #0969da; }
  .property-section { color: #6e7781; font-size: 12px; }
  .property-value { flex: 1; min-width: 80px; border: 1px solid transparent; padding: 4px; font: 13px monospace; }
  textarea.property-value { resize: vertical; white-space: pre-wrap; }
  textarea.property-multiline { min-height: 4em; }
  .property-value:focus { border-color: #0969da; }
  .property-kind { color: #6e7781; }
  .property-status { padding: 6px 16px; color: #57606a; background: #f6f8fa; }
"

fn draft_value(draft, first, key, original) =>
  if (draft != null and draft.first == first and draft.key == key) draft.text else original

fn node_view(name, node, root, key_first, key_end, section, format, draft, path, closed) any^ =>
  <div class: "property-entry",
    <div class: "property-row",
      if (node.kind != 'scalar' and len(node.children) > 0)
        apply(<property_toggle path: path, expanded: not (contains(closed, path) ^ { false })>)
      else null
      if (section != null and section != "") <span class: "property-section", section ++ " /"> else null
      if (key_first != null and format.can_rename != false and node.can_rename != false) <input class: "property-key", type: "text", value: draft_value(draft, key_first, true, name),
        ['data-first']: string(key_first), ['data-end']: string(key_end), ['data-original']: name, ['aria-label']: "Property name: " ++ name>
      else <span class: "property-name", name>
      if (node.group_header == true) <span class: "property-kind", if (node.view_only == true) "Open source to edit" else "{ }">
      else if (node.kind == 'scalar' and (format.multiline_values == true or node.multiline == true))
        <textarea class: "property-value" ++ (if (node.multiline == true) " property-multiline" else ""),
          rows: if (node.multiline == true) 3 else 1, ['data-first']: string(node.first), ['data-end']: string(node.last),
          ['data-original']: node.text, ['aria-label']: name, draft_value(draft, node.first, false, node.text)>
      else if (node.kind == 'scalar')
        <input class: "property-value", type: "text", value: draft_value(draft, node.first, false, node.text),
          ['data-first']: string(node.first), ['data-end']: string(node.last),
          ['data-original']: node.text, ['aria-label']: name>
      else <span class: "property-kind", if (node.kind == 'object') "{ }" else "[ ]">
      if (node.kind != 'scalar' and format.can_add != false and node.can_add != false) apply(<property_button action: "add", label: "+", title: if (format.can_add_element == true) "Add attribute" else "Add item", first: node.first, kind: node.kind>) else null
      if (node.can_add_element == true and format.can_add_element == true) apply(<property_button action: "add_element", label: "+ child", title: "Add child element", first: node.first>) else null
      if (not root and node.group_header != true and format.can_delete != false and node.can_delete != false) apply(<property_button action: "delete", label: "−", title: "Remove property", first: node.first>) else null
      if (node.can_delete_element == true and format.can_delete_element == true) apply(<property_button action: "delete_element", label: "− element", title: "Remove element", first: node.first>) else null
    >
    if (node.kind != 'scalar' and not (contains(closed, path) ^ { false }))
      <div class: "property-children", *[for (i, child in node.children)
        node_view(child.name, child.node, false, if (node.kind == 'object') child.first else null, child.key_end, child.group, format, draft, path ++ "." ++ string(i), closed)]>
    else null
  >

// The stable id lets the host restore this control after the tree is rebuilt.
view <property_toggle> { <button id: "property-toggle-" ++ ~.path, class: "property-toggle", ['aria-expanded']: if (~.expanded) "true" else "false",
  ['aria-label']: if (~.expanded) "Collapse" else "Expand", if (~.expanded) "▾" else "▸"> }
on click(evt) { emit("property_command", {action: "toggle", path: ~.path}) }

view <property_button> { <button class: "property-" ++ ~.action, title: ~.title, ['aria-label']: if (~.title != null) ~.title else ~.label, ~.label> }
on click(evt) { emit("property_command", {action: ~.action, node: evt.target, first: ~.first, kind: ~.kind}) }

pub fn app(session, doc, status) =>
  apply(<property_app session: session, doc: doc, status: status>, {mode: "edit"})

fn edited(format, doc, target) any^ {
  let first = int(dom.get_attribute(target, "data-first"))
  let end_pos = int(dom.get_attribute(target, "data-end"))
  let text = string(dom.get_state(target, "value"));
  (if (dom.matches(target, ".property-key")) format.rename_key(doc, first, end_pos, text)
   else format.replace_scalar(doc, first, end_pos, text)) ^ { {error: ^.message} }
}
fn draft_for(target) => {first: int(dom.get_attribute(target, "data-first")),
  key: dom.matches(target, ".property-key"), text: string(dom.get_state(target, "value"))}

fn dialog_view(dialog, session) any^ =>
  if (dialog != null and dialog.kind == 'add_property')
    files.dialog_frame("add-property", "Add " ++ (if (dialog.container == 'object') "property" else "item"),
      session.format.value_help,
      [if (dialog.container == 'object') files.field("property-new-key", "", "Property name") else null,
       files.field("property-new-value", "", "Value")],
      [files.dialog_button("cancel", "Cancel", false), files.dialog_button("property_add", "Add", true)])
  else if (dialog != null and dialog.kind == 'add_element')
    files.dialog_frame("add-element", "Add child element", "Enter an element name and optional text.",
      [files.field("property-new-key", "", "Element name"), files.field("property-new-value", "", "Text")],
      [files.dialog_button("cancel", "Cancel", false), files.dialog_button("element_add", "Add", true)])
  else if (dialog != null and dialog.kind == 'invalid_close')
    files.dialog_frame("invalid-close", "Invalid property value",
      "Correct the value before saving, or discard the unsaved edit.", [],
      [files.dialog_button("cancel", "Cancel", false), files.dialog_button("discard", "Don't Save", true)])
  else files.dialog_view(dialog, session)

fn history_step(doc, undo, redo, forward) =>
  if (forward and len(redo) > 0) {doc: redo[len(redo) - 1], undo: [*undo, doc], redo: take(redo, len(redo) - 1)}
  else if (not forward and len(undo) > 0) {doc: undo[len(undo) - 1], undo: take(undo, len(undo) - 1), redo: [*redo, doc]}
  else {doc: doc, undo: undo, redo: redo}

edit <property_app> state session: ~.session, doc: ~.doc, status: ~.status,
                          dialog: null, after_save: null, undo: [], redo: [], draft: null, closed: [] {
  let dirty = sess.is_dirty(session, doc);
  <body class: "edit-app edit-format-property", tabindex: "0",
    <div class: "property-toolbar", files.file_label(session, dirty)
      apply(<property_button action: "save", label: "Save">)
      apply(<property_button action: "save_as", label: "Save As">)
      apply(<property_button action: "undo", label: "Undo">)
      apply(<property_button action: "redo", label: "Redo">)
      apply(<property_button action: "source", label: "Source">)
    >
    <div class: "property-tree", node_view("root", doc.tree, true, null, null, null, session.format, draft, "0", closed)>
    <div class: "property-status", status>
    dialog_view(dialog, session)
  >
}
on change(evt) {
  if (evt.event_phase != 2 or not dom.matches(evt.target, ".property-value,.property-key")) { return 'pass' }
  let result = edited(session.format, doc, evt.target)
  if ((result.error != null)^) { draft = draft_for(evt.target); status = "Invalid value: " ++ result.error; return }
  draft = null
  if ((result.doc.text != doc.text)^) {
    undo = [*undo, doc]
    redo = []
    doc = result.doc
    status = ""
    files.sync_window(evt.target, session, doc)^
  }
}
on input(evt) {
  // The control owns its live text until change; guard a window close even
  // while that text has not yet been committed to the model.
  if (evt.event_phase == 2 and dom.matches(evt.target, ".property-value,.property-key")) {
    dom.set_close_guard(evt.target, true)
  }
}
on property_command(req) {
  if ((draft != null)^) { status = "Correct the invalid value before continuing."; return }
  if (req.action == "toggle") {
    closed = if (contains(closed, req.path)) [for (path in closed where path != req.path) path] else [*closed, req.path]
  }
  else if (req.action == "save") {
    let saved = files.save_now(session, doc, req.node)
    session = saved.session
    status = saved.status
    dialog = saved.dialog
  } else if (req.action == "save_as") { dialog = {kind: 'save_as', path: session.path} }
  else if (req.action == "source") {
    emit("edit_switch", src.source_view(session, doc.text, sess.is_dirty(session, doc)))
  }
  else if (req.action == "add") { dialog = {kind: 'add_property', first: req.first, container: req.kind} }
  else if (req.action == "add_element") { dialog = {kind: 'add_element', first: req.first} }
  else if (req.action == "delete" or req.action == "delete_element") {
    let result = (if (req.action == "delete_element") session.format.delete_element(doc, req.first)
                  else session.format.delete_child(doc, req.first)) ^ { {error: ^.message} }
    if ((result.error != null)^) { status = result.error }
    else {
      undo = [*undo, doc]
      redo = []
      doc = result.doc
      closed = []
      files.sync_window(req.node, session, doc)^
      dom.focus_set(dom.query_selector(dom.root_node(req.node), "body"), false)
    }
  }
  else if (req.action == "undo" or req.action == "redo") {
    let next = history_step(doc, undo, redo, req.action == "redo")
    doc = next.doc
    closed = []
    undo = next.undo
    redo = next.redo
    files.sync_window(req.node, session, doc)^
  }
}
on edit_dialog(req) {
  if ((dialog != null and ((req.action == "property_add" and dialog.kind == 'add_property') or
       (req.action == "element_add" and dialog.kind == 'add_element')))^) {
    let key = files.field_value(req.node, "property-new-key")
    let value = files.field_value(req.node, "property-new-value")
    let result = (if (req.action == "element_add") session.format.insert_element(doc, dialog.first, key, value)
                  else session.format.insert_child(doc, dialog.first, key, value)) ^ { {error: ^.message} }
    if ((result.error != null)^) { status = "Invalid " ++ (if (req.action == "element_add") "element: " else "property: ") ++ result.error; return }
    undo = [*undo, doc]
    redo = []
    doc = result.doc
    closed = []
    dialog = null
    status = ""
    files.sync_window(req.node, session, doc)^
    return
  }
  let result = files.dialog_action(req.action, req.node,
    {session: session, doc: doc, status: status, dialog: dialog, after_save: after_save})
  session = result.session
  status = result.status
  dialog = result.dialog
  after_save = result.after_save
  if (result.reloaded != null) { doc = result.reloaded; undo = []; redo = []; closed = [] }
}
on keydown(evt) {
  let primary = evt.metaKey == true or evt.ctrlKey == true
  if ((primary and lower(string(evt.key)) == "s")^) {
    if ((draft != null and not dom.matches(evt.target, ".property-value,.property-key"))^) {
      status = "Correct the invalid value before saving."
      return 'prevent-default'
    }
    // Keyboard save can arrive before the focused field's change event.
    if (dom.matches(evt.target, ".property-value,.property-key")) {
      let result = edited(session.format, doc, evt.target)
      if ((result.error != null)^) { draft = draft_for(evt.target); status = "Invalid value: " ++ result.error; return 'prevent-default' }
      draft = null
      if ((result.doc.text != doc.text)^) { undo = [*undo, doc]; redo = []; doc = result.doc }
    }
    if (evt.shiftKey == true) { dialog = {kind: 'save_as', path: session.path} }
    else {
      let saved = files.save_now(session, doc, evt.target)
      session = saved.session
      status = saved.status
      dialog = saved.dialog
    }
    return 'prevent-default'
  }
  if ((dialog != null and evt.key == "Escape")^) { dialog = null; after_save = null; return 'prevent-default' }
  if ((dialog == null and primary and lower(string(evt.key)) == "z")^) {
    let field = dom.matches(evt.target, ".property-value,.property-key")
    let pending = field and dom.get_state(evt.target, "value") != dom.get_attribute(evt.target, "data-original")
    // While typing, the control owns its local undo; otherwise use document history.
    if ((not pending and draft == null)^) {
      let next = history_step(doc, undo, redo, evt.shiftKey == true)
      doc = next.doc
      closed = []
      undo = next.undo
      redo = next.redo
      files.sync_window(evt.target, session, doc)^
      return 'prevent-default'
    }
  }
  if ((primary and evt.key == "/")^) {
    if ((draft != null and not dom.matches(evt.target, ".property-value,.property-key"))^) {
      status = "Correct the invalid value before switching views."
      return 'prevent-default'
    }
    if (dom.matches(evt.target, ".property-value,.property-key")) {
      let result = edited(session.format, doc, evt.target)
      if ((result.error != null)^) { draft = draft_for(evt.target); status = "Invalid value: " ++ result.error; return 'prevent-default' }
      draft = null
      if ((result.doc.text != doc.text)^) { doc = result.doc }
    }
    emit("edit_switch", src.source_view(session, doc.text, sess.is_dirty(session, doc)))
    return 'prevent-default'
  }
}
on closerequest(evt) {
  if ((draft != null)^) { dialog = {kind: 'invalid_close'}; return }
  let fields = dom.query_selector_all(dom.root_node(evt.target), ".property-value,.property-key")
  let changed = [for (field in fields where ((dom.get_state(field, "value") != dom.get_attribute(field, "data-original")) ^ { false })) field]
  if (len(changed) > 0) {
    let result = edited(session.format, doc, changed[0])
    if ((result.error != null)^) { draft = draft_for(changed[0]); dialog = {kind: 'invalid_close'}; return }
    doc = result.doc
  }
  if ((not sess.is_dirty(session, doc))^) { dom.request_window_close(evt.target); return }
  dialog = {kind: 'close'}
}
