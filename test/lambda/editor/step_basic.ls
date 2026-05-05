// Step constructors and basic apply (Phase R3)
import lambda.package.editor.mod_doc
import lambda.package.editor.mod_step

let d = node('doc', [
  node('paragraph', [text("Hello, world.")]),
  node('paragraph', [text("Second.")])
])

// ---------------------------------------------------------------------------
// step_replace_text
// ---------------------------------------------------------------------------
let s1 = step_replace_text([0, 0], 7, 12, "Lambda")
"s1 kind:";       s1.kind
"s1 from:";       s1.from
"s1 to:";         s1.to
let d1 = step_apply(s1, d)
"d1 text:";       doc_text(d1) == "Hello, Lambda.Second."
"d unchanged:";   doc_text(d) == "Hello, world.Second."

// Insert at start
let s2 = step_replace_text([0, 0], 0, 0, "[!] ")
let d2 = step_apply(s2, d)
"d2 text:";       doc_text(d2) == "[!] Hello, world.Second."

// Pure delete
let s3 = step_replace_text([0, 0], 5, 12, "")
let d3 = step_apply(s3, d)
"d3 text:";       doc_text(d3) == "Hello.Second."

// ---------------------------------------------------------------------------
// step_replace (children of a parent)
// ---------------------------------------------------------------------------
// Insert a horizontal rule between the two paragraphs
let s4 = step_replace([], 1, 1, [node('hr', [])])
let d4 = step_apply(s4, d)
"d4 child count:"; len(d4.content)
"d4 mid tag:";     d4.content[1].tag

// Delete the second paragraph
let s5 = step_replace([], 1, 2, [])
let d5 = step_apply(s5, d)
"d5 child count:"; len(d5.content)
"d5 text:";        doc_text(d5) == "Hello, world."

// Replace both paragraphs with a single new one
let s6 = step_replace([], 0, 2, [node('paragraph', [text("Replaced")])])
let d6 = step_apply(s6, d)
"d6 child count:"; len(d6.content)
"d6 text:";        doc_text(d6) == "Replaced"

// Replace an outer range while preserving the inner gap.
let wrap_doc = node('doc', [
  node('paragraph', [text("before")]),
  node('paragraph', [text("keep")]),
  node('paragraph', [text("after")])
])
let s6b = step_replace_around([], 0, 3, 1, 2, [node('blockquote', [])], 1)
let d6b = step_apply(s6b, wrap_doc)
"around child count:"; len(d6b.content)
"around first tag:"; d6b.content[0].tag
"around kept text:"; doc_text(d6b.content[1]) == "keep"

// ---------------------------------------------------------------------------
// step_add_mark / step_remove_mark
// ---------------------------------------------------------------------------
let m1 = step_add_mark([0, 0], 'strong')
let d7 = step_apply(m1, d)
"marks before:";   len(d.content[0].content[0].marks)
"marks after:";    len(d7.content[0].content[0].marks)
"mark[0]:";        d7.content[0].content[0].marks[0]

// Adding the same mark twice is idempotent
let d8 = step_apply(m1, d7)
"marks idemp:";    len(d8.content[0].content[0].marks)

let m2 = step_remove_mark([0, 0], 'strong')
let d9 = step_apply(m2, d7)
"marks removed:";  len(d9.content[0].content[0].marks)

// ---------------------------------------------------------------------------
// step_set_attr / step_set_node_type
// ---------------------------------------------------------------------------
let s7 = step_set_attr([0], 'align', "center")
let d10 = step_apply(s7, d)
"attr count:";     len(d10.content[0].attrs)
"attr name:";      d10.content[0].attrs[0].name
"attr value:";     d10.content[0].attrs[0].value
"attr lookup:";    attrs_get(d10.content[0].attrs, 'align') == "center"

// Replacing the same attr keeps a single entry
let s8 = step_set_attr([0], 'align', "right")
let d11 = step_apply(s8, d10)
"attr count after replace:"; len(d11.content[0].attrs)
"attr value after replace:"; attrs_get(d11.content[0].attrs, 'align') == "right"

let s9 = step_set_node_type([0], 'heading')
let d12 = step_apply(s9, d)
"new tag:";         d12.content[0].tag
"text preserved:";  doc_text(d12) == doc_text(d)
