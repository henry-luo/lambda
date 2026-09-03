// ES43 oracle for the §5.7 form walkers, and the F32 ruling that they belong
// in Lambda.
//
// form_of, radio_group and details_group were native `radiant.*` bodies. None
// of them is mechanism -- each is policy over ordinary DOM reads -- so each now
// lives in lambda/package/dom/tree.ls, written in Lambda over the published
// core. This fixture holds the Lambda definitions and the native bodies to the
// same answers on every element of two real documents, which is what makes the
// move a refactor rather than a rewrite.
import radiant
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

fn agrees(n) bool | error {
  node_eq(tree.form_of(n), radiant.form_of(n)) and
  list_eq(tree.radio_group(n), radiant.radio_group(n)) and
  list_eq(tree.details_group(n), radiant.details_group(n))
}

let forms_doc = dom.load("test/ui/test_form_controls.html")
let details_doc = dom.load("test/ui/details_accordion.html")
let form_nodes = [for (n in descendants(forms_doc) where dom.node_type(n) == 1) n]
let details_nodes = [for (n in descendants(details_doc) where dom.node_type(n) == 1) n]

{
  form_elements_checked: len(form_nodes),
  details_elements_checked: len(details_nodes),
  divergent_in_forms: [for (n in form_nodes where agrees(n) != true) dom.node_name(n)],
  divergent_in_details: [for (n in details_nodes where agrees(n) != true) dom.node_name(n)],
  radios_found: len([for (n in form_nodes where len(tree.radio_group(n)) > 0) n]),
  details_found: len([for (n in details_nodes where len(tree.details_group(n)) > 0) n])
}
