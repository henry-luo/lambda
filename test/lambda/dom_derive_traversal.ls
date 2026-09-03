// ES43: the derivation IS the specification, and this fixture is its oracle.
//
// Every §5.1/§5.2 traversal operation is written here as pure Lambda over the
// CORE operations only, exactly as dom_api.def states it, and compared against
// the native fast path on every node of a real document. A fast path that
// disagrees with its derivation is a bug in the fast path by definition -- this
// is the check that makes ESO83's class of defect (a second traversal that saw
// a different tree) impossible to reintroduce.
//
// Two Lambda facts shape how these are written. `var`/`while`/assignment are
// procedure-only (E224), so every derivation is recursive rather than looping.
// And a comma sequence of ONE element is that element, not a one-element list:
// `(node, null)` IS the node, so `for (c in ...)` would then iterate the node
// map's values instead of yielding the node (ESO97). A chain is therefore
// counted and indexed, which always produces a list.
import dom

// ---- the derivations, over core only ----
fn element_or_next(c) { if (c == null or dom.node_type(c) == 1) c else element_or_next(dom.next_sibling(c)) }
fn element_or_prev(c) { if (c == null or dom.node_type(c) == 1) c else element_or_prev(dom.previous_sibling(c)) }
fn chain_len(c) { if (c == null) 0 else 1 + chain_len(dom.next_sibling(c)) }
fn nth_from(c, i) { if (i <= 0) c else nth_from(dom.next_sibling(c), i - 1) }
fn siblings_from(c) array | error { [for (i in 0 to chain_len(c) - 1) nth_from(c, i)] }

fn d_first_element_child(n) { element_or_next(dom.first_child(n)) }
fn d_last_element_child(n) { element_or_prev(dom.last_child(n)) }
fn d_next_element_sibling(n) { element_or_next(dom.next_sibling(n)) }
fn d_previous_element_sibling(n) { element_or_prev(dom.previous_sibling(n)) }
fn d_parent_element(n) { if (dom.parent_node(n) != null and dom.node_type(dom.parent_node(n)) == 1) dom.parent_node(n) else null }
fn d_child_nodes(n) array | error { siblings_from(dom.first_child(n)) }
fn d_children(n) array | error { [for (c in d_child_nodes(n) where dom.node_type(c) == 1) c] }
fn d_root_node(n) { if (dom.parent_node(n) == null) n else d_root_node(dom.parent_node(n)) }
fn d_contains(a, b) { if (dom.same_node(b, a)) true else if (dom.parent_node(b) == null) false else d_contains(a, dom.parent_node(b)) }

// ---- comparison helpers (identity, not `==`: see ESO96) ----
fn node_eq(x, y) { if (x == null or y == null) (x == null and y == null) else dom.same_node(x, y) }
// indexing is statically error-capable (E208), so the declaration says so;
// an error then counts as divergence below, which is the safe direction.
fn list_eq(xs, ys) bool | error {
  if (len(xs) != len(ys)) false
  else if (len(xs) == 0) true
  else all([for (i in 0 to len(xs) - 1) node_eq(xs[i], ys[i])])
}

// ---- the corpus: every node in the document, in tree order ----
fn descendants(n) { for (c in d_child_nodes(n)) (c, descendants(c)) }

let doc = dom.load("test/js/dom_identity.html")
let root = dom.document_element(doc)
let nodes = (root, descendants(root))

fn agrees(n) bool | error {
  node_eq(d_first_element_child(n), dom.first_element_child(n)) and
  node_eq(d_last_element_child(n), dom.last_element_child(n)) and
  node_eq(d_next_element_sibling(n), dom.next_element_sibling(n)) and
  node_eq(d_previous_element_sibling(n), dom.previous_element_sibling(n)) and
  node_eq(d_parent_element(n), dom.parent_element(n)) and
  node_eq(d_root_node(n), dom.root_node(n)) and
  list_eq(d_child_nodes(n), dom.child_nodes(n)) and
  list_eq(d_children(n), dom.children(n)) and
  d_contains(root, n) == dom.contains(root, n)
}

{
  nodes_checked: len(nodes),
  divergent: [for (n in nodes where agrees(n) != true) dom.node_name(n)]
}
