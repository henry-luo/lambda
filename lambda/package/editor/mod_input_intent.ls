import .mod_commands
import .mod_decorations
import .mod_source_pos
import .mod_transaction

fn composition_active(state) => state.composition != null and state.composition.active
fn composition_selection(state) =>
  if (composition_active(state)) state.composition.range else state.selection

fn composition_base_selection(state) =>
  if (composition_active(state)) state.composition.base_selection else state.selection

fn composition_state(state) =>
  {doc: state.doc, selection: composition_selection(state)}

fn composition_range(sel_before, tx) =>
  text_selection(pos_min(sel_before.anchor, sel_before.head), tx.sel_after.anchor)

fn mark_composition_tx(tx, comp, add_history) =>
  tx_set_meta(tx_set_meta(tx, "composition", comp), "addToHistory", add_history)

fn state_decorations_after(state, tx) =>
  if (state.decorations == null) { null } else { deco_map_tx(state.decorations, tx) }

pub fn state_after_intent(state, tx) =>
  if (tx == null) state
  else {doc: tx.doc_after, selection: tx.sel_after,
        composition: tx_get_meta(tx, "composition"), decorations: state_decorations_after(state, tx)}

pub fn dispatch_composition_intent(state, ev) {
  if (ev.input_type == "compositionStart") {
    mark_composition_tx(tx_begin(state.doc, state.selection),
      {active: true, base_selection: state.selection, range: state.selection}, false)
  } else if (ev.input_type == "insertCompositionText") {
    let edit_state = composition_state(state)
    let tx = cmd_insert_text(edit_state, ev.data)
    if (tx == null) { null }
    else {
      let comp = {active: true, base_selection: composition_base_selection(state),
                  range: composition_range(edit_state.selection, tx)}
      mark_composition_tx(tx, comp, false)
    }
  } else if (ev.input_type == "insertFromComposition") {
    let edit_state = composition_state(state)
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

pub fn dispatch_intent(state, ev) =>
  if (ev.input_type == "insertText") cmd_insert_text(state, ev.data)
  else if (ev.input_type == "insertFromPaste" and ev.mime == "text/html") cmd_paste_html(state, ev.html, ev.data)
  else if (ev.input_type == "insertFromPaste") cmd_paste_text(state, ev.data)
  else if (ev.input_type == "compositionStart") dispatch_composition_intent(state, ev)
  else if (ev.input_type == "insertCompositionText") dispatch_composition_intent(state, ev)
  else if (ev.input_type == "insertFromComposition") dispatch_composition_intent(state, ev)
  else if (ev.input_type == "deleteCompositionText") dispatch_composition_intent(state, ev)
  else if (ev.input_type == "insertParagraph") cmd_split_block(state)
  else if (ev.input_type == "deleteContentBackward") cmd_delete_backward(state)
  else if (ev.input_type == "deleteContentForward") cmd_delete_forward(state)
  else if (ev.input_type == "formatBold") cmd_format_bold(state)
  else if (ev.input_type == "formatItalic") cmd_format_italic(state)
  else if (ev.input_type == "formatUnderline") cmd_format_underline(state)
  else null
