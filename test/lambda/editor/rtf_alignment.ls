import f: lambda.editor.format.rtf
import lambda.editor.core.doc
import lambda.editor.core.step
import lambda.editor.core.source_pos
import lambda.editor.format.model

let source = "{\\rtf1\\qc Center\\par Inherited\\par\\qr Right\\par\\qj Justified\\par\\pard Left}"
let loaded = f.import_text(source)^
let original = loaded.doc
let changed = node('doc', [*take(original.content, 4), node('p', [text("Left edited")])])
let written = f.export_text(changed, loaded.envelope)^
let back = f.import_text(written)^
let late = "{\\rtf1 First {\\b bold}\\qc centered\\par\\pard Left}"
let selected = text_selection(pos([2, 0], 0), pos([0, 0], 2))
let aligned = f.align_paragraphs({doc:original, selection:selected}, "justify")^
let at_end = text_selection(pos([0, 0], 0), pos([1, 0], 0))
let first_only = f.align_paragraphs({doc:original, selection:at_end}, "right")^
let cleared = f.align_paragraphs({doc:aligned.doc_after, selection:all_selection()}, "left")^
let split_doc = node('doc', [node('p', [text("one")]), node('p', [text("two"), text_marked("three", [mark('strong', true)])])])
let split_sel = text_selection(pos([0, 0], 0), pos([1, 1], 0))
let split_aligned = f.align_paragraphs({doc:split_doc, selection:split_sel}, "center")^
let measured_source = "{\\rtf1\\li721\\ri239\\sl361 First\\par Inherited\\par\\pard Plain}"
let measured = f.import_text(measured_source)^
let measured_edit = node('doc', [*take(measured.doc.content, 2), node('p', [text("Plain edited")])])
let measured_output = f.export_text(measured_edit, measured.envelope)^
let measured_back = f.import_text(measured_output)^;
[
  f.export_text(original, loaded.envelope)^ == source,
  [for (p in original.content) attr_get(p, 'align')] == ["center", "center", "right", "justify", null],
  f.check_roundtrip(changed, loaded.envelope) == null,
  [for (p in back.doc.content) attr_get(p, 'align')] == ["center", "center", "right", "justify", null],
  contains(written, "\\pard\\qc") and contains(written, "\\pard\\qr") and contains(written, "\\pard\\qj"),
  (f.import_text(late) ^ { null }) == null,
  (f.import_text("{\\rtf1\\qc1 invalid}") ^ { null }) == null,
  f.check_roundtrip(node('doc', [node_attrs('p', [{name:'align', value:"invalid"}], [text("bad")])]), loaded.envelope) != null,
  [for (p in aligned.doc_after.content) attr_get(p, 'align')] == ["justify", "justify", "right", "justify", null],
  aligned.sel_after == selected,
  [for (p in first_only.doc_after.content) attr_get(p, 'align')] == ["right", "center", "right", "justify", null],
  [for (p in cleared.doc_after.content) attr_get(p, 'align')] == [null, null, null, null, null],
  f.check_roundtrip(cleared.doc_after, loaded.envelope) == null,
  [for (p in split_aligned.doc_after.content) attr_get(p, 'align')] == ["center", "center"],
  [for (p in measured.doc.content) attr_get(p, 'rtf-li')] == [721, 721, null],
  [for (p in measured_back.doc.content) attr_get(p, 'rtf-ri')] == [239, 239, null],
  [for (p in measured_back.doc.content) attr_get(p, 'rtf-sl')] == [361, 361, null],
  f.check_roundtrip(measured_edit, measured.envelope) == null and contains(measured_output, "\\li721\\ri239\\sl361"),
  (f.import_text("{\\rtf1\\li-10 negative}") ^ { null }) == null,
  (f.import_text("{\\rtf1 text\\li20 late}") ^ { null }) == null
]
