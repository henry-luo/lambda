import f: lambda.editor.format.rtf
import html: lambda.editor.format.html
import lambda.editor.core.doc
import lambda.editor.core.source_pos
import lambda.editor.format.model
import lambda.editor.model
import commands: lambda.editor.core.commands

let source = "{\\rtf1\\ansi Before\\par \\trowd\\cellx1200\\cellx3000\\pard\\qc\\keep\\intbl A\\cell \\pard\\intbl {\\b B}\\cell\\row " ++
  "\\trowd\\cellx1000\\cellx3000\\pard\\intbl\\cell D\\cell\\row \\pard After}"
let loaded = f.import_text(source)^
let table = loaded.doc.content[1]
let first_row = table.content[0]
let original_cell = first_row.content[1]
let edited_cell = node_attrs('td', original_cell.attrs, [text_marked("Edited", original_cell.content[0].marks)])
let edited_row = node_attrs('tr', first_row.attrs, [first_row.content[0], edited_cell])
let changed = node('doc', [loaded.doc.content[0], node('table', [edited_row, table.content[1]]), loaded.doc.content[2]])
let written = f.export_text(changed, loaded.envelope)^
let reopened = f.import_text(written)^
let caret_pos = pos([1,0,0,0], 0)
let editor = edit_open(loaded.doc, f.descriptor.schema, text_selection(caret_pos, caret_pos))
let row_insert = commands.cmd_add_table_row(editor)^
let col_insert = commands.cmd_add_table_column(editor)^
let line_source = "{\\rtf1 First\\line Second}"
let line_loaded = f.import_text(line_source)^
let line_changed = node('doc', [node_attrs('p', line_loaded.doc.content[0].attrs,
  [*line_loaded.doc.content[0].content, text("!")])])
let ragged = f.import_text("{\\rtf1\\trowd\\cellx1200\\cellx3000\\intbl A\\cell B\\cell\\row " ++
  "\\trowd\\cellx1000\\intbl C\\cell\\row}")^
let ragged_pos = pos([0,0,1,0], 0)
let ragged_editor = edit_open(ragged.doc, f.descriptor.schema, text_selection(ragged_pos, ragged_pos))
let ragged_column = commands.cmd_add_table_column(ragged_editor)^
let invalid = [
  "{\\rtf1\\trowd\\cellx1000\\cellx900\\intbl A\\cell B\\cell\\row}",
  "{\\rtf1\\trowd\\cellx1000\\intbl A\\cell}",
  "{\\rtf1\\trowd\\cellx1000\\cellx2000\\intbl A\\cell\\row}",
  "{\\rtf1\\trowd\\cellx1000\\intbl A\\par B\\cell\\row}",
  "{\\rtf1\\trowd\\clmgf\\cellx1000\\intbl A\\cell\\row}",
  "{\\rtf1\\trowd\\cellx1000 A\\cell\\row}",
  "{\\rtf1\\trowd\\cellx1000\\cellx2000\\intbl A\\cell\\pard B\\cell\\row}",
  "{\\rtf1\\trowd\\cellx1000\\intbl A\\cell Lost\\row}",
  "{\\rtf1\\trowd\\cellx1000\\intbl A\\cell\\row Lost}"
];
[
  [for (block in loaded.doc.content) block.tag] == ['p','table','p'],
  len(table.content) == 2 and [for (row in table.content) len(row.content)] == [2,2],
  [for (row in table.content) [for (cell in row.content) attr_get(cell, 'rtf-width')]] == [[1200,1800],[1000,2000]],
  [for (row in table.content) [for (cell in row.content) node_plain_text(cell)]] == [["A","B"],["","D"]],
  f.export_text(loaded.doc, loaded.envelope)^ == source,
  f.check_roundtrip(loaded.doc, loaded.envelope) == null,
  f.check_roundtrip(changed, loaded.envelope) == null,
  html.normal_form(reopened.doc) == html.normal_form(changed),
  contains(written, "\\cellx1200\\cellx3000") and contains(written, "\\cellx1000\\cellx3000"),
  contains(written, "{\\b Edited}"),
  [for (cell in row_insert.doc_after.content[1].content[1].content) attr_get(cell, 'rtf-width')] == [1200,1800],
  attr_get(row_insert.doc_after.content[1].content[1].content[0], 'align') == "center" and
    attr_get(row_insert.doc_after.content[1].content[1].content[0], 'rtf-keep') == true,
  [for (row in col_insert.doc_after.content[1].content) [for (cell in row.content) attr_get(cell, 'rtf-width')]] ==
    [[1200,1200,1800],[1000,1000,2000]],
  f.check_roundtrip(row_insert.doc_after, loaded.envelope) == null,
  f.check_roundtrip(col_insert.doc_after, loaded.envelope) == null,
  [for (row in ragged_column.doc_after.content[0].content) [for (cell in row.content) attr_get(cell, 'rtf-width')]] ==
    [[1200,1800,1800],[1000,1000,1000]],
  f.check_roundtrip(ragged_column.doc_after, ragged.envelope) == null,
  line_loaded.doc.content[0].content[1].tag == 'br' and f.export_text(line_loaded.doc, line_loaded.envelope)^ == line_source,
  f.check_roundtrip(line_changed, line_loaded.envelope) == null,
  all([for (text in invalid) (f.import_text(text) ^ { null }) == null])
]
