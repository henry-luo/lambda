// paste_basic.ls — schema coercion for paste/drop fragments (Phase R5)
import lambda.package.editor.mod_doc
import lambda.package.editor.mod_md_schema
import lambda.package.editor.mod_paste
import lambda.package.editor.mod_commands
import lambda.package.editor.mod_source_pos
import lambda.package.editor.mod_html_paste

// ---------------------------------------------------------------------------
// 1. Drop unknown tag entirely
// ---------------------------------------------------------------------------
let unknown = node('blink', [text("danger")])
"drop unknown:"; coerce_to_schema(md_schema, unknown) == null
let para_with_unknown = node('paragraph', [text("ok"), node('blink', [text("no")])])
let para_without_unknown = coerce_to_schema(md_schema, para_with_unknown)
"drop unknown child:"; doc_text(para_without_unknown) == "ok"

// ---------------------------------------------------------------------------
// 2. Strip unknown marks from a text leaf
// ---------------------------------------------------------------------------
let leaf = text_marked("hi", ['strong', 'glitter', 'em'])
let leaf2 = coerce_to_schema(md_schema, leaf)
"keep known marks:"; len(leaf2.marks)
"strong kept:"; leaf2.marks[0] == 'strong'
"em kept:";     leaf2.marks[1] == 'em'

// ---------------------------------------------------------------------------
// 3. Atomic node — strip its content
// ---------------------------------------------------------------------------
let img_with_kids = node_attrs('image',
  [{name: 'src', value: "x.png"}],
  [text("ignored caption")])
let img2 = coerce_to_schema(md_schema, img_with_kids)
"atomic content stripped:"; len(img2.content) == 0
"atomic attrs preserved:";  img2.attrs[0].value == "x.png"

// ---------------------------------------------------------------------------
// 4. Lift inlines that landed in block context — wrap them in a paragraph
// ---------------------------------------------------------------------------
let mixed = [
  text("loose text"),
  node('paragraph', [text("a real para")]),
  text("more loose")
]
let coerced = coerce_for_md_block(mixed)
"lift count:"; len(coerced)
// Expect: [paragraph(loose text), paragraph(a real para), paragraph(more loose)]
"first lifted tag:";  coerced[0].tag == 'paragraph'
"first lifted text:"; doc_text(coerced[0]) == "loose text"
"middle preserved:";  coerced[1].tag == 'paragraph'
"middle text:";       doc_text(coerced[1]) == "a real para"
"last lifted tag:";   coerced[2].tag == 'paragraph'
"last lifted text:";  doc_text(coerced[2]) == "more loose"

// ---------------------------------------------------------------------------
// 5. A run of inline siblings is grouped into ONE paragraph
// ---------------------------------------------------------------------------
let run = [text("alpha"), text(" beta"), text(" gamma")]
let coerced2 = coerce_for_md_block(run)
"group run count:"; len(coerced2) == 1
"group run text:";  doc_text(coerced2[0]) == "alpha beta gamma"

// ---------------------------------------------------------------------------
// 6. Unwrap a stray block found in inline context
// ---------------------------------------------------------------------------
let inline_kids = [
  text("see "),
  node('paragraph', [text("inner")]),
  text(" end")
]
let coerced3 = coerce_children(md_schema, inline_kids, 'inline')
"inline unwrap count:"; len(coerced3)
// Expect three text leaves: "see ", "inner", " end"
"unwrap[0]:"; coerced3[0].text == "see "
"unwrap[1]:"; coerced3[1].text == "inner"
"unwrap[2]:"; coerced3[2].text == " end"

// ---------------------------------------------------------------------------
// 7. Nested coercion through a paragraph child
// ---------------------------------------------------------------------------
let para = node('paragraph', [text_marked("bold", ['strong', 'evil'])])
let para2 = coerce_to_schema(md_schema, para)
"nested marks count:"; len(para2.content[0].marks) == 1
"nested mark:"; para2.content[0].marks[0] == 'strong'

let code_block_marked = node('code_block', [text_marked("literal", ['strong'])])
let code_block_clean = coerce_to_schema(md_schema, code_block_marked)
"code block drops marks:"; len(code_block_clean.content[0].marks) == 0

let code_and_strong = node('paragraph', [text_marked("literal", ['strong', 'code'])])
let code_and_strong_clean = coerce_to_schema(md_schema, code_and_strong)
"exclusive mark count:"; len(code_and_strong_clean.content[0].marks) == 1
"exclusive mark kept:"; code_and_strong_clean.content[0].marks[0] == 'code'

// ---------------------------------------------------------------------------
// 8. Convert an HTML-like fragment and preserve common inline marks
// ---------------------------------------------------------------------------
let html_frag = node('p', [text("Hello "), node('strong', [text("world")])])
let md_blocks = html_fragment_to_md_blocks(html_frag)
"html block count:"; len(md_blocks) == 1
"html block tag:"; md_blocks[0].tag == 'paragraph'
"html text:"; doc_text(md_blocks[0]) == "Hello world"
"html strong mark:"; md_blocks[0].content[1].marks[0] == 'strong'

let paste_doc = node('doc', [node('paragraph', [text("Say: ")])])
let paste_state = {doc: paste_doc, selection: text_selection(pos([0, 0], 5), pos([0, 0], 5))}
let tx_html = cmd_paste_html(paste_state, html_frag, "Hello world")
let pasted_para = node_at(tx_html.doc_after, [0])
"cmd html paste text:"; doc_text(pasted_para) == "Say: Hello world"
"cmd html paste mark:"; pasted_para.content[2].marks[0] == 'strong'

let tx_raw_html = cmd_paste_html(paste_state, "<p>Hello <strong>world</strong></p>", "Hello world")
let raw_pasted_para = node_at(tx_raw_html.doc_after, [0])
"cmd raw html paste text:"; doc_text(raw_pasted_para) == "Say: Hello world"
"cmd raw html paste mark:"; raw_pasted_para.content[2].marks[0] == 'strong'

let link_frag = node('p', [text("Read "), node_attrs('a', [{name: 'href', value: "https://example.com"}], [text("more")])])
let md_link_blocks = html_fragment_to_md_blocks(link_frag)
"html link tag:"; md_link_blocks[0].content[1].tag == 'link'
"html link href:"; md_link_blocks[0].content[1].attrs[0].value == "https://example.com"
"html link text:"; doc_text(md_link_blocks[0].content[1]) == "more"
let tx_link_html = cmd_paste_html(paste_state, "<p>Read <a href='https://example.com'>more</a></p>", "Read more")
let link_pasted_para = node_at(tx_link_html.doc_after, [0])
"cmd raw html link tag:"; link_pasted_para.content[2].tag == 'link'
"cmd raw html link href:"; link_pasted_para.content[2].attrs[0].value == "https://example.com"

// ---------------------------------------------------------------------------
// 9. Schema-targeted HTML conversion keeps HTML-shaped tags for HTML sessions
// ---------------------------------------------------------------------------
let html_blocks = html_fragment_to_blocks_for_schema(html5_subset_schema,
  node('section', [node('h1', [text("Title")]), node('p', [text("Hi "), node('strong', [text("there")])])]))
"html schema block count:"; len(html_blocks) == 2
"html schema heading tag:"; html_blocks[0].tag == 'h1'
"html schema para tag:"; html_blocks[1].tag == 'p'
"html schema mark:"; html_blocks[1].content[1].marks[0] == 'strong'

let html_link_blocks = html_fragment_to_blocks_for_schema(html5_subset_schema, link_frag)
"html schema link tag:"; html_link_blocks[0].content[1].tag == 'a'
"html schema link href:"; html_link_blocks[0].content[1].attrs[0].value == "https://example.com"

let html_paste_doc = node('doc', [node('p', [text("Say: ")])])
let html_paste_state = {doc: html_paste_doc, schema: html5_subset_schema, selection: text_selection(pos([0, 0], 5), pos([0, 0], 5))}
let tx_html_schema = cmd_paste_html(html_paste_state, "<p>Hello <strong>world</strong></p>", "Hello world")
let html_schema_para = node_at(tx_html_schema.doc_after, [0])
"cmd html schema paste tag:"; html_schema_para.tag == 'p'
"cmd html schema paste text:"; doc_text(html_schema_para) == "Say: Hello world"
"cmd html schema paste mark:"; html_schema_para.content[2].marks[0] == 'strong'
