// Adapter from the shared DOM editing protocol to immutable editor state.
// Native DOM coordinates are validated and projected before reaching here;
// this module owns source-coordinate normalization and model command routing.
import .mod_doc
import .mod_source_pos
import input_intent: lambda.editor.mod_input_intent
import transaction: lambda.editor.mod_transaction
import model_registry: lambda.editor.mod_edit_registry
import request: lambda.dom.edit_request
import action: lambda.dom.edit_action
import result: lambda.dom.edit_result

fn valid_source_pos(doc, source_pos) {
  if (source_pos == null) false
  else {
    let source_node = node_at(doc, source_pos.path)
    if (source_node == null) false
    else {
      let max_offset = if (is_text(source_node)) len(source_node.text)
                       else if (is_node(source_node)) len(source_node.content)
                       else -1
      max_offset >= 0 and source_pos.offset >= 0 and source_pos.offset <= max_offset
    }
  }
}

pub fn normalize_source_pos(doc, source_pos) {
  if (source_pos == null) null
  else if (valid_source_pos(doc, source_pos)) source_pos
  else if (node_at(doc, source_pos.path) != null and is_text(node_at(doc, source_pos.path))) {
    let leaf = node_at(doc, source_pos.path)
    pos(source_pos.path, if (source_pos.offset < 0) 0 else len(leaf.text))
  }
  else if (len(source_pos.path) == 0) null
  else normalize_source_pos(doc, pos(parent_path(source_pos.path), source_pos.offset))
}

pub fn normalize_source_selection(doc, selection) {
  if (selection == null) null
  else if (selection.kind == 'text') {
    let anchor = normalize_source_pos(doc, selection.anchor)
    let head = normalize_source_pos(doc, selection.head)
    if (anchor == null or head == null) null else text_selection(anchor, head)
  }
  else if (selection.kind == 'node' and node_at(doc, selection.path) != null) selection
  else if (selection.kind == 'multi-node' and
           every([for (path in selection.paths) node_at(doc, path) != null])) selection
  else if (selection.kind == 'all') selection
  else null
}

fn selection_equal(left, right) {
  if (left == null or right == null) left == right
  else if (left.kind != right.kind) false
  else if (left.kind == 'text') pos_equal(left.anchor, right.anchor) and pos_equal(left.head, right.head)
  else if (left.kind == 'node') path_equal(left.path, right.path)
  else left == right
}

fn event_selection(editor, evt) {
  let projected = normalize_source_selection(editor.doc, evt.source_selection)
  if (projected != null) projected
  else {
    let projected_pos = normalize_source_pos(editor.doc, evt.source_pos)
    if (projected_pos == null) editor.selection else text_selection(projected_pos, projected_pos)
  }
}

fn target_selection(editor, snapshot) {
  if (snapshot == null) editor.selection
  else if (snapshot.source_target_ranges != null and len(snapshot.source_target_ranges) > 0)
    normalize_source_selection(editor.doc, snapshot.source_target_ranges[0])
  else event_selection(editor, snapshot)
}

fn with_selection(editor, selection, revision, preserve_marks) =>
  { *: editor, selection: selection,
    stored_marks: if (preserve_marks) editor.stored_marks else null,
    native_selection_revision: revision,
    events: [*editor.events, {kind: 'selection', selection: selection}] }

pub fn accept_dom_selection(editor, evt) {
  let selection = event_selection(editor, evt)
  let revision = if (type(evt.selection_revision) == int and
                     evt.selection_revision >= 0) evt.selection_revision
                 else editor.native_selection_revision + 1
  let stale = revision <= editor.native_selection_revision
  let own_commit = evt.selection_origin == "model-commit" and
                   type(evt.model_revision) == int and
                   evt.model_revision == editor.model_revision
  if (selection == null or stale)
    {editor: editor, changed: false}
  else if (selection_equal(selection, editor.selection) or own_commit)
    {editor: {*: editor, native_selection_revision: revision}, changed: false}
  else {editor: with_selection(editor, selection, revision, false), changed: true}
}

pub fn descriptor_for_intent(input_type) =>
  model_registry.descriptor_for_intent(input_type)

fn history_recorded(tx, changed) =>
  changed and transaction.tx_get_meta(tx, "addToHistory") != false

pub fn apply_transaction(editor, tx) {
  if (tx == null) editor
  else {
    let tx2 = transaction.tx_set_meta(tx, "scrollIntoView", true)
    let next = input_intent.state_after_intent(editor, tx2)
    let changed = next.doc != editor.doc
    { *: editor, doc: next.doc, selection: next.selection,
      history: next.history, decorations: next.decorations,
      composition: next.composition, stored_marks: next.stored_marks,
      model_revision: editor.model_revision + if (changed) 1 else 0,
      events: [*editor.events, {kind: 'change', transaction: tx2},
               {kind: 'selection', selection: next.selection}] }
  }
}

fn execute(editor, evt, descriptor, snapshot) {
  if (descriptor == null or descriptor.model == null)
    {editor: editor, transaction: null,
     result: result.decline(false, false, "unsupported", 0), action: null, request: null}
  else {
    let selected = target_selection(editor, snapshot)
    let combines_drag_move = descriptor.family == "drop" and evt.drag_move == true
    let source_selection = normalize_source_selection(editor.doc, evt.source_selection)
    let drop_pos = if (combines_drag_move and selected != null and selected.kind == 'text') selected.anchor
                   else normalize_source_pos(editor.doc, evt.target_pos)
    let target_editor = if (combines_drag_move) editor
                        else if (selected == null or selection_equal(selected, editor.selection)) editor
                        else with_selection(editor, selected, editor.native_selection_revision, true)
    let edit_request = request.from_event(evt, descriptor)
    let edit_action = action.lower_request(edit_request, descriptor)
    let execution_event = { *: evt, input_type: edit_request.input_type,
                            data: edit_request.data, html: edit_request.html,
                            mime: edit_request.mime,
                            drag_move: combines_drag_move,
                            source_selection: source_selection,
                            target_pos: if (drop_pos != null) drop_pos else evt.target_pos }
    let tx = if (combines_drag_move)
             input_intent.dispatch_extension(target_editor, execution_event,
                                             "move_text_selection")
             else input_intent.dispatch_descriptor(
                    target_editor, execution_event, descriptor)
    if (tx == null) {
      let drag_source_collapsed = combines_drag_move and source_selection != null and
          source_selection.kind == 'text' and
          pos_equal(source_selection.anchor, source_selection.head)
      let drag_target_inside = combines_drag_move and source_selection != null and
          source_selection.kind == 'text' and drop_pos != null and
          pos_compare(sel_lo(source_selection), drop_pos) < 1 and
          pos_compare(drop_pos, sel_hi(source_selection)) < 1
      let drag_source_node = if (combines_drag_move and source_selection != null)
                             node_at(editor.doc, source_selection.anchor.path) else null
      let drag_target_node = if (combines_drag_move and drop_pos != null)
                             node_at(editor.doc, drop_pos.path) else null
      let failure = if (combines_drag_move and source_selection == null) "drag-source-missing"
                    else if (combines_drag_move and drop_pos == null) "drag-target-missing"
                    else if (drag_source_collapsed) "drag-source-collapsed"
                    else if (drag_target_inside) "drag-target-inside-source"
                    else if (combines_drag_move and not is_text(drag_source_node)) "drag-source-not-text"
                    else if (combines_drag_move and not is_text(drag_target_node)) "drag-target-not-text"
                    else "disabled"
      {editor: editor, transaction: null,
       result: result.decline(true, false, failure, 0),
       action: edit_action, request: edit_request}
    } else {
      let next_editor = apply_transaction(target_editor, tx)
      let changed = next_editor.doc != editor.doc
      let selection_changed = not selection_equal(next_editor.selection, editor.selection)
      let edit_result = result.model_applied(changed, selection_changed,
          history_recorded(tx, changed), descriptor.history_class,
          next_editor.selection, next_editor.model_revision)
      {editor: next_editor, transaction: tx, result: edit_result,
       action: edit_action, request: edit_request}
    }
  }
}

pub fn handle_event(editor, evt) =>
  execute(editor, evt, descriptor_for_intent(evt.input_type), evt)

pub fn handle_request(editor, edit_request) =>
  execute(editor, edit_request, descriptor_for_intent(edit_request.input_type), null)

pub fn transaction_for_event(editor, evt) => handle_event(editor, evt).transaction

pub fn transaction_for_command(editor, command) {
  let evt = { *: command, origin: "toolbar", data: command.data,
              edit_plaintext_only: false }
  handle_event(editor, evt).transaction
}
