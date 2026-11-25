// markdown.ls — Markdown format adapter for lambda.editor.app
// (vibe/radiant/Radiant_Design_Edit_Mode.md §5).
//
// The editable model is the lambda.editor rich-text model over HTML tags (the
// html5_subset vocabulary the editor commands produce). Import maps the
// Markdown parser's Mark tree onto it; export rebuilds the Mark tree the
// Markdown formatter writes. Content the model does not edit but a save must
// keep — raw HTML, math, YAML front matter — is carried as atomic nodes shown
// view-only (view.ls) or in the envelope, never flattened to text.
//
// A top-level block the model cannot hold, one it only
// shows (raw HTML, display math), or one that would not survive export and
// re-import, is kept as written: its source lines (parse's `sourcepos`) in an
// atomic `md_source` node, shown view-only and saved byte for byte. So are
// source lines no block claims, such as link reference definitions.
//
// Round-trip contract (proposal §8): meaning, content, and supported metadata
// survive; source spelling of edited blocks may normalize. `normalize` states
// exactly what may differ, and `check_roundtrip` is the gate the loader and
// Save both apply.

import lambda.editor.format.model
import lambda.editor.format.view
import lambda.editor.core.doc
import lambda.editor.core.step
import schemas: lambda.editor.schema.markdown
import html: lambda.editor.format.html

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
  emoji:      {role: 'inline', content: [], marks: 'none', atomic: true, selectable: true,
               attrs: [{name: 'name', required: true, type: 'string'}]},
  footnote_ref: {role: 'inline', content: [], marks: 'none', atomic: true, selectable: true,
                 attrs: [{name: 'label', required: true, type: 'string'}]},
  md_footnote: {role: 'block', content: [{role: 'block', qty: 'plus'}], marks: 'none',
                attrs: [{name: 'label', required: true, type: 'string'}]},
  html_block: {role: 'block',  content: [], marks: 'none', atomic: true, selectable: true,
               attrs: [{name: 'html', required: true, type: 'string'}]},
  md_html_group: {role: 'block', content: [{role: 'block', qty: 'plus'}], marks: 'none'},
  math_block: {role: 'block',  content: [], marks: 'none', atomic: true, selectable: true,
               attrs: [{name: 'tex', required: true, type: 'string'}]},
  md_source:  {role: 'block',  content: [], marks: 'none', atomic: true, selectable: true,
               attrs: [{name: 'markdown', required: true, type: 'string'}]}
}

// Commands Markdown cannot represent: underline and subscript have no Markdown
// spelling (the reader's ~x~ is strikethrough), so the profile omits them.
pub let unsupported_input_types = ["formatUnderline", "formatSubscript"]

// Element tags the importer understands. A top-level block holding anything
// else is kept as written (proposal §2).
let block_tags = ['p', 'h1', 'h2', 'h3', 'h4', 'h5', 'h6', 'blockquote', 'ul', 'ol', 'li',
                  'hr', 'table', 'thead', 'tbody', 'tfoot', 'tr', 'th', 'td', 'html-block']
let inline_tags = ['span', 'strong', 'b', 'em', 'i', 'del', 's', 'strike', 'sup',
                   'code', 'a', 'img', 'br', 'raw-html', 'math', 'input', 'footnote-ref']

// ---------------------------------------------------------------------------
// Front matter envelope
// ---------------------------------------------------------------------------

fn fence_line(line) => trim(line) == "---" or trim(line) == "..."

fn closing_fence_at(lines, i) any^ {
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
pub fn split_front_matter(text) any^ {
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

fn unsupported_in(item) any^ {
  if (type(item) != element) []
  else {
    let tag = name(item)
    let own = if (member(block_tags, tag) or member(inline_tags, tag)) [] else [tag]
    // math and code are leaves whose text is the source; html-dom is the
    // parser's projection of raw HTML that html-block/raw-html already keep
    if (tag == 'code' or tag == 'math' or tag == 'html-block' or tag == 'raw-html' or tag == 'footnote-ref') own
    else [*own, *[for (c in ((content(item)) ^ { [] })) for (t in ((unsupported_in(c)) ^ { [] })) t]]
  }
}

fn doc_body(parsed) {
  let found = [for (c in ((content(parsed)) ^ { [] }) where ((type(c) == element and name(c) == 'body') ^ { false })) c]
  if (len(found) > 0) found[0] else parsed
}

fn is_footnote_ref(item) => is_node(item) and item.tag == 'footnote_ref'
fn inline_children(item, marks) any^ => anchor_atomic_edges(
  [for (c in ((content(item)) ^ { [] })) for (x in ((inline_items(c, marks)) ^ { [] })) x], is_footnote_ref)^

// Raw HTML and math keep their source text and a view-only rendering (view.ls).
fn kept_atom(tag, key, text, shown) any^ => node_attrs(tag, attr_list([[key, text], [view_attr, shown]]), [])

fn marked(item, marks, mark) any => inline_children(item, with_mark(marks, mark, true))

// Markdown has no image dimensions; a simple HTML image is its portable spelling.
fn html_fragment_items(source) any^ {
  let parsed = parse("<body>" ++ source ++ "</body>", 'html') ^ { null }
  let html = if (parsed == null) null else [for (c in content(parsed)^ where (type(c) == element and name(c) == 'html')^) c][0]
  let body = if (html == null) null else [for (c in content(html)^ where (type(c) == element and name(c) == 'body')^) c][0]
  if (body == null) [] else [for (c in content(body)^ where (type(c) != string or trim(c) != "")^) c]
}

fn sized_html_image(source) any^ {
  let items = html_fragment_items(source)^
  let img = if (len(items) == 1 and type(items[0]) == element and name(items[0]) == 'img') items[0] else null
  let extra = if (img == null) [] else [for (k, v at map(img) where not member(['src','alt','title','width','height'], k)) k]
  if (img == null or img.src == null or len(extra) > 0 or (img.width == null and img.height == null)) null
  else node_attrs('img', attr_list([['src', img.src], ['alt', img.alt], ['title', img.title],
                                   ['width', if (img.width == null) null else int(img.width)],
                                   ['height', if (img.height == null) null else int(img.height)]]), [])
}

fn image_html_paragraph(para) any^ {
  if (type(para) != element or name(para) != 'p') { return null }
  let children = if (para == null) [] else [for (c in content(para)^ where (type(c) != string or trim(c) != "")^) c]
  if (len(children) == 0 or not all([for (c in children) type(c) == element and name(c) == 'img'])) null
  else {
    let images = [for (c in children) node_attrs('img', [for (k, v at map(c)) {name:k,value:v}], [])]
    let attrs = [for (k, v at map(para)) {name:k,value:v}]
    node_attrs('p', [*attrs, {name:'edit html attrs',value:attrs}, {name:'edit source',value:html.retained_html(para)},
                     {name:'edit original',value:images}], images)
  }
}

fn image_html_block(source) any^ {
  let items = html_fragment_items(source)^
  let groups = [for (item in items) image_html_paragraph(item) ^ { null }]
  if (not any([for (group in groups) group != null])) null
  else {
    let blocks = [for (i, item in items) if (groups[i] != null) groups[i] else
      kept_atom('html_block', 'html', html.retained_html(item), html_view([item]))]
    node_attrs('md_html_group', [{name:'edit source',value:source}, {name:'edit original',value:blocks}], blocks)
  }
}

// an emoji atom retains the distinction from literal or escaped shortcode text on save.
fn emoji_atom(item, alias, marks) any^ => node_attrs('emoji',
  attr_list([['name', alias], [view_attr, html_view(format([item], 'html'))],
             [node_marks_attr, if (len(marks) > 0) marks else null]]), [])

fn inline_items(item, marks) any^ {
  if (type(item) == string) { [text_marked(item, marks)] }
  else if (type(item) == symbol) { [emoji_atom(item, string(item), marks)] }
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
      if (item["data-emoji"] != null) { [emoji_atom(item, item["data-emoji"], marks)] }
      else { [node_attrs('img', attr_list([['src', item.src], ['alt', item.alt], ['title', item.title]]), [])] }
    }
    else if (tag == 'br') { [node('br', [])] }
    else if (tag == 'footnote-ref') {
      [node_attrs('footnote_ref', attr_list([['label', item.ref], [view_attr, markdown_view(item)],
        [node_marks_attr, if (len(marks) > 0) marks else null]]), [])]
    }
    else if (tag == 'raw-html') {
      let image = sized_html_image(plain_text(item)) ^ { null };
      [if (image == null) kept_atom('raw_html', 'html', plain_text(item), html_view(plain_text(item))) else image]
    }
    else if (tag == 'math') { [kept_atom('math', 'tex', plain_text(item), math_view(plain_text(item), false))] }
    else { [] }
  }
}

fn is_block_element(item) any^ =>
  type(item) == element and (member(block_tags, name(item)) or
    (name(item) == 'code' and item.type == "block") or
    (name(item) == 'math' and item.type == "block"))

fn code_text(item) any^ {
  let s = plain_text(item)
  if (ends_with(s, "\n")) slice(s, 0, len(s) - 1) else s
}

fn cell_node(cell) any^ =>
  node_attrs(name(cell), attr_list([['align', cell.align]]), inline_children(cell, []))

fn row_node(row) => node('tr', [for (c in ((content(row)) ^ { [] }) where type(c) == element) cell_node(c)])

fn table_part(part) {
  if (name(part) == 'tr') { row_node(part) }
  else { node(name(part), [for (r in ((content(part)) ^ { [] }) where type(r) == element) row_node(r)]) }
}

fn li_node(li) any^ {
  let task = li["data-checked"] != null
  // the task checkbox is spelled by the list marker, not kept as content
  let kids = [for (c in ((content(li)) ^ { [] }) where ((not (type(c) == element and name(c) == 'input')) ^ { false })) c]
  node_attrs('li', attr_list([['checked', if (task) li["data-checked"] == "true" else null]]),
             [for (c in kids)
                for (x in (((if (is_block_element(c)) block_nodes(c) else inline_items(c, []))) ^ { [] })) x])
}

fn list_node(item) any^ {
  let tag = name(item)
  let start_at = if (tag == 'ol' and item.start != null and item.start != "1") item.start else null
  node_attrs(tag, attr_list([['start', start_at], ['loose', if (item.loose == "true") true else null]]),
             [for (li in ((content(item)) ^ { [] }) where ((type(li) == element and name(li) == 'li') ^ { false })) li_node(li)])
}

fn block_nodes(item) any^ {
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
      [node('table', [for (p in ((content(item)) ^ { [] }) where type(p) == element) table_part(p)])]
    }
    else if (tag == 'html-block') {
      let source = plain_text(item)
      let group = image_html_block(source) ^ { null }
      let image = sized_html_image(source) ^ { null };
      [if (group != null) group else if (image != null) node('p', [image])
       else kept_atom('html_block', 'html', source, html_view(source))]
    }
    else if (tag == 'math') { [kept_atom('math_block', 'tex', plain_text(item), math_view(plain_text(item), true))] }
    else if (member(inline_tags, tag)) { [node('p', inline_items(item, []))] }
    else { [] }
  }
}

fn blocks_of(item) => [for (c in ((content(item)) ^ { [] })) for (b in ((block_nodes(c)) ^ { [] })) b]

// ---------------------------------------------------------------------------
// Blocks kept as written
// ---------------------------------------------------------------------------

// Source lines kept byte for byte, with their view-only rendering (none for
// lines no block claims).
fn kept_block(text, shown) any^ =>
  node_attrs('md_source', attr_list([['markdown', text], [view_attr, shown]]), [])

fn is_kept(n) => (is_node(n) and n.tag == 'md_source') or false

// A block's source lines from parse's `sourcepos` ("L:C-L:C", 1-based).
fn source_range(item) {
  let pos = item.sourcepos
  if (pos == null) null
  else {
    let ends = split(string(pos), "-");
    {first: int(split(ends[0], ":")[0]), last: int(split(ends[1], ":")[0])}
  }
}

fn blank(line) => trim(line) == ""

fn first_filled(lines, i, stop) any^ => if (i >= stop) null else if (blank(lines[i])) first_filled(lines, i + 1, stop) else i
fn last_filled(lines, i, stop) any^ => if (stop <= i) null else if (blank(lines[stop - 1])) last_filled(lines, i, stop - 1) else stop - 1

// lines[i .. stop) without the blank lines around them, or null when blank.
fn filled_text(lines, i, stop) any^ {
  let first = first_filled(lines, i, stop)
  if (first == null) null
  else join(take(drop(lines, first), last_filled(lines, first, stop) - first + 1), "\n")
}

fn footnote_labels(item) any^ =>
  if (not is_node(item)) []
  else if (item.tag == 'footnote_ref') [attr_get(item, 'label')]
  else [for (child in item.content) for (label in footnote_labels(child)^) label]

// A block needs temporary definitions to reparse its footnote references.
fn survives(nodes) any^ {
  let labels = unique([for (item in nodes) for (label in footnote_labels(item)^) label])
  let definitions = join([for (label in labels) "\n\n[^" ++ label ++ "]: note"], "")
  let back = parse(format(<doc <body *export_blocks(nodes)>>, 'markdown') ++ definitions, 'markdown') ^ { null };
  (back != null and norm_blocks(nodes) == norm_blocks(blocks_of(doc_body(back)))) or false
}

// A block the editor only shows — raw HTML, display math — is kept as
// written rather than as an atom the formatter would respell.
fn shown_only(item) => (name(item) == 'html-block' and (sized_html_image(plain_text(item)) ^ { null }) == null and
  (image_html_block(plain_text(item)) ^ { null }) == null) or
  name(item) == 'footnote' or (name(item) == 'math' and item.type == "block")

fn contains_parsed_tag(item, tag) any^ => type(item) == element and
  (name(item) == tag or any([for (child in content(item)^) contains_parsed_tag(child, tag)^]))

fn parsed_footnotes(parsed) any^ {
  let footer = if (parsed == null) null else parsed?<footnotes>
  // the parser omits the footer without definitions; absence is not an invalid element.
  if (footer == null) [] else [for (note in content(footer)^
    where ((type(note) == element and name(note) == 'footnote') ^ { false })) note]
}

// Definitions retain their original spelling until their own content changes.
fn footnote_block(item, source) any^ {
  let children = content(item)^
  let unsupported = any([for (child in children) len(unsupported_in(child)^) > 0 or contains_parsed_tag(child, 'raw-html')^])
  let blocks = if (unsupported) [] else blocks_of(item)
  let label = string(item.label)
  let written = if (len(blocks) == 0) null else
    format(<doc <body <footnote label: label, *export_blocks(blocks)>>>, 'markdown')^
  let labels = unique([for (block in blocks) for (ref in footnote_labels(block)^) ref])
  let definitions = join([for (ref in labels where ((ref != label) ^ { false })) "\n\n[^" ++ ref ++ "]: note"], "")
  let parsed = if (written == null) null else (parse(written ++ definitions, 'markdown') ^ { null })
  let notes = [for (note in parsed_footnotes(parsed)^ where ((note.label == label) ^ { false })) note]
  if (len(notes) != 1 or norm_blocks(blocks) != norm_blocks(blocks_of(notes[0])))
    kept_block(source, markdown_view(item))
  else node_attrs('md_footnote', attr_list([['label', label], ['edit source', source], ['edit original', blocks]]), blocks)
}

fn item_blocks(item, lines, span, has_notes) any^ {
  let source = join(take(drop(lines, span.first - 1), span.last - span.first + 1), "\n")
  if (name(item) == 'footnote') { return [footnote_block(item, source)^] }
  // Definitions move to a generated footer; keep a container that declared any as source.
  let declared_notes = if (has_notes) {
    (parse(source, 'markdown') ^ { null })?<footnotes>
  } else null
  // Paired raw HTML needs its complete block to render as authored.
  let mixed_raw = contains_parsed_tag(item, 'footnote-ref')^ and contains_parsed_tag(item, 'raw-html')^
  let nodes = if (len(unsupported_in(item)) > 0 or shown_only(item) or declared_notes != null or mixed_raw)
    null else block_nodes(item)
  if (nodes != null and survives(nodes)) nodes
  else [kept_block(source, markdown_view(item))]
}

// Top-level blocks in source order; `from` is the first line no block claimed.
fn import_items(items, lines, i, from, acc, has_notes) any^ {
  if (i >= len(items)) {
    let tail = filled_text(lines, from, len(lines));
    if (tail == null) acc else [*acc, kept_block(tail, null)]
  }
  else {
    let span = source_range(items[i])
    let gap = filled_text(lines, from, span.first - 1)
    let kept_gap = if (gap == null) [] else [kept_block(gap, null)]
    import_items(items, lines, i + 1, span.last,
      [*acc, *kept_gap, *item_blocks(items[i], lines, span, has_notes)], has_notes)
  }
}

// Parse Markdown source into an editor document.
pub fn import_text(source) map^ {
  let parts = split_front_matter(source)
  let parsed = parse(parts.body, {type: 'markdown', sourcepos: true}) ^ {
    raise error("the Markdown source could not be parsed", ^)
  }
  // Definitions are in a generated footer, but their spans identify authored order.
  let all_notes = parsed_footnotes(parsed)^
  let has_notes = len(all_notes) > 0
  let body = [for (c in ((content(doc_body(parsed))) ^ { [] }) where ((type(c) == element and name(c) != 'footnotes') ^ { false })) c]
  let notes = [for (c in all_notes where ((c.sourcepos != null) ^ { false })) c]
  // A definition inside a container is already retained with that container.
  let separate = [for (note in notes, let n = source_range(note)
    where (not (any([for (item in body, let p = source_range(item))
      p != null and p.first <= n.first and p.last >= n.last]) or false))) note]
  let items = sort([*body, *separate], {by: (item) => source_range(item).first})
  // without its lines a block could be neither kept as written nor checked
  if (any([for (it in items) it.sourcepos == null]) or false) {
    raise error("the Markdown parser did not report where each block is")
  }
  else {
    let blocks = import_items(items, split(parts.body, "\n"), 0, 0, [], has_notes);
    {doc: node('doc', blocks), envelope: {front_matter: parts.front_matter}}
  }
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

fn export_atom(item) any^ {
  if (is_text(item)) { [item.text] }
  else if (not is_node(item)) { [] }
  else if (item.tag == 'a') {
    [<a href: attr_get(item, 'href'), title: attr_get(item, 'title'), *export_inline(item.content, 0)>]
  }
  else if (item.tag == 'img') {
    let width = attr_get(item, 'width')
    let height = attr_get(item, 'height')
    if (width == null and height == null) [<img src: attr_get(item, 'src'), alt: attr_get(item, 'alt'), title: attr_get(item, 'title')>]
    else {
      let attrs = [['src', attr_get(item, 'src')], ['alt', attr_get(item, 'alt')], ['title', attr_get(item, 'title')],
                   ['width', width], ['height', height]]
      let raw = "<img" ++ join([for (pair in attrs where pair[1] != null)
        " " ++ string(pair[0]) ++ "=\"" ++ escape_attr(string(pair[1])) ++ "\""], "") ++ ">";
      [<'raw-html' raw>]
    }
  }
  else if (item.tag == 'emoji') {
    let parsed = parse(":" ++ attr_get(item, 'name') ++ ":", 'markdown')^;
    content(content(doc_body(parsed))[0])
  }
  else if (item.tag == 'br') { [<br>] }
  else if (item.tag == 'footnote_ref') { [<'footnote-ref' ref: attr_get(item, 'label')>] }
  else if (item.tag == 'raw_html') { [<'raw-html' attr_get(item, 'html')>] }
  else if (item.tag == 'math') { [<math type: "inline", attr_get(item, 'tex')>] }
  // any other node (commands can produce mark nodes) keeps its content
  else { export_inline(item.content, 0) }
}

fn export_inline(items, level) any^ => nest_marks(items, mark_order, wrap_mark, export_atom)

fn is_inline_model(n) any^ =>
  is_text(n) or (is_node(n) and (n.tag == 'a' or n.tag == 'img' or n.tag == 'br' or
    n.tag == 'raw_html' or n.tag == 'math' or n.tag == 'emoji' or n.tag == 'footnote_ref' or member(mark_order, n.tag))) or false

// A list item's content: inline runs are its text, blocks keep their shape.
fn export_item_content(items, i, n, run, acc) any^ {
  if (i >= n) { [*acc, *export_inline(run, 0)] }
  else if (is_inline_model(items[i])) { export_item_content(items, i + 1, n, [*run, items[i]], acc) }
  else { export_item_content(items, i + 1, n, [], [*acc, *export_inline(run, 0), *export_block(items[i])]) }
}

fn export_li(li) any^ {
  let checked = attr_get(li, 'checked')
  let kids = export_item_content(li.content, 0, len(li.content), [], [])
  if (checked == null) <li *kids>
  else <li ["data-checked"]: if (checked) "true" else "false", *kids>
}

fn loose_attr(lst) any^ => if (attr_get(lst, 'loose') == true) "true" else null

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

fn html_attr_text(attrs) any^ => join([for (a in attrs where a.value != null)
  " " ++ string(a.name) ++ "=\"" ++ escape_attr(string(a.value)) ++ "\""], "")

fn image_block_text(n) any^ =>
  if (n.content == attr_get(n, 'edit original')) attr_get(n, 'edit source')
  else "<p" ++ html_attr_text(attr_get(n, 'edit html attrs')) ++ ">\n" ++
    html.inline_html(n.content) ++ "\n</p>"

fn html_group_text(n) any^ =>
  if (n.content == attr_get(n, 'edit original')) attr_get(n, 'edit source')
  else join([for (child in n.content) if (child.tag == 'p') image_block_text(child) else attr_get(child, 'html')], "\n")

fn export_block(n) any^ {
  if (is_text(n)) { [<p n.text>] }
  else if (not is_node(n)) { [] }
  else {
    let tag = n.tag
    if (tag == 'md_html_group') { [<'html-block' html_group_text(n)>] }
    else if (tag == 'p' and attr_get(n, 'edit html attrs') != null) { [<'html-block' image_block_text(n)>] }
    else if (tag == 'p') { [<p *export_inline(n.content, 0)>] }
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

fn export_blocks(nodes) => [for (n in nodes) for (b in ((export_block(n)) ^ { [] })) b]

fn drop_final_newlines(s) any^ => if (ends_with(s, "\n")) drop_final_newlines(slice(s, 0, len(s) - 1)) else s

// A run of editable blocks as the formatter writes it, or nothing.
fn run_text(run) any^ {
  let md = if (len(run) == 0) "" else drop_final_newlines(format(<doc <body *export_blocks(run)>>, 'markdown'))
  if (md == "") [] else [md]
}

fn footnote_text(note) any^ =>
  if (note.content == attr_get(note, 'edit original')) attr_get(note, 'edit source')
  else drop_final_newlines(format(<doc <body <footnote label: attr_get(note, 'label'), *export_blocks(note.content)>>>, 'markdown'))

// Editable runs go through the formatter; a kept block is its source lines as
// read. Top-level blocks are separated by one blank line.
fn export_parts(nodes, i, run, acc) any^ {
  if (i >= len(nodes)) { [*acc, *run_text(run)] }
  else if (is_kept(nodes[i])) { export_parts(nodes, i + 1, [], [*acc, *run_text(run), attr_get(nodes[i], 'markdown')]) }
  else if (is_node(nodes[i]) and nodes[i].tag == 'md_footnote')
    { export_parts(nodes, i + 1, [], [*acc, *run_text(run), footnote_text(nodes[i])]) }
  else { export_parts(nodes, i + 1, [*run, nodes[i]], acc) }
}

// Serialize an editor document (plus its envelope) to Markdown source.
pub fn export_text(doc, envelope) any^ {
  let parts = export_parts(doc.content, 0, [], [])
  let md = if (len(parts) == 0) "" else join(parts, "\n\n") ++ "\n"
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
  // Trim at actual line breaks, not at inline element boundaries.
  let trimmed = [for (i, l in lines) {
    let start = if (i > 0) trim_start(l) else l;
    if (i < len(lines) - 1) trim_end(start) else start}]
  if (single_line) join(trimmed, " ") else join(trimmed, "\n")
}

fn norm_inline_item(it) any^ {
  if (is_text(it)) { text_marked(it.text, sorted_by_name(it.marks)) }
  else if (is_node(it) and member(mark_order, it.tag)) {
    // a mark node is its content with that mark applied
    it
  }
  else if (is_node(it)) {
    let attrs = sorted_attrs(it)
    let comparable = if (it.tag == 'img') [for (a in attrs)
      if (a.name == 'width' or a.name == 'height') {*:a, value:string(a.value)} else a] else attrs
    node_attrs(it.tag, comparable, norm_inline_seq(it.content, false, false))
  }
  else { it }
}

fn drop_trailing_breaks(items) any^ {
  if (len(items) > 0 and is_node(items[len(items) - 1]) and items[len(items) - 1].tag == 'br')
    drop_trailing_breaks(take(items, len(items) - 1))
  else items
}

fn trim_run_text(items, single_line, trim_edges) =>
  [for (i, it in items) if (is_text(it)) {
    let value = norm_text(it.text, single_line)
    let leading = if (trim_edges and i == 0) trim_start(value) else value;
    text_marked(if (trim_edges and i == len(items) - 1) trim_end(leading) else leading, it.marks)
  } else it]

fn norm_inline_seq(items, single_line, trim_edges = true) any^ {
  let flat = flatten_mark_nodes(items, mark_order)
  let merged = merge_leaves([for (it in flat) norm_inline_item(it)])
  drop_trailing_breaks(merge_leaves(trim_run_text(merged, single_line, trim_edges)))
}

fn paragraph_unit(n) any^ => is_inline_model(n) or (is_node(n) and n.tag == 'p')

fn adjacent_paragraphs(items, i, prev) any^ {
  if (i >= len(items)) false
  else {
    let it = items[i]
    let unit = if (is_inline_model(it)) 'run' else if (is_node(it) and it.tag == 'p') 'p' else 'other'
    if ((unit == 'p' and (prev == 'run' or prev == 'p')) or (unit == 'run' and prev == 'p')) true
    else adjacent_paragraphs(items, i + 1, unit)
  }
}

fn list_is_loose(lst) any^ =>
  attr_get(lst, 'loose') == true or any([for (li in lst.content) is_node(li) and adjacent_paragraphs(li.content, 0, 'none')])

// A tight item's paragraphs are its text; a loose item's text is a paragraph.
fn norm_li_content(items, loose) any^ {
  let unwrapped = if (loose) items
                  else [for (it in items) for (x in (((if (is_node(it) and it.tag == 'p') it.content else [it])) ^ { [] })) x]
  norm_mixed(unwrapped, loose, 0, len(unwrapped), [], [])
}

fn close_run(run, loose, acc) any^ {
  let seq = norm_inline_seq(run, false)
  if (len(seq) == 0) acc
  else if (loose) [*acc, node('p', seq)]
  else [*acc, *seq]
}

fn norm_mixed(items, loose, i, n, run, acc) any^ {
  if (i >= n) { close_run(run, loose, acc) }
  else if (is_inline_model(items[i])) { norm_mixed(items, loose, i + 1, n, [*run, items[i]], acc) }
  else { norm_mixed(items, loose, i + 1, n, [], [*close_run(run, loose, acc), *norm_block(items[i])]) }
}

fn norm_list(lst) any^ {
  let loose = list_is_loose(lst)
  let start_at = attr_get(lst, 'start')
  let attrs = attr_list([['loose', if (loose) true else null],
                         ['start', if (start_at == null or start_at == "1") null else start_at]])
  node_attrs(lst.tag, attrs,
    [for (li in lst.content where ((is_node(li)) ^ { false }))
       node_attrs('li', sorted_attrs(li), norm_li_content(li.content, loose))])
}

fn norm_cell(c) any => node_attrs(c.tag, sorted_attrs(c), norm_inline_seq(c.content, true))

fn norm_table_part(p) any^ {
  if (p.tag == 'tr') node('tr', [for (c in p.content) norm_cell(c)])
  else node(p.tag, [for (r in p.content) node('tr', [for (c in r.content) norm_cell(c)])])
}

fn norm_block(n) any^ {
  if (is_text(n)) { norm_block(node('p', [n])) }
  else if (not is_node(n)) { [] }
  else {
    let tag = n.tag
    if (tag == 'p') {
      let seq = norm_inline_seq(n.content, false)
      if (len(seq) == 0) [] else [node('p', seq)]
    }
    else if (is_heading_tag(tag)) { [node(tag, norm_inline_seq(n.content, true))] }
    else if (tag == 'blockquote' or tag == 'md_html_group') { [node(tag, norm_blocks(n.content))] }
    else if (tag == 'ul' or tag == 'ol') { [norm_list(n)] }
    else if (tag == 'pre' or tag == 'code_block') {
      let code = node_plain_text(n)
      let body = if (ends_with(code, "\n")) slice(code, 0, len(code) - 1) else code
      let lang = attr_get(n, 'language');
      [node_attrs('pre', attr_list([['language', if (lang == "") null else lang]]), [text(body)])]
    }
    else if (tag == 'table') { [node('table', [for (p in n.content) norm_table_part(p)])] }
    else if (tag == 'md_footnote') { [node_attrs(tag, attr_list([['label', attr_get(n, 'label')]]), norm_blocks(n.content))] }
    else if (tag == 'hr' or tag == 'html_block' or tag == 'math_block' or tag == 'md_source') {
      [node_attrs(tag, sorted_attrs(n), [])]
    }
    else if (is_inline_model(n)) { norm_block(node('p', [n])) }
    else { norm_blocks(n.content) }
  }
}

fn norm_blocks(nodes) => [for (n in nodes) for (b in ((norm_block(n)) ^ { [] })) b]

pub fn normal_form(doc) any => node('doc', norm_blocks(doc.content))

// null when `doc` survives export and re-import unchanged in normal form,
// else a sentence naming the first block that would change.
pub fn check_roundtrip(doc, envelope) any^ {
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
  id: 'markdown', name: "Markdown", suffixes: [".md", ".markdown", ".mdown", ".mkdn"],
  surface: 'rich_text', schema: schema, schema_preset: 'html5_subset',
  unsupported_input_types: unsupported_input_types,
  // kept as written: shown view-only, never written into (proposal §2)
  view_only_tags: ['md_source', 'raw_html', 'html_block', 'math', 'math_block', 'emoji', 'footnote_ref'],
  import_text: import_text, export_text: export_text, check_roundtrip: check_roundtrip,
  toolbar: 'markdown'
}
