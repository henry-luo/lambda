import editor: lambda.editor.app
import src: lambda.editor.surface.source
import sess: lambda.editor.session

pn main() {
  let root = "temp/edit-unknown"
  let plain = root ++ "-script"
  let custom = root ++ ".custom"
  let bad = root ++ ".invalid"
  let image = root ++ ".ppm"
  let copy = root ++ "-copy"
  let copy_custom = root ++ "-copy.custom"
  let refused = root ++ "-copy.png"
  for (path in [copy, copy_custom, refused]) { if (exists(path)) io.delete(path)^ }
  output("#!/bin/sh\necho hello\n", plain)^
  output("message = 雪\n", custom)^
  output(b'\x61628063', bad)^
  output("P3\n1 1\n255\n255 0 0\n", image)^
  let plain_page = editor.open_document(plain, {}) ^ { null }
  let custom_page = editor.open_document(custom, {}) ^ { null }
  let invalid = editor.open_document(bad, {}) ^ { ^.message }
  let media = editor.open_document(image, {}) ^ { ^.message }
  let content = "hello\n"
  let loaded = src.descriptor.import_text(content)
  let plain_session = sess.new_session(plain, src.descriptor, content, loaded)
  let custom_session = sess.new_session(custom, src.descriptor, content, loaded)
  let plain_copy = sess.save(plain_session, loaded.doc, copy, false)
  let custom_copy = sess.save(custom_session, loaded.doc, copy_custom, false)
  let rejected = sess.save(plain_session, loaded.doc, refused, false)
  print(format([plain_page is element, custom_page is element,
    contains(invalid, "not safely readable as text"), contains(media, "non-text document"),
    plain_copy.ok, custom_copy.ok, not rejected.ok and not exists(refused)], 'json')^ ++ "\n")
}
