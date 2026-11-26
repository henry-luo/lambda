import session: lambda.editor.session
import source: lambda.editor.surface.source

pn main() {
  let path = "temp/edit-viewer/reload-safety.txt"
  let original = "safe\n"
  let pdf_text = "%PDF-1.7\n"
  output(original, path, 'text')^
  let loaded = source.descriptor.import_text(original)^
  let current = session.new_session(path, source.descriptor, original, loaded)
  output(binary([97, 0, 98])^, path, 'binary')^
  let nul = session.reload(current)
  output(binary([97, 255, 98])^, path, 'binary')^
  let invalid_utf8 = session.reload(current)
  output(pdf_text, path, 'text')^
  let pdf = session.reload(current)
  output("changed\n", path, 'text')^
  let text = session.reload(current)
  let rejected_format = {*:source.descriptor, check_roundtrip:(doc, envelope) => "cannot preserve"}
  let rejected_session = session.new_session(path, rejected_format, original, loaded)
  let rejected = session.reload(rejected_session);
  // Reload uses the same UTF-8 admission rule as open (D10.2.6v2).
  [nul.ok and nul.session.disk_text == "a\u0000b",
   not invalid_utf8.ok and contains(invalid_utf8.status, "not safely readable as UTF-8 text"),
   pdf.ok and pdf.session.disk_text == pdf_text,
   text.ok and text.session.disk_text == "changed\n",
   not rejected.ok and contains(rejected.status, "cannot preserve")]
}
