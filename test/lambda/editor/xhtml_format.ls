import x: lambda.editor.format.xhtml
import lambda.editor.core.doc

let source = "<?xml version=\"1.0\"?>\n<!DOCTYPE html>\n<html xmlns=\"http://www.w3.org/1999/xhtml\"><head><title>T</title></head>\n<body>\n  <p>Hello world</p>\n\n\n</body></html>\n"
let loaded = x.import_text(source)^
let changed = node('doc', [node('p', [text("Edited & safe")])])
let written = x.export_text(changed, loaded.envelope)^
let reopened = x.import_text(written)^
let with_break = x.import_text("<html xmlns=\"http://www.w3.org/1999/xhtml\"><head><title>T</title></head>\n<body>\n  <p>Hello<br/>world</p>\n\n\n</body></html>")^
let break_edit = node('doc', [node('p', [text("Hello"), node('br', []), text("world edited")])])
let break_written = x.export_text(break_edit, with_break.envelope)^
let break_reopened = x.import_text(break_written)^
let compact = x.import_text("<html xmlns=\"http://www.w3.org/1999/xhtml\"><head></head><body><p>compact</p></body></html>")^
let compact_written = x.export_text(changed, compact.envelope)^
let variants = ["<p>A<br/>B<img src=\"x.png\" alt=\"x\"/></p>",
  "<ul><li>A</li><li>B</li></ul>", "<table><tbody><tr><td>A</td><td>B</td></tr></tbody></table>",
  "<p><b>A</b> <i>B</i></p>", "<pre>  a\n b  </pre>"]
let variants_kept = [for (body in variants) {
  let text = "<html xmlns=\"http://www.w3.org/1999/xhtml\"><head></head><body>" ++ body ++ "</body></html>"
  let item = x.import_text(text)^;
  x.check_roundtrip(item.doc, item.envelope)^ == null
}];
[
  x.export_text(loaded.doc, loaded.envelope)^ == source,
  x.check_roundtrip(changed, loaded.envelope)^ == null,
  starts_with(written, "<?xml version=\"1.0\"?>\n<!DOCTYPE html>\n"),
  contains(written, "Edited &amp; safe"),
  reopened.doc == changed,
  (x.import_text("<html xmlns=\"http://www.w3.org/1999/xhtml\"><body><p>unclosed</body></html>") ^ { null }) == null,
  (x.import_text("<h:html xmlns:h=\"http://www.w3.org/1999/xhtml\"><h:body/></h:html>") ^ { null }) == null,
  x.check_roundtrip(changed, compact.envelope)^ == null,
  x.check_roundtrip(with_break.doc, with_break.envelope)^ == null,
  x.check_roundtrip(break_edit, with_break.envelope)^ == null,
  break_reopened.doc == break_edit,
  parse(break_written, 'xhtml')^ != null,
  (x.import_text(compact_written)^).doc == changed,
  (x.import_text("<html xmlns=\"http://www.w3.org/1999/xhtml\"> <head></head><body><p>mixed</p></body></html>") ^ { null }) == null,
  all(variants_kept)
]
