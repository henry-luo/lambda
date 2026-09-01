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
let intro = dom.query(root, "#intro")
let body = dom.closest(intro, "body")

// mutate, then read the mutation back out through serialization
let _ = dom.set_attr(intro, "data-checked", "yes")
let copy = dom.clone(intro, true)
let _appended = dom.append(body, copy)
let p_after_append = len(dom.query_all(root, "p"))
let _removed = dom.remove(copy)

{
  root_tag: dom.tag(root),
  intro_tag: dom.tag(intro),
  intro_text: dom.text(intro),
  body_tag: dom.tag(body),
  matches_html: dom.matches(root, "html"),
  closest_body: dom.tag(dom.closest(intro, "body")),
  by_id: dom.tag(dom.element_by_id(root, "intro")),
  attr_read_back: dom.attr(intro, "data-checked"),
  has_attr: dom.has_attr(intro, "data-checked"),
  outer_html: dom.outer_html(intro),
  p_count_with_clone: p_after_append,
  p_count_after_remove: len(dom.query_all(root, "p")),
  contains_intro: dom.contains(root, intro)
}
