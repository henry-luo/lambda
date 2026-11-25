// Structured data surface; the source-backed model keeps untouched tokens.
import dom
import sess: lambda.editor.session
import files: lambda.editor.app.files
import src: lambda.editor.surface.source
import chrome: lambda.doc.document_chrome

let PROPERTY_ROW_HEIGHT = 30
let PROPERTY_TREE_PADDING = 16
// The first template render precedes viewport measurement.
let DEFAULT_TREE_ROWS = 20

pub let css = "
  .property-tree { padding: " ++ string(PROPERTY_TREE_PADDING) ++ "px;
                   font: 13px/1.4 var(--document-mono); }
  .property-children { padding-left: 18px; border-left: 1px solid var(--document-rule); }
  .property-row { display: flex; align-items: flex-start; min-height: " ++ string(PROPERTY_ROW_HEIGHT) ++ "px;
                  gap: 8px; border-bottom: 1px solid #f0f2f5; }
  .property-entry:nth-child(even) > .property-row { background: var(--document-stripe); }
  .property-row:hover { background: var(--document-hover); }
  .property-row[data-selected='true'] { background: #e8f1ff; }
  .property-menu { position: fixed; z-index: 30; background: white; border: 1px solid #d0d7de;
                   border-radius: 6px; padding: 4px; box-shadow: 0 4px 16px rgba(0,0,0,.18); }
  .property-menu button { display: block; width: 160px; padding: 7px 10px; border: 0;
                          background: white; text-align: left; cursor: pointer; }
  .property-menu button:hover { background: var(--document-hover); }
  .property-toggle { border: 0; background: transparent; min-width: 22px;
                     color: var(--document-muted); cursor: pointer; }
  .property-row button:not(.property-toggle) { flex: none; min-width: 22px; height: 22px;
                     border: 1px solid var(--document-rule); border-radius: 4px; background: white;
                     color: var(--document-muted); cursor: pointer; }
  .property-row button:not(.property-toggle):hover { color: var(--document-key); }
  .property-more { margin: 4px 0; cursor: pointer; }
  .property-name, .property-key { min-width: 0; box-sizing: border-box;
                                  color: var(--document-key); }
  .property-resize { flex: none; width: 6px; align-self: stretch; cursor: col-resize; }
  .property-resize:hover { background: #d2e5fb; }
  .property-name { overflow-wrap: anywhere; padding: 4px; }
  .property-key { border: 1px solid transparent; padding: 4px; background: transparent; font: inherit; }
  .property-section { color: #6e7781; font-size: 12px; }
  .property-value { flex: 1; min-width: 80px; border: 1px solid transparent; padding: 4px;
                    color: var(--document-value); background: transparent; font: 13px var(--document-mono); }
  textarea.property-value { resize: vertical; white-space: pre-wrap; }
  .property-value-wrap { flex: 1; min-width: 80px; display: grid; }
  .property-value-mirror { box-sizing: border-box; padding: 4px; border: 1px solid transparent;
                           font: 13px/1.4 var(--document-mono); white-space: pre-wrap;
                           overflow-wrap: anywhere; visibility: hidden; grid-area: 1 / 1; }
  .property-value-wrap textarea.property-value { grid-area: 1 / 1; width: 100%; height: 100%;
    box-sizing: border-box; overflow: hidden; resize: none; line-height: 1.4; }
  textarea.property-multiline { min-height: 4em; }
  /* The property border supplies the focus indicator in place of the UA outline. */
  .property-key:focus, .property-value:focus { border-color: #0969da; outline: none; }
  .property-kind { color: var(--document-muted); font-style: italic; }
  /* Keep document chrome outside the property surface's scrollport. */
  body.edit-format-property { height: 100vh; min-height: 0; overflow: hidden; }
  .property-content { flex: 1; min-height: 0; overflow: auto; }
  .property-status.edit-status { position: static; flex: none; }
  .property-grid-wrap { padding: 16px; overflow: auto; }
  .property-grid { border-collapse: collapse; }
  .property-grid th, .property-grid td { border: 1px solid #d0d7de; padding: 4px; vertical-align: top; }
  .property-grid th { background: var(--document-stripe); color: var(--document-key); font-weight: 500; }
  .property-grid .property-value { width: 160px; box-sizing: border-box; }
  .property-grid-cell { display: flex; align-items: start; }
"

fn draft_value(draft, first, key, original) =>
  if (draft != null and draft.first == first and draft.key == key) draft.text else original

fn value_view(name, node, format, draft, id = null, wrap = false) any^ {
  let original = if (node.value_literal == true) node.display_text else node.text
  let multiline = node.value_literal != true and (format.multiline_values == true or node.multiline == true or node.multiline_capable == true)
  let value = draft_value(draft, node.first, false, original)
  if (wrap) <div class:"property-value-wrap",
    <div class:"property-value-mirror", ['aria-hidden']:"true", value ++ " ">
    <textarea id:id, class:"property-value" ++ (if (node.multiline == true) " property-multiline" else ""), rows:1, ['data-first']:string(node.first), ['data-end']:string(node.last),
      ['data-original']:original, ['data-value-literal']:if (node.value_literal == true) "json" else null,
      ['data-single-line']:if (multiline) null else "true", ['aria-label']:name, value>
  >
  else if (multiline)
    <textarea id: id, class: "property-value" ++ (if (node.multiline == true) " property-multiline" else ""),
      rows: if (node.multiline == true) 3 else 1, ['data-first']: string(node.first), ['data-end']: string(node.last),
      ['data-original']: original, ['aria-label']: name, draft_value(draft, node.first, false, original)>
  else <input id: id, class: "property-value", type: "text", value: draft_value(draft, node.first, false, original),
      ['data-first']: string(node.first), ['data-end']: string(node.last),
      ['data-original']: original, ['data-value-literal']: if (node.value_literal == true) "json" else null,
      title: if (node.value_literal == true) "Edit this value as a quoted JSON string" else null, ['aria-label']: name>
}

fn child_path(path, index) => path ++ "." ++ string(index)

fn initial_pages(tree, rows) map^ {
  var pending = [{node: tree, path: "0"}], pages = {}, cursor = 0, remaining = rows - 1
  // Visit shallow groups first. A wide group can consume the remaining budget
  // without mounting its other children or visiting their descendants.
  while (remaining > 0 and cursor < len(pending)) {
    let group = pending[cursor]
    let children = group.node.children
    let count = min(remaining, len(children))
    if (count > 0) {
      pages = {*:pages, [group.path]:count}
      remaining = remaining - count
      if (remaining > 0) {
        pending = [*pending, *[for (i, child in children where (len(child.node.children) > 0)^)
          {node:child.node, path:child_path(group.path, i)}]]
      }
    }
    cursor = cursor + 1
  }
  pages
}

fn node_view(name, node, root, key_first, key_end, section, format, draft, path, tree, key_literal = false) any^ {
  let branch = tree.branches[path]
  let initial = tree.pages[path]
  let count = len(node.children)
  let expanded = count > 0 and (if (branch == null) initial != null else branch.open)
  let limit = if (branch != null) branch.limit else initial or tree.rows;
  <div class: "property-entry",
    <div class: "property-row", ['data-first']: string(node.first), ['data-kind']: string(node.kind),
      ['data-add']: if (node.kind != 'scalar' and format.can_add != false and node.can_add != false) "true" else "false",
      ['data-add-element']: if (node.can_add_element == true and format.can_add_element == true) "true" else "false",
      ['data-delete']: if (not root and node.group_header != true and format.can_delete != false and node.can_delete != false) "true" else "false",
      ['data-delete-element']: if (node.can_delete_element == true and format.can_delete_element == true) "true" else "false",
      if (count > 0)
        apply(<property_toggle path: path, expanded: expanded, limit: limit>)
      else null
      if (section != null and section != "") <span class: "property-section", section ++ " /"> else null
      if (key_first != null and format.can_rename != false and node.can_rename != false) <input class: "property-key", style:"flex:0 0 " ++ string(tree.width) ++ "%", type: "text", value: draft_value(draft, key_first, true, name),
        ['data-first']: string(key_first), ['data-end']: string(key_end), ['data-original']: name,
        ['data-key-literal']: if (key_literal) "json" else null,
        title: if (key_literal) "Edit this name as a quoted JSON string" else null, ['aria-label']: "Property name: " ++ name>
      else <span class: "property-name", style:"flex:0 0 " ++ string(tree.width) ++ "%", name>;
      <span class:"property-resize", title:"Resize property columns", role:"separator", ['aria-label']:"Resize property columns">
      if (node.group_header == true) <span class: "property-kind", if (node.view_only == true) "Open source to edit" else "{ }">
      else if (node.kind == 'scalar')
        value_view(name, node, format, draft, "property-value-" ++ path, true)
      else <span class: "property-kind", chrome.collection_summary(node.kind, count)>
    >
    if (expanded)
      <div class: "property-children",
        for (i, child in take(node.children, limit))
          node_view(child.name, child.node, false, if (node.kind == 'object') child.first else null,
            child.key_end, child.group, format, draft, child_path(path, i), tree, child.key_literal == true)
        if (count > limit)
          apply(<property_button action: "more", path: path, limit: limit,
            label: "Show more (" ++ string(count - limit) ++ " remaining)">)>
    else null
  >
}

// Rows are data, including the first row; numeric headings do not assume a header.
fn grid_view(tree, format, draft) any^ {
  let widths = [for (row in tree.children) len(row.node.children)]
  let columns = if (len(widths) == 0) 0 else max(widths)
  let add = apply(<property_button action: "add", label: "+ row", title: "Add row", first: tree.first, kind: tree.kind>)
  let headings = [<th "Row">, *[for (i in 0 to columns - 1) <th
    *["Column " ++ string(i + 1),
      apply(<property_button action: "insert_column", label: "+", title: "Insert column before column " ++ string(i + 1), first: i>),
      apply(<property_button action: "delete_column", label: "−", title: "Remove column " ++ string(i + 1), first: i>)]>],
    <th *["Actions", if (columns > 0) apply(<property_button action: "insert_column", label: "+ column", title: "Add column", first: columns>) else null]>]
  let rows = [for (row_index, row in tree.children) {
    let insert = apply(<property_button action: "insert_row", label: "+", title: "Insert row before row " ++ string(row_index + 1), first: row.node.first>)
    let remove = apply(<property_button action: "delete", label: "−", title: "Remove row", first: row.node.first, kind: row.node.kind>)
    let cells = [for (cell_index, cell in row.node.children) {
      let field = value_view("Row " ++ string(row_index + 1) ++ ", column " ++ string(cell_index + 1), cell.node, format, draft,
                             "grid-cell-" ++ string(row_index) ++ "-" ++ string(cell_index))
      let remove_field = apply(<property_button action: "delete", label: "−", title: "Remove field", first: cell.node.first, kind: cell.node.kind>);
      <td <div class: "property-grid-cell", *[field, remove_field]>>
    }]
    let add_field = apply(<property_button action: "add", label: "+ field", title: "Add field", first: row.node.first, kind: row.node.kind>);
    <tr *[<th *[string(row_index + 1), insert, remove]>, *cells,
      *[for (i in (len(row.node.children)^) to columns - 1) <td>], <td add_field>]>
  }]
  let table = <table class: "property-grid", <thead <tr *headings>> <tbody *rows>>;
  <div class: "property-grid-wrap", *[add, table]>
}

// The stable id lets the host restore this control after the tree is rebuilt.
view <property_toggle> { <button id: "property-toggle-" ++ ~.path, class: "property-toggle", ['aria-expanded']: if (~.expanded) "true" else "false",
  ['aria-label']: if (~.expanded) "Collapse" else "Expand", if (~.expanded) "▾" else "▸"> }
on click(evt) { emit("property_command", {action: "toggle", path: ~.path, open: not ~.expanded, limit: ~.limit}) }

view <property_button> { <button class: "property-" ++ ~.action ++ (if (~.toolbar) " edit-btn" else ""),
  title: ~.title, ['aria-label']: if (~.title != null) ~.title else ~.label, ~.label> }
on click(evt) { emit("property_command", {action: ~.action, node: evt.target, first: ~.first, kind: ~.kind, path: ~.path, limit: ~.limit}) }

pub fn app(session, doc, status) =>
  apply(<property_app session: session, doc: doc, status: status>, {mode: "edit"})

fn edited(format, doc, target) any^ {
  let first = int(dom.get_attribute(target, "data-first"))
  let end_pos = int(dom.get_attribute(target, "data-end"))
  let text = string(dom.get_state(target, "value"));
  (if (dom.matches(target, ".property-key")) {
     if (dom.get_attribute(target, "data-key-literal") == "json") format.rename_key_literal(doc, first, end_pos, text)
     else format.rename_key(doc, first, end_pos, text)
   }
   else if (dom.get_attribute(target, "data-value-literal") == "json") format.replace_scalar_literal(doc, first, end_pos, text)
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
      [if (dialog.container != 'object') null else if (session.format.multiline_keys == true)
         <textarea id: "property-new-key", class: "edit-dialog-field", rows: 2, placeholder: "Property name (Shift+Enter for a new line)", autofocus: "autofocus">
       else files.field("property-new-key", "", "Property name", true),
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

edit <property_app> () any^ state session: ~.session, doc: ~.doc, status: ~.status,
                          dialog: null, after_save: null, undo: [], redo: [], draft: null,
                          tree_rows: DEFAULT_TREE_ROWS, tree_measured: false, branches: {}, menu: null, column_width: 38.0, resize: null {
  let dirty = sess.is_dirty(session, doc)
  let content = if (session.format.id in ['csv', 'tsv']) grid_view(doc.tree, session.format, draft)
    else {
      let tree = {rows:tree_rows, pages:initial_pages(doc.tree, tree_rows)^, branches:branches, width:column_width};
      <div class: "property-tree", node_view("root", doc.tree, true, null, null, null, session.format, draft, "0", tree)>
    };
  <body class: "edit-app edit-format-property", tabindex: "0",
    <div class: "property-toolbar edit-toolbar edit-document-bar", files.file_label(session, dirty);
      <div class: "edit-document-actions",
        if (not (session.format.id in ['csv', 'tsv'])) apply(<property_button action:"toolbar_add", label:"+", title:"Add to selected node", toolbar:true>) else null
        for (action, label in {save:"Save", save_as:"Save As", undo:"Undo", redo:"Redo", source:"Source"})
          apply(<property_button action:string(action), label:label, toolbar:true>)>
    >
    <div class: "property-content", content>
    <div class: "property-status edit-status", role:"status", status>
    if (menu != null) <div class:"property-menu", role:"menu", style:"left:" ++ string(menu.x) ++ "px;top:" ++ string(menu.y) ++ "px",
      if (menu.add) apply(<property_button action:"add", label:if (menu.element) "Add attribute" else "Add item", first:menu.first, kind:menu.kind>) else null
      if (menu.element) apply(<property_button action:"add_element", label:"Add child element", first:menu.first>) else null
      if (menu.delete) apply(<property_button action:"delete", label:"Delete node", first:menu.first>) else null
      if (menu.delete_element) apply(<property_button action:"delete_element", label:"Delete node", first:menu.first>) else null
    > else null
    dialog_view(dialog, session)
  >
}
// Keep selection on the mounted row so clicking an input does not rebuild it.
on click(evt) {
  let target = if (dom.node_type(evt.target) == 1) evt.target else dom.parent_node(evt.target)
  let row = if (target == null) null else dom.closest(target, ".property-row")
  if (row != null) {
    let old = dom.query_selector(dom.root_node(row), ".property-row[data-selected='true']")
    if (old != null) { dom.remove_attribute(old, "data-selected") }
    dom.set_attribute(row, "data-selected", "true")
  }
  if (menu != null and (target == null or dom.closest(target, ".property-menu,.property-toolbar_add") == null)) { menu = null }
  return 'pass'
}
on editor_dismiss_menu(_) { menu = null }
on contextmenu(evt) {
  let target = if (dom.node_type(evt.target) == 1) evt.target else dom.parent_node(evt.target)
  let row = if (target == null) null else dom.closest(target, ".property-row")
  if (row == null) { return 'pass' }
  let can_delete = dom.get_attribute(row, "data-delete") == "true"
  let can_delete_element = dom.get_attribute(row, "data-delete-element") == "true"
  if (not can_delete and not can_delete_element) { return 'pass' }
  menu = {x:evt.clientX, y:evt.clientY, first:int(dom.get_attribute(row, "data-first")),
          delete:can_delete, delete_element:can_delete_element}
  return 'prevent-default'
}
on mousedown(evt) {
  if (evt.button != 0 or not dom.matches(evt.target, ".property-resize")) { return 'pass' }
  if (draft != null) { status = "Correct the invalid value before continuing."; return 'prevent-default' }
  let row = dom.closest(evt.target, ".property-row")
  let box = dom.bounding_box(row)
  if (box != null and box.width > 0) { resize = {x:evt.x, width:column_width, row_width:box.width} }
  return 'prevent-default'
}
on mousemove(evt) {
  if (resize != null) { column_width = max(10.0, min(80.0, resize.width + (evt.x - resize.x) * 100.0 / resize.row_width)) }
  return 'pass'
}
on mouseup(evt) {
  if (resize != null) {
    column_width = max(10.0, min(80.0, resize.width + (evt.x - resize.x) * 100.0 / resize.row_width))
    resize = null
  }
  return 'pass'
}
// A viewer can mount the editor after the document's load event has fired.
on render(evt) {
  if (evt.event_phase != 2 or tree_measured) { return 'pass' }
  let surface = dom.query_selector(evt.target, ".property-tree")
  if (surface == null) { return 'pass' }
  let viewport = dom.viewport_size(surface)
  let bounds = dom.bounding_box(surface)
  if ((viewport != null and bounds != null and viewport.height > 0)^) {
    tree_rows = max(2, int((viewport.height - bounds.y - PROPERTY_TREE_PADDING * 2) / PROPERTY_ROW_HEIGHT))
    tree_measured = true
  }
  return 'pass'
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
    let wrapper = dom.closest(evt.target, ".property-value-wrap")
    if (wrapper != null) {
      let mirror = dom.query_selector(wrapper, ".property-value-mirror")
      if (mirror != null) { dom.set_text_content(mirror, (string(dom.get_state(evt.target, "value"))^) ++ " ") }
    }
    dom.set_close_guard(evt.target, true)
  }
}
on property_command(req) {
  if ((draft != null)^) { status = "Correct the invalid value before continuing."; return }
  if (req.action == "toolbar_add") {
    let root = dom.root_node(req.node)
    let selected = dom.query_selector(root, ".property-row[data-selected='true']")
    // A scalar belongs to its enclosing collection, which accepts new siblings.
    let entry = if (selected == null) null else dom.closest(selected, ".property-entry")
    let parent = if (entry == null) null else dom.closest(dom.parent_node(entry), ".property-entry")
    let row = if (selected == null) dom.query_selector(root, ".property-row")
              else if (dom.get_attribute(selected, "data-kind") == "scalar" and parent != null)
                dom.query_selector(parent, ".property-row")
              else selected
    if (row == null) { return }
    let can_add = dom.get_attribute(row, "data-add") == "true"
    let can_add_element = dom.get_attribute(row, "data-add-element") == "true"
    let first = int(dom.get_attribute(row, "data-first"))
    let kind = symbol(dom.get_attribute(row, "data-kind"))
    if (can_add and can_add_element) {
      let box = dom.bounding_box(req.node)
      menu = {x:box.x, y:box.y + box.height, first:first, kind:kind, add:true, element:true}
    } else if (can_add) { dialog = {kind:'add_property', first:first, container:kind} }
    else if (can_add_element) { dialog = {kind:'add_element', first:first} }
    else { status = "Select a container to add an item." }
    return
  }
  if (menu != null) { menu = null }
  if (req.action == "toggle") {
    branches = {*:branches, [req.path]:{open:req.open, limit:req.limit}}
  }
  else if (req.action == "more") {
    branches = {*:branches, [req.path]:{open:true, limit:req.limit + tree_rows}}
  }
  else if (req.action == "save") {
    let saved = files.save_now(session, doc, req.node)
    session = saved.session
    status = saved.status
    dialog = saved.dialog
  } else if (req.action == "save_as") { dialog = files.save_as_dialog(session, req.node)^ }
  else if (req.action == "source") {
    emit("edit_switch", src.source_view(session, doc.text, sess.is_dirty(session, doc)))
  }
  else if (req.action == "add") { dialog = {kind: 'add_property', first: req.first, container: req.kind} }
  else if (req.action == "add_element") { dialog = {kind: 'add_element', first: req.first} }
  else if (req.action in ["delete", "delete_element", "insert_column", "delete_column", "insert_row"]) {
    let result = (if (req.action == "delete_element") session.format.delete_element(doc, req.first)
                  else if (req.action == "insert_row") session.format.insert_row(doc, req.first)
                  else if (req.action == "insert_column") session.format.insert_column(doc, req.first)
                  else if (req.action == "delete_column") session.format.delete_column(doc, req.first)
                  else session.format.delete_child(doc, req.first)) ^ { {error: ^.message} }
    if ((result.error != null)^) { status = result.error }
    else {
      status = ""
      undo = [*undo, doc]
      redo = []
      doc = result.doc
      branches = {}
      files.sync_window(req.node, session, doc)^
      dom.focus_set(dom.query_selector(dom.root_node(req.node), "body"), false)
    }
  }
  else if (req.action == "undo" or req.action == "redo") {
    let next = history_step(doc, undo, redo, req.action == "redo")
    doc = next.doc
    branches = {}
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
    branches = {}
    dialog = null
    status = ""
    files.sync_window(req.node, session, doc)^
    dom.focus_set(dom.query_selector(dom.root_node(req.node), "body"), false)
    return
  }
  let result = files.dialog_action(req.action, req.node,
    {session: session, doc: doc, status: status, dialog: dialog, after_save: after_save})^
  if ((result.navigation != null)^) {
    session = result.session; status = result.status; dialog = result.dialog; after_save = result.after_save
    emit("viewer_navigation", {entry:result.navigation, node:req.node})
    return
  }
  if ((result.source_text != null)^) {
    emit("edit_switch", src.reloaded_view(session, result.source_text, result.status)^)
    return
  }
  session = result.session
  status = result.status
  dialog = result.dialog
  after_save = result.after_save
  if (result.reloaded != null) { doc = result.reloaded; undo = []; redo = []; branches = {} }
  if (dialog == null) { files.focus_surface(req.node)^ }
}
on editor_navigate(evt) {
  if ((dialog != null)^) { return }
  if ((draft != null)^) { status = "Correct the invalid value before continuing."; return }
  // A tree click may arrive before the focused field's change event.
  let fields = dom.query_selector_all(dom.root_node(evt.target), ".property-value,.property-key")
  let changed = [for (field in fields where ((dom.get_state(field, "value") != dom.get_attribute(field, "data-original")) ^ { false })) field]
  if (len(changed) > 0) {
    let result = commit_field(session.format, doc, undo, redo, changed[0])^
    draft = result.draft; status = result.status
    if (not result.ok) { return }
    doc = result.doc; undo = result.undo; redo = result.redo
  }
  let decision = files.navigation_decision(evt.target, session, doc, evt.detail)^
  if (decision.navigate) { emit("viewer_navigation", {entry:evt.detail, node:evt.target}); return }
  dialog = decision.dialog
}
on keydown(evt) {
  if (evt.key == "Escape" and menu != null) { menu = null; return 'prevent-default' }
  if ((dialog != null)^) {
    return files.dialog_keydown(dialog, evt)^
  }
  let primary = evt.metaKey == true or evt.ctrlKey == true
  let horizontal = evt.key == "Tab" and not primary
  let vertical = evt.key == "Enter" and primary and evt.altKey != true
  if ((dialog == null and (horizontal or vertical) and
       dom.matches(evt.target, ".property-grid .property-value"))^) {
    let root = dom.root_node(evt.target)
    let id = dom.get_attribute(evt.target, "id")
    let direction = if (evt.shiftKey == true) -1 else 1
    let next_field = (if (horizontal) {
      let fields = dom.query_selector_all(root, ".property-grid .property-value")
      let positions = [for (i, field in fields where dom.get_attribute(field, "id") == id) i]
      let next = if (len(positions) > 0) positions[0] + direction else -1
      if (next >= 0 and next < len(fields)) fields[next] else null
    } else {
      // Cell IDs carry the row and column so uneven rows are not flattened.
      let parts = split(id, "-")^
      if (len(parts) == 4) {
        let row = int(parts[2]) + direction
        if (row < 0) null else dom.query_selector(root, "#grid-cell-" ++ string(row) ++ "-" ++ parts[3])
      } else null
    })^
    if (next_field != null or vertical) {
      // Programmatic focus changes do not necessarily dispatch a change event.
      let result = commit_field(session.format, doc, undo, redo, evt.target)^
      draft = result.draft; status = result.status
      if (not result.ok) { return 'prevent-default' }
      doc = result.doc; undo = result.undo; redo = result.redo
      files.sync_window(evt.target, session, doc)^
      if (next_field != null) { dom.focus_set(next_field, false) }
      return 'prevent-default'
    }
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
       (dom.matches(evt.target, "input.property-value,input.property-key") or
        dom.get_attribute(evt.target, "data-single-line") == "true"))^) {
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
    if (evt.shiftKey == true) { dialog = files.save_as_dialog(session, evt.target)^ }
    else {
      let saved = files.save_now(session, doc, evt.target)
      session = saved.session
      status = saved.status
      dialog = saved.dialog
    }
    return 'prevent-default'
  }
  if ((dialog == null and primary and lower(string(evt.key)) == "z")^) {
    let field = dom.matches(evt.target, ".property-value,.property-key")
    let pending = field and dom.get_state(evt.target, "value") != dom.get_attribute(evt.target, "data-original")
    // While typing, the control owns its local undo; otherwise use document history.
    if ((not pending and draft == null)^) {
      let next = history_step(doc, undo, redo, evt.shiftKey == true)
      doc = next.doc
      branches = {}
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
  files.release_focus(evt.target)^
  dialog = {kind: 'close'}
}
