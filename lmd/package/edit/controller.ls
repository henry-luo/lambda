// shared model-editor lifecycle. State stays in the calling template (S12.1.3);
// selection and completion policy stays above Radiant's DOM mechanisms (D7.2.5).
import dom
import lambda.editor.mod_editor
import lambda.editor.mod_step
import image: lambda.editor.mod_image
import html: .html
import result: lambda.dom.edit_result

// DOM reads retain fn colour even when their receiver is mutable (D7.4.6).
pub fn surface(node, id = "edit-surface") {
    if (node == null) null else dom.get_element_by_id(dom.root_node(node), id)
}

pub fn is_control(node) {
    let target = if (node == null or dom.node_type(node) == 1) node else dom.parent_node(node)
    target != null and dom.closest(target, "input,textarea,select") != null
}

pub pn mount(editor, node, preset = "html5_subset", id = "edit-surface") {
    if (editor.mounted or preset == null) { return editor }
    let host = surface(node, id)
    if (host == null) editor else edit_mount(editor, host, preset)
}

// toolbar hit positions name the toolbar; only its document-selection snapshot
// may target the model. A control's document snapshot needs an ownership proof;
// its private caret and hit position never replace the document bookmark.
pub fn selection_snapshot(evt, from_surface, from_control, document_selection = null) map | null {
    let selection = if (document_selection == false or (from_control and document_selection != true)) null
                    else evt.source_selection
    let position = if (from_control or not from_surface) null else evt.source_pos
    if (selection == null and position == null) null else {
        source_selection: selection, source_pos: position,
        selection_revision: evt.selection_revision, selection_origin: evt.selection_origin,
        model_revision: evt.model_revision
    }
}

pub fn accept_selection(editor, snapshot) =>
    if (snapshot == null) {editor: editor, changed: false}
    else edit_accept_dom_selection(editor, snapshot)

fn owns_selection(host, node) {
    let selection = if (host == null) null else dom.document_selection(dom.root_node(node))
    if (selection == null) false else {
        let boundary = dom.selection_boundaries(selection)
        boundary.anchor_node != null and boundary.focus_node != null and
            dom.contains(host,boundary.anchor_node) and dom.contains(host,boundary.focus_node)
    }
}

pub pn adopt(editor, evt, preset = "html5_subset", id = "edit-surface") {
    let host = surface(evt.target, id)
    let snapshot = selection_snapshot(evt,
        host != null and dom.contains(host, evt.target), is_control(evt.target),
        evt.source_selection != null and owns_selection(host,evt.target))
    if (snapshot == null) { return {editor: editor, changed: false} }
    let bound = mount(editor, evt.target, preset, id)
    let accepted = accept_selection(bound, snapshot)
    // completion echoes can acknowledge a native revision without moving the
    // logical selection. Keep that revision even when no redraw is requested.
    {*: accepted, changed: accepted.changed or bound.mounted != editor.mounted}
}

// document replacement reuses the logical surface and advances its generation;
// pending completions must resolve the replacement DOM, never the retired host.
pub fn replace(previous, next) map => {
    *: next, mounted: previous.mounted, preset: previous.preset,
    surface_handle: previous.surface_handle, model_revision: previous.model_revision + 1,
    native_selection_revision: previous.native_selection_revision
}

pub pn restore(editor, enabled = true) {
    if (enabled and editor.mounted) edit_restore_selection(editor) else false
}

pub pn focus(node, id = "edit-surface") {
    let host = surface(node, id)
    if (host == null) false else dom.focus_set(host, false)
}

// an atomic selection can have no native text range; copy its model payload before Cut removes it.
pub fn clipboard_result(editor, input_type, outcome) {
    let selected = image.selected(editor)
    if (selected == null or not contains(["copy", "deleteByCut"], input_type) or
        not outcome.claimed or outcome.failure != null) outcome
    else result.with_clipboard_image(outcome, attrs_get(selected.node.attrs, 'src'),
        html.inline_html([{*:selected.node,tag:'img'}]), attrs_get(selected.node.attrs, 'alt') or "")
}

pub pn handle(editor, evt, read_only = false, preset = "html5_subset", id = "edit-surface", selection = null) {
    if (read_only) { return {editor: editor, result: result.decline(true, false, "read-only", 0)} }
    // copy must not redraw or create history; native still owns clipboard transport (D7.2.5).
    if (evt.input_type == "copy") {
        return {editor: editor, result: clipboard_result(editor, evt.input_type,
            result.model_applied(false, false, false, "", editor.selection, editor.model_revision))}
    }
    // keep the host event intact; an owned atomic selection is a separate model target.
    let target = if (selection != null) selection
        else if (evt.input_type == "deleteByCut" and image.selected(editor) != null) editor.selection else null
    let run = edit_handle_dom_action(mount(editor, evt.target, preset, id), evt, target)
    if (evt.input_type == "deleteByCut") {*:run, result:clipboard_result(editor, evt.input_type, run.result)} else run
}

pub pn request(editor, input_type, payload = {}) {
    let run = edit_handle_request(editor, edit_request_from_toolbar(input_type, payload))
    if (run.editor.surface_handle != null) dom.finish_model_edit(run.editor.surface_handle, run.result)
    run
}
