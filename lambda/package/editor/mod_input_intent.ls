import .mod_commands
import .mod_decorations
import .mod_history
import .mod_source_pos
import .mod_transaction

fn composition_active(state) => state.composition != null and state.composition.active
fn composition_selection(state) =>
  if (composition_active(state)) state.composition.range else state.selection

fn composition_base_selection(state) =>
  if (composition_active(state)) state.composition.base_selection else state.selection

fn composition_base_doc(state) =>
  if (composition_active(state) and state.composition.base_doc != null) state.composition.base_doc else state.doc

fn composition_state(state) =>
  {doc: state.doc, selection: composition_selection(state)}

fn composition_range(sel_before, tx) =>
  text_selection(pos_min(sel_before.anchor, sel_before.head), tx.sel_after.anchor)

fn mark_composition_tx(tx, comp, add_history) =>
  tx_set_meta(tx_set_meta(tx, "composition", comp), "addToHistory", add_history)

fn state_decorations_after(state, tx) =>
  if (state.decorations == null) { null } else { deco_map_tx(state.decorations, tx) }

fn tx_adds_history(tx) => tx_get_meta(tx, "addToHistory") != false

fn mark_typing_history(tx) => tx_set_meta(tx, "historyGroup", "typing")
fn mark_scroll_into_view(tx) =>
  if (tx == null) { null } else { tx_set_meta(tx, "scrollIntoView", true) }

fn state_history_after(state, tx) {
  let hist_meta = tx_get_meta(tx, "history")
  if (hist_meta != null) { hist_meta }
  else if (state.history == null) { null }
  else if (tx_adds_history(tx)) { history_push(state.history, tx) }
  else { state.history }
}

fn state_stored_marks_after(state, tx) {
  let stored = tx_get_meta(tx, "storedMarks")
  if (stored != null) { stored } else { state.stored_marks }
}

pub fn state_after_intent(state, tx) =>
  if (tx == null) state
  else {doc: tx.doc_after, selection: tx.sel_after,
        composition: tx_get_meta(tx, "composition"), decorations: state_decorations_after(state, tx),
        history: state_history_after(state, tx), stored_marks: state_stored_marks_after(state, tx)}

fn history_result_tx(state, r) {
  if (not r.ok) { null }
  else {
    {doc_before: state.doc, doc_after: r.doc, steps: [],
     sel_before: state.selection, sel_after: r.sel,
     meta: [{name: "history", value: r.hist}, {name: "addToHistory", value: false}]}
  }
}

fn dispatch_history_intent(state, ev) {
  if (state.history == null) { null }
  else if (ev.input_type == "historyUndo") { history_result_tx(state, history_undo(state.history, state.doc)) }
  else if (ev.input_type == "historyRedo") { history_result_tx(state, history_redo(state.history, state.doc)) }
  else { null }
}

pub fn dispatch_composition_intent(state, ev) {
  if (ev.input_type == "compositionStart") {
    mark_composition_tx(tx_begin(state.doc, state.selection),
      {active: true, base_doc: state.doc, base_selection: state.selection, range: state.selection}, false)
  } else if (ev.input_type == "insertCompositionText") {
    let edit_state = composition_state(state)
    let tx = cmd_insert_text(edit_state, ev.data)
    if (tx == null) { null }
    else {
      let comp = {active: true, base_selection: composition_base_selection(state),
                  base_doc: composition_base_doc(state),
                  range: composition_range(edit_state.selection, tx)}
      mark_composition_tx(tx, comp, false)
    }
  } else if (ev.input_type == "insertFromComposition") {
    let edit_state = {doc: composition_base_doc(state), selection: composition_base_selection(state),
                      stored_marks: state.stored_marks, schema: state.schema}
    let tx = cmd_insert_text(edit_state, ev.data)
    if (tx == null) { null } else { mark_composition_tx(tx, null, true) }
  } else if (ev.input_type == "deleteCompositionText") {
    if (not composition_active(state)) { null }
    else {
      let tx = cmd_insert_text(composition_state(state), "")
      if (tx == null) { null } else { mark_composition_tx(tx, null, false) }
    }
  } else { null }
}

fn dispatch_intent_raw(state, ev) =>
  if (ev.input_type == "insertText") {
    let tx = cmd_insert_text(state, ev.data)
    if (tx == null) { null } else { mark_typing_history(tx) }
  }
  else if (ev.input_type == "insertFromPaste" and ev.mime == "text/html") cmd_paste_html(state, ev.html, ev.data)
  else if (ev.input_type == "insertFromPaste") cmd_paste_text(state, ev.data)
  else if (ev.input_type == "insertImage") cmd_insert_image(state, ev.src, ev.alt)
  else if (ev.input_type == "insertLink") cmd_insert_link(state, ev.href, ev.title, ev.label)
  else if (ev.input_type == "insertHorizontalRule") cmd_insert_horizontal_rule(state)
  else if (ev.input_type == "insertCodeBlock") cmd_insert_code_block(state, ev.data)
  else if (ev.input_type == "formatBlockquote") cmd_wrap_blockquote(state)
  else if (ev.input_type == "formatLiftBlockquote") cmd_lift_blockquote(state)
  else if (ev.input_type == "insertTable") cmd_insert_table(state, ev.rows, ev.cols, ev.header)
  else if (ev.input_type == "insertTableRow") cmd_add_table_row(state)
  else if (ev.input_type == "deleteTableRow") cmd_delete_table_row(state)
  else if (ev.input_type == "insertTableColumn") cmd_add_table_column(state)
  else if (ev.input_type == "deleteTableColumn") cmd_delete_table_column(state)
  else if (ev.input_type == "insertFromDrop" and ev.source_path != null) cmd_move_node(state, ev.source_path, ev.target_parent_path, ev.target_index)
  else if (ev.input_type == "insertFromDrop" and ev.slice != null) cmd_insert_at(state, ev.target_parent_path, ev.target_index, ev.slice)
  else if (ev.input_type == "compositionStart") dispatch_composition_intent(state, ev)
  else if (ev.input_type == "insertCompositionText") dispatch_composition_intent(state, ev)
  else if (ev.input_type == "insertFromComposition") dispatch_composition_intent(state, ev)
  else if (ev.input_type == "deleteCompositionText") dispatch_composition_intent(state, ev)
  else if (ev.input_type == "insertParagraph") cmd_split_block(state)
  else if (ev.input_type == "insertLineBreak") cmd_insert_line_break(state)
  else if (ev.input_type == "deleteContentBackward") cmd_delete_backward(state)
  else if (ev.input_type == "deleteContentForward") cmd_delete_forward(state)
  else if (ev.input_type == "deleteWordBackward") cmd_delete_word_backward(state)
  else if (ev.input_type == "formatBold") cmd_format_bold(state)
  else if (ev.input_type == "formatItalic") cmd_format_italic(state)
  else if (ev.input_type == "formatUnderline") cmd_format_underline(state)
  else if (ev.input_type == "formatIndent") cmd_indent_list_item(state)
  else if (ev.input_type == "formatOutdent") cmd_outdent_list_item(state)
  else if (ev.input_type == "selectAll") cmd_select_all(state)
  else if (ev.input_type == "historyUndo") dispatch_history_intent(state, ev)
  else if (ev.input_type == "historyRedo") dispatch_history_intent(state, ev)
  else null

pub fn dispatch_intent(state, ev) => mark_scroll_into_view(dispatch_intent_raw(state, ev))
