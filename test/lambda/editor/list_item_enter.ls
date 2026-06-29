// Enter in a blank list item lifts/outdents it (Mac-Notes behaviour) instead of
// creating another empty item. Mirrors the JS reference cmdEnterEmptyListItem.
// (cmd_split_block routes an empty-list-item caret to the lift.)
import lambda.package.editor.mod_doc
import lambda.package.editor.mod_source_pos
import lambda.package.editor.mod_md_schema
import lambda.package.editor.mod_commands

fn st(d, sel) => {doc: d, selection: sel, schema: html5_subset_schema, stored_marks: null}
fn st_md(d, sel) => {doc: d, selection: sel, schema: md_schema, stored_marks: null}

// 1. top-level empty single-item list -> exit to an empty paragraph
let d1 = node('doc', [node('ul', [node('li', [])])])
let tx1 = cmd_split_block(st(d1, text_selection(pos([0,0],0), pos([0,0],0))))
"t1_count:"; len(tx1.doc_after.content)
"t1_is_p:"; tx1.doc_after.content[0].tag == 'p'
"t1_caret:"; tx1.sel_after.kind == 'text'

// 2. ol(li "y", empty li): Enter in the empty 2nd item exits, list keeps "y"
let d2 = node('doc', [node('ol', [node('li', [text("y")]), node('li', [])])])
let tx2 = cmd_split_block(st(d2, text_selection(pos([0,1],0), pos([0,1],0))))
"t2_count:"; len(tx2.doc_after.content)
"t2_keeps_list:"; tx2.doc_after.content[0].tag == 'ol' and len(tx2.doc_after.content[0].content) == 1
"t2_para_after:"; tx2.doc_after.content[1].tag == 'p'

// 3. empty item in the MIDDLE splits the list around an empty paragraph
let d3 = node('doc', [node('ul', [node('li', [text("one")]), node('li', []), node('li', [text("two")])])])
let tx3 = cmd_split_block(st(d3, text_selection(pos([0,1],0), pos([0,1],0))))
"t3_count:"; len(tx3.doc_after.content)
"t3_first_list:"; tx3.doc_after.content[0].tag == 'ul'
"t3_mid_p:"; tx3.doc_after.content[1].tag == 'p'
"t3_last_list:"; tx3.doc_after.content[2].tag == 'ul'

// 4. nested empty item outdents one level (stays a list item, parent level)
let d4 = node('doc', [node('ul', [node('li', [text("a"), node('ul', [node('li', [])])])])])
let tx4 = cmd_split_block(st(d4, text_selection(pos([0,0,1,0],0), pos([0,0,1,0],0))))
"t4_outer_items:"; len(tx4.doc_after.content[0].content)
"t4_both_li:"; tx4.doc_after.content[0].content[0].tag == 'li' and tx4.doc_after.content[0].content[1].tag == 'li'

// 5. a NON-empty list item still splits into a new item (unchanged behaviour)
let d5 = node('doc', [node('ol', [node('li', [text("y")])])])
let tx5 = cmd_split_block(st(d5, text_selection(pos([0,0,0],1), pos([0,0,0],1))))
"t5_items:"; len(tx5.doc_after.content[0].content)
"t5_both_li:"; tx5.doc_after.content[0].content[0].tag == 'li' and tx5.doc_after.content[0].content[1].tag == 'li'

// 6. same lift works for the md schema (list / list_item tags)
let d6 = node('doc', [node('list', [node('list_item', [node('paragraph', [])])])])
let tx6 = cmd_split_block(st_md(d6, text_selection(pos([0,0,0],0), pos([0,0,0],0))))
"t6_count:"; len(tx6.doc_after.content)
"t6_is_para:"; tx6.doc_after.content[0].tag == 'paragraph'
