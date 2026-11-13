import app: lambda.editor.app
import md: lambda.editor.format.markdown
import lambda.editor.core.source_pos
import lambda.editor.core.commands
import lambda.editor.schema.markdown

let f = app.format_for_path("example.wiki")
let sources = ["* one\n** nested\n* two\n", "# first\n# second\n",
  "{| class=\"wikitable\"\n! A !! B\n|-\n| one || two\n|}\n", "<pre>hello</pre>\n", "<pre>one\ntwo</pre>\n"]
let loaded = [for (source in sources) f.import_text(source)^]
let table = loaded[2]
let caret = text_selection(pos([0, 1, 0, 0, 0], 0), pos([0, 1, 0, 0, 0], 0))
let added = cmd_add_table_row({doc:table.doc, selection:caret, schema:html5_subset_schema})^
let typed = cmd_insert_text({doc:added.doc_after, selection:added.sel_after, schema:html5_subset_schema}, "new")^
let table_output = f.export_text(typed.doc_after, table.envelope)^
let table_reopened = f.import_text(table_output)^;
[
  all([for (i, value in loaded) f.export_text(value.doc, value.envelope)^ == sources[i]]),
  all([for (value in loaded) f.check_roundtrip(value.doc, value.envelope) == null]),
  (f.import_text("Before <pre>hello</pre> after\n") ^ { null }) == null,
  (f.import_text("<pre class=\"x\">hello</pre>\n") ^ { null }) == null,
  (f.import_text("{{Template|x}}\n") ^ { null }) == null,
  f.check_roundtrip(typed.doc_after, table.envelope) == null,
  md.normal_form(table_reopened.doc) == md.normal_form(typed.doc_after)
]
