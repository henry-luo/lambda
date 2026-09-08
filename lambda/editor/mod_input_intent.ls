import .mod_commands
import .mod_decorations
import .mod_history
import .mod_source_pos
import .mod_transaction

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

fn dispatch_intent_raw(st, ev) =>
  if (ev.input_type == "insertText") {
    let af = if (ev.data == " ") { cmd_autoformat_list(st) } else { null }
    if (af != null) { af }
    else {
      let tx = cmd_insert_text(st, ev.data)
      if (tx == null) { null } else { mark_typing_history(tx) }
    }
  }
  else if (ev.input_type == "insertFromPaste" and ev.mime == "text/html") cmd_paste_html(st, ev.html, ev.data)
  else if (ev.input_type == "insertFromPaste") cmd_paste_text(st, ev.data)
  else if (ev.input_type == "insertImage") cmd_insert_image(st, ev.src, ev.alt)
  else if (ev.input_type == "insertLink") cmd_insert_link(st, ev.href, ev.title, ev.label)
  else if (ev.input_type == "insertHorizontalRule") cmd_insert_horizontal_rule(st)
  else if (ev.input_type == "insertCodeBlock") cmd_insert_code_block(st, ev.data)
  else if (ev.input_type == "formatBlockquote") cmd_wrap_blockquote(st)
  else if (ev.input_type == "formatLiftBlockquote") cmd_lift_blockquote(st)
  else if (ev.input_type == "insertTable") cmd_insert_table(st, ev.rows, ev.cols, ev.header)
  else if (ev.input_type == "insertTableRow") cmd_add_table_row(st)
  else if (ev.input_type == "deleteTableRow") cmd_delete_table_row(st)
  else if (ev.input_type == "insertTableColumn") cmd_add_table_column(st)
  else if (ev.input_type == "deleteTableColumn") cmd_delete_table_column(st)
  else if (ev.input_type == "insertFromDrop" and ev.source_path != null) cmd_move_node(st, ev.source_path, ev.target_parent_path, ev.target_index)
  else if (ev.input_type == "insertFromDrop" and ev.slice != null) cmd_insert_at(st, ev.target_parent_path, ev.target_index, ev.slice)
  else if (ev.input_type == "compositionStart") dispatch_composition_intent(st, ev)
  else if (ev.input_type == "insertCompositionText") dispatch_composition_intent(st, ev)
  else if (ev.input_type == "insertFromComposition") dispatch_composition_intent(st, ev)
  else if (ev.input_type == "deleteCompositionText") dispatch_composition_intent(st, ev)
  else if (ev.input_type == "insertParagraph") cmd_insert_paragraph(st)
  else if (ev.input_type == "insertLineBreak") cmd_insert_line_break(st)
  else if (ev.input_type == "deleteContentBackward") cmd_delete_backward(st)
  else if (ev.input_type == "deleteContentForward") cmd_delete_forward(st)
  else if (ev.input_type == "deleteWordBackward") cmd_delete_word_backward(st)
  else if (ev.input_type == "deleteByCut") cmd_delete_forward(st)
  else if (ev.input_type == "formatBold") cmd_format_bold(st)
  else if (ev.input_type == "formatItalic") cmd_format_italic(st)
  else if (ev.input_type == "formatUnderline") cmd_format_underline(st)
  else if (ev.input_type == "formatIndent") cmd_indent_list_item(st)
  else if (ev.input_type == "formatOutdent") cmd_outdent_list_item(st)
  else if (ev.input_type == "selectAll") cmd_select_all(st)
  else if (ev.input_type == "historyUndo") dispatch_history_intent(st, ev)
  else if (ev.input_type == "historyRedo") dispatch_history_intent(st, ev)
  else null

pub fn dispatch_intent(st, ev) => mark_scroll_into_view(dispatch_intent_raw(st, ev))
