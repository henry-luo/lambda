// List editing: (1) Tab/Shift-Tab indent/outdent preserve the caret inside the
// moved item; (2) Backspace at the start of the 2nd of two adjacent lists joins
// them. Mirrors the JS reference (the live editor's behaviour).
import lambda.package.editor.mod_doc
import lambda.package.editor.mod_source_pos
import lambda.package.editor.mod_md_schema
import lambda.package.editor.mod_commands

fn st(d, sel) => {doc: d, selection: sel, schema: html5_subset_schema, stored_marks: null}

// --- list join on backspace at start of 2nd list ---
let d1 = node('doc', [node('ul', [node('li', [text("one")])]), node('ul', [node('li', [text("two")])])])
let j1 = cmd_delete_backward(st(d1, text_selection(pos([1,0,0],0), pos([1,0,0],0))))
"join_count:"; len(j1.doc_after.content)
"join_one_list:"; j1.doc_after.content[0].tag == 'ul' and len(j1.doc_after.content[0].content) == 2
"join_text:"; doc_text(j1.doc_after) == "onetwo"
"join_caret_in_2nd:"; j1.sel_after.kind == 'text' and j1.sel_after.anchor.path == [0,1,0]

// different kinds join into the first list's kind
let d2 = node('doc', [node('ol', [node('li', [text("one")])]), node('ul', [node('li', [text("two")])])])
let j2 = cmd_delete_backward(st(d2, text_selection(pos([1,0,0],0), pos([1,0,0],0))))
"diffkind_tag:"; j2.doc_after.content[0].tag
"diffkind_items:"; len(j2.doc_after.content[0].content)

// backspace at start of a list NOT preceded by a list -> no join (unchanged: null)
let d3 = node('doc', [node('paragraph', [text("p")]), node('ul', [node('li', [text("x")])])])
"no_join_when_prev_not_list:"; cmd_delete_backward(st(d3, text_selection(pos([1,0,0],0), pos([1,0,0],0)))) == null

// --- indent preserves the caret (from any position in the item) ---
let d4 = node('doc', [node('ul', [node('li', [text("a")]), node('li', [text("bc")])])])
let i4 = cmd_indent_list_item(st(d4, text_selection(pos([0,1,0],1), pos([0,1,0],1))))
"indent_caret_text:"; i4.sel_after.kind == 'text'
"indent_caret_path:"; i4.sel_after.anchor.path == [0,0,1,0,0]
"indent_caret_off:"; i4.sel_after.anchor.offset == 1

// indent then outdent round-trips the document
let s4b = {doc: i4.doc_after, selection: i4.sel_after, schema: html5_subset_schema, stored_marks: null}
let o4 = cmd_outdent_list_item(s4b)
"roundtrip_items:"; len(o4.doc_after.content[0].content)
"roundtrip_text:"; doc_text(o4.doc_after) == "abc"
"roundtrip_caret:"; o4.sel_after.kind == 'text'
