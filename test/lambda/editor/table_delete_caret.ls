import lambda.editor.core.doc
import lambda.editor.core.step
import lambda.editor.core.source_pos
import lambda.editor.core.commands
import lambda.editor.schema.markdown

let doc = node('doc', [node('table', [node('tbody', [
  node('tr', [node('td', [node('p', [text("nested")])])]),
  node('tr', [node('td', [text("left")]), node('td', [text("right")])])
])])])
let selected = text_selection(pos([0, 0, 1, 1, 0], 0), pos([0, 0, 1, 1, 0], 0))
let row = cmd_delete_table_row({doc: doc, schema: html5_subset_schema, selection: selected})
let row_text = cmd_insert_text({doc: row.doc_after, schema: html5_subset_schema, selection: row.sel_after}, "X")
let col = cmd_delete_table_column({doc: doc, schema: html5_subset_schema, selection: selected})
let col_text = cmd_insert_text({doc: col.doc_after, schema: html5_subset_schema, selection: col.sel_after}, "Y")
let empty_doc = node('doc', [node('table', [node('tbody', [
  node('tr', [node('td', [])]), node('tr', [node('td', [text("remove")])])
])])])
let empty_row = cmd_delete_table_row({doc: empty_doc, schema: html5_subset_schema,
  selection: text_selection(pos([0, 0, 1, 0, 0], 0), pos([0, 0, 1, 0, 0], 0))})
let empty_text = cmd_insert_text({doc: empty_row.doc_after, schema: html5_subset_schema, selection: empty_row.sel_after}, "Z");
[
  row.sel_after.kind == 'text' and path_equal(row.sel_after.anchor.path, [0, 0, 0, 0, 0, 0]),
  doc_text(row_text.doc_after) == "Xnested",
  col.sel_after.kind == 'text' and path_equal(col.sel_after.anchor.path, [0, 0, 1, 0, 0]),
  doc_text(col_text.doc_after) == "nestedYleft",
  empty_text != null and doc_text(empty_text.doc_after) == "Z"
]
