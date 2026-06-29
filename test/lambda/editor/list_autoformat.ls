// Markdown list autoformat: typing a space after a bare marker turns the block
// into a list. "-"/"*"/"+" -> bullet, "N." -> ordered. (cmd_autoformat_list,
// also wired into the insertText intent.)
import lambda.package.editor.mod_doc
import lambda.package.editor.mod_step
import lambda.package.editor.mod_source_pos
import lambda.package.editor.mod_md_schema
import lambda.package.editor.mod_commands
import lambda.package.editor.mod_input_intent

fn st(d, sel, sch) => {doc: d, selection: sel, schema: sch, stored_marks: null}
fn caret_at(path, off) => text_selection(pos(path, off), pos(path, off))

// html schema: "- " -> ul/li
let h1 = st(node('doc', [node('p', [text("-")])]), caret_at([0,0], 1), html5_subset_schema)
let t1 = cmd_autoformat_list(h1)
"ul_tag:"; t1.doc_after.content[0].tag
"ul_item_tag:"; t1.doc_after.content[0].content[0].tag
"ul_item_empty:"; len(t1.doc_after.content[0].content[0].content) == 0

// "1. " -> ol
let h2 = st(node('doc', [node('p', [text("1.")])]), caret_at([0,0], 2), html5_subset_schema)
"ol_tag:"; cmd_autoformat_list(h2).doc_after.content[0].tag

// "*" and "+" -> ul; "42." -> ol
"star_ul:"; cmd_autoformat_list(st(node('doc', [node('p', [text("*")])]), caret_at([0,0],1), html5_subset_schema)).doc_after.content[0].tag == 'ul'
"plus_ul:"; cmd_autoformat_list(st(node('doc', [node('p', [text("+")])]), caret_at([0,0],1), html5_subset_schema)).doc_after.content[0].tag == 'ul'
"num_ol:"; cmd_autoformat_list(st(node('doc', [node('p', [text("42.")])]), caret_at([0,0],3), html5_subset_schema)).doc_after.content[0].tag == 'ol'

// md schema: "- " -> list/list_item with a paragraph
let m1 = st(node('doc', [node('paragraph', [text("-")])]), caret_at([0,0], 1), md_schema)
let tm1 = cmd_autoformat_list(m1)
"md_list_tag:"; tm1.doc_after.content[0].tag
"md_item_tag:"; tm1.doc_after.content[0].content[0].tag
"md_ordered_false:"; attrs_get(tm1.doc_after.content[0].attrs, 'ordered') == null
let m2 = st(node('doc', [node('paragraph', [text("1.")])]), caret_at([0,0], 2), md_schema)
"md_ordered_true:"; attrs_get(cmd_autoformat_list(m2).doc_after.content[0].attrs, 'ordered')

// not a bare marker -> no autoformat
"no_format_extra:"; cmd_autoformat_list(st(node('doc', [node('p', [text("-x")])]), caret_at([0,0],2), html5_subset_schema)) == null
"no_format_midcaret:"; cmd_autoformat_list(st(node('doc', [node('p', [text("-")])]), caret_at([0,0],0), html5_subset_schema)) == null
"no_format_in_li:"; cmd_autoformat_list(st(node('doc', [node('ul', [node('li', [text("-")])])]), caret_at([0,0,0],1), html5_subset_schema)) == null

// via the insertText intent: typing " " after "-" triggers autoformat
let intent_doc = node('doc', [node('p', [text("-")])])
let it = dispatch_intent(st(intent_doc, caret_at([0,0], 1), html5_subset_schema), {input_type: "insertText", data: " "})
"intent_ul:"; it.doc_after.content[0].tag == 'ul'
// typing " " elsewhere still inserts a space
let it2 = dispatch_intent(st(node('doc', [node('p', [text("ab")])]), caret_at([0,0], 2), html5_subset_schema), {input_type: "insertText", data: " "})
"intent_space:"; doc_text(it2.doc_after) == "ab "
