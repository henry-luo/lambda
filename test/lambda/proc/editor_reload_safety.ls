import session: lambda.editor.session
import source: lambda.editor.surface.source

pn main() {
  let path = "temp/edit-viewer/reload-safety.txt"
  let original = "safe\n"
  output(original, path, 'text')^
  let loaded = source.descriptor.import_text(original)^
  let current = session.new_session(path, source.descriptor, original, loaded)
  output(binary([97, 0, 98])^, path, 'binary')^
  let nul = session.reload(current)
  output(binary([97, 255, 98])^, path, 'binary')^
  let invalid_utf8 = session.reload(current)
  output("%PDF-1.7\n", path, 'text')^
  let pdf = session.reload(current)
  output("changed\n", path, 'text')^
  let text = session.reload(current)
  let rejected_format = {*:source.descriptor, check_roundtrip:(doc, envelope) => "cannot preserve"}
  let rejected_session = session.new_session(path, rejected_format, original, loaded)
  let rejected = session.reload(rejected_session);
  [not nul.ok, not invalid_utf8.ok, not pdf.ok,
   text.ok and text.session.disk_text == "changed\n",
   not rejected.ok and contains(rejected.status, "cannot preserve")]
}
