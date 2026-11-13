// XHTML uses XML syntax; admit only documents whose full XML tree survives
// conversion through the HTML editor and back (D10.1.4).
import html: lambda.editor.format.html

fn root_source(source) any^ {
  let first = index_of(source, "<html")
  let end_pos = last_index_of(source, "</html>")
  if (first == null or end_pos == null or end_pos < first) raise error("this XHTML root requires source mode")
  else {before: slice(source, 0, first)^, root: slice(source, first, end_pos + 7)^,
        after: slice(source, end_pos + 7, len(source))^}
}

fn as_xml(doc, envelope) any^ {
  let text = html.export_text(doc, envelope.html, envelope.compact)^
  let parsed = parse(text, 'html')^
  let roots = [for (item in content(parsed)^ where ((type(item) == element and name(item) == 'html') ^ { false })) item]
  if (len(roots) != 1) raise error("the XHTML root could not be serialized")
  else envelope.before ++ format(roots[0], 'xml')^ ++ envelope.after
}

pub fn import_text(source) map^ {
  let original = parse(source, 'xhtml')^
  let parts = root_source(source)^
  // Project the strict XML tree to HTML before using the HTML model adapter.
  let html_parts = root_source(format(original, 'html')^)^
  let loaded = html.import_text(html_parts.root)^
  let envelope = {html: loaded.envelope, before: parts.before, after: parts.after,
                  source: source, original: loaded.doc, compact: false}
  let written = as_xml(loaded.doc, envelope)^
  if (parse(written, 'xhtml')^ == original) {doc: loaded.doc, envelope: envelope}
  else {
    let compact = {*: envelope, compact: true}
    let compact_written = as_xml(loaded.doc, compact)^
    if (parse(compact_written, 'xhtml')^ != original) raise error("this XHTML document cannot be preserved in rich mode")
    else {doc: loaded.doc, envelope: compact}
  }
}

pub fn export_text(doc, envelope) any^ =>
  if (doc == envelope.original) envelope.source else as_xml(doc, envelope)^

pub fn check_roundtrip(doc, envelope) any^ {
  let problem = html.check_roundtrip(doc, envelope.html)^
  if (problem != null) problem else {
    let written = as_xml(doc, envelope) ^ { null }
    let valid = if (written == null) null else (parse(written, 'xhtml') ^ { null })
    // XML permits </br>, but feeding it directly to the HTML parser creates a
    // second break. Reimport through XHTML's HTML serialization instead.
    let html_text = if (valid == null) null else (format(valid, 'html') ^ { null })
    let parts = if (html_text == null) null else (root_source(html_text) ^ { null })
    let back = if (parts == null) null else (html.import_text(parts.root) ^ { null })
    if (back == null or not html.same_envelope(back.envelope, envelope.html) or
        html.normal_form(back.doc) != html.normal_form(doc))
      "this XHTML document cannot preserve the edit"
    else null
  }
}

pub let descriptor = {*: html.descriptor, id: 'xhtml', name: "XHTML", suffixes: [".xhtml", ".xht"],
  fallback_source: true, import_text: import_text, export_text: export_text, check_roundtrip: check_roundtrip}
