// Pasted nested HTML lists are normalized to the flat indent-level model:
// a nested sublist is flattened into the parent list, each formerly-nested item
// carried over with an `indent` level = its nesting depth. (flatten_nested_lists,
// applied in cmd_paste_html.)
import lambda.package.editor.mod_doc
import lambda.package.editor.mod_step
import lambda.package.editor.mod_source_pos
import lambda.package.editor.mod_md_schema
import lambda.package.editor.mod_commands

// 1. flatten_nested_lists directly (no schema coercion → exact attrs)
let nested = [node('ul', [
  node('li', [text("a"), node('ul', [node('li', [text("b")]), node('li', [text("c")])])]),
  node('li', [text("d")])
])]
let flat = flatten_nested_lists(nested)
"one_list:"; len(flat) == 1
"flat_tag:"; flat[0].tag == 'ul'
"item_count:"; len(flat[0].content)
"all_li:"; [for (it in flat[0].content) it.tag]
"a_level0:"; attrs_get(flat[0].content[0].attrs, 'indent') == null
"b_level1:"; attrs_get(flat[0].content[1].attrs, 'indent') == 1
"c_level1:"; attrs_get(flat[0].content[2].attrs, 'indent') == 1
"d_level0:"; attrs_get(flat[0].content[3].attrs, 'indent') == null
"text_order:"; doc_text(flat[0]) == "abcd"

// 2. deeper nesting -> increasing levels
let deep = flatten_nested_lists([node('ol', [node('li', [text("a"),
  node('ol', [node('li', [text("b"), node('ol', [node('li', [text("c")])])])])])])])
"deep_count:"; len(deep[0].content)
"deep_b:"; attrs_get(deep[0].content[1].attrs, 'indent') == 1
"deep_c:"; attrs_get(deep[0].content[2].attrs, 'indent') == 2

// 3. already-flat list is unchanged (idempotent)
let already = [node('ul', [node('li', [text("a")]), node_attrs('li', [{name:'indent',value:1}], [text("b")])])]
let again = flatten_nested_lists(already)
"idempotent_count:"; len(again[0].content) == 2
"idempotent_b:"; attrs_get(again[0].content[1].attrs, 'indent') == 1

// 4. end-to-end: pasting nested HTML over an all-selection inserts a flat list
fn st(d, sel) => {doc: d, selection: sel, schema: html5_subset_schema, stored_marks: null}
let tx = cmd_paste_html(st(node('doc', [node('p', [text("x")])]), all_selection()),
  "<ul><li>a<ul><li>b</li><li>c</li></ul></li><li>d</li></ul>", "")
"paste_list_tag:"; node_at(tx.doc_after, [0]).tag == 'ul'
"paste_items:"; len(node_at(tx.doc_after, [0]).content) == 4
"paste_b_level1:"; attrs_get(node_at(tx.doc_after, [0, 1]).attrs, 'indent') == 1
"paste_text:"; doc_text(node_at(tx.doc_after, [0])) == "abcd"

// 5. mid-paragraph block paste splits the paragraph and inserts the list as a
//    sibling between the halves (parity with the JS reference)
let mid = cmd_paste_html(st(node('doc', [node('p', [text("abcd")])]), text_selection(pos([0,0],2), pos([0,0],2))),
  "<ul><li>x<ul><li>y</li></ul></li></ul>", "")
"mid_tags:"; [for (b in mid.doc_after.content) b.tag]
"mid_head:"; doc_text(mid.doc_after.content[0]) == "ab"
"mid_list:"; mid.doc_after.content[1].tag == 'ul'
"mid_y_indent:"; attrs_get(node_at(mid.doc_after,[1,1]).attrs, 'indent') == 1
"mid_tail:"; doc_text(mid.doc_after.content[2]) == "cd"

// end-of-paragraph paste appends the list (no empty tail paragraph)
let endp = cmd_paste_html(st(node('doc', [node('p', [text("abcd")])]), text_selection(pos([0,0],4), pos([0,0],4))),
  "<ul><li>x</li></ul>", "")
"end_tags:"; [for (b in endp.doc_after.content) b.tag]

// a single paragraph pasted mid-text still merges inline (no split)
let parap = cmd_paste_html(st(node('doc', [node('p', [text("abcd")])]), text_selection(pos([0,0],2), pos([0,0],2))),
  "<p>Z</p>", "")
"para_count:"; len(parap.doc_after.content)
"para_text:"; doc_text(parap.doc_after) == "abZcd"
