// DOM ↔ source-position bridge (Radiant integration, step 1)
import lambda.package.editor.mod_doc
import lambda.package.editor.mod_source_pos
import lambda.package.editor.mod_dom_bridge

let p1 = node('paragraph', [text("Hello, "), text("world.")])
let p2 = node('paragraph', [text("Second line.")])
let d  = node('doc', [p1, p2])

// ---------------------------------------------------------------------------
// path_take — list-prefix utility (used by ancestor walk)
// ---------------------------------------------------------------------------
"take 0:";   path_equal(path_take([0, 1, 2], 0), [])
"take 1:";   path_equal(path_take([0, 1, 2], 1), [0])
"take 3:";   path_equal(path_take([0, 1, 2], 3), [0, 1, 2])

// ---------------------------------------------------------------------------
// source_pos_from_dom — what Radiant calls when the caret moves
// ---------------------------------------------------------------------------
let lk_text = dom_lookup([0, 1], 3, 'text')
let sp = source_pos_from_dom(lk_text)
"sp path:";        path_equal(sp.path, [0, 1])
"sp offset:";      sp.offset == 3

let lk_elem = dom_lookup([0], 1, 'element')
let sp_e = source_pos_from_dom(lk_elem)
"sp_e path:";      path_equal(sp_e.path, [0])
"sp_e offset:";    sp_e.offset == 1

"sp bad kind:";    source_pos_from_dom(dom_lookup([0], 0, 'widget')) == null

// ---------------------------------------------------------------------------
// source_selection_from_dom — drag from one text leaf to another
// ---------------------------------------------------------------------------
let anchor = dom_lookup([0, 0], 7, 'text')
let head   = dom_lookup([1, 0], 6, 'text')
let sel = source_selection_from_dom(anchor, head)
"sel kind:";       sel.kind == 'text'
"sel anchor path:";path_equal(sel.anchor.path, [0, 0])
"sel anchor off:"; sel.anchor.offset == 7
"sel head path:";  path_equal(sel.head.path, [1, 0])
"sel head off:";   sel.head.offset == 6

// ---------------------------------------------------------------------------
// dom_lookup_from_source_pos — what Radiant calls to position the caret
// ---------------------------------------------------------------------------
let dl = dom_lookup_from_source_pos(d, pos([0, 1], 3))
"dl subtree text:"; dl.subtree.text == "world."
"dl path:";         path_equal(dl.path, [0, 1])
"dl offset:";       dl.dom_offset == 3
"dl kind:";         dl.hit_kind == 'text'

let dl2 = dom_lookup_from_source_pos(d, pos([1], 0))
"dl2 subtree tag:"; dl2.subtree.tag == 'paragraph'
"dl2 kind:";        dl2.hit_kind == 'element'

"dl bad path:";     dom_lookup_from_source_pos(d, pos([9, 9], 0)) == null

// Selection -> dual DOM lookup
let dsel = dom_lookup_from_source_selection(d, text_selection(pos([0,0], 0), pos([1,0], 6)))
"dsel anchor kind:"; dsel.anchor.hit_kind == 'text'
"dsel head off:";    dsel.head.dom_offset == 6
"dsel head text:";   dsel.head.subtree.text == "Second line."

// ---------------------------------------------------------------------------
// ancestors_along — root→leaf walk used by event bubbling
// ---------------------------------------------------------------------------
let anc = ancestors_along(d, [0, 1])
"anc len:";         len(anc) == 3
"anc[0] path:";     path_equal(anc[0].path, [])
"anc[0] tag:";      anc[0].node.tag == 'doc'
"anc[1] path:";     path_equal(anc[1].path, [0])
"anc[1] tag:";      anc[1].node.tag == 'paragraph'
"anc[2] path:";     path_equal(anc[2].path, [0, 1])
"anc[2] text:";     anc[2].node.text == "world."
