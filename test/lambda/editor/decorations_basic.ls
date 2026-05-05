// decorations_basic.ls — exercise the decoration set (Phase R5)
import lambda.package.editor.mod_doc
import lambda.package.editor.mod_step
import lambda.package.editor.mod_transaction
import lambda.package.editor.mod_source_pos
import lambda.package.editor.mod_decorations

let s0 = deco_empty()
"empty count:"; len(s0.items)

// Add three decorations
let d_hit  = deco_inline(pos([0, 0], 0), pos([0, 0], 5),  {class: "find-hit"})
let d_typo = deco_inline(pos([0, 0], 7), pos([0, 0], 12), {class: "spellcheck"})
let d_node = deco_node([0],  {class: "active-block"})
let s1 = deco_add(deco_add(deco_add(s0, d_hit), d_typo), d_node)
"after add count:"; len(s1.items)

// deco_at — query by position
let hits_at_2 = deco_at(s1, pos([0, 0], 2))
"hits at offset 2:"; len(hits_at_2)
let hits_at_8 = deco_at(s1, pos([0, 0], 8))
"hits at offset 8:"; len(hits_at_8)
// node decoration covers any descendant
"hits in nested path:"; (len(deco_at(s1, pos([0, 0], 99))) >= 1)

// remove by class
let s2 = deco_remove_class(s1, "spellcheck")
"after remove count:"; len(s2.items)

// concat
let s3 = deco_concat(s1, s2)
"concat count:"; len(s3.items)

// deco_map — translate every position through a step
let step = step_replace_text([0, 0], 0, 0, "XXX")    // insert 3 chars at offset 0
fn map_p(p) => step_map(step, p)
let s_mapped = deco_map(s1, map_p)
let d0_after = s_mapped.items[0]   // d_hit shifted
"mapped from:"; d0_after.from.offset
"mapped to:";   d0_after.to.offset

// find_in_leaf — locate every occurrence of "ab" in a string
let hits = find_in_leaf("ababab__ab", "ab")
"find count:"; len(hits)
"find first:"; hits[0]
"find last:";  hits[len(hits) - 1]
"find empty needle:"; len(find_in_leaf("abc", ""))

// find_decorations — wrap them up as inline decos
let fd = find_decorations([0, 0], "Hello hello", "hello", {class: "find-hit"})
"find decos count:"; len(fd.items)
"find first deco from:"; fd.items[0].from.offset
"find first deco to:";   fd.items[0].to.offset

// find_decorations_in_doc — build a set across all text leaves
let find_doc = node('doc', [
	node('paragraph', [text("foo")]),
	node('paragraph', [text("bar foo")])
])
let doc_hits = find_decorations_in_doc(find_doc, "foo", {class: "find-hit"})
"doc find count:"; len(doc_hits.items)
"doc find first path:"; doc_hits.items[0].from.path
"doc find second path:"; doc_hits.items[1].from.path
"doc find second from:"; doc_hits.items[1].from.offset

// deco_map_tx — preserve overlay positions through a full transaction
let tx0 = tx_begin(find_doc, text_selection(pos([0, 0], 0), pos([0, 0], 0)))
let tx1 = tx_step(tx0, step_replace_text([0, 0], 0, 0, "X"))
let mapped_doc_hits = deco_map_tx(doc_hits, tx1)
"tx mapped first from:"; mapped_doc_hits.items[0].from.offset
"tx mapped first to:"; mapped_doc_hits.items[0].to.offset

// decorations_project_doc — lower decoration attrs into a renderer-facing tree
let visual_set = deco_add(doc_hits, deco_node([1], {class: "active-block"}))
let visual_doc = decorations_project_doc(find_doc, visual_set)
"visual first span tag:"; visual_doc.content[0].content[0].tag == 'span'
"visual first span class:"; visual_doc.content[0].content[0].attrs[0].value == "find-hit"
"visual first span text:"; visual_doc.content[0].content[0].content[0].text == "foo"
"visual second node class:"; visual_doc.content[1].attrs[0].value == "active-block"
"visual second prefix:"; visual_doc.content[1].content[0].text == "bar "
"visual second hit class:"; visual_doc.content[1].content[1].attrs[0].value == "find-hit"
"visual source unchanged:"; find_doc.content[0].content[0].kind == 'text'
