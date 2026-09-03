// Regression test for the §5.7 form walkers, now written in Lambda.
//
// This began as an oracle comparing the Lambda definitions against the native
// radiant.* bodies, and that comparison is what proved the move faithful -- it
// caught four contract errors the catalog's derivations had guessed wrong. The
// native bodies are retired now, so there is nothing left to compare against
// and the test pins the answers themselves.
//
// form_of, radio_group and details_group were native `radiant.*` bodies. None
// of them is mechanism -- each is policy over ordinary DOM reads -- so each now
// lives in lambda/package/dom/tree.ls, written in Lambda over the published
// core. This fixture holds the Lambda definitions and the native bodies to the
// same answers on every element of two real documents, which is what makes the
// move a refactor rather than a rewrite.
import dom
import tree: lambda.package.dom.tree

fn chain_len(c) { if (c == null) 0 else 1 + chain_len(dom.next_sibling(c)) }
fn nth_from(c, i) { if (i <= 0) c else nth_from(dom.next_sibling(c), i - 1) }
fn kids(n) array | error { [for (i in 0 to chain_len(dom.first_child(n)) - 1) nth_from(dom.first_child(n), i)] }
fn descendants(n) { for (c in kids(n)) (c, descendants(c)) }

fn node_eq(x, y) { if (x == null or y == null) (x == null and y == null) else dom.same_node(x, y) }
fn list_eq(xs, ys) bool | error {
  if (len(xs) != len(ys)) false
  else if (len(xs) == 0) true
  else all([for (i in 0 to len(xs) - 1) node_eq(xs[i], ys[i])])
}


let forms_doc = dom.load("test/ui/test_form_controls.html")
let details_doc = dom.load("test/ui/details_accordion.html")
let form_nodes = [for (n in descendants(forms_doc) where dom.node_type(n) == 1) n]
let details_nodes = [for (n in descendants(details_doc) where dom.node_type(n) == 1) n]

{
  form_elements_checked: len(form_nodes),
  details_elements_checked: len(details_nodes),
  radio_group_sizes: [for (n in form_nodes where len(tree.radio_group(n)) > 0) len(tree.radio_group(n))],
  details_group_sizes: [for (n in details_nodes where len(tree.details_group(n)) > 0) len(tree.details_group(n))],
  form_owners: [for (n in form_nodes where tree.form_of(n) != null) dom.node_name(tree.form_of(n))],
  radios_found: len([for (n in form_nodes where len(tree.radio_group(n)) > 0) n]),
  details_found: len([for (n in details_nodes where len(tree.details_group(n)) > 0) n])
}
