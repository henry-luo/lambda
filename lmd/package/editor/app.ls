// lambda.editor.app — the document-authoring application behind `lambda edit`
// (vibe/radiant/Radiant_Design_Edit_Mode.md).
//
// The CLI selects edit mode and hands this package a local path; format
// selection has one owner, the registry below (proposal §4). Opening is an
// effectful procedure (S12.1.1v2): read the source, import it through the
// format adapter, prove the adapter can write it back without loss, and only
// then build the editing page. A part the editor cannot edit is kept as
// written and shown view-only; a document it still cannot keep is refused
// with a diagnostic instead of opening as a lossy editor (proposal §2).

import md: lambda.editor.format.markdown
import html: lambda.editor.format.html
import markup: lambda.editor.format.markup
import rtf: lambda.editor.format.rtf
import properties: lambda.editor.format.key_value
import delimited: lambda.editor.format.delimited
import xml: lambda.editor.format.xml
import property: lambda.editor.surface.property
import shell: lambda.editor.app.shell
import src: lambda.editor.surface.source
import sess: lambda.editor.session
import files: lambda.editor.app.files
import tools: lambda.editor.app.toolbar
import utf8: lambda.io.utf8
import lambda.editor.model

// Every editable format, in lookup order. The source surface opens the text
// formats no rich surface claims, and any file under `--source`
// (Radiant_Design_Source_Editor CED20).
pub let formats = [md.descriptor, html.descriptor, *markup.formats, rtf.descriptor, properties.descriptor,
                   properties.ini_descriptor, delimited.csv_descriptor, delimited.tsv_descriptor, xml.descriptor, src.descriptor]

// These viewer formats are binary containers or media, even with --source.
let non_text_suffixes = [".pdf", ".png", ".jpg", ".jpeg", ".gif", ".psd", ".docx", ".pptx", ".epub",
  ".webp", ".bmp", ".tif", ".tiff", ".ico", ".mp3", ".wav", ".ogg", ".mp4", ".mov", ".webm",
  ".zip", ".7z", ".rar", ".tar", ".gz"]
pub fn is_non_text_path(path) =>
  any([for (s in non_text_suffixes) ends_with(lower(path), s)]) or false

fn supported_suffixes() => join([for (f in formats) for (s in f.suffixes) s], ", ")

// The format whose suffix the path ends with, or null.
pub fn format_for_path(path) {
  let lower_path = lower(path)
  let found = [for (f in formats) for (s in f.suffixes where ((ends_with(lower_path, s)) ^ { false })) f]
  if (len(found) == 0) null else found[0]
}

pub pn open_document(path, options) element^ {
  if (is_non_text_path(path)) {
    raise error("'" ++ sess.basename(path) ++ "' is a non-text document and cannot be edited")
  }
  let selected = if (options != null and options.source == true) src.descriptor else format_for_path(path)
  if (selected == null) {
    raise error("'" ++ sess.basename(path) ++ "' is not a document type lambda edit supports (" ++
                supported_suffixes() ++ ")")
  }
  let source = input(path, 'text') ^ { raise error("could not read " ++ path, ^) }
  // PDF can be entirely ASCII, so the text and UTF-8 checks cannot identify it.
  if (starts_with(source, "%PDF-")^) {
    raise error("'" ++ sess.basename(path) ++ "' is a non-text document and cannot be edited")
  }
  // A NUL cannot safely pass through the source editor's text interfaces.
  // Compare the read bytes too, so a truncated read cannot become a save.
  let bytes = input(path, 'binary') ^ { raise error("could not read " ++ path, ^) }
  if (((index_of(source, "\u0000") != null) or (bytes != null and
       (binary(source)^ != bytes or not utf8.is_valid(bytes)^)))^) {
    raise error("the file is not safely readable as text")
  }
  let attempt = selected.import_text(source) ^ { {error: ^.message} }
  if (attempt.error != null and selected.fallback_source != true) { raise error(attempt.error) }
  let mismatch = if (attempt.error != null) null else selected.check_roundtrip(attempt.doc, attempt.envelope)
  if ((mismatch != null and selected.fallback_source != true)^) {
    raise error("the " ++ selected.name ++ " editor cannot keep this document exactly: " ++ mismatch)
  }
  let reason = if (attempt.error != null) attempt.error else mismatch
  let format = if (reason != null) src.descriptor else selected
  let loaded = if (reason != null) src.descriptor.import_text(source) else attempt
  // the rich format behind the view switch, null for plain text
  let named = format_for_path(path)
  let rich_format = if (named != null and named.surface != 'source') named else null
  let session = {*: sess.new_session(path, format, source, loaded), rich_format: rich_format}
  let status = if (reason != null) "Opened source: " ++ reason else "Opened " ++ session.name ++ "."
  page(if (format.surface == 'source') {mode: 'source', session: session, doc: loaded.doc, status: status}
       else if (format.surface == 'property') {mode: 'property', session: session, doc: loaded.doc, status: status}
       else {mode: 'rich', session: session, doc: edit_open(loaded.doc, format.schema, null), status: status})
}

// The application root: the rich (or drawing) surface or the source surface
// over one file (Radiant_Design_Source_Editor OQ7, CED20). Each surface keeps
// its own state; switching hands the other the view it builds from the
// current text, `shown` = {mode, session, doc, status}.
edit <edit_doc> () any^ state shown: ~.shown {
  // the surface's own <body> is this template's result as well; both handle
  // its events, the surface first (render_map_wrapper_lookup)
  if (shown.mode == 'source') src.app(shown.session, shown.doc, shown.status)
  else if (shown.mode == 'property') property.app(shown.session, shown.doc, shown.status)
  else shell.app(shown.session, shown.doc, shown.status)
}
on edit_switch(next) {
  shown = next
}

fn page(shown) =>
  <html lang: "en",
    <head
      <meta charset: "UTF-8">
      <title sess.window_title(shown.session, false)>
      // Math projections carry their measured glyph outlines in SVG.
      <style files.css ++ tools.css ++ shell.surface_css ++ src.surface_css ++ property.css>
    >
    apply(<edit_doc shown: shown>, {mode: "edit"})
  >
