import lambda.package.editor.mod_doc
import lambda.package.editor.mod_editor
import lambda.package.editor.mod_source_pos
import lambda.package.editor.mod_step
import lambda.package.editor.mod_transaction

let d0 = node('doc', [node('paragraph', [text("Hello")])])
let caret = text_selection(pos([0, 0], 5), pos([0, 0], 5))

let editor0 = edit_open(d0, editor_schemas.markdown, caret)
"open kind:"; editor0.kind == 'editor'
"open schema doc role:"; editor0.schema.doc.role == 'block'
"open history empty:"; len(editor0.history.undo) == 0

let mounted = edit_mount(editor0, 'window0', 'markdown_wysiwyg')
"mount flag:"; mounted.mounted
"mount event:"; mounted.events[0].kind == 'mount'

let editor1 = edit_exec(editor0, edit_cmd_insert_text("!"))
"exec insert doc:"; doc_text(editor1.doc) == "Hello!"
"exec insert caret:"; editor1.selection.anchor.offset == 6
"exec change event:"; editor1.events[0].kind == 'change'
"exec selection event:"; editor1.events[1].kind == 'selection'
"exec scroll meta:"; tx_get_meta(editor1.events[0].transaction, "scrollIntoView")
"exec history depth:"; len(editor1.history.undo) == 1

let editor2 = edit_exec(editor1, edit_cmd_history_undo())
"exec undo doc:"; doc_text(editor2.doc) == "Hello"
"exec undo selection:"; editor2.selection.anchor.offset == 5
"exec undo redo depth:"; len(editor2.history.redo) == 1

let editor3 = edit_exec(editor2, edit_cmd_history_redo())
"exec redo doc:"; doc_text(editor3.doc) == "Hello!"
"exec redo selection:"; editor3.selection.anchor.offset == 6

let editor4 = edit_exec(editor0, edit_cmd_toggle_mark('strong'))
"exec stored mark:"; has_mark(editor4.stored_marks, 'strong')
"exec stored history:"; len(editor4.history.undo) == 0

let editor5 = edit_exec(editor4, edit_cmd_insert_text("*"))
"exec stored insert doc:"; doc_text(editor5.doc) == "Hello*"
"exec stored insert mark:"; has_mark(node_at(editor5.doc, [0, 1]).marks, 'strong')

let paste_editor = edit_exec(editor0, edit_cmd_paste_text(" world"))
"exec paste text:"; doc_text(paste_editor.doc) == "Hello world"
let delete_editor = edit_exec(paste_editor, edit_cmd_delete_backward())
"exec delete backward:"; doc_text(delete_editor.doc) == "Hello worl"

let heading_doc = node('doc', [node_attrs('heading', [{name: 'level', value: 2}], [text("Title")])])
let heading_editor = edit_open(heading_doc, editor_schemas.markdown, text_selection(pos([0, 0], 5), pos([0, 0], 5)))
let split_editor = edit_exec(heading_editor, edit_cmd_split_block())
"exec split default:"; node_at(split_editor.doc, [1]).tag == 'paragraph'

let typed_editor = edit_exec(editor0, edit_cmd_set_block_type('heading'))
"exec set type:"; node_at(typed_editor.doc, [0]).tag == 'heading'

let node_select_editor = edit_open(node('doc', [
	node('paragraph', [text("A")]),
	node('paragraph', [text("B")])
]), editor_schemas.markdown, node_selection([1]))
let node_typed_editor = edit_exec(node_select_editor, edit_cmd_set_block_type('heading'))
"exec set type node:"; node_at(node_typed_editor.doc, [1]).tag == 'heading'

let span_select_editor = edit_open(node('doc', [node('paragraph', [text("A"), text("B")])]),
	editor_schemas.markdown, text_selection(pos([0, 0], 0), pos([0, 1], 1)))
let span_typed_editor = edit_exec(span_select_editor, edit_cmd_set_block_type('heading'))
"exec set type span:"; node_at(span_typed_editor.doc, [0]).tag == 'heading'

let all_select_editor = edit_open(node('doc', [
	node('paragraph', [text("A")]),
	node('paragraph', [text("B")])
]), editor_schemas.markdown, all_selection())
let all_typed_editor = edit_exec(all_select_editor, edit_cmd_set_block_type('heading'))
"exec set type all first:"; node_at(all_typed_editor.doc, [0]).tag == 'heading'
"exec set type all second:"; node_at(all_typed_editor.doc, [1]).tag == 'heading'
"exec set type all selection:"; all_typed_editor.selection.kind == 'all'

let html_editor = edit_open(<doc <html; <body; <p; "Hi">>>>, editor_schemas.html5_subset, null)
"open html schema:"; html_editor.schema.html.role == 'block'

let html_text_editor = edit_open(node('doc', [node('p', [text("Hi")])]), editor_schemas.html5_subset,
	text_selection(pos([0, 0], 2), pos([0, 0], 2)))
let html_break_editor = edit_exec(html_text_editor, edit_cmd_insert_line_break())
"exec html line-break:"; node_at(html_break_editor.doc, [0, 1]).tag == 'br'
let html_paste_editor = edit_exec(html_text_editor, edit_cmd_paste_html("<p> there</p>", " there"))
"exec html paste tag:"; node_at(html_paste_editor.doc, [0]).tag == 'p'
"exec html paste text:"; doc_text(html_paste_editor.doc) == "Hi there"
