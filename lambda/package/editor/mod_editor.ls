// mod_editor.ls - public Lambda-side editor session facade.
//
// This module is a thin API layer over the lower-level command, transaction,
// history, and input-intent modules. It gives scripts a first-class editor
// value over the common DOM editing protocol and the Radiant mechanism waist.

import .mod_commands
import .mod_decorations
import .mod_doc_schema
import .mod_history
import .mod_input_intent
import .mod_md_schema
import .mod_source_pos
import .mod_transaction
import dom_adapter: lambda.editor.mod_dom_adapter
import edit_request: lambda.dom.edit_request
import edit_registry: lambda.dom.edit_registry
import dom

pub let editor_schemas = {
  markdown: markdown_schema,
  commonmark_strict: commonmark_strict_schema,
  html5_subset: html5_subset_schema,
  // doc = md_schema + drawing-layer entries: lets an editor host inline
  // <drawing> blocks (the Stage-4 schema seam). The drawing block is atomic +
  // editable, so flow-mode descends into it (canvas mode) rather than selecting
  // it as one opaque unit; its inner shapes are the selectable units.
  doc: doc_schema
}

fn schema_or_default(schema) => if (schema == null) markdown_schema else schema
fn selection_or_start(sel) => if (sel == null) text_selection(pos([0, 0], 0), pos([0, 0], 0)) else sel

pub fn edit_open(doc, schema, selection) =>
  {kind: 'editor', doc: doc, schema: schema_or_default(schema), selection: selection_or_start(selection),
   history: history_new(), decorations: null, composition: null, stored_marks: null,
   events: [], mounted: false, preset: null, surface_handle: null,
   model_revision: 0, native_selection_revision: 0}

pub fn edit_mount(editor, surface, preset) {
  let handle = if (type(surface) == string or surface == null) null
               else dom.bind_model_edit_surface(surface, editor.model_revision);
  { *: editor, events: [*editor.events, {kind: 'mount', surface: surface, preset: preset}],
    mounted: true, preset: preset, surface_handle: handle }
}

// keep this pure state transition total when its public signature crosses a module boundary.
pub fn edit_set_selection(editor, selection) map =>
  { *: editor, selection: selection, stored_marks: null,
    events: [*editor.events, {kind: 'selection', selection: selection}] }

pub fn edit_cmd_insert_text(text) => {name: 'insert_text', input_type: "insertText", data: text}
pub fn edit_cmd_paste_text(text) => {name: 'paste_text', input_type: "insertFromPaste", data: text, mime: "text/plain"}
pub fn edit_cmd_paste_html(html, fallback_text) => {name: 'paste_html', input_type: "insertFromPaste", html: html, data: fallback_text, mime: "text/html"}
pub fn edit_cmd_insert_image(src, alt) => {name: 'insert_image', input_type: "insertImage", src: src, alt: alt}
pub fn edit_cmd_insert_link(href, title, label) => {name: 'insert_link', input_type: "insertLink", href: href, title: title, label: label}
pub fn edit_cmd_insert_horizontal_rule() => {name: 'insert_horizontal_rule', input_type: "insertHorizontalRule"}
pub fn edit_cmd_insert_code_block(text) => {name: 'insert_code_block', input_type: "insertCodeBlock", data: text}
pub fn edit_cmd_wrap_blockquote() => {name: 'wrap_blockquote', input_type: "formatBlockquote"}
pub fn edit_cmd_lift_blockquote() => {name: 'lift_blockquote', input_type: "formatLiftBlockquote"}
pub fn edit_cmd_insert_table(rows, cols, header) => {name: 'insert_table', input_type: "insertTable", rows: rows, cols: cols, header: header}
pub fn edit_cmd_add_table_row() => {name: 'add_table_row', input_type: "insertTableRow"}
pub fn edit_cmd_delete_table_row() => {name: 'delete_table_row', input_type: "deleteTableRow"}
pub fn edit_cmd_add_table_column() => {name: 'add_table_column', input_type: "insertTableColumn"}
pub fn edit_cmd_delete_table_column() => {name: 'delete_table_column', input_type: "deleteTableColumn"}
pub fn edit_cmd_delete_backward() => {name: 'delete_backward', input_type: "deleteContentBackward"}
pub fn edit_cmd_delete_forward() => {name: 'delete_forward', input_type: "deleteContentForward"}
pub fn edit_cmd_delete_multi_node() => {name: 'delete_multi_node', input_type: "modelDeleteMultiNode"}
pub fn edit_cmd_insert_line_break() => {name: 'insert_line_break', input_type: "insertLineBreak"}

fn mark_input_type(mark) {
  if (mark == 'strong') "formatBold"
  else if (mark == 'em') "formatItalic"
  else if (mark == 'u') "formatUnderline"
  else "modelToggleMark"
}

pub fn edit_cmd_toggle_mark(mark, value) =>
  {name: 'toggle_mark', input_type: mark_input_type(mark), mark: mark, value: value}
pub fn edit_cmd_wrap_list(kind) =>
  {name: 'wrap_list', input_type: if (kind == 'ordered') "insertOrderedList" else "insertUnorderedList", kind: kind}
pub fn edit_cmd_indent_list_item() => {name: 'indent_list_item', input_type: "formatIndent"}
pub fn edit_cmd_outdent_list_item() => {name: 'outdent_list_item', input_type: "formatOutdent"}
pub fn edit_cmd_split_block() => {name: 'split_block', input_type: "insertParagraph"}
pub fn edit_cmd_set_block_type(tag) => {name: 'set_block_type', input_type: "formatBlock", tag: tag}
pub fn edit_cmd_history_undo() => {name: 'history_undo', input_type: "historyUndo"}
pub fn edit_cmd_history_redo() => {name: 'history_redo', input_type: "historyRedo"}
pub fn edit_cmd_move_text_selection(source_selection, target_pos) =>
  {name: 'move_text_selection', input_type: "modelMoveTextSelection",
   source_selection: source_selection, target_pos: target_pos}

fn command_tx(editor, command) => dom_adapter.transaction_for_command(editor, command)

pub fn edit_exec(editor, command) => dom_adapter.apply_transaction(editor, command_tx(editor, command))

pub fn edit_can_exec(editor, command) => command_tx(editor, command) != null

pub fn edit_apply(editor, tx) => dom_adapter.apply_transaction(editor, tx)

pub fn edit_dispatch(editor, intent) => dom_adapter.handle_event(editor, intent).editor

pub fn edit_can_dispatch(editor, intent) => dom_adapter.transaction_for_event(editor, intent) != null

pub fn edit_handle_dom_action(editor, action_event) => dom_adapter.handle_event(editor, action_event)
pub fn edit_handle_request(editor, request) => dom_adapter.handle_request(editor, request)
pub fn edit_accept_dom_selection(editor, evt) => dom_adapter.accept_dom_selection(editor, evt)

pub fn edit_request_from_toolbar(input_type, payload) {
  let descriptor = dom_adapter.descriptor_for_intent(input_type)
  if (descriptor == null) null else edit_request.from_toolbar(descriptor, payload)
}

pub fn edit_descriptor(spelling) => edit_registry.descriptor(spelling)

pub fn edit_set_decorations(editor, decorations) =>
  { *: editor, decorations: decorations,
    events: [*editor.events, {kind: 'decorations', decorations: decorations}] }

// A search decoration can fail to build from malformed editor state; exposing
// that value preserves the session instead of publishing a partial editor.
pub fn edit_find(editor, needle, attrs) map | error =>
  edit_set_decorations(editor, find_decorations_in_doc(editor.doc, needle, attrs))
