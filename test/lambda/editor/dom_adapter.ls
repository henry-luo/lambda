// Model-backend coverage for the common DOM editing protocol (D7.2.5).
import lambda.editor.mod_doc
import lambda.editor.mod_editor
import lambda.editor.mod_source_pos
import adapter: lambda.editor.mod_dom_adapter
import model_registry: lambda.editor.mod_edit_registry

fn platform_event(input_type, data, source_selection, target_ranges) => {
  input_type: input_type, origin: "platform", command: null,
  data: data, html: null, mime: "text/plain",
  composition_phase: null, clipboard: null, drag: null,
  composition_caret: null, edit_plaintext_only: false,
  source_pos: null, source_selection: source_selection,
  source_target_ranges: target_ranges, target_pos: null,
  drag_move: false
}

let doc0 = node('doc', [node('paragraph', [text("A")])])
let caret1 = text_selection(pos([0, 0], 1), pos([0, 0], 1))
let editor0 = edit_open(doc0, editor_schemas.markdown, caret1)
let inserted = adapter.handle_event(
  editor0, platform_event("insertText", "!", caret1, [caret1]))

let marked_editor = {*: inserted.editor, stored_marks: [{type: 'strong'}]}
let echo = adapter.accept_dom_selection(marked_editor, {
  source_selection: inserted.editor.selection, source_pos: null,
  selection_revision: 7, selection_origin: "model-commit",
  model_revision: inserted.editor.model_revision
})
let stale = adapter.accept_dom_selection(echo.editor, {
  source_selection: text_selection(pos([0, 0], 0), pos([0, 0], 0)), source_pos: null,
  selection_revision: 6, selection_origin: "pointer", model_revision: 0
})
let pointer = adapter.accept_dom_selection(echo.editor, {
  source_selection: text_selection(pos([0, 0], 0), pos([0, 0], 0)), source_pos: null,
  selection_revision: 8, selection_origin: "pointer", model_revision: 0
})

let drag_doc = node('doc', [node('paragraph', [text("abcd")])])
let drag_source = text_selection(pos([0, 0], 1), pos([0, 0], 3))
let drag_target = text_selection(pos([0, 0], 4), pos([0, 0], 4))
let drag_editor = edit_open(drag_doc, editor_schemas.markdown, drag_source)
let drag_event = {*: platform_event("insertFromDrop", "bc", drag_source,
                                    [drag_target]),
                  drag_move: true, target_pos: pos([0, 0], 4)}
let moved = adapter.handle_event(drag_editor, drag_event)

let soft_line_target = text_selection(pos([0, 0], 2), pos([0, 0], 4))
let soft_line_deleted = adapter.handle_event(
  drag_editor, platform_event("deleteSoftLineForward", null,
                              text_selection(pos([0, 0], 2), pos([0, 0], 2)),
                              [soft_line_target]))

let unsupported = adapter.handle_event(
  editor0, platform_event("unknownEdit", "x", caret1, [caret1]))
let extension = model_registry.descriptor_for_intent("insertCodeBlock")

{
  insert: {claimed: inserted.result.claimed,
           changed: inserted.result.changed,
           text: doc_text(inserted.editor.doc),
           revision: inserted.editor.model_revision,
           history: len(inserted.editor.history.undo)},
  selection: {echo_changed: echo.changed,
              echo_revision: echo.editor.native_selection_revision,
              echo_marks_preserved: echo.editor.stored_marks == marked_editor.stored_marks,
              stale_changed: stale.changed,
              pointer_changed: pointer.changed,
              pointer_offset: pointer.editor.selection.head.offset,
              pointer_marks_cleared: pointer.editor.stored_marks == null},
  drag: {claimed: moved.result.claimed,
         text: doc_text(moved.editor.doc),
         transactions: len(moved.editor.history.undo),
         revision: moved.editor.model_revision},
  soft_line_delete: {claimed: soft_line_deleted.result.claimed,
                     text: doc_text(soft_line_deleted.editor.doc),
                     target: model_registry.descriptor_for_intent(
                         "deleteSoftLineForward").target_rule},
  unsupported: {claimed: unsupported.result.claimed,
                failure: unsupported.result.failure,
                unchanged: unsupported.editor.doc == editor0.doc},
  extension: {registry_valid: model_registry.registry != null,
              dom: extension.dom, model: extension.model.command_key}
}
