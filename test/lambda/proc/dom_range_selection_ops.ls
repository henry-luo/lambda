// Range and selection mutators from a Lambda script with no JS realm (ESO113):
// select_node_contents / collapse / select_node / selection_collapse / extend /
// set_base_and_extent (5 arguments, which the interpreter now admits) and
// select_all_children, with their result maps built from the document's Input.
import dom
let doc = dom.load("test/js/dom_identity.html")
let intro = dom.query_selector(doc, "#intro")
let text = dom.first_child(intro)
let r = dom.document_create_range(doc)
dom.range_select_node_contents(r, intro)
let b1 = dom.range_boundaries(r)
dom.range_collapse(r, true)
let c1 = dom.range_collapsed(r)
dom.range_select_node(r, intro)
let s = dom.document_selection(doc)
dom.selection_add_range(s, r)
dom.selection_collapse(s, text, 2)
let sb = dom.selection_boundaries(s)
dom.selection_extend(s, text, 5)
dom.set_base_and_extent(s, text, 0, text, 3)
let cnt = dom.selection_get_range_count(s)
dom.selection_select_all_children(s, intro)
{ b1: b1, c1: c1, sb: sb, cnt: cnt, focus_off: dom.selection_get_focus_offset(s), anchor_is_text: dom.same_node(dom.selection_get_anchor_node(s), text) }
