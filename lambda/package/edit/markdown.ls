// markdown.ls — Markdown format adapter for lambda.edit
// (vibe/radiant/Radiant_Design_Edit_Mode.md §5).
//
// The editable model is the lambda.editor rich-text model over HTML tags (the
// html5_subset vocabulary the editor commands produce). Import maps the
// Markdown parser's Mark tree onto it; export rebuilds the Mark tree the
// Markdown formatter writes. Content the model does not edit but a save must
// keep — raw HTML, math, YAML front matter — is carried as opaque atomic
// nodes or in the envelope, never flattened to text.
//
// Round-trip contract (proposal §8): meaning, content, and supported metadata
// survive; source spelling may normalize. `normalize` states exactly what may
// differ, and `check_roundtrip` is the gate the loader and Save both apply.

import .model
import lambda.editor.mod_doc
import lambda.editor.mod_step
import schemas: lambda.editor.mod_md_schema

// ---------------------------------------------------------------------------
// Profile
// ---------------------------------------------------------------------------

// The html5 subset plus the atomic carriers of preserved source.
pub let schema = {
  *: schemas.html5_subset_schema,
  del:        {role: 'mark',   content: [{role: 'inline', qty: 'star'}], marks: 'all'},
  sup:        {role: 'mark',   content: [{role: 'inline', qty: 'star'}], marks: 'all'},
  raw_html:   {role: 'inline', content: [], marks: 'none', atomic: true, selectable: true,
               attrs: [{name: 'html', required: true, type: 'string'}]},
  math:       {role: 'inline', content: [], marks: 'none', atomic: true, selectable: true,
               attrs: [{name: 'tex', required: true, type: 'string'}]},
  html_block: {role: 'block',  content: [], marks: 'none', atomic: true, selectable: true,
               attrs: [{name: 'html', required: true, type: 'string'}]},
  math_block: {role: 'block',  content: [], marks: 'none', atomic: true, selectable: true,
               attrs: [{name: 'tex', required: true, type: 'string'}]}
}

// Commands Markdown cannot represent: underline and subscript have no Markdown
// spelling (the reader's ~x~ is strikethrough), so the profile omits them.
pub let unsupported_input_types = ["formatUnderline", "formatSubscript"]

// Element tags the importer understands. Anything else in the parsed tree is
// reported, and the file is not opened for editing (proposal §2).
let block_tags = ['p', 'h1', 'h2', 'h3', 'h4', 'h5', 'h6', 'blockquote', 'ul', 'ol', 'li',
                  'hr', 'table', 'thead', 'tbody', 'tfoot', 'tr', 'th', 'td', 'html-block']
let inline_tags = ['span', 'strong', 'b', 'em', 'i', 'del', 's', 'strike', 'sup',
                   'code', 'a', 'img', 'br', 'raw-html', 'math', 'input']

// ---------------------------------------------------------------------------
// Front matter envelope
// ---------------------------------------------------------------------------

fn fence_line(line) => trim(line) == "---" or trim(line) == "..."

fn closing_fence_at(lines, i) {
  if (i >= len(lines)) null
  else if (fence_line(lines[i])) i
  else closing_fence_at(lines, i + 1)
}

fn blank_lines_from(lines, i) {
  if (i < len(lines) - 1 and trim(lines[i]) == "") blank_lines_from(lines, i + 1) else i
}

// YAML front matter is kept verbatim — with the blank lines that separate it
// from the body — since the Markdown parser has no model for it and the editor
// never shows or changes it.
pub fn split_front_matter(text) {
  let lines = split(text, "\n")
  let close = if (len(lines) > 1 and trim(lines[0]) == "---") closing_fence_at(lines, 1) else null
  if (close == null) {front_matter: null, body: text}
  else {
    let body_at = blank_lines_from(lines, close + 1);
    {front_matter: join(take(lines, body_at), "\n") ++ "\n", body: join(drop(lines, body_at), "\n")}
  }
}

// ---------------------------------------------------------------------------
// Import: parser Mark tree -> editor model
// ---------------------------------------------------------------------------

fn unsupported_in(item) {
  if (type(item) != element) []
  else {
    let tag = name(item)
    let own = if (member(block_tags, tag) or member(inline_tags, tag)) [] else [tag]
    // math and code are leaves whose text is the source; html-dom is the
    // parser's projection of raw HTML that html-block/raw-html already keep
    if (tag == 'code' or tag == 'math' or tag == 'html-block' or tag == 'raw-html') own
    else [*own, *[for (c in content(item)) for (t in unsupported_in(c)) t]]
  }
}

fn doc_body(parsed) {
  let found = [for (c in content(parsed) where type(c) == element and name(c) == 'body') c]
  if (len(found) > 0) found[0] else parsed
}

fn inline_children(item, marks) => [for (c in content(item)) for (x in inline_items(c, marks)) x]

fn marked(item, marks, mark) any => inline_children(item, with_mark(marks, mark, true))

fn inline_items(item, marks) {
  if (type(item) == string) { [text_marked(item, marks)] }
  // the parser keeps a :name: emoji shortcode as a bare symbol
  else if (type(item) == symbol) { [text_marked(":" ++ string(item) ++ ":", marks)] }
  else if (type(item) != element) { [] }
  else {
    let tag = name(item)
    if (tag == 'span') { inline_children(item, marks) }
    else if (tag == 'strong' or tag == 'b') { marked(item, marks, 'strong') }
    else if (tag == 'em' or tag == 'i') { marked(item, marks, 'em') }
    else if (tag == 'del' or tag == 's' or tag == 'strike') { marked(item, marks, 'del') }
    else if (tag == 'sup') { marked(item, marks, 'sup') }
    else if (tag == 'code') { [text_marked(plain_text(item), with_mark(marks, 'code', true))] }
    else if (tag == 'a') {
      [node_attrs('a', attr_list([['href', item.href], ['title', item.title]]),
                  inline_children(item, marks))]
    }
    else if (tag == 'img') {
      [node_attrs('img', attr_list([['src', item.src], ['alt', item.alt], ['title', item.title]]), [])]
    }
    else if (tag == 'br') { [node('br', [])] }
    else if (tag == 'raw-html') { [node_attrs('raw_html', [{name: 'html', value: plain_text(item)}], [])] }
    else if (tag == 'math') { [node_attrs('math', [{name: 'tex', value: plain_text(item)}], [])] }
    else { [] }
  }
}

fn is_block_element(item) =>
  type(item) == element and (member(block_tags, name(item)) or
    (name(item) == 'code' and item.type == "block") or
    (name(item) == 'math' and item.type == "block"))

fn code_text(item) {
  let s = plain_text(item)
  if (ends_with(s, "\n")) slice(s, 0, len(s) - 1) else s
}

fn cell_node(cell) =>
  node_attrs(name(cell), attr_list([['align', cell.align]]), inline_children(cell, []))

fn row_node(row) => node('tr', [for (c in content(row) where type(c) == element) cell_node(c)])

fn table_part(part) {
  if (name(part) == 'tr') { row_node(part) }
  else { node(name(part), [for (r in content(part) where type(r) == element) row_node(r)]) }
}

fn li_node(li) {
  let task = li["data-checked"] != null
  // the task checkbox is spelled by the list marker, not kept as content
  let kids = [for (c in content(li) where not (type(c) == element and name(c) == 'input')) c]
  node_attrs('li', attr_list([['checked', if (task) li["data-checked"] == "true" else null]]),
             [for (c in kids)
                for (x in (if (is_block_element(c)) block_nodes(c) else inline_items(c, []))) x])
}

fn list_node(item) {
  let tag = name(item)
  let start_at = if (tag == 'ol' and item.start != null and item.start != "1") item.start else null
  node_attrs(tag, attr_list([['start', start_at], ['loose', if (item.loose == "true") true else null]]),
             [for (li in content(item) where type(li) == element and name(li) == 'li') li_node(li)])
}

fn block_nodes(item) {
  if (type(item) == string) { if (trim(item) == "") [] else [node('p', [text(item)])] }
  else if (type(item) != element) { [] }
  else {
    let tag = name(item)
    if (tag == 'p') { [node('p', inline_children(item, []))] }
    else if (is_heading_tag(tag)) { [node(tag, inline_children(item, []))] }
    else if (tag == 'blockquote') { [node('blockquote', blocks_of(item))] }
    else if (tag == 'ul' or tag == 'ol') { [list_node(item)] }
    else if (tag == 'code') {
      [node_attrs('pre', attr_list([['language', item.language]]), [text(code_text(item))])]
    }
    else if (tag == 'hr') { [node('hr', [])] }
    else if (tag == 'table') {
      [node('table', [for (p in content(item) where type(p) == element) table_part(p)])]
    }
    else if (tag == 'html-block') { [node_attrs('html_block', [{name: 'html', value: plain_text(item)}], [])] }
    else if (tag == 'math') { [node_attrs('math_block', [{name: 'tex', value: plain_text(item)}], [])] }
    else if (member(inline_tags, tag)) { [node('p', inline_items(item, []))] }
    else { [] }
  }
}

fn blocks_of(item) => [for (c in content(item)) for (b in block_nodes(c)) b]

// Parse Markdown source into an editor document, or raise a diagnostic that
// names the constructs the editor cannot keep.
pub fn import_text(source) map^ {
  let parts = split_front_matter(source)
  let parsed = parse(parts.body, 'markdown') ^ { raise error("the Markdown source could not be parsed", ^) }
  let body = doc_body(parsed)
  let unknown = unique([for (c in content(body)) for (t in unsupported_in(c)) t]) or []
  if (len(unknown) > 0) {
    raise error("the Markdown editor cannot keep " ++ join([for (t in unknown) "<" ++ string(t) ++ ">"], ", ") ++
                " content yet; open it with 'lambda view' instead")
  }
  else { {doc: node('doc', blocks_of(body)), envelope: {front_matter: parts.front_matter}} }
}

// ---------------------------------------------------------------------------
// Export: editor model -> Mark tree -> Markdown
// ---------------------------------------------------------------------------

// Marks nest in this order, so adjacent leaves sharing a mark share its element.
let mark_order = ['strong', 'em', 'del', 'sup', 'code']

fn wrap_mark(mark, value, kids) {
  if (mark == 'strong') [<strong *kids>]
  else if (mark == 'em') [<em *kids>]
  else if (mark == 'del') [<del *kids>]
  else if (mark == 'sup') [<sup *kids>]
  else if (mark == 'code') [<code type: "inline", *kids>]
  else kids
}

fn export_atom(item) {
  if (is_text(item)) { [item.text] }
  else if (not is_node(item)) { [] }
  else if (item.tag == 'a') {
    [<a href: attr_get(item, 'href'), title: attr_get(item, 'title'), *export_inline(item.content, 0)>]
  }
  else if (item.tag == 'img') {
    [<img src: attr_get(item, 'src'), alt: attr_get(item, 'alt'), title: attr_get(item, 'title')>]
  }
  else if (item.tag == 'br') { [<br>] }
  else if (item.tag == 'raw_html') { [<'raw-html' attr_get(item, 'html')>] }
  else if (item.tag == 'math') { [<math type: "inline", attr_get(item, 'tex')>] }
  // any other node (commands can produce mark nodes) keeps its content
  else { export_inline(item.content, 0) }
}

fn export_inline(items, level) => nest_marks(items, mark_order, wrap_mark, export_atom)

fn is_inline_model(n) =>
  is_text(n) or (is_node(n) and (n.tag == 'a' or n.tag == 'img' or n.tag == 'br' or
    n.tag == 'raw_html' or n.tag == 'math' or member(mark_order, n.tag))) or false

// A list item's content: inline runs are its text, blocks keep their shape.
fn export_item_content(items, i, n, run, acc) {
  if (i >= n) { [*acc, *export_inline(run, 0)] }
  else if (is_inline_model(items[i])) { export_item_content(items, i + 1, n, [*run, items[i]], acc) }
  else { export_item_content(items, i + 1, n, [], [*acc, *export_inline(run, 0), *export_block(items[i])]) }
}

fn export_li(li) {
  let checked = attr_get(li, 'checked')
  let kids = export_item_content(li.content, 0, len(li.content), [], [])
  if (checked == null) <li *kids>
  else <li ["data-checked"]: if (checked) "true" else "false", *kids>
}

fn loose_attr(lst) => if (attr_get(lst, 'loose') == true) "true" else null

fn export_cell(cell) {
  let align = attr_get(cell, 'align')
  if (cell.tag == 'th') <th align: align, *export_inline(cell.content, 0)>
  else <td align: align, *export_inline(cell.content, 0)>
}

fn export_row(row) => <tr *[for (c in row.content) export_cell(c)]>

fn export_table_part(part) {
  if (part.tag == 'tr') export_row(part)
  else if (part.tag == 'thead') <thead *[for (r in part.content) export_row(r)]>
  else if (part.tag == 'tfoot') <tfoot *[for (r in part.content) export_row(r)]>
  else <tbody *[for (r in part.content) export_row(r)]>
}

fn heading_mark(tag, kids) {
  if (tag == 'h1') <h1 level: "1", *kids>
  else if (tag == 'h2') <h2 level: "2", *kids>
  else if (tag == 'h3') <h3 level: "3", *kids>
  else if (tag == 'h4') <h4 level: "4", *kids>
  else if (tag == 'h5') <h5 level: "5", *kids>
  else <h6 level: "6", *kids>
}

fn export_block(n) {
  if (is_text(n)) { [<p n.text>] }
  else if (not is_node(n)) { [] }
  else {
    let tag = n.tag
    if (tag == 'p') { [<p *export_inline(n.content, 0)>] }
    else if (is_heading_tag(tag)) { [heading_mark(tag, export_inline(n.content, 0))] }
    else if (tag == 'blockquote') { [<blockquote *export_blocks(n.content)>] }
    else if (tag == 'ul') { [<ul loose: loose_attr(n), *[for (li in n.content) export_li(li)]>] }
    else if (tag == 'ol') {
      [<ol start: attr_get(n, 'start'), loose: loose_attr(n), *[for (li in n.content) export_li(li)]>]
    }
    else if (tag == 'pre' or tag == 'code_block') {
      [<code type: "block", language: attr_get(n, 'language'), node_plain_text(n)>]
    }
    else if (tag == 'hr') { [<hr>] }
    else if (tag == 'table') { [<table *[for (p in n.content) export_table_part(p)]>] }
    else if (tag == 'html_block') { [<'html-block' attr_get(n, 'html')>] }
    else if (tag == 'math_block') { [<math type: "block", attr_get(n, 'tex')>] }
    else if (is_inline_model(n)) { [<p *export_inline([n], 0)>] }
    // a container the profile does not name keeps its blocks
    else { export_blocks(n.content) }
  }
}

fn export_blocks(nodes) => [for (n in nodes) for (b in export_block(n)) b]

// Serialize an editor document (plus its envelope) to Markdown source.
pub fn export_text(doc, envelope) {
  let md = format(<doc <body *export_blocks(doc.content)>>, 'markdown')
  let front = if (envelope != null and envelope.front_matter != null) envelope.front_matter else ""
  front ++ md
}

// ---------------------------------------------------------------------------
// Normal form: what a Markdown round trip may change
// ---------------------------------------------------------------------------

// Markdown cannot hold leading/trailing blanks of a line, empty paragraphs,
// a trailing hard break, a tight item's paragraph wrapper, or a line break in
// a heading or table cell; attribute order and "start 1" carry no meaning.

fn norm_text(s, single_line) {
  let lines = split(s, "\n")
  let trimmed = [for (l in lines) trim(l)]
  if (single_line) join(trimmed, " ") else join(trimmed, "\n")
}

fn norm_inline_item(it) {
  if (is_text(it)) { text_marked(it.text, sorted_by_name(it.marks)) }
  else if (is_node(it) and member(mark_order, it.tag)) {
    // a mark node is its content with that mark applied
    it
  }
  else if (is_node(it)) { node_attrs(it.tag, sorted_attrs(it), norm_inline_seq(it.content, false)) }
  else { it }
}

fn drop_trailing_breaks(items) {
  if (len(items) > 0 and is_node(items[len(items) - 1]) and items[len(items) - 1].tag == 'br')
    drop_trailing_breaks(take(items, len(items) - 1))
  else items
}

fn trim_run_text(items, single_line) =>
  [for (it in items) if (is_text(it)) text_marked(norm_text(it.text, single_line), it.marks) else it]

fn norm_inline_seq(items, single_line) {
  let flat = flatten_mark_nodes(items, mark_order)
  let merged = merge_leaves([for (it in flat) norm_inline_item(it)])
  drop_trailing_breaks(merge_leaves(trim_run_text(merged, single_line)))
}

fn paragraph_unit(n) => is_inline_model(n) or (is_node(n) and n.tag == 'p')

fn adjacent_paragraphs(items, i, prev) {
  if (i >= len(items)) false
  else {
    let it = items[i]
    let unit = if (is_inline_model(it)) 'run' else if (is_node(it) and it.tag == 'p') 'p' else 'other'
    if ((unit == 'p' and (prev == 'run' or prev == 'p')) or (unit == 'run' and prev == 'p')) true
    else adjacent_paragraphs(items, i + 1, unit)
  }
}

fn list_is_loose(lst) =>
  attr_get(lst, 'loose') == true or any([for (li in lst.content) is_node(li) and adjacent_paragraphs(li.content, 0, 'none')])

// A tight item's paragraphs are its text; a loose item's text is a paragraph.
fn norm_li_content(items, loose) {
  let unwrapped = if (loose) items
                  else [for (it in items) for (x in (if (is_node(it) and it.tag == 'p') it.content else [it])) x]
  norm_mixed(unwrapped, loose, 0, len(unwrapped), [], [])
}

fn close_run(run, loose, acc) {
  let seq = norm_inline_seq(run, false)
  if (len(seq) == 0) acc
  else if (loose) [*acc, node('p', seq)]
  else [*acc, *seq]
}

fn norm_mixed(items, loose, i, n, run, acc) {
  if (i >= n) { close_run(run, loose, acc) }
  else if (is_inline_model(items[i])) { norm_mixed(items, loose, i + 1, n, [*run, items[i]], acc) }
  else { norm_mixed(items, loose, i + 1, n, [], [*close_run(run, loose, acc), *norm_block(items[i])]) }
}

fn norm_list(lst) {
  let loose = list_is_loose(lst)
  let start_at = attr_get(lst, 'start')
  let attrs = attr_list([['loose', if (loose) true else null],
                         ['start', if (start_at == null or start_at == "1") null else start_at]])
  node_attrs(lst.tag, attrs,
    [for (li in lst.content where is_node(li))
       node_attrs('li', sorted_attrs(li), norm_li_content(li.content, loose))])
}

fn norm_cell(c) any => node_attrs(c.tag, sorted_attrs(c), norm_inline_seq(c.content, true))

fn norm_table_part(p) {
  if (p.tag == 'tr') node('tr', [for (c in p.content) norm_cell(c)])
  else node(p.tag, [for (r in p.content) node('tr', [for (c in r.content) norm_cell(c)])])
}

fn norm_block(n) {
  if (is_text(n)) { norm_block(node('p', [n])) }
  else if (not is_node(n)) { [] }
  else {
    let tag = n.tag
    if (tag == 'p') {
      let seq = norm_inline_seq(n.content, false)
      if (len(seq) == 0) [] else [node('p', seq)]
    }
    else if (is_heading_tag(tag)) { [node(tag, norm_inline_seq(n.content, true))] }
    else if (tag == 'blockquote') { [node('blockquote', norm_blocks(n.content))] }
    else if (tag == 'ul' or tag == 'ol') { [norm_list(n)] }
    else if (tag == 'pre' or tag == 'code_block') {
      let code = node_plain_text(n)
      let body = if (ends_with(code, "\n")) slice(code, 0, len(code) - 1) else code
      let lang = attr_get(n, 'language');
      [node_attrs('pre', attr_list([['language', if (lang == "") null else lang]]), [text(body)])]
    }
    else if (tag == 'table') { [node('table', [for (p in n.content) norm_table_part(p)])] }
    else if (tag == 'hr' or tag == 'html_block' or tag == 'math_block') { [node_attrs(tag, sorted_attrs(n), [])] }
    else if (is_inline_model(n)) { norm_block(node('p', [n])) }
    else { norm_blocks(n.content) }
  }
}

fn norm_blocks(nodes) => [for (n in nodes) for (b in norm_block(n)) b]

pub fn normal_form(doc) any => node('doc', norm_blocks(doc.content))

// null when `doc` survives export and re-import unchanged in normal form,
// else a sentence naming the first block that would change.
pub fn check_roundtrip(doc, envelope) {
  let back = import_text(export_text(doc, envelope)) ^ { null }
  if (back == null) "the saved Markdown could not be read back"
  else {
    let a = normal_form(doc).content
    let b = normal_form(back.doc).content
    first_block_difference(a, b, 0)
  }
}

// ---------------------------------------------------------------------------
// Descriptor
// ---------------------------------------------------------------------------

pub let descriptor = {
  id: 'markdown', name: "Markdown", suffixes: [".md", ".markdown"],
  surface: 'rich_text', schema: schema, schema_preset: 'html5_subset',
  unsupported_input_types: unsupported_input_types,
  import_text: import_text, export_text: export_text, check_roundtrip: check_roundtrip,
  toolbar: 'markdown'
}
