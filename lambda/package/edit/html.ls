// html.ls — HTML format adapter for lambda.edit
// (vibe/radiant/Radiant_Design_Edit_Mode.md §5 HTML).
//
// The parsed document is the envelope: the doctype, the <html> and <body>
// attributes, and the whole <head> (metadata, styles, resource references,
// scripts) stay as the parser's Mark tree and are written back as read. Only
// the body's flow content becomes the lambda.editor model. An element outside
// the editable profile keeps its parsed subtree in an atomic `raw_html` /
// `html_block` node, so a save writes it back untouched instead of flattening
// it (proposal §2). Scripts and event-handler attributes are data: the surface
// never projects the head, and model attributes reach the surface only where
// editing needs them (a link's address, an image's source).
//
// Round-trip contract (proposal §8): content, structure, and attributes
// survive; white space between blocks and inside running text normalizes as
// HTML renders it, and the body is re-indented. `check_roundtrip` is the gate
// the loader and Save both apply.

import .model
import lambda.editor.mod_doc
import lambda.editor.mod_step
import schemas: lambda.editor.mod_md_schema

// ---------------------------------------------------------------------------
// Profile
// ---------------------------------------------------------------------------

let mark_entry = {role: 'mark', content: [{role: 'inline', qty: 'star'}], marks: 'all'}

// The html5 subset plus the HTML marks it lacks and the atomic carriers of
// retained source.
pub let schema = {
  *: schemas.html5_subset_schema,
  s: mark_entry, del: mark_entry, sub: mark_entry, sup: mark_entry,
  raw_html:   {role: 'inline', content: [], marks: 'none', atomic: true, selectable: true,
               attrs: [{name: 'source', required: true}]},
  html_block: {role: 'block',  content: [], marks: 'none', atomic: true, selectable: true,
               attrs: [{name: 'source', required: true}]}
}

// Flow containers the editor opens, and the blocks that hold a run of text.
let flow_tags = ['div', 'section', 'article', 'main', 'header', 'footer', 'nav', 'aside', 'blockquote']
let text_block_tags = ['p', 'h1', 'h2', 'h3', 'h4', 'h5', 'h6']
let block_model_tags = ['p', 'h1', 'h2', 'h3', 'h4', 'h5', 'h6', 'div', 'section', 'article', 'main',
                        'header', 'footer', 'nav', 'aside', 'blockquote', 'ul', 'ol', 'pre', 'table',
                        'figure', 'hr', 'html_block']

// Marks, outermost first, as export nests them; code excludes the others.
let mark_order = ['strong', 'b', 'em', 'i', 'u', 's', 'del', 'sub', 'sup', 'span', 'code']

// Phrasing content (HTML §3.2.5.2.5) sits inside a run of text; comments,
// script-supporting and metadata elements may sit anywhere.
let phrasing_tags = ['a', 'abbr', 'area', 'audio', 'b', 'bdi', 'bdo', 'br', 'button', 'canvas',
  'cite', 'code', 'data', 'datalist', 'del', 'dfn', 'em', 'embed', 'i', 'iframe', 'img', 'input',
  'ins', 'kbd', 'label', 'map', 'mark', 'math', 'meter', 'object', 'output', 'picture', 'progress',
  'q', 'ruby', 's', 'samp', 'select', 'slot', 'small', 'span', 'strong', 'sub', 'sup', 'svg',
  'textarea', 'time', 'u', 'var', 'video', 'wbr']
let anywhere_tags = ['#comment', 'script', 'template', 'noscript', 'style', 'link', 'meta']

// A paragraph the source did not spell: a container's lone run of text. A
// space never occurs in a parsed attribute name (see node_marks_attr).
let implied_attr = 'edit implied'
// The attributes of <code> inside <pre>, or true for a bare <code>.
let code_attr = 'edit code'

// ---------------------------------------------------------------------------
// White space as HTML renders it
// ---------------------------------------------------------------------------

// HTML white space is the ASCII set; a no-break space is text.
fn space_char(ch) => ch == " " or ch == "\n" or ch == "\t" or ch == "\r"
fn lead_spaces(s, i) => if (i < len(s) and space_char(s[i])) lead_spaces(s, i + 1) else i
fn is_space_text(s) => lead_spaces(s, 0) == len(s)

fn to_spaces(s) => replace(replace(replace(s, "\n", " "), "\t", " "), "\r", " ")

// Runs of white space become one space.
fn collapse_spaces(s) {
  let t = to_spaces(s)
  let words = [for (w in split(t, " ") where w != "") w]
  let lead = if (starts_with(t, " ")) " " else ""
  let trail = if (ends_with(t, " ")) " " else ""
  if (len(words) == 0) (if (t == "") "" else " ") else lead ++ join(words, " ") ++ trail
}

fn settled(items, at_space) => {items: items, at_space: at_space}

// Settle a run's white space: a space after the run's start, a line break,
// or another space is not rendered, nor one before a break or at the end.
fn settle_run(items) => trim_run_end(settle_at(items, 0, true, []).items)

fn settle_at(items, i, at_space, acc) {
  if (i >= len(items)) settled(acc, at_space)
  else {
    let it = items[i]
    if (is_text(it)) {
      let c = collapse_spaces(it.text)
      let s = if (at_space and starts_with(c, " ")) slice(c, 1, len(c)) else c
      if (s == "") settle_at(items, i + 1, at_space, acc)
      else settle_at(items, i + 1, ends_with(s, " "), [*acc, text_marked(s, it.marks)])
    }
    else if (is_node(it) and it.tag == 'a') {
      let inner = settle_at(it.content, 0, at_space, [])
      settle_at(items, i + 1, inner.at_space, [*acc, with_content(it, inner.items)])
    }
    else if (is_node(it) and it.tag == 'br') settle_at(items, i + 1, true, [*trim_run_end(acc), it])
    else settle_at(items, i + 1, false, [*acc, it])
  }
}

fn trim_run_end(items) {
  let n = len(items)
  let tail_item = if (n > 0) items[n - 1] else null
  if (tail_item == null or not is_text(tail_item) or not ends_with(tail_item.text, " ")) items
  else {
    let s = slice(tail_item.text, 0, len(tail_item.text) - 1)
    if (s == "") trim_run_end(take(items, n - 1)) else [*take(items, n - 1), text_marked(s, tail_item.marks)]
  }
}

// ---------------------------------------------------------------------------
// Import: parsed Mark tree -> editor model
// ---------------------------------------------------------------------------

fn first_named(el, tag) {
  let found = [for (c in content(el) where type(c) == element and name(c) == tag) c]
  if (len(found) > 0) found[0] else null
}

// A Mark element's attributes as model attrs, in source order.
fn source_attrs(el) => [for (k, v at map(el)) {name: k, value: v}]

// A mark's value: its element's attributes, or true when it has none.
fn mark_value_of(el) {
  let attrs = source_attrs(el)
  if (len(attrs) == 0) true else attrs
}

fn child_kind(c) {
  if (type(c) == string) (if (is_space_text(c)) 'space' else 'inline')
  else if (type(c) != element) 'space'
  else if (member(anywhere_tags, name(c))) 'anywhere'
  else if (member(phrasing_tags, name(c))) 'inline'
  else 'block'
}

fn opaque_block(source) => node_attrs('html_block', [{name: 'source', value: source}], [])

fn with_marks_attr(attrs, marks) =>
  if (len(marks) == 0) attrs else [*attrs, {name: node_marks_attr, value: marks}]

// A retained inline element keeps the marks around it (node_marks_attr).
fn opaque_inline(el, marks) any => node_attrs('raw_html', with_marks_attr([{name: 'source', value: el}], marks), [])

fn inline_children(el, marks) => [for (c in content(el)) for (x in inline_items(c, marks)) x]

fn all_text(el) => all([for (c in content(el)) type(c) == string]) or false

fn inline_items(item, marks) {
  if (type(item) == string) { [text_marked(item, marks)] }
  else if (type(item) != element) { [] }
  else {
    let tag = name(item)
    // an empty mark element carries only its attributes (an icon <i class>),
    // and a named anchor links nowhere: both stay as written
    if (len(content(item)) == 0 and tag != 'img' and tag != 'br') { [opaque_inline(item, marks)] }
    else if (tag == 'code') {
      if (all_text(item)) [text_marked(plain_text(item), with_mark(marks, 'code', mark_value_of(item)))]
      else [opaque_inline(item, marks)]
    }
    else if (member(mark_order, tag)) { inline_children(item, with_mark(marks, tag, mark_value_of(item))) }
    else if (tag == 'a' and item.href != null) { [node_attrs('a', source_attrs(item), inline_children(item, marks))] }
    else if (tag == 'img' or tag == 'br') { [node_attrs(tag, with_marks_attr(source_attrs(item), marks), [])] }
    else { [opaque_inline(item, marks)] }
  }
}

// Children in block context: all block-level (the white space between them is
// layout), one run of inline content, or a mix the profile cannot hold.
fn flow_of(children) {
  let kinds = [for (c in children) child_kind(c)]
  if (not member(kinds, 'inline'))
    {kind: 'blocks', items: [for (c in children where child_kind(c) != 'space') for (b in block_nodes(c)) b]}
  else if (not member(kinds, 'block'))
    {kind: 'run', items: settle_run([for (c in children) for (x in inline_items(c, [])) x])}
  else {kind: 'mixed', items: []}
}

fn implied_p(items) => node_attrs('p', [{name: implied_attr, value: true}], items)

// A flow container's blocks, or null when the profile cannot hold its mix.
fn container_blocks(el) {
  let flow = flow_of([for (c in content(el)) c])
  if (flow.kind == 'blocks') flow.items
  else if (flow.kind == 'run') (if (len(flow.items) == 0) [] else [implied_p(flow.items)])
  else null
}

fn significant_children(el) => [for (c in content(el) where child_kind(c) != 'space') c]

fn all_present(xs) => len(xs) > 0 and (all([for (x in xs) x != null]) or false)

fn block_nodes(el) {
  if (type(el) != element) { [] }
  else {
    let tag = name(el)
    if (member(text_block_tags, tag)) { [node_attrs(tag, source_attrs(el), settle_run(inline_children(el, [])))] }
    else if (member(flow_tags, tag)) {
      let kids = container_blocks(el)
      // a quote holds at least one block (schema); an empty one stays as written
      if (kids == null or (tag == 'blockquote' and len(kids) == 0)) [opaque_block(el)]
      else [node_attrs(tag, source_attrs(el), kids)]
    }
    else if (tag == 'ul' or tag == 'ol') { list_nodes(el) }
    else if (tag == 'pre') { pre_nodes(el) }
    else if (tag == 'table') { table_nodes(el) }
    else if (tag == 'figure') { figure_nodes(el) }
    else if (tag == 'hr') { [node_attrs('hr', source_attrs(el), [])] }
    else { [opaque_block(el)] }
  }
}

fn list_nodes(el) {
  let items = significant_children(el)
  let lis = [for (c in items) if (type(c) == element and name(c) == 'li') li_node(c) else null]
  if (all_present(lis)) [node_attrs(name(el), source_attrs(el), lis)] else [opaque_block(el)]
}

// A list item holds runs of text and blocks in any order (schema: li).
fn li_node(li) {
  let kids = li_content([for (c in content(li)) c], 0, [], [])
  node_attrs('li', source_attrs(li), if (len(kids) == 0) [text("")] else kids)
}

fn li_content(children, i, run, acc) {
  if (i >= len(children)) [*acc, *settle_run(run)]
  else if (child_kind(children[i]) == 'block')
    li_content(children, i + 1, [], [*acc, *settle_run(run), *block_nodes(children[i])])
  else li_content(children, i + 1, [*run, *inline_items(children[i], [])], acc)
}

// Preformatted text is kept exactly; a <code> inside keeps its attributes.
fn pre_nodes(el) {
  let kids = [for (c in content(el)) c]
  if (all_text(el)) [node_attrs('pre', source_attrs(el), [text(plain_text(el))])]
  else if (len(kids) == 1 and type(kids[0]) == element and name(kids[0]) == 'code' and all_text(kids[0]))
    [node_attrs('pre', [*source_attrs(el), {name: code_attr, value: mark_value_of(kids[0])}],
                [text(plain_text(kids[0]))])]
  else [opaque_block(el)]
}

fn table_nodes(el) {
  let parts = [for (p in significant_children(el)) table_part(p)]
  if (all_present(parts)) [node_attrs('table', source_attrs(el), parts)] else [opaque_block(el)]
}

fn table_part(p) {
  if (type(p) != element) null
  else if (name(p) == 'tr') row_node(p)
  else if (name(p) == 'thead' or name(p) == 'tbody' or name(p) == 'tfoot') {
    let rows = [for (r in significant_children(p)) row_node(r)]
    if (all_present(rows)) node_attrs(name(p), source_attrs(p), rows) else null
  }
  else null
}

fn row_node(r) {
  if (type(r) != element or name(r) != 'tr') null
  else {
    let cells = [for (c in significant_children(r)) cell_node(c)]
    if (all_present(cells)) node_attrs('tr', source_attrs(r), cells) else null
  }
}

// A cell holds a run of text (schema: td/th); block content keeps the table as written.
fn cell_node(c) {
  if (type(c) != element or not (name(c) == 'td' or name(c) == 'th')) null
  else {
    let flow = flow_of([for (k in content(c)) k])
    if (flow.kind == 'run' or (flow.kind == 'blocks' and len(flow.items) == 0))
      node_attrs(name(c), source_attrs(c), flow.items)
    else null
  }
}

fn figure_nodes(el) {
  let parts = [for (k in significant_children(el)) figure_part(k)]
  if (all_present(parts)) [node_attrs('figure', source_attrs(el), parts)] else [opaque_block(el)]
}

fn figure_part(k) {
  if (type(k) == element and name(k) == 'img') node_attrs('img', source_attrs(k), [])
  else if (type(k) == element and name(k) == 'figcaption') {
    let flow = flow_of([for (c in content(k)) c])
    if (flow.kind == 'run') node_attrs('figcaption', source_attrs(k), flow.items) else null
  }
  else null
}

// A run kept as written, without the white space at its edges (not rendered
// next to blocks, and re-spelled by the export's indentation).
fn trimmed_run(run) {
  let n = len(run)
  let items = [for (i in 0 to n - 1) trim_run_item(run[i], i == 0, i == n - 1)]
  let kept = [for (x in items where not (type(x) == string and x == "")) x]
  if (len(kept) == 1) kept[0] else kept
}

fn trim_run_item(x, at_start, at_end) {
  if (type(x) != string) x
  else {
    let head = if (at_start) slice(x, lead_spaces(x, 0), len(x)) else x
    if (at_end) trim_end_spaces(head) else head
  }
}

fn trail_spaces(s, i) => if (i > 0 and space_char(s[i - 1])) trail_spaces(s, i - 1) else i
fn trim_end_spaces(s) => slice(s, 0, trail_spaces(s, len(s)))

// The document's blocks. A body mixing runs of text with blocks keeps each
// run as written, since the model's root holds blocks only.
fn body_blocks(body) {
  let children = [for (c in content(body)) c]
  let flow = flow_of(children)
  if (flow.kind == 'blocks') flow.items
  else if (flow.kind == 'run') (if (len(flow.items) == 0) [] else [implied_p(flow.items)])
  else mixed_blocks(children, 0, [], [])
}

fn mixed_blocks(children, i, run, acc) {
  if (i >= len(children)) close_mixed_run(run, acc)
  else if (child_kind(children[i]) == 'block')
    mixed_blocks(children, i + 1, [], [*close_mixed_run(run, acc), *block_nodes(children[i])])
  else mixed_blocks(children, i + 1, [*run, children[i]], acc)
}

fn close_mixed_run(run, acc) =>
  if (all([for (c in run) child_kind(c) == 'space']) or false) acc else [*acc, opaque_block(trimmed_run(run))]

// Whether the source spells the tag itself (the parser supplies missing ones).
fn has_tag(lower_source, tag) bool =>
  any([for (end in [">", " ", "\n", "\t"]) contains(lower_source, "<" ++ tag ++ end)]) or false

fn document_items(parsed, before) {
  let kids = [for (c in content(parsed)) c]
  let at = index_of_html(kids, 0)
  if (before) take(kids, at) else drop(kids, at + 1)
}

fn index_of_html(kids, i) =>
  if (i >= len(kids) or (type(kids[i]) == element and name(kids[i]) == 'html')) i else index_of_html(kids, i + 1)

// Parse HTML source into an editor document and the envelope kept beside it.
pub fn import_text(source) map^ {
  if (starts_with(trim(source), "<?xml"))
    raise error("XHTML documents cannot be edited yet; open it with 'lambda view' instead")
  else import_parsed(source, parse(source, 'html') ^ { raise error("the HTML source could not be parsed", ^) })
}

fn import_parsed(source, parsed) map {
  let html = first_named(parsed, 'html')
  let head = first_named(html, 'head')
  let body = first_named(html, 'body')
  let lower_source = lower(source)
  let document = has_tag(lower_source, "html") or has_tag(lower_source, "head") or has_tag(lower_source, "body")
  let envelope = {
    document: document,
    head_tag: has_tag(lower_source, "head") or len(significant_children(head)) > 0,
    before: document_items(parsed, true), after: document_items(parsed, false),
    html_attrs: source_attrs(html), head: head, body_attrs: source_attrs(body),
    between: [for (c in content(html) where child_kind(c) != 'space' and
                                           not (type(c) == element and (name(c) == 'head' or name(c) == 'body'))) c]
  }
  {doc: node('doc', body_blocks(body)), envelope: envelope}
}

// ---------------------------------------------------------------------------
// Export: editor model -> HTML text
// ---------------------------------------------------------------------------

fn escape_text(s) => replace(replace(replace(s, "&", "&amp;"), "<", "&lt;"), ">", "&gt;")
fn escape_attr(s) => replace(replace(s, "&", "&amp;"), "\"", "&quot;")

// Source attributes; the adapter's own bookkeeping names hold a space.
fn attrs_html(attrs) =>
  join([for (a in attrs where a.value != null and not contains(string(a.name), " "))
          " " ++ string(a.name) ++ "=\"" ++ escape_attr(string(a.value)) ++ "\""], "")

fn open_tag(tag, attrs) => "<" ++ string(tag) ++ attrs_html(attrs) ++ ">"
fn close_tag(tag) => "</" ++ string(tag) ++ ">"

// Retained source: an element (or comment) through the HTML formatter, a
// kept run item by item, text escaped.
fn retained_html(source) {
  if (type(source) == string) escape_text(source)
  else if (type(source) == array) join([for (x in source) retained_html(x)], "")
  else format([source], 'html') or ""
}

fn indent(depth) => join([for (i in 0 to depth - 1) "  "], "")

fn is_implied(n) => is_node(n) and n.tag == 'p' and attr_get(n, implied_attr) == true

fn blocks_html(blocks, depth) => join([for (b in blocks) "\n" ++ indent(depth) ++ block_html(b, depth)], "")

// A container's blocks go one per line; a lone implied paragraph is the
// container's own run of text again.
fn container_html(n, depth) {
  let kids = n.content
  if (len(kids) == 1 and is_implied(kids[0])) open_tag(n.tag, n.attrs) ++ inline_html(kids[0].content) ++ close_tag(n.tag)
  else if (len(kids) == 0) open_tag(n.tag, n.attrs) ++ close_tag(n.tag)
  else open_tag(n.tag, n.attrs) ++ blocks_html(kids, depth + 1) ++ "\n" ++ indent(depth) ++ close_tag(n.tag)
}

fn is_block_model(n) => (is_node(n) and member(block_model_tags, n.tag)) or false

// A list item's runs stay inline; its blocks go on their own lines.
fn li_html(n, depth) {
  let has_blocks = any([for (c in n.content) is_block_model(c)]) or false
  if (not has_blocks) open_tag('li', n.attrs) ++ inline_html(n.content) ++ close_tag('li')
  else open_tag('li', n.attrs) ++ li_parts_at(n.content, 0, [], depth, "") ++ "\n" ++ indent(depth) ++ close_tag('li')
}

fn li_parts_at(items, i, run, depth, acc) {
  if (i >= len(items)) acc ++ inline_html(run)
  else if (is_block_model(items[i]))
    li_parts_at(items, i + 1, [], depth, acc ++ inline_html(run) ++ "\n" ++ indent(depth + 1) ++ block_html(items[i], depth + 1))
  else li_parts_at(items, i + 1, [*run, items[i]], depth, acc)
}

fn pre_html(n) {
  let body = node_plain_text(n)
  let code = attr_get(n, code_attr)
  // the parser drops a newline right after <pre>, so a leading one is doubled
  let lead = if (code == null and starts_with(body, "\n")) "\n" else ""
  let inner = if (code == null) escape_text(body)
              else open_tag('code', if (type(code) == array) code else []) ++ escape_text(body) ++ close_tag('code')
  open_tag('pre', n.attrs) ++ lead ++ inner ++ close_tag('pre')
}

fn block_html(n, depth) {
  if (is_text(n)) "<p>" ++ escape_text(n.text) ++ "</p>"
  else if (not is_node(n)) ""
  else {
    let tag = n.tag
    if (tag == 'html_block') retained_html(attr_get(n, 'source'))
    else if (member(text_block_tags, tag) or tag == 'figcaption' or tag == 'td' or tag == 'th')
      open_tag(tag, n.attrs) ++ inline_html(n.content) ++ close_tag(tag)
    else if (tag == 'hr') open_tag(tag, n.attrs)
    else if (tag == 'pre') pre_html(n)
    else if (tag == 'li') li_html(n, depth)
    else if (tag == 'img') inline_html([n])
    else container_html(n, depth)
  }
}

fn wrap_mark(mark, value, kids) =>
  [open_tag(mark, if (type(value) == array) value else []) ++ join(kids, "") ++ close_tag(mark)]

fn inline_atom(item) {
  if (is_text(item)) [escape_text(item.text)]
  else if (not is_node(item)) []
  else if (item.tag == 'a') [open_tag('a', item.attrs) ++ inline_html(item.content) ++ close_tag('a')]
  else if (item.tag == 'img' or item.tag == 'br') [open_tag(item.tag, item.attrs)]
  else if (item.tag == 'raw_html') [retained_html(attr_get(item, 'source'))]
  // a mark node (commands can produce <strong> nodes) spells its mark
  else if (member(mark_order, item.tag)) [open_tag(item.tag, item.attrs) ++ inline_html(item.content) ++ close_tag(item.tag)]
  else [inline_html(item.content)]
}

fn inline_html(items) => join(nest_marks(items, mark_order, wrap_mark, inline_atom), "")

fn body_html(blocks, depth) =>
  if (len(blocks) == 1 and is_implied(blocks[0])) inline_html(blocks[0].content) else blocks_html(blocks, depth)

// Serialize an editor document with its envelope back to HTML source.
pub fn export_text(doc, envelope) {
  if (not envelope.document) {
    // a fragment is the head-level elements the parser moved into <head>,
    // then its body content
    let head_items = significant_children(envelope.head)
    let body = body_html(doc.content, 0)
    join([for (h in head_items) retained_html(h) ++ "\n"], "") ++
      (if (starts_with(body, "\n")) slice(body, 1, len(body)) else body) ++ "\n"
  }
  else {
    let before = join([for (x in envelope.before) retained_html(x) ++ "\n"], "")
    let head = if (envelope.head_tag) "\n" ++ retained_html(envelope.head) else ""
    let between = join([for (x in envelope.between) "\n" ++ retained_html(x)], "")
    let after = join([for (x in envelope.after) "\n" ++ retained_html(x)], "")
    before ++ open_tag('html', envelope.html_attrs) ++ head ++ between ++ "\n" ++
      open_tag('body', envelope.body_attrs) ++ body_html(doc.content, 1) ++ "\n" ++ close_tag('body') ++
      "\n" ++ close_tag('html') ++ after ++ "\n"
  }
}

// ---------------------------------------------------------------------------
// Normal form: what an HTML round trip may change
// ---------------------------------------------------------------------------

// Leaf cuts, attribute and mark order, an implied paragraph's spelling, and
// white space HTML does not render carry no meaning; preformatted text and
// retained source compare exactly.
fn norm_attrs(n) => [for (a in sorted_attrs(n) where a.name != implied_attr) a]

fn norm_inline(items) => merge_leaves(settle_run(merge_leaves(
  [for (it in flatten_mark_nodes(items, mark_order)) norm_inline_item(it)])))

fn norm_inline_item(it) {
  if (is_text(it)) text_marked(it.text, sorted_by_name(it.marks))
  else if (is_node(it) and it.tag == 'a') node_attrs('a', norm_attrs(it), norm_inline(it.content))
  else if (is_node(it)) node_attrs(it.tag, norm_attrs(it), it.content)
  else it
}

fn norm_node(n) {
  if (not is_node(n)) n
  else if (member(text_block_tags, n.tag) or n.tag == 'figcaption' or n.tag == 'td' or n.tag == 'th')
    node_attrs(n.tag, norm_attrs(n), norm_inline(n.content))
  else if (n.tag == 'li') node_attrs('li', norm_attrs(n), norm_li(n.content, 0, [], []))
  else if (n.tag == 'pre' or n.tag == 'html_block' or n.tag == 'hr' or n.tag == 'img') node_attrs(n.tag, norm_attrs(n), n.content)
  else node_attrs(n.tag, norm_attrs(n), [for (c in n.content) norm_node(c)])
}

fn norm_li(items, i, run, acc) {
  if (i >= len(items)) [*acc, *norm_inline(run)]
  else if (is_block_model(items[i])) norm_li(items, i + 1, [], [*acc, *norm_inline(run), norm_node(items[i])])
  else norm_li(items, i + 1, [*run, items[i]], acc)
}

pub fn normal_form(doc) any => node('doc', [for (b in doc.content) norm_node(b)])

fn envelope_key(e) => [e.document, e.head_tag, e.before, e.html_attrs, e.head, e.between, e.body_attrs, e.after]

// null when `doc` and its envelope survive export and re-import unchanged in
// normal form, else a sentence naming what would change.
pub fn check_roundtrip(doc, envelope) {
  let back = import_text(export_text(doc, envelope)) ^ { null }
  if (back == null) "the saved HTML could not be read back"
  else if (envelope_key(back.envelope) != envelope_key(envelope)) "the document head or attributes would change"
  else first_block_difference(normal_form(doc).content, normal_form(back.doc).content, 0)
}

// ---------------------------------------------------------------------------
// Descriptor
// ---------------------------------------------------------------------------

pub let descriptor = {
  id: 'html', name: "HTML", suffixes: [".html", ".htm"],
  surface: 'rich_text', schema: schema, schema_preset: 'html5_subset',
  unsupported_input_types: [], underline: true,
  import_text: import_text, export_text: export_text, check_roundtrip: check_roundtrip,
  toolbar: 'html'
}
