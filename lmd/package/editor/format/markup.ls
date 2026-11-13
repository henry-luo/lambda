// Shared lightweight-markup adapter. A writer is admitted only when its
// round trip retains the original parsed document and the editable model.
import md: lambda.editor.format.markdown

// Parser-created plain spans and text-only nowiki guards do not change the
// document meaning; retain every other element and attribute in the check.
fn literal_wiki_span(item) any^ => type(item) == element and name(item) == 'span' and
  item["data-wiki-nowiki"] == "true" and
  all([for (key, value at map(item)) string(key) in ["data-wiki-nowiki", "source"]]) and
  all([for (child in (content(item) ^ { [] })) type(child) == string])
fn merge_text(parts, i, acc) any^ {
  if (i >= len(parts)) acc
  else if (parts[i] is string and len(acc) > 0 and acc[len(acc) - 1] is string)
    merge_text(parts, i + 1, [*take(acc, len(acc) - 1), acc[len(acc) - 1] ++ parts[i]])^
  else merge_text(parts, i + 1, [*acc, parts[i]])^
}
fn semantic_parts(item) any^ {
  if (type(item) != element) [item]
  else if (literal_wiki_span(item)^ or (name(item) == 'span' and len(map(item)) == 0))
    [for (child in content(item)^) for (part in semantic_parts(child)^) part]
  else {
    let attrs = map(item)
    let children = semantic_children(content(item)^)^
    // Same-line pre blocks may be wrapped in an otherwise empty parser paragraph.
    if (name(item) == 'p' and len(attrs) == 0 and len(children) == 1 and children[0] is map and
        children[0].tag == 'code' and children[0].attributes.type == "block") children
    else [{tag:name(item), attributes:attrs, children:children}]
  }
}
fn semantic_children(items) any^ =>
  merge_text([for (item in items) for (part in semantic_parts(item)^) part], 0, [])^
fn same_wiki_document(a, b) any^ => semantic_parts(a)^ == semantic_parts(b)^

fn to_markdown(source, kind) any^ {
  let parsed = parse(source, kind)^
  let markdown = format(parsed, 'markdown')^;
  {parsed: parsed, markdown: markdown}
}
fn from_model(doc, envelope) any^ {
  let markdown = md.export_text(doc, envelope.markdown)^
  let parsed = parse(markdown, 'markdown')^
  format(parsed, envelope.kind)^
}
fn import_as(source, kind) map^ {
  let converted = to_markdown(source, kind)^
  let loaded = md.import_text(converted.markdown)^
  let envelope = {kind: kind, markdown: loaded.envelope, source: source, original: loaded.doc}
  let written = from_model(loaded.doc, envelope)^
  let back = parse(written, kind)^
  if (back != converted.parsed and (kind != 'wiki' or not same_wiki_document(converted.parsed, back)^)) {
    raise error("this document contains markup the editor cannot preserve")
  }
  else { {doc: loaded.doc, envelope: envelope} }
}
fn export_text(doc, envelope) any^ =>
  if (doc == envelope.original) envelope.source else (from_model(doc, envelope) ^ { null })
fn check_roundtrip(doc, envelope) any^ {
  let markdown_problem = md.check_roundtrip(doc, envelope.markdown)
  if (markdown_problem != null) markdown_problem
  else {
    let written = from_model(doc, envelope) ^ { null }
    if (written == null) "this format cannot represent the edit"
    else {
      let converted = to_markdown(written, envelope.kind) ^ { null }
      let back = if (converted == null) null else (md.import_text(converted.markdown) ^ { null })
      if (back == null or md.normal_form(back.doc) != md.normal_form(doc))
        "this format cannot preserve the edit"
      else null
    }
  }
}
fn descriptor(kind, title, suffixes) => {
  id: kind, name: title, suffixes: suffixes, surface: 'rich_text', fallback_source: true,
  schema: md.schema, schema_preset: 'html5_subset',
  unsupported_input_types: md.unsupported_input_types, view_only_tags: md.descriptor.view_only_tags,
  import_text: (source) any^ => import_as(source, kind)^, export_text: export_text, check_roundtrip: check_roundtrip
}
pub let formats = [
  descriptor('rst', "reStructuredText", [".rst", ".rest"]),
  descriptor('org', "Org", [".org"]),
  descriptor('wiki', "MediaWiki", [".wiki", ".mediawiki"]),
  descriptor('asciidoc', "AsciiDoc", [".adoc", ".asciidoc", ".asc"]),
  descriptor('textile', "Textile", [".textile", ".txtl"])
]
