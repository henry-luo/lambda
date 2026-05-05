// mod_html_paste.ls — convert parsed HTML-like fragments to editor nodes
// (Phase R5: HTML paste adapter before schema coercion).

import .mod_doc
import .mod_paste
import .mod_md_schema

fn is_list(v) => type(v) == array or type(v) == list

fn add_mark_to_text(n, mark) =>
  if (is_text(n)) { text_marked(n.text, [*n.marks, mark]) } else { n }

fn add_mark_at(nodes, mark, i, n, acc) {
  if (i >= n) { acc }
  else { add_mark_at(nodes, mark, i + 1, n, [*acc, add_mark_to_text(nodes[i], mark)]) }
}
fn add_mark(nodes, mark) => add_mark_at(nodes, mark, 0, len(nodes), [])

fn frag_concat_at(a, b, i, n, acc) {
  if (i >= n) { acc }
  else { frag_concat_at(a, b, i + 1, n, [*acc, b[i]]) }
}
fn frag_concat(a, b) => frag_concat_at(a, b, 0, len(b), a)

fn html_tag(n) {
  if (is_node(n)) { n.tag }
  else if (type(n) == element) { name(n) }
  else { null }
}

fn element_children_at(n, i, total, acc) {
  if (i >= total) { acc }
  else { element_children_at(n, i + 1, total, [*acc, n[i]]) }
}

fn html_children(n) {
  if (is_node(n)) { n.content }
  else if (type(n) == element) { element_children_at(n, 0, len(n), []) }
  else if (is_list(n)) { n }
  else { [] }
}

fn attr_value_at(attrs, key, i, n) {
  if (i >= n) { null }
  else if (attrs[i].name == key) { attrs[i].value }
  else { attr_value_at(attrs, key, i + 1, n) }
}
fn attr_value(n, key) =>
  if (is_node(n)) { attr_value_at(n.attrs, key, 0, len(n.attrs)) }
  else if (type(n) == element and key == 'href') { n.href }
  else if (type(n) == element and key == 'title') { n.title }
  else if (type(n) == element and key == 'src') { n.src }
  else if (type(n) == element and key == 'alt') { n.alt }
  else { null }

fn html_schema_mode(schema) => schema.p != null and schema.paragraph == null

fn paragraph_tag(schema) => if (html_schema_mode(schema)) { 'p' } else { 'paragraph' }
fn image_tag(schema) => if (html_schema_mode(schema)) { 'img' } else { 'image' }
fn link_tag(schema) => if (html_schema_mode(schema)) { 'a' } else { 'link' }
fn code_block_tag(schema) => if (html_schema_mode(schema)) { 'pre' } else { 'code_block' }
fn list_tag(schema, ordered) =>
  if (html_schema_mode(schema)) { if (ordered) { 'ol' } else { 'ul' } } else { 'list' }
fn list_item_tag(schema) => if (html_schema_mode(schema)) { 'li' } else { 'list_item' }

fn heading_node(schema, level, kids) =>
  if (html_schema_mode(schema) and level == 1) { node('h1', kids) }
  else if (html_schema_mode(schema) and level == 2) { node('h2', kids) }
  else if (html_schema_mode(schema) and level == 3) { node('h3', kids) }
  else { node_attrs('heading', [{name: 'level', value: level}], kids) }

fn image_node(schema, n) =>
  node_attrs(image_tag(schema), [{name: 'src', value: attr_value(n, 'src')}, {name: 'alt', value: attr_value(n, 'alt')}], [])

fn link_node(schema, n) =>
  node_attrs(link_tag(schema), [{name: 'href', value: attr_value(n, 'href')}, {name: 'title', value: attr_value(n, 'title')}],
    convert_inline_schema(schema, n))

fn blockquote_node(schema, n) => node('blockquote', coerce_children(schema, convert_children_schema(schema, n), 'block'))
fn code_block_node(schema, n) => node(code_block_tag(schema), [text(doc_text(node('fragment', convert_children_schema(schema, n))))])
fn table_node(schema, n) => node('table', normalize_table_children(schema, convert_children_schema(schema, n)))
fn table_section_node(schema, tag, n) => node(tag, convert_children_schema(schema, n))
fn table_row_node(schema, n) => node('tr', convert_children_schema(schema, n))
fn table_cell_node(schema, tag, n) => node(tag, convert_inline_schema(schema, n))

fn is_list_node(schema, n) {
  if (not is_node(n)) { false }
  else if (html_schema_mode(schema)) { n.tag == 'ul' or n.tag == 'ol' }
  else { n.tag == 'list' }
}

fn is_list_item_node(schema, n) => is_node(n) and n.tag == list_item_tag(schema)

fn list_item_from_loose(schema, n) {
  if (is_list_node(schema, n)) { node(list_item_tag(schema), [node(paragraph_tag(schema), []), n]) }
  else { node(list_item_tag(schema), list_item_blocks_schema(schema, node('fragment', [n]))) }
}

fn normalize_list_children_at(schema, kids, i, n, acc) {
  if (i >= n) { acc }
  else if (is_list_item_node(schema, kids[i])) { normalize_list_children_at(schema, kids, i + 1, n, [*acc, kids[i]]) }
  else { normalize_list_children_at(schema, kids, i + 1, n, [*acc, list_item_from_loose(schema, kids[i])]) }
}

fn normalize_list_children(schema, kids) => normalize_list_children_at(schema, kids, 0, len(kids), [])

fn has_table_section(kids, i, n) {
  if (i >= n) { false }
  else if (is_node(kids[i]) and (kids[i].tag == 'thead' or kids[i].tag == 'tbody' or kids[i].tag == 'tfoot')) { true }
  else { has_table_section(kids, i + 1, n) }
}

fn table_direct_rows(kids, i, n, acc) {
  if (i >= n) { acc }
  else if (is_node(kids[i]) and kids[i].tag == 'tr') { table_direct_rows(kids, i + 1, n, [*acc, kids[i]]) }
  else { table_direct_rows(kids, i + 1, n, acc) }
}

fn table_non_rows(kids, i, n, acc) {
  if (i >= n) { acc }
  else if (is_node(kids[i]) and kids[i].tag == 'tr') { table_non_rows(kids, i + 1, n, acc) }
  else { table_non_rows(kids, i + 1, n, [*acc, kids[i]]) }
}

fn normalize_table_children(schema, kids) {
  if (not html_schema_mode(schema) or has_table_section(kids, 0, len(kids))) { kids }
  else {
    let rows = table_direct_rows(kids, 0, len(kids), [])
    let rest = table_non_rows(kids, 0, len(kids), [])
    if (len(rows) == 0) { kids } else { [node('tbody', rows), *rest] }
  }
}

fn convert_children_at_schema(schema, kids, i, n, acc) {
  if (i >= n) { acc }
  else { convert_children_at_schema(schema, kids, i + 1, n, frag_concat(acc, html_to_editor_fragment_for_schema(schema, kids[i]))) }
}
fn convert_children_schema(schema, n) {
  let kids = html_children(n)
  convert_children_at_schema(schema, kids, 0, len(kids), [])
}

fn convert_children_at(kids, i, n, acc) => convert_children_at_schema(md_schema, kids, i, n, acc)
fn convert_children(n) => convert_children_schema(md_schema, n)

fn convert_inline_schema(schema, n) => coerce_children(schema, convert_children_schema(schema, n), 'inline')
fn convert_inline(n) => convert_inline_schema(md_schema, n)

fn list_item_blocks_schema(schema, n) {
  let blocks = coerce_children(schema, convert_children_schema(schema, n), 'block')
  let para = paragraph_tag(schema)
  if (len(blocks) == 0) { [node(para, [])] }
  else if (blocks[0].tag == para) { blocks }
  else { list_concat([node(para, [])], blocks) }
}
fn list_item_blocks(n) => list_item_blocks_schema(md_schema, n)

pub fn html_to_editor_fragment_for_schema(schema, n) {
  if (type(n) == string) { if (len(n) == 0) { [] } else { [text(n)] } }
  else if (is_text(n)) { [n] }
  else if (is_list(n)) { convert_children_at_schema(schema, n, 0, len(n), []) }
  else {
    let tag = html_tag(n)
    if (tag == null) { [] }
    else if (tag == 'html' or tag == 'body' or tag == 'main' or tag == 'article' or tag == 'section') {
      convert_children_schema(schema, n)
    }
    else if (tag == 'p' or tag == 'div') { [node(paragraph_tag(schema), convert_inline_schema(schema, n))] }
    else if (tag == 'h1') { [heading_node(schema, 1, convert_inline_schema(schema, n))] }
    else if (tag == 'h2') { [heading_node(schema, 2, convert_inline_schema(schema, n))] }
    else if (tag == 'h3') { [heading_node(schema, 3, convert_inline_schema(schema, n))] }
    else if (tag == 'strong' or tag == 'b') { add_mark(convert_inline_schema(schema, n), 'strong') }
    else if (tag == 'em' or tag == 'i') { add_mark(convert_inline_schema(schema, n), 'em') }
    else if (tag == 'code') { add_mark(convert_inline_schema(schema, n), 'code') }
    else if (tag == 'blockquote') { [blockquote_node(schema, n)] }
    else if (tag == 'pre') { [code_block_node(schema, n)] }
    else if (tag == 'hr') { [node('hr', [])] }
    else if (tag == 'a') { [link_node(schema, n)] }
    else if (tag == 'br') { [node(schema_hard_break(schema), [])] }
    else if (tag == 'img') { [image_node(schema, n)] }
    else if (tag == 'ul') { [node_attrs(list_tag(schema, false), [{name: 'ordered', value: false}], normalize_list_children(schema, convert_children_schema(schema, n)))] }
    else if (tag == 'ol') { [node_attrs(list_tag(schema, true), [{name: 'ordered', value: true}], normalize_list_children(schema, convert_children_schema(schema, n)))] }
    else if (tag == 'li') { [node(list_item_tag(schema), list_item_blocks_schema(schema, n))] }
    else if (tag == 'table' and schema.table != null) { [table_node(schema, n)] }
    else if ((tag == 'thead' or tag == 'tbody' or tag == 'tfoot') and schema[tag] != null) { [table_section_node(schema, tag, n)] }
    else if (tag == 'tr' and schema.tr != null) { [table_row_node(schema, n)] }
    else if ((tag == 'td' or tag == 'th') and schema[tag] != null) { [table_cell_node(schema, tag, n)] }
    else { convert_children_schema(schema, n) }
  }
}

pub fn html_to_editor_fragment(n) => html_to_editor_fragment_for_schema(md_schema, n)

pub fn html_fragment_to_md_blocks(fragment) =>
  coerce_for_md_block(html_to_editor_fragment(fragment))

pub fn html_fragment_to_blocks_for_schema(schema, fragment) =>
  coerce_children(schema, html_to_editor_fragment_for_schema(schema, fragment), 'block')
