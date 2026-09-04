// Ranges and selections from a Lambda script with no JS realm (ESO113).
// document_create_range / document_selection take the document explicitly;
// the ambient create_range / selection are JS's.
import dom
let doc = dom.load("test/js/dom_identity.html")
let intro = dom.query_selector(doc, "#intro")
let text = dom.first_child(intro)
let r = dom.document_create_range(doc)
dom.range_set_start(r, text, 1)
dom.range_set_end(r, text, 4)
let s = dom.document_selection(doc)
dom.selection_add_range(s, r)
{ collapsed: dom.range_get_collapsed(r), start_offset: dom.range_get_start_offset(r), end_offset: dom.range_get_end_offset(r),
  text: dom.range_to_string(r), same_start: dom.same_node(dom.range_get_start_container(r), text),
  sel_count: dom.selection_get_range_count(s), anchor_offset: dom.selection_get_anchor_offset(s), sel_type: dom.selection_get_type(s) }
