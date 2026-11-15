import f: lambda.editor.format.rtf
import sess: lambda.editor.session
import lambda.editor.core.doc
import lambda.editor.core.source_pos
import lambda.editor.format.model
import lambda.editor.model
import commands: lambda.editor.core.commands

fn editor_for(loaded: map, path) any^ => edit_open(loaded.doc^, sess.edit_schema(f.descriptor, loaded.envelope)^,
  text_selection(pos(path, 0), pos(path, 0)))
let basic = f.import_text("{\\rtf1 Start}")^
let basic_editor = editor_for(basic, [0,0])^
let basic_tx = commands.cmd_insert_table(basic_editor, 2, 2, true)^
let basic_table = basic_tx.doc_after.content[1]
let configured = f.import_text("{\\rtf1\\paperw10000\\margl100\\margr200\\gutter2 Start}")^
let configured_tx = commands.cmd_insert_table(editor_for(configured, [0,0])^, 2, 3, true)^
let narrow = f.import_text("{\\rtf1\\paperw100\\margl100\\margr100 Start}")^
let narrow_tx = commands.cmd_insert_table(editor_for(narrow, [0,0])^, 2, 2, true)^
let existing = f.import_text("{\\rtf1\\trowd\\cellx2000\\intbl A\\cell\\row \\pard End}")^
let adjacent_tx = commands.cmd_insert_table(editor_for(existing, [0,0,0,0])^, 2, 2, true)^
let custom_written = f.export_text(configured_tx.doc_after, configured.envelope)^
let reopened = f.import_text(custom_written)^;
[
  basic.envelope.table_width == 8640,
  [for (row in basic_table.content) [for (cell in row.content) attr_get(cell, 'rtf-width')]] == [[4320,4320],[4320,4320]],
  all([for (row in basic_table.content) all([for (cell in row.content) cell.tag == 'td'])]),
  f.check_roundtrip(basic_tx.doc_after, basic.envelope) == null,
  configured.envelope.table_width == 9698,
  [for (cell in configured_tx.doc_after.content[1].content[0].content) attr_get(cell, 'rtf-width')] == [3233,3233,3232],
  contains(custom_written, "\\cellx3233\\cellx6466\\cellx9698"),
  f.check_roundtrip(configured_tx.doc_after, configured.envelope) == null,
  len(reopened.doc.content[1].content) == 2,
  narrow_tx == null,
  [for (block in adjacent_tx.doc_after.content) block.tag] == ['table','p','table','p'],
  f.check_roundtrip(adjacent_tx.doc_after, existing.envelope) == null
]
