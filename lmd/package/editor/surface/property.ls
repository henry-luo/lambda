// Structured data surface; the source-backed model keeps untouched tokens.
import dom
import sess: lambda.editor.session
import files: lambda.editor.app.files
import src: lambda.editor.surface.source
import chrome: lambda.doc.document_chrome
import context_menu: lambda.ui.core.menu

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
  .property-menu { position: fixed; z-index: 30; background: white; border: 1px solid #d0d7de;
                   border-radius: 6px; padding: 4px; box-shadow: 0 4px 16px rgba(0,0,0,.18);
                   max-height: calc(100vh - 16px); overflow: auto; }
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

fn node_view(name, node, key_first, key_end, section, format, draft, path, tree, key_literal = false) any^ {
  let branch = tree.branches[path]
  let initial = tree.pages[path]
  let count = len(node.children)
  let expanded = count > 0 and (if (branch == null) initial != null else branch.open)
  let limit = if (branch != null) branch.limit else initial or tree.rows;
  <div class: "property-entry",
    <div id:"property-row-" ++ path, class:"property-row", tabindex:if (node.kind != 'scalar') "0" else null,
      if (count > 0)
        apply(<property_toggle path: path, expanded: expanded, limit: limit>)
      else null
      if (section != null and section != "") <span class: "property-section", section ++ " /"> else null
      if (key_first != null and format.can_rename != false and node.can_rename != false) <input id:"property-key-" ++ path, class: "property-key", style:"flex:0 0 " ++ string(tree.width) ++ "%", type: "text", value: draft_value(draft, key_first, true, name),
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
          node_view(child.name, child.node, if (node.kind == 'object') child.first else null,
            child.key_end, child.group, format, draft, child_path(path, i), tree, child.key_literal == true)
        if (count > limit)
          apply(<property_button action: "more", path: path, limit: limit,
            label: "Show more (" ++ string(count - limit) ++ " remaining)">)>
    else null
  >
}

fn grid_columns(tree) {
  let widths = [for (row in tree.children) len(row.node.children)]
  if (len(widths) == 0) 0 else max(widths)
}

// Rows are data, including the first row; numeric headings do not assume a header.
fn grid_view(tree, format, draft) any^ {
  let columns = grid_columns(tree)
  let headings = [<th id:"property-grid-corner", scope:"col", tabindex:"0",
    title:"Table actions (right-click or Shift+F10)", "Row">,
    *[for (i in 0 to columns - 1) <th scope:"col", ['data-grid-column']:string(i), "Column " ++ string(i + 1)>]]
  let rows = [for (row_index, row in tree.children) {
    let cells = [for (i in 0 to columns - 1) {
      let cell = row.node.children[i];
      <td ['data-grid-column']:string(i), if (cell != null)
        value_view("Row " ++ string(row_index + 1) ++ ", column " ++ string(i + 1), cell.node, format, draft,
          "grid-cell-" ++ string(row_index) ++ "-" ++ string(i)) else null>
    }];
    <tr ['data-grid-row']:string(row_index), <th scope:"row", string(row_index + 1)>; *cells>
  }];
  <div class:"property-grid-wrap", <table class:"property-grid", <thead <tr *headings>> <tbody *rows>>>
}

fn grid_menu_items(tree, row_index, column) {
  let row = if (row_index != null) tree.children[row_index].node else null
  let cell = if (row != null and column != null) row.children[column].node else null
  let columns = grid_columns(tree);
  [
    {action:"add", label:"Add row", first:tree.first, kind:tree.kind},
    if (row != null) {action:"insert_row", label:"Insert row above", first:row.first} else null,
    if (row != null) {action:"delete", label:"Remove row", first:row.first} else null,
    if (columns > 0) {action:"insert_column", label:"Add column", first:columns} else null,
    if (column != null) {action:"insert_column", label:"Insert column before", first:column} else null,
    if (column != null) {action:"delete_column", label:"Remove column", first:column} else null,
    if (row != null) {action:"add", label:"Add field to row", first:row.first, kind:row.kind} else null,
    if (cell != null) {action:"delete", label:"Remove field", first:cell.first} else null
  ]
}

fn grid_menu_target(target) map^ {
  let row = dom.closest(target, "[data-grid-row]")
  let column = dom.closest(target, "[data-grid-column]")
  {row:if (row == null) null else int(dom.get_attribute(row, "data-grid-row")),
   column:if (column == null) null else int(dom.get_attribute(column, "data-grid-column"))}
}

fn tree_menu_items(tree, format, row_id) any^ {
  // Resolve the rendered path after committing a field, which may shift source offsets.
  let path = split(row_id, ".")^
  var node = tree, parent = null, i = 1
  while (i < len(path) and node != null) {
    parent = node
    node = parent.children[int(path[i])^].node
    i = i + 1
  }
  if (node == null) { return [] }
  let container = if (node.kind == 'scalar') parent else node
  let can_add = container != null and format.can_add != false and container.can_add != false
  let can_add_element = container != null and format.can_add_element == true and container.can_add_element == true
  let can_delete = len(path) > 1 and node.group_header != true and format.can_delete != false and node.can_delete != false
  let can_delete_element = format.can_delete_element == true and node.can_delete_element == true
  let label = if (can_add_element) "Add attribute" else if (container.kind == 'object') "Add property" else "Add item";
  [if (can_add) {action:"add", label:label, first:container.first, kind:container.kind} else null,
   if (can_add_element) {action:"add_element", label:"Add child element", first:container.first} else null,
   if (can_delete) {action:"delete", label:"Delete node", first:node.first} else null,
   if (can_delete_element) {action:"delete_element", label:"Delete node", first:node.first} else null]
}

pn request_property_menu(node, x, y, origin = null) {
  let target = if (dom.node_type(node) == 1) node else dom.parent_node(node)
  if (target == null or dom.closest(target, ".property-content") == null) { return 'pass' }
  let grid = dom.closest(target, ".property-grid-content") != null
  let row = if (grid) null else dom.closest(target, ".property-row")
  if (not grid and row == null) { return 'pass' }
  let context = if (grid) grid_menu_target(target)^ else {row_id:dom.get_attribute(row, "id")}
  let surface = dom.get_element_by_id(dom.root_node(target), "property-surface")
  let accepted = dom.dispatch(surface, {type:"property_menu", bubbles:false, cancelable:true,
    detail:{*:context, grid:grid, node:target, x:x, y:y, origin:origin}})
  return if (accepted) 'pass' else 'prevent-default'
}

// The stable id lets the host restore this control after the tree is rebuilt.
view <property_toggle> { <button id: "property-toggle-" ++ ~.path, class: "property-toggle", ['aria-expanded']: if (~.expanded) "true" else "false",
  ['aria-label']: if (~.expanded) "Collapse" else "Expand", if (~.expanded) "▾" else "▸"> }
on click(evt) { emit("property_command", {action: "toggle", path: ~.path, open: not ~.expanded, limit: ~.limit}) }

view <property_button> { <button class: "property-" ++ ~.action ++ (if (~.toolbar) " edit-btn" else ""), role: ~.role,
  title: ~.title, ['aria-label']: if (~.title != null) ~.title else ~.label, ~.label> }
on click(evt) { emit("property_command", {action: ~.action, node: evt.target, first: ~.first, kind: ~.kind, path: ~.path, limit: ~.limit}) }

pub fn app(session, doc, status, embedded = false, modes = null) =>
  apply(<property_app session:session, doc:doc, status:status, embedded:embedded, modes:modes>, {mode:"edit"})

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
  let grid = session.format.id in ['csv', 'tsv']
  let content = if (grid) grid_view(doc.tree, session.format, draft)
    else {
      let tree = {rows:tree_rows, pages:initial_pages(doc.tree, tree_rows)^, branches:branches, width:column_width};
      <div class:"property-tree", node_view("root", doc.tree, null, null, null, session.format, draft, "0", tree)>
    };
  <body id:"property-surface", class: "edit-app edit-format-property", tabindex: "0",
    <div class: "property-toolbar edit-toolbar edit-document-bar",
      files.file_label(session, dirty, false, ~.modes, {event:"property_command", detail:{action:"source"}});
      <div class: "edit-document-actions",
        for (action, label in {save:"Save", save_as:"Save As", undo:"Undo", redo:"Redo", source:"Source"}
            where action != 'source' or ~.modes == null)
          apply(<property_button action:string(action), label:label, toolbar:true>)>
    >
    if (~.embedded) files.status_notice(status) else null;
    <div class:"property-content" ++ (if (grid) " property-grid-content" else ""), content>
    if (~.embedded) null else <div class: "property-status edit-status", role:"status", status>
    if (menu != null) context_menu.frame(menu.id, "property-surface",
      <div id:menu.id, class:"property-menu", role:"menu", ['aria-label']:menu.label,
        style:"left:" ++ string(menu.x) ++ "px;top:" ++ string(menu.y) ++ "px",
        for (item in menu.items) apply(<property_button role:"menuitem", *:item>)
      >) else null
    dialog_view(dialog, session)
  >
}
on click(evt) {
  let target = if (dom.node_type(evt.target) == 1) evt.target else dom.parent_node(evt.target)
  if (menu != null and (target == null or dom.closest(target, ".property-menu") == null)) { menu = null }
  return 'pass'
}
on editor_dismiss_menu(_) { menu = null }
on property_menu(evt) {
  if (dialog != null or draft != null) { return 'prevent-default' }
  let req = evt.detail
  let field = dom.query_selector(evt.target, ".property-value:focus,.property-key:focus")
  // Commit the live field before resolving the menu's source positions.
  if (field != null and dom.get_state(field, "value") != dom.get_attribute(field, "data-original")) {
    let result = commit_field(session.format, doc, undo, redo, field)^
    draft = result.draft; status = result.status
    if (not result.ok) { return 'prevent-default' }
    doc = result.doc; undo = result.undo; redo = result.redo
    files.sync_window(req.node, session, doc)^
  }
  let origin = req.origin or (if (field != null) dom.get_attribute(field, "id") else null)
  let actions = if (req.grid) grid_menu_items(doc.tree, req.row, req.column)
    else tree_menu_items(doc.tree, session.format, req.row_id)^
  let items = [for (action in actions where action != null) action]
  if (len(items) == 0) { menu = null; return 'pass' }
  menu = {id:if (req.grid) "property-grid-menu" else "property-tree-menu",
    label:if (req.grid) "Table actions" else "Property actions", x:req.x, y:req.y, origin:origin, items:items}
  return 'prevent-default'
}
on contextmenu(evt) {
  return request_property_menu(evt.target, evt.clientX, evt.clientY)^
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
    emit("edit_switch", src.source_view(session, doc.text, sess.is_dirty(session, doc), ~.embedded))
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
      files.focus_surface(req.node)^
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
    files.focus_surface(req.node)^
    return
  }
  let result = files.dialog_action(req.action, req.node,
    {session: session, doc: doc, status: status, dialog: dialog, after_save: after_save})^
  if ((result.navigation != null)^) {
    session = result.session; status = result.status; dialog = result.dialog; after_save = result.after_save
    emit("viewer_navigation", files.navigation_result(result.navigation, req.node, session)^)
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
  if (decision.navigate) { emit("viewer_navigation", files.navigation_result(evt.detail, evt.target, session)^); return }
  dialog = decision.dialog
}
on keydown(evt) {
  if (menu != null) {
    let action = context_menu.key_action(evt.key)^
    if (action == 'dismiss') {
      let origin = menu.origin
      menu = null
      if (origin != null) dom.focus_set(dom.get_element_by_id(dom.root_node(evt.target), origin), false)
      else { files.focus_surface(evt.target)^ }
      return if (evt.key == "Escape") 'prevent-default' else 'pass'
    }
    if (dom.closest(evt.target, ".property-menu") != null) {
      if (action == 'navigate') {
        context_menu.navigate(evt.target, menu.id, evt.key)^
        return 'prevent-default'
      }
      return 'pass'
    }
  }
  if ((dialog != null)^) {
    return files.dialog_keydown(dialog, evt)^
  }
  if (evt.key == "ContextMenu" or (evt.key == "F10" and evt.shiftKey == true)) {
    let box = dom.bounding_box(evt.target)
    if (box == null) { return 'pass' }
    return request_property_menu(evt.target, box.x, box.y + box.height, dom.get_attribute(evt.target, "id"))^
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
    emit("edit_switch", src.source_view(session, doc.text, sess.is_dirty(session, doc), ~.embedded))
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
