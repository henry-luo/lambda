import adapter: lambda.editor.format.docx
import session: lambda.editor.session
import docx: lambda.doc.docx
import lambda.editor.core.doc

pn main() {
  let path = "temp/editor-docx-session.docx"
  let copy = "temp/editor-docx-copy.docx"
  let snapshot = input("test/input/docx/minimal.docx", 'binary')^
  output(snapshot, path, 'binary')^
  let opened = adapter.open_snapshot(snapshot)^
  let initial = session.new_session(path, adapter.descriptor, snapshot, opened)
  let noop = session.save(initial, opened.doc, path, false)
  let noop_bytes = input(path, 'binary')^
  let changed = replace_node_at(opened.doc, [0, 0], text("Edited DOCX!"))^
  let saved = session.save(noop.session, changed, path, false)
  let first = docx.extract_text(path)^
  let again = session.save(saved.session, changed, path, false)
  let second_change = replace_node_at(changed, [0, 0], text("Second edit"))^
  let saved_again = session.save(again.session, second_change, path, false)
  let second = docx.extract_text(path)^
  let copied = session.save(saved_again.session, second_change, copy, true)
  let copied_text = docx.extract_text(copy)^
  output(snapshot, path, 'binary')^
  let conflict = session.save(saved_again.session, second_change, path, false)
  let reread = session.reload(saved_again.session);
  [noop.ok, noop_bytes == snapshot, saved.ok, contains(first, "Edited DOCX!"),
   again.ok, saved_again.ok, contains(second, "Second edit"), copied.ok,
   contains(copied_text, "Second edit"), not conflict.ok and conflict.conflict == 'changed',
   reread.ok and reread.doc == opened.doc]
}
