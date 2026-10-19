// an erased native member callee still obeys S12.1.1v3.
import dom
fn change(node) => node.set_attribute("data-colour", "wrong")
pn main() {
    let doc = dom.load("test/js/dom_identity.html")
    change(dom.get_element_by_id(doc, "intro"))
}
