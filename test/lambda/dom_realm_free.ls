// The DOM core is realm-neutral (ES33): a Lambda script drives a document with
// no JS realm bound. This fixture is the regression test for the two defects
// that made that untrue.
//
// ESO81 -- the realm fallback. Reads whose property had no handler fell through
// to a prototype lookup, which built an intrinsic constructor out of the JS
// realm's pool and faulted on a null pool. `node_value` on an element was the
// smallest case; the property WRITES and the geometry results were the visible
// ones, which is why set_text_content, set_inner_html, bounding_box,
// client_rects and scroll_state were all withheld from `dom.*`.
//
// ESO93 -- the document node. The document answered nothing: no node type, no
// name, not the parent of <html>, and not usable as the document argument to
// create_node. A walk upward stopped at the document element while JS
// getRootNode() answered the Document.
import radiant
import dom

let doc = radiant.load("test/js/dom_identity.html")
let root = radiant.root(doc)
let intro = dom.get_element_by_id(root, "intro")
let docnode = dom.parent_node(root)

// writes, previously excluded for faulting without a realm
let _1 = dom.set_text_content(intro, "written with no realm")
let text_after = dom.text_content(intro)
let _2 = dom.set_inner_html(intro, "<b>bold</b> tail")

// a node created through the document reached via ownerDocument
let made = dom.create_node(dom.owner_document(intro), 1, "section", null)
let _3 = dom.append_child(root, made)

{
  // ESO93: the document is a node
  document_name: dom.node_name(docnode),
  document_type: dom.node_type(docnode),
  document_parent: dom.parent_node(docnode),
  document_is_root_of_intro: dom.same_node(docnode, dom.root_node(intro)),
  owner_document_usable: dom.node_name(made),

  // ESO81: writes and geometry answer instead of faulting
  text_after_write: text_after,
  html_after_write: dom.inner_html(intro),
  node_value_of_element: dom.node_value(intro),
  scroll_state: dom.scroll_state(intro),
  bounding_box_keys: len(dom.bounding_box(intro)),
  client_rects_count: len(dom.client_rects(intro))
}
