// DOCX editing retains the captured package and patches only admitted body runs.
import common: lambda.doc.docx.common
import package: lambda.doc.docx.package
import model: lambda.doc.docx.model
import render: lambda.doc.docx.render
import writer: lambda.doc.docx
import xml: lambda.editor.format.xml
import html: lambda.editor.format.html
import archive: lambda.editor.format.package
import source: lambda.editor.format.source_text
import lambda.editor.core.doc
import lambda.editor.core.step

let options = {style_map:{}, include_empty_paragraphs:true, images:"data-uri",
  id_prefix:"", unsupported:"warn", presentation:"semantic"}
let word_uris = ["http://schemas.openxmlformats.org/wordprocessingml/2006/main",
  "http://purl.oclc.org/ooxml/wordprocessingml/main"]

fn local_name(name: string) => split(name, ":")[last]
fn element_children(node) => [for (entry in node.children where entry.node.kind == 'object') entry.node]
fn parsed_children(node) => [for (child in node.children where child is map and child.tag != null) child]
fn source_attr(node, key) => [for (entry in node.children where entry.name == "@" ++ key) entry.node.text][0]
fn align(raw, parsed) any^ => len(raw) == len(parsed) and
  all([for (i,item in raw) local_name(item.tag) == parsed[i].tag])
fn raw_body(root, parsed) any^ {
  let raw = element_children(root)
  let parts = parsed_children(parsed)
  if (not align(raw, parts)^) { raise error("DOCX source tree does not match parsed XML") }
  let indexes = [for (i,part in parts where part.uri == common.W and part.tag == "body") i]
  if (len(indexes) != 1) { raise error("DOCX body is missing or ambiguous") }
  return raw[indexes[0]]
}
fn word_namespace(root) any^ {
  let parts = split(root.tag, ":")^
  let key = if (len(parts) == 1) "xmlns" else "xmlns:" ++ parts[0]
  let uri = source_attr(root, key)
  if (not (uri in word_uris)) { raise error("DOCX Word namespace cannot be retained") }
  return uri
}
fn plain_attributes(node) any^ => len(node.attrs) == 0
fn supported_property(node) any^ => node.uri == common.W and
  node.tag in ["b", "i", "u", "strike", "vertAlign"] and
  len(parsed_children(node)^) == 0 and
  all([for (key,value in node.attrs) string(key) == common.W ++ "#val" and
    (if (node.tag == "vertAlign") value in ["superscript", "subscript"]
     else if (node.tag == "u") value in ["single", "1", "true", "on"]
     else value in ["1", "true", "on"])])
fn supported_run(node) any^ {
  if (node.uri != common.W or node.tag != "r" or not plain_attributes(node)^) { return false }
  return all([for (child in (parsed_children(node)^))
    child.uri == common.W and (if (child.tag == "rPr")
      plain_attributes(child)^ and all([for (property in (parsed_children(child)^)) supported_property(property)^])
    else if (child.tag == "t") all([for (key,value in child.attrs)
      string(key) == "http://www.w3.org/XML/1998/namespace#space"])
    else child.tag in ["tab", "br", "cr"] and plain_attributes(child)^)])
}
fn supported_paragraph(node) any^ {
  if (node.uri != common.W or node.tag != "p") { return false }
  let children = parsed_children(node)^
  return all([for (i,child in children)
    if (child.uri == common.W and child.tag == "pPr") i == 0 else supported_run(child)^])
}
let paragraph_tags = ['p', 'h1', 'h2', 'h3', 'h4', 'h5', 'h6']
fn direct_inline(item) any^ {
  if (item.kind == "break") { return node('br', []) }
  if (item.kind != "text") { return null }
  let marks = [for (entry in [{key:'b',name:'strong'}, {key:'i',name:'em'},
      {key:'u',name:'u'}, {key:'strike',name:'s'}] where item.marks[entry.key] == true)
      {name:entry.name, value:true}] ++
    (if (item.marks.vertAlign == "superscript") [{name:'sup',value:true}]
     else if (item.marks.vertAlign == "subscript") [{name:'sub',value:true}] else []);
  return text_marked(item.text, marks)
}
fn block_preview(pkg, block) any^ {
  let one = {*:pkg, diagnostics:[], body:{*:pkg.body, children:[block]}}
  let converted = model.build(one, options)^
  // The HTML importer consumes the serialized block preview.
  let result = render.render(converted, options, true)^
  let loaded = html.import_text(result.html)^
  let article = loaded.doc.content[0]
  let linear = len(converted.blocks) == 1 and converted.blocks[0].kind == "paragraph" and
    all([for (item in converted.blocks[0].children) item.kind in ["text", "break"] and
      (item.kind != "text" or (item.marks.dstrike != true and
        not contains(item.text, "\n") and not contains(item.text, "\r")))])
  let direct = if (linear and len(article.content) == 1)
    node_attrs(article.content[0].tag, article.content[0].attrs,
      [for (item in converted.blocks[0].children) direct_inline(item)^]) else null;
  {blocks:article.content, direct:direct, html:result.html,
    diagnostics:converted.diagnostics ++ result.diagnostics}
}
fn readonly_block(preview, label) => node_attrs('html_block', [{name:'source',
  value:if (len(preview.blocks) == 0) "<p>" ++ label ++ "</p>" else preview.html}], [])
fn anchored(node, index) any^ => node_attrs(node.tag,
  attrs_set(node.attrs, 'docx anchor', index), node.content)

// Comments and other unprojected source between children must not be rewritten.
fn clean_gaps(text, node) any^ {
  let children = element_children(node)
  let first = node.close_end
  let end_pos = node.close_at
  if (node.self_closing) false
  else all([for (i in 0 to len(children))
    trim(source.raw(text, if (i == 0) first else children[i - 1].last,
      if (i == len(children)) end_pos else children[i].first)) == ""])
}
fn clean_run(text, raw, parsed) any^ {
  let raw_children = element_children(raw)
  let resolved = parsed_children(parsed)^
  if (not align(raw_children, resolved)^ or not clean_gaps(text, raw)^) { return false }
  return all([for (i,child in resolved)
    if (child.tag == "rPr") clean_gaps(text, raw_children[i])^ and
      all([for (property in (element_children(raw_children[i])^))
        property.self_closing or clean_gaps(text, property)^])
    else if (child.tag == "t") {
      let part = raw_children[i]
      let contents = [for (entry in part.children where not (starts_with(entry.name, "@")^)) entry]
      len(contents) == 1 and contents[0].name == "#text" and not part.self_closing and
        contents[0].node.first == part.close_end and contents[0].node.last == part.close_at
    }
    else raw_children[i].self_closing or clean_gaps(text, raw_children[i])^])
}
fn clean_paragraph(text, raw, parsed) any^ {
  let children = element_children(raw)
  let resolved = parsed_children(parsed)^
  if (not align(children, resolved)^ or not clean_gaps(text, raw)^) { return false }
  return all([for (i,child in resolved)
    child.tag == "pPr" or clean_run(text, children[i], child)^])
}
fn paired_children(raw, parsed) any^ {
  let left = element_children(raw)
  let right = parsed_children(parsed)^
  if (not align(left, right)^) { raise error("DOCX table source is ambiguous") }
  return [for (i,item in right) {raw:left[i], parsed:item}]
}
fn table_cell(pkg, text, pair, row, column) map^ {
  let children = paired_children(pair.raw, pair.parsed)^
  if (any([for (item in children) item.parsed.uri != common.W or
      not (item.parsed.tag in ["tcPr", "p"])])) { raise error("unsupported DOCX cell content") }
  let properties = [for (item in children where item.parsed.tag == "tcPr") item.parsed]
  if (any([for (item in properties) len(common.descendants(item, "gridSpan", common.W)^) > 0 or
      len(common.descendants(item, "vMerge", common.W)^) > 0 or len(common.descendants(item, "hMerge", common.W)^) > 0]))
    { raise error("merged DOCX cells are retained read-only") }
  let paragraphs = [for (item in children where item.parsed.tag == "p") item]
  if (len(paragraphs) != 1) { raise error("multi-paragraph DOCX cells are retained read-only") }
  let paragraph = paragraphs[0]
  let preview = block_preview(pkg, paragraph.parsed)^
  if (not supported_paragraph(paragraph.parsed)^ or not clean_paragraph(text, paragraph.raw, paragraph.parsed)^ or
      len(preview.blocks) != 1 or preview.blocks[0].tag != 'p' or preview.direct == null or len(preview.diagnostics) != 0)
    { raise error("unsupported DOCX cell paragraph") }
  let cell = node('td', preview.direct.content);
  return {view:cell, raw:paragraph.raw, paragraph:preview.direct, path:[0,row,column]}
}
fn table_row(pkg, text, pair, row) map^ {
  let children = paired_children(pair.raw, pair.parsed)^
  if (any([for (item in children) item.parsed.uri != common.W or
      not (item.parsed.tag in ["trPr", "tc"])])) { raise error("unsupported DOCX row content") }
  let source_cells = [for (item in children where item.parsed.tag == "tc") item]
  if (len(source_cells) == 0) { raise error("DOCX table row has no cells") }
  let cells = [for (i,item in source_cells) table_cell(pkg, text, item, row, i)^];
  return {view:node('tr', [for (cell in cells) cell.view]), cells:cells}
}
fn table_model(pkg, text, raw, parsed) map^ {
  if (parsed.uri != common.W or parsed.tag != "tbl") { raise error("not a DOCX table") }
  let children = paired_children(raw, parsed)^
  if (any([for (item in children) item.parsed.uri != common.W or
      not (item.parsed.tag in ["tblPr", "tblGrid", "tr"])])) { raise error("unsupported DOCX table content") }
  let source_rows = [for (item in children where item.parsed.tag == "tr") item]
  if (len(source_rows) == 0) { raise error("DOCX table has no rows") }
  let rows = [for (i,item in source_rows) table_row(pkg, text, item, i)^];
  return {view:node('table', [node('tbody', [for (row in rows) row.view])]),
    cells:[for (row in rows) *row.cells]}
}

pub fn open_snapshot(snapshot) map^ {
  if (not (snapshot is binary)) { raise error("DOCX editor needs package bytes") }
  let captured = parse(snapshot, 'zip')^
  let pkg = package.open_package(captured)^
  let xml_text = input(pkg.parts[pkg.main], 'text')^
  let scanned = xml.import_text(xml_text)^
  let root = scanned.doc.tree.children[0].node
  let namespace = word_namespace(root)^
  let body = raw_body(root, pkg.root)^
  let raw = element_children(body)
  let parsed = parsed_children(pkg.body)
  let root_rels = package.relationships(pkg.parts, "", true)^
  let signed = any([for (rel in root_rels) contains(rel.type, "/digital-signature/")])
  if (not align(raw, parsed)^) { raise error("DOCX body does not match its source") }
  let candidates = [for (i,part in parsed where not (part.uri == common.W and part.tag == "sectPr"),
    let preview = block_preview(pkg, part)^,
    let table = if (not signed and part.uri == common.W and part.tag == "tbl")
      (table_model(pkg, xml_text, raw[i], part) ^ { null }) else null,
    let paragraph = not signed and supported_paragraph(part)^ and clean_paragraph(xml_text, raw[i], part)^ and len(preview.blocks) == 1 and
      preview.blocks[0].tag in paragraph_tags and len(preview.diagnostics) == 0 and preview.direct != null,
    let editable = paragraph or table != null)
    {raw:raw[i], parsed:part, editable:editable, table:table, deletable:paragraph and plain_attributes(part)^ and
      not any([for (child in parsed_children(part)^) child.tag == "pPr"]),
     view:if (paragraph) preview.direct else if (table != null) table.view else readonly_block(preview, "Unsupported DOCX content") }]
  let records = [for (i,record in candidates) {*:record, view:anchored(record.view, i)}]
  let endings = [for (i,part in parsed where part.uri == common.W and part.tag == "sectPr") raw[i].first]
  let body_tail = if (len(endings) > 0) endings[0] else body.close_at
  let doc = node('doc', if (len(records) == 0 and not signed) [node('p', [])]
    else [for (record in records) record.view]);
  return {doc:doc, envelope:{snapshot:snapshot, archive:captured, pkg:pkg, xml:xml_text,
    namespace:namespace, records:records, original:doc, signed:signed, body_tail:body_tail, body:body}}
}

fn writable_inline(node) any^ {
  if (is_text(node)) { return not contains(node.text, "\n") and not contains(node.text, "\r") and
    all([for (mark in node.marks)
      mark.name in ['strong', 'b', 'em', 'i', 'u', 's', 'del', 'sub', 'sup'] and mark.value == true]) }
  else if (not is_node(node)) { return false }
  else if (node.tag == 'br') { return len(node.attrs) == 0 and len(node.content) == 0 }
  else { return node.tag in ['strong', 'b', 'em', 'i', 'u', 's', 'del', 'sub', 'sup'] and
    len(node.attrs) == 0 and all([for (child in node.content) writable_inline(child)^]) }
}
fn writer_runs(block, namespace) any^ {
  let sample = html.import_text("<p></p>")^
  let fragment = html.export_text(node('doc', [block]), sample.envelope)^
  let written = writer.export_document(fragment, {unsupported:"error"})^
  let word = [for (item in (content(written.archive)^) where item.name == "word") item][0]
  let member = [for (item in (content(word)^) where item.name == "document.xml") item][0]
  let text = format(content(member)[0], 'xml')^
  let root = (xml.import_text(text)^).doc.tree.children[0].node
  let body = [for (child in (element_children(root)^) where (local_name(child.tag)^) == "body") child][0]
  let blocks = element_children(body)
  let paragraphs = [for (item in blocks where (local_name(item.tag)^) == "p") item]
  if (len(paragraphs) != 1) { raise error("DOCX edit must remain one paragraph") }
  let children = element_children(paragraphs[0])
  if (any([for (child in children) not (local_name(child.tag) in ["pPr", "r"])]))
    { raise error("DOCX edit needs unsupported package relationships") }
  return join([for (child in children where (local_name(child.tag)^) == "r") {
    let raw = source.raw(text, child.first, child.last)
    if (not starts_with(raw, "<w:r>")) { raise error("DOCX writer emitted an unsupported run") }
    "<w:r xmlns:w=\"" ++ namespace ++ "\">" ++ source.raw(raw, 5, len(raw))
  }], "")
}
fn changed_paragraph(record, block, envelope) map^ {
  if (block.tag != record.view.tag or block.attrs != record.view.attrs or
      not all([for (child in block.content) writable_inline(child)^]))
    { raise error("DOCX paragraph formatting or structure cannot be preserved") }
  let raw = record.raw
  let children = element_children(raw)
  let runs = [for (child in children where (local_name(child.tag)^) == "r") child]
  let at = if (len(runs) > 0) runs[0].first else
    if (len(children) > 0) children[last].last else raw.close_end
  let last_pos = if (len(runs) > 0) runs[last].last else at;
  return {first:at, last:last_pos, text:writer_runs(block, envelope.namespace)^}
}
fn table_valid(before, after) any^ {
  if (not is_node(after) or before.tag != after.tag or before.attrs != after.attrs) { return false }
  if (before.tag in ['td', 'th']) { return all([for (child in after.content) writable_inline(child)^]) }
  if (len(before.content) != len(after.content)) { return false }
  return all([for (i,child in before.content) table_valid(child, after.content[i])^])
}
pub fn validate_model(doc, envelope) any^ {
  if (doc.tag != 'doc') { return "DOCX document structure cannot be changed." }
  let anchors = [for (block in doc.content, let id = attrs_get(block.attrs, 'docx anchor')^ where id != null) id]
  if (any([for (i,id in anchors) not (id is int) or id < 0 or id >= len(envelope.records) or
      (i > 0 and anchors[i - 1] >= id)])) { return "DOCX source anchors cannot be moved or duplicated." }
  if (any([for (i,record in envelope.records where not record.deletable)
      not contains(anchors, i)])) { return "DOCX retained content is read-only." }
  let problems = [for (block in doc.content, let id = attrs_get(block.attrs, 'docx anchor'),
      let record = if (id == null) null else envelope.records[id])
    if (id == null and (block.tag != 'p' or len(block.attrs) != 0))
      "New DOCX content must be a simple paragraph."
    else if (record != null and not record.editable and block != record.view)
      "DOCX retained content is read-only."
    else if (record != null and record.table != null and not table_valid(record.view, block)^)
      "DOCX table structure is read-only."
    else if (record != null and record.table != null) null
    else if (record != null and record.editable and (block.tag != record.view.tag or block.attrs != record.view.attrs))
      "This DOCX paragraph change cannot be preserved."
    else if ((record == null or record.editable) and
      not all([for (child in block.content) writable_inline(child)^]))
      "This DOCX inline change cannot be preserved."
    else null];
  return [for (problem in problems where problem != null) problem][0]
}
fn written_paragraph(record, block, envelope) any^ {
  if (block == record.view) source.raw(envelope.xml, record.raw.first, record.raw.last)
  else {
    let change = changed_paragraph(record, block, envelope)^;
    source.raw(envelope.xml, record.raw.first, change.first) ++ change.text ++
      source.raw(envelope.xml, change.last, record.raw.last)
  }
}
fn written_table(record, block, envelope) any^ {
  if (block == record.view) { return source.raw(envelope.xml, record.raw.first, record.raw.last) }
  let cells = reverse(record.table.cells)
  var text = source.raw(envelope.xml, record.raw.first, record.raw.last), i = 0
  while ((i < len(cells))^) {
    let cell = cells[i]
    let current = node_at(block, cell.path)^
    if (current != cell.view) {
      let paragraph = node_attrs(cell.paragraph.tag, cell.paragraph.attrs, current.content)
      let change = changed_paragraph({raw:cell.raw, view:cell.paragraph}, paragraph, envelope)^
      let first = change.first - record.raw.first, last_pos = change.last - record.raw.first
      text = source.raw(text, 0, first) ++ change.text ++ source.raw(text, last_pos, len(text))
    }
    i = i + 1
  }
  return text
}
fn inserted_paragraph(block, namespace) any^ =>
  "<w:p xmlns:w=\"" ++ namespace ++ "\">" ++ writer_runs(block, namespace)^ ++ "</w:p>"
pub fn export_tree(doc, envelope) map^ {
  if (doc == envelope.original) { return {tree:envelope.archive, changed:false} }
  if (envelope.signed == true)
    { raise error("signed DOCX packages cannot be edited without invalidating signatures") }
  let problem = validate_model(doc, envelope)^
  if (problem != null) { raise error(problem) }
  var text = "", cursor = 0, previous = -1, i = 0
  while ((i < len(envelope.records))^) {
    let record = envelope.records[i]
    let matches = [for (j,block in doc.content where (attrs_get(block.attrs, 'docx anchor')^) == i) j]
    text = text ++ source.raw(envelope.xml, cursor, record.raw.first)
    if (len(matches) > 0) {
      let at = matches[0]
      let inserted = [for (j,block in doc.content where j > previous and j < at) inserted_paragraph(block, envelope.namespace)^]
      text = text ++ join(inserted, "") ++ (if (record.table != null)
        written_table(record, doc.content[at], envelope)^ else written_paragraph(record, doc.content[at], envelope)^)
      previous = at
    }
    cursor = record.raw.last
    i = i + 1
  }
  let trailing = [for (j,block in doc.content where j > previous) inserted_paragraph(block, envelope.namespace)^]
  text = text ++ source.raw(envelope.xml, cursor, envelope.body_tail) ++
    (if (envelope.body.self_closing) ">" else "") ++ join(trailing, "") ++
    (if (envelope.body.self_closing) "</" ++ envelope.body.tag ++ ">" ++
      source.raw(envelope.xml, envelope.body.close_end, len(envelope.xml))
     else source.raw(envelope.xml, envelope.body_tail, len(envelope.xml)))
  let checked = xml.import_text(text)^
  let tree = archive.replace_members(envelope.archive, map([(envelope.pkg.main, text)]), true)^;
  return {tree:tree, changed:true}
}
fn check_roundtrip(doc, envelope) any^ {
  let result = export_tree(doc, envelope) ^ { ^.message }
  if (result is string) result else null
}
fn anchored_schema(entry) => {*:entry, attrs:[*(entry.attrs or []), {name:'docx anchor', keep_on_split:false}]}
let schema = {*:html.schema, p:anchored_schema(html.schema.p), h1:anchored_schema(html.schema.h1),
  h2:anchored_schema(html.schema.h2), h3:anchored_schema(html.schema.h3),
  h4:anchored_schema(html.schema.h4), h5:anchored_schema(html.schema.h5), h6:anchored_schema(html.schema.h6)}
pub let descriptor = {*:html.descriptor, id:'docx', name:"DOCX", suffixes:[".docx"], schema:schema,
  binary:true, fallback_source:false, protect_view_only:true, import_text:open_snapshot,
  unsupported_input_types:["formatBlock", "insertUnorderedList", "insertOrderedList",
    "formatIndent", "formatOutdent", "formatBlockquote", "formatLiftBlockquote", "insertLink", "insertImage",
    "insertCodeBlock", "insertHorizontalRule", "insertTable", "insertTableRow", "deleteTableRow",
    "insertTableColumn", "deleteTableColumn", "modelToggleMark"],
  allowed_model_marks:['del'], validate_model:validate_model,
  export_tree:export_tree, check_roundtrip:check_roundtrip}
