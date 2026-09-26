// lambda.edit session save (vibe/radiant/Radiant_Design_Edit_Mode.md §8): an
// external change or an existing Save As target stops the write until the
// caller confirms, unusable targets are refused, and the saved checkpoint
// moves only after a write succeeded.
import md: lambda.edit.markdown
import sess: lambda.edit.session
import lambda.editor.mod_doc

pn reset(path, text) {
  let written = output(text, path, {format: 'text'})
  if (written is error) { print("setup failed: " ++ path ++ "\n") }
}

pn remove(path) {
  if (exists(path)) { io.delete(path)^ }
}

fn report(result) => [result.ok, result.conflict, result.status]

pn show(value) { print(value); print("\n") }

pn main() {
  let dir = "./temp/edit_session_save"
  let path = dir ++ "/doc.md"
  let copy = dir ++ "/copy.md"
  reset(path, "# Doc\n\nSome text.\n")
  reset(dir ++ "/taken.md", "taken\n")
  remove(copy)

  let source = input(path, 'text')^
  let loaded = md.import_text(source)
  let session = sess.new_session(path, md.descriptor, source, loaded)
  let edited = node('doc', [*loaded.doc.content, node('p', [text("Added.")])])
  show(["clean", sess.is_dirty(session, loaded.doc), "dirty", sess.is_dirty(session, edited)])

  // a plain save writes the file and moves the checkpoint
  let saved = sess.save(session, edited, path, false)
  show(report(saved))
  show([input(path, 'text')^])
  show(["clean after save", not sess.is_dirty(saved.session, edited)])

  // someone else writes the file: the next save stops, the file keeps their text
  reset(path, "# Theirs\n")
  let again = node('doc', [*edited.content, node('p', [text("More.")])])
  let stopped = sess.save(saved.session, again, path, false)
  show(report(stopped))
  show([input(path, 'text')^, sess.is_dirty(stopped.session, again)])
  let forced = sess.save(saved.session, again, path, true)
  show(report(forced))
  show([input(path, 'text')^])

  // Save As: refused targets write nothing
  show(report(sess.save(forced.session, again, sess.resolve_path(dir, "nowhere/x.md"), false)))
  show(report(sess.save(forced.session, again, sess.resolve_path(dir, "copy.txt"), false)))
  show(["no copy.txt", not exists(dir ++ "/copy.txt")])
  let taken = sess.save(forced.session, again, sess.resolve_path(dir, "taken.md"), false)
  show(report(taken))
  show([input(dir ++ "/taken.md", 'text')^])

  // Save As to a new name in the same folder continues the session there
  let as_copy = sess.save(forced.session, again, sess.resolve_path(dir, "copy.md"), false)
  show(report(as_copy))
  show([as_copy.session.name, input(copy, 'text')^ == input(path, 'text')^])

  // another folder would break the image's relative path, so it is refused
  let pictured = node('doc', [node('p', [node_attrs('img', [{name: 'src', value: "pic.png"},
                                                         {name: 'alt', value: "pic"}], [])])])
  show(report(sess.save(as_copy.session, pictured, sess.resolve_path(dir, "../moved.md"), false)))
  show(["not moved", not exists("./temp/moved.md")])
}
