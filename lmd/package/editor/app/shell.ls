// shell.ls — the edit application page (vibe/radiant/Radiant_Design_Edit_Mode.md
// §3, §7, §8): a top toolbar, the format's editing surface, a status line, and
// the file dialogs, with the session state behind them.
//
// One `edit <edit_app>` instance owns the session. Its state is seeded from
// the model the loader built and changes only in its `on` handlers (S12.1.3);
// the body is a pure projection of that state. Editing requests from keys,
// IME, clipboard and toolbar all go through lambda.editor's descriptor path.

import dom
import lambda.editor.model
import lambda.editor.core.source_pos
import edit_result: lambda.dom.edit_result
import sess: lambda.editor.session
import files: lambda.editor.app.files
import tools: lambda.editor.app.toolbar
import rich: lambda.editor.surface.rich_text
import dr: lambda.editor.surface.drawing
import src: lambda.editor.surface.source
import controller: lambda.editor.controller
import lambda.editor.format.model

// ---------------------------------------------------------------------------
// Page
// ---------------------------------------------------------------------------

let shell_css = "
  /* the body grows with the document: a sticky toolbar and status line stay
     inside their containing block, so a body fixed to the viewport's height
     would carry both away once the page scrolls past its first screen */
  body { margin: 0; min-height: 100%;
         font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif;
         background: #ffffff; color: #1f2328; }
  .edit-app { display: flex; flex-direction: column; min-height: 100%; }
  .edit-main { flex: 1; max-width: 980px; width: 100%; margin: 0 auto; }
  .edit-status { position: sticky; bottom: 0; padding: 4px 12px; font-size: 12px;
                 color: #57606a; background: #f6f8fa; border-top: 1px solid #d0d7de; }
  .edit-table-menu { position: fixed; z-index: 30; background: white; border: 1px solid #d0d7de;
                     border-radius: 6px; padding: 4px; box-shadow: 0 4px 16px rgba(0,0,0,.18); }
  .edit-table-menu .edit-group { display: flex; flex-direction: column; align-items: stretch; }
  .edit-table-menu .edit-btn { width: 150px; text-align: left; }
"

// The surfaces' own stylesheets; the edit application adds the shared ones.
pub let surface_css = shell_css ++ rich.css ++ dr.css

// The rich or drawing surface as the edit application's body. The template
// renders <body> itself, so a key or close request that no element claims (it
// targets <body>, as in a browser) still reaches it.
pub fn app(session, editor, status) =>
  apply(<edit_app session: session, editor: editor, status: status>, {mode: "edit"})

// The source view of the document (Radiant_Design_Source_Editor OQ7).
fn source_view(session, editor) any^ =>
  src.source_view(session, session.format.export_text(editor.doc, session.envelope),
                  sess.is_dirty(session, editor.doc))

// ---------------------------------------------------------------------------
// Dialogs
// ---------------------------------------------------------------------------

// The surface's own dialogs; the file dialogs come from files.ls.
fn dialog_view(dialog, session) any^ {
  if (dialog == null) null
  else if (member(files.dialog_kinds, dialog.kind)) files.dialog_view(dialog, session)
  else if (dialog.kind == 'link') {
    files.dialog_frame("link", "Insert link", "Link the selection (or insert the address).",
      [files.field("edit-dialog-href", "", "https://", true)],
      [files.dialog_button("cancel", "Cancel", false), files.dialog_button("link_ok", "Insert", true)])
  }
  else if (dialog.kind == 'style') {
    files.dialog_frame("style", "Fill and stroke",
      "Any SVG paint: #rrggbb, a color name, or none. An empty field is left as it is.",
      [files.field("edit-dialog-fill", dialog.fill, "Fill"), files.field("edit-dialog-stroke", dialog.stroke, "Stroke"),
       files.field("edit-dialog-width", dialog.width, "Stroke width")],
      [files.dialog_button("cancel", "Cancel", false), files.dialog_button("style_ok", "Apply", true)])
  }
  else if (dialog.kind == 'words') {
    // not kind 'text': every dialog has an .edit-dialog-text description
    files.dialog_frame("words", "Text", "The words the text object shows.",
      [files.field("edit-dialog-text", dialog.text, "Text")],
      [files.dialog_button("cancel", "Cancel", false), files.dialog_button("text_ok", "OK", true)])
  }
  else if (dialog.kind == 'image') {
    files.dialog_frame("image", "Insert image", "Image address and description.",
      [files.field("edit-dialog-src", "", "images/picture.png"), files.field("edit-dialog-alt", "", "Description")],
      [files.dialog_button("cancel", "Cancel", false), files.dialog_button("image_ok", "Insert", true)])
  }
  else null
}

// The document of an event target. A click on a label targets its text node;
// `root_node` answers for any connected node, where owner_document has no
// value for a text node in a document without a JS realm.
fn doc_root(node) => dom.root_node(node)

// Bind the editor to its surface once. A caret placed by a click reports no
// `selectionchange`, so the first handler of any kind mounts; without a bound
// surface a toolbar or dialog completion could not restore the selection.
pn mounted(editor, node, format) {
  if (editor.mounted or format.surface != 'rich_text') { return editor }
  controller.mount(editor,node,format.schema_preset)
}

// Run one editing request through the model path and complete it on the
// surface — the descriptor/request route every toolbar command and dialog
// insert takes (Lambda DOM Editable §8.3). Returns the adapter's run record.
let run_request = controller.request

// A view-only part is kept as written (proposal §2): an edit may remove it as
// a unit, but never write into it or replace it. A click in a part puts the
// action's caret inside it (or selects it), and a stray key must not
// overwrite a block whose source the user cannot see.
let unit_families = ["delete", "drag_delete", "cut", "copy", "history_undo", "history_redo",
                     "selection", "setting"]

// The view-only part a model path is or lies in (the outermost), or null.
fn view_only_part(session, doc, path) any^ {
  let parts = if (len(path) == 0) []
              else [for (i in 1 to len(path) where ((member(session.format.view_only_tags, node_tag_at(doc, take(path, i)))) ^ { false }))
                      take(path, i)]
  if (len(parts) == 0) null else parts[0]
}

// The paths a selection's ends lie at; a node selection's node.
fn selection_ends(selection) {
  if (selection == null) []
  else if (selection.kind == 'node') [selection.path]
  else if (selection.kind == 'text') [selection.anchor.path, selection.head.path]
  else []
}

fn writes_view_only(session, doc, selection, input_type) =>
  (any([for (p in selection_ends(selection)) view_only_part(session, doc, p) != null]) and
   not member(unit_families, edit_action_family(input_type))) ^ { false }

// A deletion from inside one view-only part removes the whole part: the
// action is retargeted to the part as a node selection.
fn deleted_part(session, doc, selection, input_type) any^ {
  let ends = selection_ends(selection)
  let part = if (len(ends) == 0 or edit_action_family(input_type) != "delete") null
             else view_only_part(session, doc, ends[0])
  if (part != null and all([for (p in ends) view_only_part(session, doc, p) == part])) node_selection(part)
  else null
}

let view_only_status = "This part is view-only and is saved as written; press Delete to remove it."

fn command_problem(session, editor, item) any^ =>
  if (item == null or not tools.command_supported(item, session.format.unsupported_input_types, session.format.allowed_model_marks)^)
    "Not available in " ++ session.format.name ++ " documents."
  else if (writes_view_only(session, editor.doc, editor.selection, item.input_type)) view_only_status
  else null

fn primary_key(evt) => evt.metaKey == true or evt.ctrlKey == true

fn key_is(evt, letter) any^ => lower(string(evt.key)) == letter

fn keyboard_format_command(evt) =>
  if (not primary_key(evt)) null
  else if (evt.shiftKey == true and evt.altKey != true) {
    if (evt.key in ["7", "&"]) "ol" else if (evt.key in ["8", "*"]) "ul"
    else if (key_is(evt, "x") ^ { false }) "strike" else null
  }
  else if (evt.shiftKey == true) null
  else if (evt.altKey == true) {
    if (evt.key == "0") "p" else if (evt.key == "1") "h1" else if (evt.key == "2") "h2"
    else if (evt.key == "3") "h3" else null
  }
  else if (key_is(evt, "k") ^ { false }) "link"
  else if (key_is(evt, "e") ^ { false }) "code" else null

// Toolbar and keyboard commands share model dispatch and status handling.
pn apply_format_item(editor, item) {
  let input_type = if (item.lift != null and edit_inside(editor, item.inside)) item.lift else item.input_type
  let run = run_request(editor, input_type, item.payload)
  {editor: run.editor, status: if (run.result.failure == null) "" else "Could not apply " ++ item.title ++ "."}
}

// ---------------------------------------------------------------------------
// Drawing input: drawing.ls owns the model edits; these read the rendered
// canvas to turn pointer positions into user units and pressed elements
// into objects.
// ---------------------------------------------------------------------------

fn is_drawing(session) => session.format.surface == 'drawing'

fn canvas_node(node) { dom.query_selector(doc_root(node), ".edit-canvas") }

// A viewport point (a mouse record's `x`/`y`, MouseEvent's clientX/clientY)
// in the drawing's user units, through the canvas box.
fn user_point(canvas, cx, cy, doc) {
  let r = if (canvas == null) null else dom.bounding_box(canvas)
  let vb = dr.view_box(doc)
  if (r == null or r.width == 0 or r.height == 0) {x: vb.x, y: vb.y}
  else {x: vb.x + (cx - r.left) * vb.w / r.width, y: vb.y + (cy - r.top) * vb.h / r.height}
}

fn element_attr(node, key) {
  if (node == null or dom.node_type(node) != 1) null else dom.get_attribute(node, key)
}

// The top-level object a pressed node belongs to ({index, node}), or null
// for the canvas background.
fn object_hit(node) any^ {
  if (node == null) null else {
    let index = element_attr(node, "data-edit-path")
    if (index != null) {index: int(index), node: node}
    else if (dom.node_type(node) == 1 and dom.matches(node, ".edit-canvas")) null
    else object_hit(dom.parent_node(node))
  }
}

// An object's frame: its own geometry when the tools know it, else the box
// it rendered, in user units.
fn frame_of(canvas, hit, doc) any^ {
  let r = dom.bounding_box(hit.node)
  let a = if (r == null) null else user_point(canvas, r.left, r.top, doc)
  let b = if (r == null) null else user_point(canvas, r.left + r.width, r.top + r.height, doc)
  dr.frame_box(doc.content[hit.index], if (a == null) null else dr.box(a.x, a.y, b.x - a.x, b.y - a.y))
}

// One drawing transaction is one undoable edit.
fn apply_edit(editor, tx) any^ => if (tx == null) editor else edit_apply(editor, tx)

fn shifted(b, dx, dy) any^ => if (b == null) null else dr.box(b.x + dx, b.y + dy, b.w, b.h)

fn style_dialog(ds, doc) any^ {
  let first = if (len(ds.picked) > 0) doc.content[ds.picked[0].index] else null
  if (first == null) {kind: 'style', fill: ds.style.fill, stroke: ds.style.stroke, width: ds.style.width}
  else {kind: 'style', fill: dr.paint_value(first, "fill"), stroke: dr.paint_value(first, "stroke"),
        width: dr.paint_value(first, "stroke-width")}
}

// The view fits the drawing into the main area.
fn fit_zoom(node, doc) any^ {
  let main = dom.query_selector(doc_root(node), ".edit-main")
  let r = if (main == null) null else dom.bounding_box(main)
  let vb = dr.view_box(doc)
  if (r == null or vb.w <= 0 or vb.h <= 0) 1.0
  else max(min((r.width - 64.0) / vb.w, (r.height - 64.0) / vb.h), 0.05)
}

fn zoomed(zoom, factor) => min(max(zoom * factor, 0.05), 16.0)

fn text_dialog_for(doc, index) =>
  {kind: 'words', index: index, text: node_plain_text(doc.content[index])}

// ---------------------------------------------------------------------------
// The application template
// ---------------------------------------------------------------------------

fn format_groups(session) any^ =>
  if (is_drawing(session)) tools.drawing_groups()
  else tools.rich_text_groups(session.format.underline == true, session.format.unsupported_input_types,
                              session.format.allowed_model_marks)

pn insert_dialog_link(editor, target) {
  let href = trim(files.field_value(target, "edit-dialog-href"))
  if (href == "") editor else run_request(editor, "insertLink", {href: href, title: "", label: href}).editor
}

fn surface_of(session, editor, ds) any =>
  if (is_drawing(session)) dr.surface(editor.doc, ds)
  else rich.surface(editor, "edit-" ++ string(session.format.id),
    if (session.format.surface_style == null) null else session.format.surface_style(session.envelope))

view <table_context> {
  <div class: "edit-table-menu", role: "group", ['aria-label']: "Table actions", style: "left:" ++ string(~.x) ++ "px;top:" ++ string(~.y) ++ "px",
    tools.group(tools.table_menu, ~.editor, ~.dirty, null)>
}
on render(evt) {
  if (evt.event_phase == 2 and ~.keyboard) {
    let first = dom.query_selector(evt.target, ".edit-btn-table_row_add")
    if (first != null) { dom.focus_set(first, true) }
  }
  return 'pass'
}

edit <edit_app> state editor: ~.editor, session: ~.session, status: ~.status, dialog: null, after_save: null,
                      ds: dr.new_state(), table_menu: null {
  let dirty = sess.is_dirty(session, editor.doc);
  <body class: "edit-app edit-format-" ++ string(session.format.id),
    tools.toolbar(files.file_label(session, dirty), editor, dirty,
                  if (is_drawing(session)) ds else null, format_groups(session), tools.source_view_group,
                  not member(session.format.unsupported_input_types, "insertTable"));
    <div class: "edit-main", surface_of(session, editor, ds)>
    <div class: "edit-status", role: "status", status>
    if (table_menu != null) apply(<table_context x: table_menu.x, y: table_menu.y, keyboard: table_menu.keyboard, editor: editor, dirty: dirty>) else null
    dialog_view(dialog, session)
  >
}
on beforeinput(evt) {
  return 'pass'
}
on contextmenu(evt) {
  let target = if (dom.node_type(evt.target) == 1) evt.target else dom.parent_node(evt.target)
  if ((is_drawing(session) or target == null or dom.closest(target, "td,th") == null)^) { return 'pass' }
  let selected = controller.adopt(editor, evt, session.format.schema_preset)
  editor = selected.editor
  table_menu = {x: evt.clientX, y: evt.clientY}
  return 'prevent-default'
}
on editaction(evt) {
  // A copy changes nothing in the model; Radiant has already copied the DOM
  // selection. Claim it as a no-op without writing state: a render would
  // replace the selected nodes, and the editor has no copy command to run.
  if (evt.input_type == "copy") {
    return controller.handle(editor,evt).result
  }
  editor = mounted(editor, evt.target, session.format)
  // A command the format cannot write is declined, not applied and lost later.
  if (member(session.format.unsupported_input_types, evt.input_type)^) {
    status = "Not available in " ++ session.format.name ++ " documents."
    return edit_result.decline(true, false, "unsupported", 0)
  }
  let target = edit_action_selection(editor, evt)
  if (writes_view_only(session, editor.doc, target, evt.input_type)^) {
    status = view_only_status
    // The status redraw replaces the caret's DOM node even though no edit ran.
    controller.restore(editor)^
    return edit_result.decline(true, false, "view-only", 0)
  }
  // The caret decides (a deletion's target range runs past the part). The
  // part's deletion runs as a model request on the part: the event's input
  // fields are host-backed, so a copy of the event would lose them.
  let part = deleted_part(session, editor.doc, evt.source_selection or target, evt.input_type)
  let run = if (part == null) controller.handle(editor, evt, false,
                if (session.format.surface == 'rich_text') session.format.schema_preset else null)
            else edit_handle_request(edit_set_selection(editor, part), edit_request_from_toolbar(evt.input_type, {}))
  editor = run.editor
  if ((not (run.result.failure is null))^) { status = "Could not apply " ++ evt.input_type ++ "." }
  files.sync_window(evt.target, session, editor.doc)
  return run.result
}
on selectionchange(evt) {
  let accepted = controller.adopt(editor,evt,session.format.schema_preset)
  if (accepted.changed) {
    editor = accepted.editor
    controller.restore(editor)
  }
}
on edit_cmd(req) {
  table_menu = null
  editor = mounted(editor, req.node, session.format)
  let cmd = req.cmd
  if (cmd == "view_source") {
    emit("edit_switch", source_view(session, editor)^)
    return
  }
  if (cmd == "save") {
    let saved = files.save_now(session, editor.doc, req.node)
    session = saved.session
    status = saved.status
    if (saved.dialog != null) { dialog = saved.dialog }
  }
  else if (cmd == "save_as") {
    dialog = {kind: 'save_as', path: session.path}
  }
  else if (is_drawing(session)) {
    let picked = dr.picked_indices(ds)
    if (starts_with(cmd, "tool_")^) { ds = {*: ds, tool: symbol(slice(cmd, 5, len(cmd))), gesture: null} }
    else if (cmd == "style") { dialog = style_dialog(ds, editor.doc) }
    else if (cmd == "undo" or cmd == "redo") {
      editor = run_request(editor, if (cmd == "undo") "historyUndo" else "historyRedo", {}).editor
      // indices may name other objects once the tree changes back
      ds = {*: ds, picked: [], gesture: null}
    }
    else if (cmd == "delete" and len(picked) > 0) {
      editor = apply_edit(editor, dr.delete_tx(editor.doc, picked))
      ds = {*: ds, picked: []}
    }
    else if (cmd == "duplicate" and len(picked) > 0) {
      let copies = dr.duplicate_tx(editor.doc, picked)
      editor = apply_edit(editor, copies.tx)
      let boxes = [for (q in ds.picked) shifted(q.box, 10.0, 10.0)]
      ds = ({*: ds, picked: [for (j, pick of ((copies.picked) ^ { [] })) {index: pick, box: boxes[j]}]})^
    }
    else if ((cmd == "front" or cmd == "back") and len(picked) == 1) {
      let moved = dr.order_tx(editor.doc, picked[0], cmd == "front")
      if ((moved != null)^) {
        editor = apply_edit(editor, moved.tx)
        ds = {*: ds, picked: [{*: ds.picked[0], index: moved.index}]}
      }
    }
    else if (cmd == "zoom_in") { ds = {*: ds, zoom: zoomed(ds.zoom, 1.25)} }
    else if (cmd == "zoom_out") { ds = {*: ds, zoom: zoomed(ds.zoom, 0.8)} }
    else if (cmd == "zoom_fit") { ds = {*: ds, zoom: fit_zoom(req.node, editor.doc)} }
    files.sync_window(req.node, session, editor.doc)
  }
  else {
    let item = if (cmd == "undo" or cmd == "redo") tools.find_item([tools.file_group], cmd)
               else tools.find_item([*format_groups(session), tools.table_insert, tools.table_menu], cmd)
    if (item == null) { status = "Unknown command " ++ cmd ++ "." }
    else if (command_problem(session, editor, item)^ != null) { status = command_problem(session, editor, item)^ }
    else if (item.dialog != null) { dialog = {kind: item.dialog} }
    else {
      let result = apply_format_item(editor, item)
      editor = result.editor
      status = result.status
      files.sync_window(req.node, session, editor.doc)^
      if (starts_with(cmd, "table_")^) {
        // Menu activation must return typing to the retained document selection.
        controller.focus(req.node)^
        controller.restore(editor)^
      }
    }
  }
}
on edit_dialog(req) {
  editor = mounted(editor, req.node, session.format)
  let action = req.action
  var edited = false
  // the file dialogs' actions are shared with the source surface (files.ls)
  let filed = files.dialog_action(action, req.node,
    {session: session, doc: editor.doc, status: status, dialog: dialog, after_save: after_save})
  if (filed.handled^) {
    session = filed.session
    status = filed.status
    dialog = filed.dialog
    after_save = filed.after_save
    if ((filed.reloaded != null)^) {
      editor = controller.replace(editor,edit_open(filed.reloaded, session.format.schema, null))
      ds = {*: ds, picked: [], gesture: null}
    }
  }
  else if (action == "style_ok") {
    let changes = [["fill", trim(files.field_value(req.node, "edit-dialog-fill"))],
                   ["stroke", trim(files.field_value(req.node, "edit-dialog-stroke"))],
                   ["stroke-width", trim(files.field_value(req.node, "edit-dialog-width"))]]
    dialog = null
    if ((len(ds.picked) > 0)^) {
      editor = apply_edit(editor, dr.paint_tx(editor.doc, dr.picked_indices(ds), changes))
      files.sync_window(req.node, session, editor.doc)^
    }
    else {
      // with nothing picked the dialog sets the paint of new shapes
      ds = {*: ds, style: {fill: if (changes[0][1] == "") ds.style.fill else changes[0][1],
                           stroke: if (changes[1][1] == "") ds.style.stroke else changes[1][1],
                           width: if (changes[2][1] == "") ds.style.width else changes[2][1]}}
    }
  }
  else if (action == "text_ok") {
    let words = trim(files.field_value(req.node, "edit-dialog-text"))
    let target = dialog
    dialog = null
    if ((words != "" and target.index != null)^) {
      editor = apply_edit(editor, dr.retext_tx(editor.doc, target.index, words))
      files.sync_window(req.node, session, editor.doc)^
    }
    else if (words != "") {
      editor = apply_edit(editor, dr.insert_tx(editor.doc, dr.new_text(editor.doc, target.point, words, ds.style)))
      ds = {*: ds, tool: 'select', picked: [{index: len(editor.doc.content) - 1, box: null}]}
      files.sync_window(req.node, session, editor.doc)^
    }
  }
  else if (action == "link_ok") {
    let next = insert_dialog_link(editor, req.node)
    dialog = null
    if ((next != editor)^) {
      editor = next
      edited = true
      files.sync_window(req.node, session, editor.doc)^
    }
  }
  else if (action == "image_ok") {
    let src = trim(files.field_value(req.node, "edit-dialog-src"))
    let alt = trim(files.field_value(req.node, "edit-dialog-alt"))
    dialog = null
    if (src != "") {
      let run = run_request(editor, "insertImage", {src: src, alt: alt})
      editor = run.editor
      edited = true
      files.sync_window(req.node, session, editor.doc)^
    }
  }
  if (dialog == null) {
    files.focus_surface(req.node)^
    // an insert completed through the surface already; otherwise put the
    // caret back where the model has it (proposal §5 selection bookmark)
    if (not edited) { controller.restore(editor) }
  }
}
on keydown(evt) {
  if ((dialog != null and dialog.kind == 'link' and evt.key == "Enter" and
       dom.matches(evt.target, "#edit-dialog-href"))^) {
    editor = insert_dialog_link(editor, evt.target)
    dialog = null
    files.sync_window(evt.target, session, editor.doc)^
    files.focus_surface(evt.target)^
    controller.restore(editor)^
    return 'prevent-default'
  }
  if ((evt.key == "Escape" and table_menu != null)^) {
    table_menu = null
    controller.restore(editor)^
    controller.focus(evt.target)^
    return 'prevent-default'
  }
  let format_command = keyboard_format_command(evt)
  if ((dialog == null and not is_drawing(session) and format_command != null)^) {
    let host = controller.surface(evt.target)
    if ((host != null and dom.contains(host, evt.target))^) {
      let selected = controller.adopt(editor, evt, session.format.schema_preset)
      editor = selected.editor
      let item = tools.find_item(format_groups(session), format_command)
      let problem = command_problem(session, editor, item)^
      if (problem != null) { status = problem }
      else if ((item.dialog != null)^) {
        dialog = {kind: item.dialog}
        // Release the editing host so the new dialog field can autofocus.
        dom.clear_editing_focus(host)
      }
      else {
        let result = apply_format_item(editor, item)
        editor = result.editor
        status = result.status
        files.sync_window(evt.target, session, editor.doc)^
        controller.restore(editor)^
      }
      return 'prevent-default'
    }
  }
  if ((dialog == null and session.format.align_paragraphs != null and primary_key(evt) and evt.shiftKey == true and
       lower(string(evt.key)) in ["l", "e", "r", "j"])^) {
    let host = controller.surface(evt.target)
    if ((host != null and dom.contains(host, evt.target))^) {
      let selected = controller.adopt(editor, evt, session.format.schema_preset)
      let key = lower(string(evt.key))
      let align = if (key == "l") "left" else if (key == "e") "center" else if (key == "r") "right" else "justify"
      let tx = session.format.align_paragraphs(selected.editor, align)^
      if (tx != null) {
        let problem = session.format.check_roundtrip(tx.doc_after, session.envelope)
        if ((problem != null)^) { status = problem }
        else {
          editor = apply_edit(selected.editor, tx)^
          status = ""
          files.sync_window(evt.target, session, editor.doc)^
          controller.restore(editor)^
        }
      }
      return 'prevent-default'
    }
  }
  if ((dialog == null and not is_drawing(session) and
       (evt.key == "ContextMenu" or (evt.key == "F10" and evt.shiftKey == true)))^) {
    let selected = controller.adopt(editor, evt, session.format.schema_preset)
    let current = selected.editor
    if ((edit_inside(current, 'td') or edit_inside(current, 'th'))^) {
      let selection = dom.document_selection(dom.root_node(evt.target))
      let boundary = if (selection == null) null else dom.selection_boundaries(selection)
      let anchor = if (boundary == null) null else boundary.anchor_node
      let target = if (anchor == null or dom.node_type(anchor) == 1) anchor else dom.parent_node(anchor)
      let cell = if (target == null) null else dom.closest(target, "td,th")
      let box = if (cell == null) dom.bounding_box(evt.target) else dom.bounding_box(cell)
      editor = current
      table_menu = {x: box.x, y: box.y + box.height, keyboard: true}
      return 'prevent-default'
    }
  }
  if ((primary_key(evt) and key_is(evt, "s"))^) {
    if (evt.shiftKey == true) { dialog = {kind: 'save_as', path: session.path} }
    else {
      let saved = files.save_now(session, editor.doc, evt.target)
      session = saved.session
      status = saved.status
      if ((saved.dialog != null)^) { dialog = saved.dialog }
    }
    return 'prevent-default'
  }
  if ((evt.key == "Escape" and dialog != null)^) {
    dialog = null
    after_save = null
    return 'prevent-default'
  }
  // Cmd/Ctrl+/ switches to the source view (OQ7)
  if ((primary_key(evt) and evt.key == "/" and dialog == null)^) {
    emit("edit_switch", source_view(session, editor)^)
    return 'prevent-default'
  }
  if ((not is_drawing(session) or dialog != null)^) { return 'pass' }
  let picked = dr.picked_indices(ds)
  let nudge = if (evt.shiftKey == true) 10.0 else 1.0
  let dx = if (evt.key == "ArrowLeft") 0.0 - nudge else if (evt.key == "ArrowRight") nudge else 0.0
  let dy = if (evt.key == "ArrowUp") 0.0 - nudge else if (evt.key == "ArrowDown") nudge else 0.0
  if (evt.key == "Escape") {
    // Escape cancels a gesture, then clears the selection, then the tool
    if (ds.gesture != null) { ds = {*: ds, gesture: null} }
    else if (len(picked) > 0) { ds = {*: ds, picked: []} }
    else { ds = {*: ds, tool: 'select'} }
  }
  else if ((evt.key == "Delete" or evt.key == "Backspace") and len(picked) > 0) {
    editor = apply_edit(editor, dr.delete_tx(editor.doc, picked))
    ds = {*: ds, picked: []}
  }
  else if ((primary_key(evt) and key_is(evt, "z"))^) {
    editor = run_request(editor, if (evt.shiftKey == true) "historyRedo" else "historyUndo", {}).editor
    ds = {*: ds, picked: [], gesture: null}
  }
  else if ((primary_key(evt) and key_is(evt, "d") and len(picked) > 0)^) {
    let copies = dr.duplicate_tx(editor.doc, picked)
    editor = apply_edit(editor, copies.tx)
    ds = ({*: ds, picked: [for (j, pick of ((copies.picked) ^ { [] })) {index: pick, box: shifted(ds.picked[j].box, 10.0, 10.0)}]})^
  }
  else if ((dx != 0.0 or dy != 0.0) and len(picked) > 0) {
    editor = apply_edit(editor, dr.move_tx(editor.doc, picked, dx, dy))
    ds = {*: ds, picked: [for (q in ds.picked) {*: q, box: shifted(q.box, dx, dy)}]}
  }
  else if ((not primary_key(evt) and member(["v", "r", "e", "l", "t"], lower(string(evt.key))))^) {
    let key = lower(string(evt.key))
    ds = {*: ds, gesture: null,
          tool: if (key == "v") 'select' else if (key == "r") 'rect' else if (key == "e") 'ellipse'
                else if (key == "l") 'line' else 'text'}
  }
  else { return 'pass' }
  files.sync_window(evt.target, session, editor.doc)^
  'prevent-default'
}
// A gesture runs from press to release (proposal §6): the press picks the
// object, handle, or start point; the release commits one transaction; Escape
// in between cancels. Without pointer moves the surface shows no preview.
on mousedown(evt) {
  if ((not is_drawing(session) and evt.source_pos != null)^) {
    let rich_surface = dom.get_element_by_id(doc_root(evt.target), "edit-surface")
    if (rich_surface != null and dom.contains(rich_surface, evt.target)) {
      let part = view_only_part(session, editor.doc, evt.source_pos.path)
      if ((part != null)^) {
        // Native caret placement skips false islands; select their model node.
        editor = edit_set_selection(editor, node_selection(part))
        controller.restore(editor)^
      }
    }
  }
  if ((not is_drawing(session) or dialog != null)^) { return 'pass' }
  let surface = dom.get_element_by_id(doc_root(evt.target), "edit-surface")
  if (surface == null or not dom.contains(surface, evt.target)) { return 'pass' }
  let canvas = canvas_node(evt.target)
  let p = user_point(canvas, evt.x, evt.y, editor.doc)
  let handle = element_attr(evt.target, "data-edit-handle")
  let hit = object_hit(evt.target)
  if ((ds.tool == 'select' and handle != null and len(ds.picked) == 1)^) {
    ds = {*: ds, gesture: {kind: 'resize', handle: handle, start: p, box: ds.picked[0].box}}
  }
  else if ((ds.tool == 'select' and hit != null)^) {
    let entry = {index: hit.index, box: frame_of(canvas, hit, editor.doc)}
    let already = member(dr.picked_indices(ds), hit.index)
    let picked = if (evt.shiftKey == true and already) [for (q in ds.picked where (q.index != hit.index)^) q]
                 else if (evt.shiftKey == true) [*ds.picked, entry]
                 else if (already) ds.picked
                 else [entry]
    ds = {*: ds, picked: picked, gesture: {kind: 'move', start: p}}
  }
  else if (ds.tool == 'select') { ds = {*: ds, picked: [], gesture: null} }
  else if ((ds.tool == 'text' and hit != null and dr.text_editable(editor.doc.content[hit.index]))^) {
    dialog = text_dialog_for(editor.doc, hit.index)
  }
  else if (ds.tool == 'text') { dialog = {kind: 'words', point: p, text: ""} }
  else { ds = {*: ds, gesture: {kind: 'create', start: p}} }
  files.focus_surface(evt.target)^
  'prevent-default'
}
on mouseup(evt) {
  if ((not is_drawing(session) or ds.gesture == null)^) { return 'pass' }
  let g = ds.gesture
  let p = user_point(canvas_node(evt.target), evt.x, evt.y, editor.doc)
  let dx = p.x - g.start.x
  let dy = p.y - g.start.y
  let moved = abs(dx) >= 0.5 or abs(dy) >= 0.5
  ds = {*: ds, gesture: null}
  if (g.kind == 'move' and moved) {
    editor = apply_edit(editor, dr.move_tx(editor.doc, dr.picked_indices(ds), dx, dy))
    ds = {*: ds, picked: [for (q in ds.picked) {*: q, box: shifted(q.box, dx, dy)}]}
  }
  else if (g.kind == 'resize' and moved and g.box != null) {
    let index = ds.picked[0].index
    let b = dr.resized_box(g.box, g.handle, dx, dy)
    editor = apply_edit(editor, dr.resize_tx(editor.doc, index, b))
    ds = {*: ds, picked: [{index: index, box: dr.frame_box(editor.doc.content[index], b)}]}
  }
  else if (g.kind == 'create') {
    let shape = dr.new_shape(editor.doc, ds.tool, g.start, p, ds.style)
    editor = apply_edit(editor, dr.insert_tx(editor.doc, shape))
    ds = {*: ds, tool: 'select', picked: [{index: len(editor.doc.content) - 1, box: dr.frame_box(shape, null)}]}
  }
  files.sync_window(evt.target, session, editor.doc)^
  'prevent-default'
}
on dblclick(evt) {
  if ((not is_drawing(session) or dialog != null)^) { return 'pass' }
  let hit = object_hit(evt.target)
  if ((hit == null or not dr.text_editable(editor.doc.content[hit.index]))^) { return 'pass' }
  dialog = text_dialog_for(editor.doc, hit.index)
  'prevent-default'
}
on closerequest(evt) {
  // the host sends this only while the close guard is armed, but a request
  // that races a save finds the session clean and simply closes
  if ((not sess.is_dirty(session, editor.doc))^) {
    dom.request_window_close(evt.target)
    return
  }
  dialog = {kind: 'close'}
}
