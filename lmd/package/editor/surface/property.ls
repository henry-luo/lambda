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
  .property-grid-wrap { padding: 16px; overflow: auto; }
  .property-grid { border-collapse: collapse; }
  .property-grid th, .property-grid td { border: 1px solid #d0d7de; padding: 4px; vertical-align: top; }
  .property-grid th { background: #f6f8fa; font-weight: 500; }
  .property-grid .property-value { width: 160px; box-sizing: border-box; }
  .property-grid-cell { display: flex; align-items: start; }
"

fn draft_value(draft, first, key, original) =>
  if (draft != null and draft.first == first and draft.key == key) draft.text else original

fn value_view(name, node, format, draft, id = null) any^ =>
  if (format.multiline_values == true or node.multiline == true)
    <textarea id: id, class: "property-value" ++ (if (node.multiline == true) " property-multiline" else ""),
      rows: if (node.multiline == true) 3 else 1, ['data-first']: string(node.first), ['data-end']: string(node.last),
      ['data-original']: node.text, ['aria-label']: name, draft_value(draft, node.first, false, node.text)>
  else <input id: id, class: "property-value", type: "text", value: draft_value(draft, node.first, false, node.text),
      ['data-first']: string(node.first), ['data-end']: string(node.last),
      ['data-original']: node.text, ['aria-label']: name>

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
      else if (node.kind == 'scalar')
        value_view(name, node, format, draft)
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

// Rows are data, including the first row; numeric headings do not assume a header.
fn grid_view(tree, format, draft) any^ {
  let widths = [for (row in tree.children) len(row.node.children)]
  let columns = if (len(widths) == 0) 0 else max(widths)
  let add = apply(<property_button action: "add", label: "+ row", title: "Add row", first: tree.first, kind: tree.kind>)
  let headings = [<th "Row">, *[for (i in 0 to columns - 1) <th ("Column " ++ string(i + 1))>], <th "Actions">]
  let rows = [for (row_index, row in tree.children) {
    let remove = apply(<property_button action: "delete", label: "−", title: "Remove row", first: row.node.first, kind: row.node.kind>)
    let cells = [for (cell_index, cell in row.node.children) {
      let field = value_view("Row " ++ string(row_index + 1) ++ ", column " ++ string(cell_index + 1), cell.node, format, draft,
                             "grid-cell-" ++ string(row_index) ++ "-" ++ string(cell_index))
      let remove_field = apply(<property_button action: "delete", label: "−", title: "Remove field", first: cell.node.first, kind: cell.node.kind>);
      <td <div class: "property-grid-cell", *[field, remove_field]>>
    }]
    let add_field = apply(<property_button action: "add", label: "+ field", title: "Add field", first: row.node.first, kind: row.node.kind>);
    <tr *[<th *[string(row_index + 1), remove]>, *cells,
      *[for (i in (len(row.node.children)^) to columns - 1) <td>], <td add_field>]>
  }]
  let table = <table class: "property-grid", <thead <tr *headings>> <tbody *rows>>;
  <div class: "property-grid-wrap", *[add, table]>
}

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
fn draft_matches(draft, target) => draft != null and dom.matches(target, ".property-value,.property-key") and
  draft.first == (int(dom.get_attribute(target, "data-first")) ^ { null }) and draft.key == dom.matches(target, ".property-key")

// A field commit is shared by change, Enter, save and source-switch actions.
fn commit_field(format, doc, undo, redo, target) map^ {
  let result = edited(format, doc, target)
  if (result.error != null) {doc:doc, undo:undo, redo:redo, draft:draft_for(target), status:"Invalid value: " ++ result.error, ok:false}
  else if (result.doc.text != doc.text) {doc:result.doc, undo:[*undo, doc], redo:[], draft:null, status:"", ok:true}
  else {doc:doc, undo:undo, redo:redo, draft:null, status:"", ok:true}
}

fn dialog_view(dialog, session) any^ =>
  if (dialog != null and dialog.kind == 'add_property')
    files.dialog_frame("add-property", "Add " ++ (if (dialog.container == 'object') "property" else "item"),
      session.format.value_help,
      [if (dialog.container == 'object') files.field("property-new-key", "", "Property name", true) else null,
       files.field("property-new-value", "", "Value", dialog.container != 'object')],
      [files.dialog_button("cancel", "Cancel", false), files.dialog_button("property_add", "Add", true)])
  else if (dialog != null and dialog.kind == 'add_element')
    files.dialog_frame("add-element", "Add child element", "Enter an element name and optional text.",
      [files.field("property-new-key", "", "Element name", true), files.field("property-new-value", "", "Text")],
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
  let dirty = sess.is_dirty(session, doc)
  let content = if (session.format.id in ['csv', 'tsv']) grid_view(doc.tree, session.format, draft)
    else <div class: "property-tree", node_view("root", doc.tree, true, null, null, null, session.format, draft, "0", closed)>;
  <body class: "edit-app edit-format-property", tabindex: "0",
    <div class: "property-toolbar", files.file_label(session, dirty)
      apply(<property_button action: "save", label: "Save">)
      apply(<property_button action: "save_as", label: "Save As">)
      apply(<property_button action: "undo", label: "Undo">)
      apply(<property_button action: "redo", label: "Redo">)
      apply(<property_button action: "source", label: "Source">)
    >
    <div class: "property-content", content>
    <div class: "property-status", status>
    dialog_view(dialog, session)
  >
}
on change(evt) {
  if (evt.event_phase != 2 or not dom.matches(evt.target, ".property-value,.property-key")) { return 'pass' }
  if ((draft != null and not draft_matches(draft, evt.target))^) { status = "Correct the invalid value before continuing."; return }
  let result = commit_field(session.format, doc, undo, redo, evt.target)^
  draft = result.draft; status = result.status
  if (not result.ok) { return }
  doc = result.doc; undo = result.undo; redo = result.redo
  files.sync_window(evt.target, session, doc)^
}
on input(evt) {
  // The control owns its live text until change; guard a window close even
  // while that text has not yet been committed to the model.
  if (evt.event_phase == 2 and dom.matches(evt.target, ".property-value,.property-key")) {
    if ((draft != null and not draft_matches(draft, evt.target))^) {
      // Keep the invalid draft authoritative until it is corrected.
      dom.set_state(evt.target, "value", dom.get_attribute(evt.target, "data-original"))
      status = "Correct the invalid value before continuing."
      let selector = (if (draft.key) ".property-key" else ".property-value") ++ "[data-first='" ++ string(draft.first) ++ "']"
      let field = dom.query_selector(dom.root_node(evt.target), selector)
      if (field != null) { dom.focus_set(field, false) }
      return
    }
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
    dom.focus_set(dom.query_selector(dom.root_node(req.node), "body"), false)
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
  if ((dialog == null and evt.key == "Tab" and not primary and
       dom.matches(evt.target, ".property-grid .property-value"))^) {
    let fields = dom.query_selector_all(dom.root_node(evt.target), ".property-grid .property-value")
    let id = dom.get_attribute(evt.target, "id")
    let positions = [for (i, field in fields where dom.get_attribute(field, "id") == id) i]
    if (len(positions) > 0) {
      let next = positions[0] + (if (evt.shiftKey == true) -1 else 1)
      if ((next >= 0 and next < len(fields))^) {
        // Programmatic focus changes do not necessarily dispatch a change event.
        let result = commit_field(session.format, doc, undo, redo, evt.target)^
        draft = result.draft; status = result.status
        if (not result.ok) { return 'prevent-default' }
        doc = result.doc; undo = result.undo; redo = result.redo
        files.sync_window(evt.target, session, doc)^
        dom.focus_set(fields[next], false)
        return 'prevent-default'
      }
    }
  }
  if ((dialog != null and dialog.kind in ['add_property', 'add_element'] and evt.key == "Enter" and not primary and
       dom.matches(evt.target, "#property-new-key,#property-new-value"))^) {
    let selector = if (dialog.kind == 'add_element') ".edit-dialog-element_add" else ".edit-dialog-property_add"
    let button = dom.query_selector(dom.root_node(evt.target), selector)
    if (button != null) { dom.keyboard_click(button) }
    return 'prevent-default'
  }
  if ((dialog == null and evt.key == "Escape" and dom.matches(evt.target, ".property-value,.property-key"))^) {
    if ((draft != null and not draft_matches(draft, evt.target))^) {
      status = "Correct the invalid value before continuing."
    } else {
      // A field draft is separate from the committed document and its history.
      dom.set_state(evt.target, "value", dom.get_attribute(evt.target, "data-original"))
      draft = null
      status = ""
      files.sync_window(evt.target, session, doc)^
    }
    return 'prevent-default'
  }
  if ((dialog == null and evt.key == "Enter" and not primary and
       dom.matches(evt.target, "input.property-value,input.property-key"))^) {
    if ((draft != null and not draft_matches(draft, evt.target))^) {
      status = "Correct the invalid value before continuing."
      return 'prevent-default'
    }
    let result = commit_field(session.format, doc, undo, redo, evt.target)^
    draft = result.draft; status = result.status
    if (result.ok) {
      doc = result.doc; undo = result.undo; redo = result.redo
      files.sync_window(evt.target, session, doc)^
    }
    return 'prevent-default'
  }
  if ((primary and lower(string(evt.key)) == "s")^) {
    if ((draft != null and not draft_matches(draft, evt.target))^) {
      status = "Correct the invalid value before saving."
      return 'prevent-default'
    }
    // Keyboard save can arrive before the focused field's change event.
    if (dom.matches(evt.target, ".property-value,.property-key")) {
      let result = commit_field(session.format, doc, undo, redo, evt.target)^
      draft = result.draft; status = result.status
      if (not result.ok) { return 'prevent-default' }
      doc = result.doc; undo = result.undo; redo = result.redo
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
    if ((draft != null and not draft_matches(draft, evt.target))^) {
      status = "Correct the invalid value before switching views."
      return 'prevent-default'
    }
    if (dom.matches(evt.target, ".property-value,.property-key")) {
      let result = commit_field(session.format, doc, undo, redo, evt.target)^
      draft = result.draft; status = result.status
      if (not result.ok) { return 'prevent-default' }
      doc = result.doc; undo = result.undo; redo = result.redo
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
