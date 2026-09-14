import .mod_commands
import .mod_decorations
import .mod_history
import .mod_source_pos
import .mod_transaction
import edit_registry: lambda.editor.mod_edit_registry

fn composition_active(st) => st.composition != null and st.composition.active
fn composition_selection(st) =>
  if (composition_active(st)) st.composition.range else st.selection

fn composition_base_selection(st) =>
  if (composition_active(st)) st.composition.base_selection else st.selection

fn composition_base_doc(st) =>
  if (composition_active(st) and st.composition.base_doc != null) st.composition.base_doc else st.doc

fn composition_state(st) =>
  {doc: st.doc, selection: composition_selection(st)}

// Composition updates are derived from host-editor selection data; retain an
// invalid selection as an error rather than mapping it to a different range.
fn composition_range(sel_before, tx) map | error =>
  text_selection(pos_min(sel_before.anchor, sel_before.head), tx.sel_after.anchor)

fn mark_composition_tx(tx, comp, add_history) =>
  tx_set_meta(tx_set_meta(tx, "composition", comp), "addToHistory", add_history)

fn state_decorations_after(st, tx) =>
  if (st.decorations == null) { null } else { deco_map_tx(st.decorations, tx) }

fn tx_adds_history(tx) => tx_get_meta(tx, "addToHistory") != false

fn mark_typing_history(tx) => tx_set_meta(tx, "historyGroup", "typing")
fn mark_scroll_into_view(tx) =>
  if (tx == null) { null } else { tx_set_meta(tx, "scrollIntoView", true) }

fn state_history_after(st, tx) {
  let hist_meta = tx_get_meta(tx, "history")
  if (hist_meta != null) { hist_meta }
  else if (st.history == null) { null }
  else if (tx_adds_history(tx)) { history_push(st.history, tx) }
  else { st.history }
}

fn state_stored_marks_after(st, tx) {
  let stored = tx_get_meta(tx, "storedMarks")
  if (stored != null) { stored } else { st.stored_marks }
}

pub fn state_after_intent(st, tx) =>
  if (tx == null) st
  else {doc: tx.doc_after, selection: tx.sel_after,
        composition: tx_get_meta(tx, "composition"), decorations: state_decorations_after(st, tx),
        history: state_history_after(st, tx), stored_marks: state_stored_marks_after(st, tx)}

fn history_result_tx(st, r) {
  if (not r.ok) { null }
  else {
    {doc_before: st.doc, doc_after: r.doc, steps: [],
     sel_before: st.selection, sel_after: r.sel,
     meta: [{name: "history", value: r.hist}, {name: "addToHistory", value: false}]}
  }
}

fn dispatch_history_intent(st, ev) {
  if (st.history == null) { null }
  else if (ev.input_type == "historyUndo") { history_result_tx(st, history_undo(st.history, st.doc)) }
  else if (ev.input_type == "historyRedo") { history_result_tx(st, history_redo(st.history, st.doc)) }
  else { null }
}

pub fn dispatch_composition_intent(st, ev) {
  if (ev.input_type == "compositionStart") {
    mark_composition_tx(tx_begin(st.doc, st.selection),
      {active: true, base_doc: st.doc, base_selection: st.selection, range: st.selection}, false)
  } else if (ev.input_type == "insertCompositionText") {
    let edit_state = composition_state(st)
    let tx = cmd_insert_text(edit_state, ev.data)
    if (tx == null) { null }
    else {
      let comp = {active: true, base_selection: composition_base_selection(st),
                  base_doc: composition_base_doc(st),
                  range: composition_range(edit_state.selection, tx)}
      mark_composition_tx(tx, comp, false)
    }
  } else if (ev.input_type == "insertFromComposition") {
    let edit_state = {doc: composition_base_doc(st), selection: composition_base_selection(st),
                      stored_marks: st.stored_marks, schema: st.schema}
    let tx = cmd_insert_text(edit_state, ev.data)
    if (tx == null) { null } else { mark_composition_tx(tx, null, true) }
  } else if (ev.input_type == "deleteCompositionText") {
    if (not composition_active(st)) { null }
    else {
      let tx = cmd_insert_text(composition_state(st), "")
      if (tx == null) { null } else { mark_composition_tx(tx, null, false) }
    }
  } else { null }
}

fn dispatch_model_key(st, ev, key) =>
  if (key == "insert_text") {
    let af = if (ev.data == " ") { cmd_autoformat_list(st) } else { null }
    if (af != null) { af }
    else {
      let tx = cmd_insert_text(st, ev.data)
      if (tx == null) { null } else { mark_typing_history(tx) }
    }
  }
  else if (key == "paste" and ev.mime == "text/html") cmd_paste_html(st, ev.html, ev.data)
  else if (key == "paste") cmd_paste_text(st, ev.data)
  else if (key == "insert_image") cmd_insert_image(st, ev.src, ev.alt)
  else if (key == "insert_link") cmd_insert_link(st, ev.href, ev.title, ev.label)
  else if (key == "insert_horizontal_rule") cmd_insert_horizontal_rule(st)
  else if (key == "insert_code_block") cmd_insert_code_block(st, ev.data)
  else if (key == "wrap_blockquote") cmd_wrap_blockquote(st)
  else if (key == "lift_blockquote") cmd_lift_blockquote(st)
  else if (key == "insert_table") cmd_insert_table(st, ev.rows, ev.cols, ev.header)
  else if (key == "insert_table_row") cmd_add_table_row(st)
  else if (key == "delete_table_row") cmd_delete_table_row(st)
  else if (key == "insert_table_column") cmd_add_table_column(st)
  else if (key == "delete_table_column") cmd_delete_table_column(st)
  else if (key == "drop" and ev.drag_move == true) cmd_move_text_selection(st, ev.source_selection, ev.target_pos)
  else if (key == "drop" and ev.source_path != null) cmd_move_node(st, ev.source_path, ev.target_parent_path, ev.target_index)
  else if (key == "drop" and ev.slice != null) cmd_insert_at(st, ev.target_parent_path, ev.target_index, ev.slice)
  else if (key == "composition") dispatch_composition_intent(st, ev)
  else if (key == "insert_paragraph") cmd_insert_paragraph(st)
  else if (key == "insert_line_break") cmd_insert_line_break(st)
  else if (key == "delete_backward") cmd_delete_backward(st)
  else if (key == "delete_forward") cmd_delete_forward(st)
  else if (key == "delete_word_backward") cmd_delete_word_backward(st)
  else if (key == "format_bold") cmd_format_bold(st)
  else if (key == "format_italic") cmd_format_italic(st)
  else if (key == "format_underline") cmd_format_underline(st)
  else if (key == "toggle_mark") cmd_toggle_mark(st, ev.mark, ev.value)
  else if (key == "wrap_list") cmd_wrap_list(st,
      if (ev.kind != null) ev.kind else if (ev.input_type == "insertOrderedList") 'ordered' else 'bullet')
  else if (key == "indent_list_item") cmd_indent_list_item(st)
  else if (key == "outdent_list_item") cmd_outdent_list_item(st)
  else if (key == "set_block_type") cmd_set_block_type(st, ev.tag)
  else if (key == "delete_multi_node") cmd_delete_multi_node(st)
  else if (key == "move_text_selection") cmd_move_text_selection(st, ev.source_selection, ev.target_pos)
  else if (key == "select_all") cmd_select_all(st)
  else if (key == "history_undo" or key == "history_redo") dispatch_history_intent(st, ev)
  else null

pub fn dispatch_descriptor(st, ev, descriptor) {
  let key = if (descriptor == null or descriptor.model == null) null
            else descriptor.model.command_key;
  mark_scroll_into_view(dispatch_model_key(st, ev, key))
}

pub fn dispatch_extension(st, ev, key) =>
  mark_scroll_into_view(dispatch_model_key(st, ev, key))

pub fn dispatch_intent(st, ev) {
  let descriptor = edit_registry.descriptor_for_intent(ev.input_type);
  dispatch_descriptor(st, ev, descriptor)
}
