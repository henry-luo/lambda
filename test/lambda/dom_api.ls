// `import dom` — the Lambda-facing door onto the DOM core (ES36).
// This is the POC-1 exit test: a pure Lambda script queries, mutates and
// serializes a real Radiant DOM, through the same core the JS surface uses.
//
// The document itself still comes from `radiant.load`: creating one needs
// Radiant's loader, which lives above this module's link target (ESO80).
import radiant
import dom

let doc = radiant.load("test/js/dom_identity.html")
let root = radiant.root(doc)
let intro = dom.query_selector(root, "#intro")
let body = dom.closest(intro, "body")

// mutate, then read the mutation back out through serialization
let _ = dom.set_attribute(intro, "data-checked", "yes")
let copy = dom.clone_node(intro, true)
let _appended = dom.append_child(body, copy)
let p_after_append = len(dom.query_selector_all(root, "p"))
let _removed = dom.remove(copy)

{
  root_tag: dom.node_name(root),
  intro_tag: dom.node_name(intro),
  intro_text: dom.text_content(intro),
  body_tag: dom.node_name(body),
  matches_html: dom.matches(root, "html"),
  closest_body: dom.node_name(dom.closest(intro, "body")),
  by_id: dom.node_name(dom.get_element_by_id(root, "intro")),
  attr_read_back: dom.get_attribute(intro, "data-checked"),
  has_attr: dom.has_attribute(intro, "data-checked"),
  outer_html: dom.outer_html(intro),
  p_count_with_clone: p_after_append,
  p_count_after_remove: len(dom.query_selector_all(root, "p")),
  contains_intro: dom.contains(root, intro)
}
