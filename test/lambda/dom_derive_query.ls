// ES43 oracle for the §5.4 query and §5.5 serialization clusters.
//
// Each derivation is written here in Lambda over the CORE operations only and
// compared against the native fast path, on every node of a document and over
// several selectors. A fast path that disagrees with its derivation is a bug in
// the fast path by definition.
import dom

// chain helpers: a comma sequence of one element IS that element (ESO97), so a
// chain is counted and indexed rather than accumulated.
fn chain_len(c) { if (c == null) 0 else 1 + chain_len(dom.next_sibling(c)) }
fn nth_from(c, i) { if (i <= 0) c else nth_from(dom.next_sibling(c), i - 1) }
fn kids(n) array | error { [for (i in 0 to chain_len(dom.first_child(n)) - 1) nth_from(dom.first_child(n), i)] }

// tree-order descendants, over core only
fn descendants(n) { for (c in kids(n)) (c, descendants(c)) }

// the derivations
fn d_query_selector_all(root, sel) array | error {
  [for (d in descendants(root) where dom.node_type(d) == 1 and dom.matches(d, sel)) d]
}
fn d_query_selector(root, sel) {
  let hits = d_query_selector_all(root, sel)
  if (len(hits) == 0) null else hits[0]
}
// closest is an Element operation (DOM 4.9): on anything else it is not
// applicable, and the fast path answers null. The oracle made the derivation
// say so rather than quietly walking up from a text node.
fn d_closest(n, sel) {
  if (dom.node_type(n) != 1) null
  else if (dom.matches(n, sel)) n
  else if (dom.parent_element(n) != null) d_closest(dom.parent_element(n), sel)
  else null
}
fn d_get_element_by_id(root, id) {
  let hits = [for (d in descendants(root) where dom.node_type(d) == 1 and dom.get_attribute(d, "id") == id) d]
  if (len(hits) == 0) null else hits[0]
}
fn d_has_attribute(n, attr) { dom.get_attribute(n, attr) != null }
fn d_inner_html(n) { dom.serialize(n, false) }
fn d_outer_html(n) { dom.serialize(n, true) }

// comparison helpers: identity, never `==` (ESO96)
fn node_eq(x, y) { if (x == null or y == null) (x == null and y == null) else dom.same_node(x, y) }
fn list_eq(xs, ys) bool | error {
  if (len(xs) != len(ys)) false
  else if (len(xs) == 0) true
  else all([for (i in 0 to len(xs) - 1) node_eq(xs[i], ys[i])])
}

let doc = dom.load("test/js/dom_identity.html")
let root = dom.document_element(doc)
let nodes = (root, descendants(root))
let selectors = ["p", "li", "ul", "body", "#intro", "#list", "div p", ".missing", "em"]

fn selector_agrees(sel) bool | error {
  node_eq(d_query_selector(root, sel), dom.query_selector(root, sel)) and
  list_eq(d_query_selector_all(root, sel), dom.query_selector_all(root, sel))
}
fn node_agrees(n) bool | error {
  node_eq(d_closest(n, "body"), dom.closest(n, "body")) and
  node_eq(d_closest(n, "ul"), dom.closest(n, "ul")) and
  d_has_attribute(n, "id") == dom.has_attribute(n, "id") and
  d_inner_html(n) == dom.inner_html(n) and
  d_outer_html(n) == dom.outer_html(n)
}

{
  selectors_checked: len(selectors),
  nodes_checked: len(nodes),
  divergent_selectors: [for (s in selectors where selector_agrees(s) != true) s],
  divergent_nodes: [for (n in nodes where node_agrees(n) != true) dom.node_name(n)],
  by_id_agrees: node_eq(d_get_element_by_id(root, "intro"), dom.get_element_by_id(root, "intro"))
}
