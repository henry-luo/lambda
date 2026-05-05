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

pub fn edit_set_selection(editor, selection) =>
  {kind: 'editor', doc: editor.doc, schema: editor.schema, selection: selection,
   history: editor.history, decorations: editor.decorations, composition: editor.composition,
   stored_marks: null, events: [*editor.events, {kind: 'selection', selection: selection}],
   mounted: editor.mounted, preset: editor.preset}

pub fn edit_cmd_insert_text(text) => {name: 'insert_text', text: text}
pub fn edit_cmd_paste_text(text) => {name: 'paste_text', text: text}
pub fn edit_cmd_paste_html(html, fallback_text) => {name: 'paste_html', html: html, fallback_text: fallback_text}
pub fn edit_cmd_insert_image(src, alt) => {name: 'insert_image', src: src, alt: alt}
pub fn edit_cmd_insert_link(href, title, label) => {name: 'insert_link', href: href, title: title, label: label}
pub fn edit_cmd_insert_horizontal_rule() => {name: 'insert_horizontal_rule'}
pub fn edit_cmd_insert_code_block(text) => {name: 'insert_code_block', text: text}
pub fn edit_cmd_wrap_blockquote() => {name: 'wrap_blockquote'}
pub fn edit_cmd_lift_blockquote() => {name: 'lift_blockquote'}
pub fn edit_cmd_insert_table(rows, cols, header) => {name: 'insert_table', rows: rows, cols: cols, header: header}
pub fn edit_cmd_add_table_row() => {name: 'add_table_row'}
pub fn edit_cmd_delete_table_row() => {name: 'delete_table_row'}
pub fn edit_cmd_add_table_column() => {name: 'add_table_column'}
pub fn edit_cmd_delete_table_column() => {name: 'delete_table_column'}
pub fn edit_cmd_delete_backward() => {name: 'delete_backward'}
pub fn edit_cmd_delete_forward() => {name: 'delete_forward'}
pub fn edit_cmd_insert_line_break() => {name: 'insert_line_break'}
pub fn edit_cmd_toggle_mark(mark) => {name: 'toggle_mark', mark: mark}
pub fn edit_cmd_indent_list_item() => {name: 'indent_list_item'}
pub fn edit_cmd_outdent_list_item() => {name: 'outdent_list_item'}
pub fn edit_cmd_split_block() => {name: 'split_block'}
pub fn edit_cmd_set_block_type(tag) => {name: 'set_block_type', tag: tag}
pub fn edit_cmd_history_undo() => {name: 'history_undo'}
pub fn edit_cmd_history_redo() => {name: 'history_redo'}

fn command_tx(editor, command) {
  if (command.name == 'insert_text') { cmd_insert_text(editor, command.text) }
  else if (command.name == 'paste_text') { cmd_paste_text(editor, command.text) }
  else if (command.name == 'paste_html') { cmd_paste_html(editor, command.html, command.fallback_text) }
  else if (command.name == 'insert_image') { cmd_insert_image(editor, command.src, command.alt) }
  else if (command.name == 'insert_link') { cmd_insert_link(editor, command.href, command.title, command.label) }
  else if (command.name == 'insert_horizontal_rule') { cmd_insert_horizontal_rule(editor) }
  else if (command.name == 'insert_code_block') { cmd_insert_code_block(editor, command.text) }
  else if (command.name == 'wrap_blockquote') { cmd_wrap_blockquote(editor) }
  else if (command.name == 'lift_blockquote') { cmd_lift_blockquote(editor) }
  else if (command.name == 'insert_table') { cmd_insert_table(editor, command.rows, command.cols, command.header) }
  else if (command.name == 'add_table_row') { cmd_add_table_row(editor) }
  else if (command.name == 'delete_table_row') { cmd_delete_table_row(editor) }
  else if (command.name == 'add_table_column') { cmd_add_table_column(editor) }
  else if (command.name == 'delete_table_column') { cmd_delete_table_column(editor) }
  else if (command.name == 'delete_backward') { cmd_delete_backward(editor) }
  else if (command.name == 'delete_forward') { cmd_delete_forward(editor) }
  else if (command.name == 'insert_line_break') { cmd_insert_line_break(editor) }
  else if (command.name == 'toggle_mark') { cmd_toggle_mark(editor, command.mark) }
  else if (command.name == 'indent_list_item') { cmd_indent_list_item(editor) }
  else if (command.name == 'outdent_list_item') { cmd_outdent_list_item(editor) }
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

pub fn edit_apply(editor, tx) => editor_after_tx(editor, tx)

pub fn edit_dispatch(editor, intent) => editor_after_tx(editor, dispatch_intent(editor, intent))

pub fn edit_set_decorations(editor, decorations) =>
  {kind: 'editor', doc: editor.doc, schema: editor.schema, selection: editor.selection,
   history: editor.history, decorations: decorations, composition: editor.composition,
   stored_marks: editor.stored_marks,
   events: [*editor.events, {kind: 'decorations', decorations: decorations}],
   mounted: editor.mounted, preset: editor.preset}

pub fn edit_find(editor, needle, attrs) =>
  edit_set_decorations(editor, find_decorations_in_doc(editor.doc, needle, attrs))
