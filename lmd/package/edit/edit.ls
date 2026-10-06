// lambda.edit — the document-authoring application behind `lambda edit`
// (vibe/radiant/Radiant_Design_Edit_Mode.md).
//
// The CLI selects edit mode and hands this package a local path; format
// selection has one owner, the registry below (proposal §4). Opening is an
// effectful procedure (S12.1.1v2): read the source once, import it through the
// format adapter, prove the adapter can write it back without loss, and only
// then build the editing page. A part the editor cannot edit is kept as
// written and shown view-only; a document it still cannot keep is refused
// with a diagnostic instead of opening as a lossy editor (proposal §2).

import md: lambda.edit.markdown
import html: lambda.edit.html
import svg: lambda.edit.svg
import shell: lambda.edit.shell
import src: lambda.edit.source
import sess: lambda.edit.session
import lambda.editor.mod_editor

// Every editable format, in lookup order. The source surface opens the text
// formats no rich surface claims, and any file under `--source`
// (Radiant_Design_Source_Editor CED20).
pub let formats = [md.descriptor, html.descriptor, svg.descriptor, src.descriptor]

fn supported_suffixes() => join([for (f in formats) for (s in f.suffixes) s], ", ")

// The format whose suffix the path ends with, or null.
pub fn format_for_path(path) {
  let lower_path = lower(path)
  let found = [for (f in formats) for (s in f.suffixes where ends_with(lower_path, s)) f]
  if (len(found) == 0) null else found[0]
}

pub pn open_document(path, options) element^ {
  let format = if (options != null and options.source == true) src.descriptor else format_for_path(path)
  if (format == null) {
    raise error("'" ++ sess.basename(path) ++ "' is not a document type lambda edit supports (" ++
                supported_suffixes() ++ ")")
  }
  let source = input(path, 'text') ^ { raise error("could not read " ++ path, ^) }
  let loaded = format.import_text(source) ^ { raise error(^.message) }
  // Refuse to open what Save could not write back (proposal §2, §9).
  let mismatch = format.check_roundtrip(loaded.doc, loaded.envelope)
  if (mismatch != null) {
    raise error("the " ++ format.name ++ " editor cannot keep this document exactly: " ++ mismatch)
  }
  let session = sess.new_session(path, format, source, loaded)
  let status = "Opened " ++ session.name ++ "."
  if (format.surface == 'source') src.page(session, loaded.doc, status)
  else shell.page(session, edit_open(loaded.doc, format.schema, null), status)
}
