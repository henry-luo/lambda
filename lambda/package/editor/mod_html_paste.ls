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

fn html_children(n) {
  if (is_node(n)) { n.content }
  else if (type(n) == element) { [for (c in n) c] }
  else if (is_list(n)) { n }
  else { [] }
}

fn attr_value_at(attrs, key, i, n) {
  if (i >= n) { null }
  else if (attrs[i].name == key) { attrs[i].value }
  else { attr_value_at(attrs, key, i + 1, n) }
}
fn attr_value(n, key) =>
  if (is_node(n)) { attr_value_at(n.attrs, key, 0, len(n.attrs)) } else { null }

fn convert_children_at(kids, i, n, acc) {
  if (i >= n) { acc }
  else { convert_children_at(kids, i + 1, n, frag_concat(acc, html_to_editor_fragment(kids[i]))) }
}
fn convert_children(n) {
  let kids = html_children(n)
  convert_children_at(kids, 0, len(kids), [])
}

fn convert_inline(n) => coerce_children(md_schema, convert_children(n), 'inline')

fn list_item_blocks(n) {
  let blocks = coerce_children(md_schema, convert_children(n), 'block')
  if (len(blocks) == 0) { [node('paragraph', [])] }
  else if (blocks[0].tag == 'paragraph') { blocks }
  else { list_concat([node('paragraph', [])], blocks) }
}

pub fn html_to_editor_fragment(n) {
  if (type(n) == string) { if (len(n) == 0) { [] } else { [text(n)] } }
  else if (is_text(n)) { [n] }
  else if (is_list(n)) { convert_children_at(n, 0, len(n), []) }
  else {
    let tag = html_tag(n)
    if (tag == null) { [] }
    else if (tag == 'html' or tag == 'body' or tag == 'main' or tag == 'article' or tag == 'section') {
      convert_children(n)
    }
    else if (tag == 'p' or tag == 'div') { [node('paragraph', convert_inline(n))] }
    else if (tag == 'h1') { [node_attrs('heading', [{name: 'level', value: 1}], convert_inline(n))] }
    else if (tag == 'h2') { [node_attrs('heading', [{name: 'level', value: 2}], convert_inline(n))] }
    else if (tag == 'h3') { [node_attrs('heading', [{name: 'level', value: 3}], convert_inline(n))] }
    else if (tag == 'strong' or tag == 'b') { add_mark(convert_inline(n), 'strong') }
    else if (tag == 'em' or tag == 'i') { add_mark(convert_inline(n), 'em') }
    else if (tag == 'code') { add_mark(convert_inline(n), 'code') }
    else if (tag == 'br') { [node('hard_break', [])] }
    else if (tag == 'img') { [node_attrs('image', [{name: 'src', value: attr_value(n, 'src')}, {name: 'alt', value: attr_value(n, 'alt')}], [])] }
    else if (tag == 'ul') { [node_attrs('list', [{name: 'ordered', value: false}], convert_children(n))] }
    else if (tag == 'ol') { [node_attrs('list', [{name: 'ordered', value: true}], convert_children(n))] }
    else if (tag == 'li') { [node('list_item', list_item_blocks(n))] }
    else { convert_children(n) }
  }
}

pub fn html_fragment_to_md_blocks(fragment) =>
  coerce_for_md_block(html_to_editor_fragment(fragment))
