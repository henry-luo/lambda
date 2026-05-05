// mod_editor.ls - public Lambda-side editor session facade.
//
// This module is a thin API layer over the lower-level command, transaction,
// history, and input-intent modules. It gives scripts a first-class editor
// value today while the native Radiant EditSession ABI catches up.

import .mod_commands
import .mod_decorations
import .mod_history
import .mod_input_intent
import .mod_md_schema
import .mod_source_pos
import .mod_transaction

pub editor_schemas = {
  markdown: markdown_schema,
  commonmark_strict: commonmark_strict_schema,
  html5_subset: html5_subset_schema
}

fn schema_or_default(schema) => if (schema == null) markdown_schema else schema
fn selection_or_start(sel) => if (sel == null) text_selection(pos([0, 0], 0), pos([0, 0], 0)) else sel

pub fn edit_open(doc, schema, selection) =>
  {kind: 'editor', doc: doc, schema: schema_or_default(schema), selection: selection_or_start(selection),
   history: history_new(), decorations: null, composition: null, stored_marks: null,
   events: [], mounted: false, preset: null}

pub fn edit_mount(editor, window, preset) =>
  {kind: 'editor', doc: editor.doc, schema: editor.schema, selection: editor.selection,
   history: editor.history, decorations: editor.decorations, composition: editor.composition,
   stored_marks: editor.stored_marks, events: [*editor.events, {kind: 'mount', window: window, preset: preset}],
   mounted: true, preset: preset}

pub fn edit_cmd_insert_text(text) => {name: 'insert_text', text: text}
pub fn edit_cmd_toggle_mark(mark) => {name: 'toggle_mark', mark: mark}
pub fn edit_cmd_split_block() => {name: 'split_block'}
pub fn edit_cmd_set_block_type(tag) => {name: 'set_block_type', tag: tag}
pub fn edit_cmd_history_undo() => {name: 'history_undo'}
pub fn edit_cmd_history_redo() => {name: 'history_redo'}

fn command_tx(editor, command) {
  if (command.name == 'insert_text') { cmd_insert_text(editor, command.text) }
  else if (command.name == 'toggle_mark') { cmd_toggle_mark(editor, command.mark) }
  else if (command.name == 'split_block') { cmd_split_block(editor) }
  else if (command.name == 'set_block_type') { cmd_set_block_type(editor, command.tag) }
  else if (command.name == 'history_undo') { dispatch_intent(editor, {input_type: "historyUndo"}) }
  else if (command.name == 'history_redo') { dispatch_intent(editor, {input_type: "historyRedo"}) }
  else { null }
}

fn editor_after_tx(editor, tx) {
  if (tx == null) { editor }
  else {
    let tx2 = tx_set_meta(tx, "scrollIntoView", true)
    let next = state_after_intent(editor, tx2)
    {kind: 'editor', doc: next.doc, schema: editor.schema, selection: next.selection,
     history: next.history, decorations: next.decorations, composition: next.composition,
     stored_marks: next.stored_marks,
     events: [*editor.events, {kind: 'change', transaction: tx2}, {kind: 'selection', selection: next.selection}],
     mounted: editor.mounted, preset: editor.preset}
  }
}

pub fn edit_exec(editor, command) => editor_after_tx(editor, command_tx(editor, command))
