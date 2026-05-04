// paste_basic.ls — schema coercion for paste/drop fragments (Phase R5)
import lambda.package.editor.mod_doc
import lambda.package.editor.mod_md_schema
import lambda.package.editor.mod_paste

// ---------------------------------------------------------------------------
// 1. Drop unknown tag entirely
// ---------------------------------------------------------------------------
let unknown = node('blink', [text("danger")])
"drop unknown:"; coerce_to_schema(md_schema, unknown) == null

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
