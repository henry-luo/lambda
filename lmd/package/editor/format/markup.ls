// Shared lightweight-markup adapter. A writer is admitted only when its
// round trip retains the original parsed document and the editable model.
import md: lambda.editor.format.markdown
import source_text: lambda.editor.format.source_text

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
// retain the complete authored .TH line and its preamble as the document header.
fn man_header(source: string) any^ {
  var i = 0
  while (i < len(source)) {
    let line = source_text.natural_line(source, i)^
    let text = source_text.raw(source, i, line.end)^
    if (starts_with(text, ".TH ") or starts_with(text, ".TH\t"))
      { return source_text.raw(source, 0, line.next)^ }
    if (trim(text) != "" and not starts_with(text, ".\\\""))
      { raise error("man preamble requires source mode") }
    i = int(line.next)^
  }
  raise error("man title header is missing")
}
fn man_body(written: string, header: string) any^ {
  let first = source_text.natural_line(written, 0)^
  let title = source_text.natural_line(written, first.next)^
  if (not starts_with(source_text.raw(written, 0, first.end)^, ".TH ") or
      not starts_with(source_text.raw(written, first.next, title.end)^, ".SH "))
    { raise error("man title cannot be retained") }
  let separator = if (ends_with(header, "\n") or ends_with(header, "\r")) "" else "\n"
  return header ++ separator ++ source_text.raw(written, title.next, len(written))^
}
fn from_model(doc, envelope) any^ {
  let markdown = md.export_text(doc, envelope.markdown)^
  let parsed = parse(markdown, 'markdown')^
  let written = format(parsed, envelope.kind)^
  if (envelope.kind == 'man') man_body(written, envelope.header)^ else written
}
fn import_as(source, kind) map^ {
  let converted = to_markdown(source, kind)^
  let loaded = md.import_text(converted.markdown)^
  let header = if (kind == 'man') man_header(source)^ else null
  let envelope = {kind: kind, markdown: loaded.envelope, source: source, original: loaded.doc, header: header}
  let written = from_model(loaded.doc, envelope)^
  let back = parse(written, kind)^
  if (back != converted.parsed and (not (kind in ['wiki', 'man']) or not same_wiki_document(converted.parsed, back)^)) {
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
  descriptor('textile', "Textile", [".textile", ".txtl"]),
  descriptor('man', "Man page", [".man", ".1", ".2", ".3", ".4", ".5", ".6", ".7", ".8", ".9", ".1m", ".3p"])
]
