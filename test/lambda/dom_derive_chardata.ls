// ES43 oracle for the §5.3 character-data cluster: text_content.
//
// The derivation recurses over child_nodes and concatenates the values of the
// text and comment nodes; the fast path reads the textContent property. They
// must agree on every node of the document, including the text nodes
// themselves, where the derivation's base case applies.
import dom

fn chain_len(c) { if (c == null) 0 else 1 + chain_len(dom.next_sibling(c)) }
fn nth_from(c, i) { if (i <= 0) c else nth_from(dom.next_sibling(c), i - 1) }
fn d_child_nodes(n) array | error { [for (i in 0 to chain_len(dom.first_child(n)) - 1) nth_from(dom.first_child(n), i)] }

// the derivation, over core only
fn d_text_content(n) string | error {
  if (dom.node_type(n) == 3 or dom.node_type(n) == 8) dom.node_value(n)
  else join([for (k in d_child_nodes(n)) d_text_content(k)], "")
}

fn descendants(n) { for (c in d_child_nodes(n)) (c, descendants(c)) }

let doc = dom.load("test/js/dom_identity.html")
let root = dom.document_element(doc)
let nodes = (root, descendants(root))

{
  nodes_checked: len(nodes),
  divergent: [for (n in nodes where (d_text_content(n) == dom.text_content(n)) != true) dom.node_name(n)],
  root_text: d_text_content(root) == dom.text_content(root),
  sample: dom.text_content(dom.query_selector(root, "#list"))
}
