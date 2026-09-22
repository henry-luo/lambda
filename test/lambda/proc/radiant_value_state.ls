// `value` is engine-backed state (ES4): the HTML attribute is the default and
// the live buffer answers once the control has one — the same seed-then-live
// shape `checked` has with the `checked` attribute.
import radiant
import dom

let doc = dom.load("test/lambda/dom/value_state.html")
let laid = radiant.layout(doc)
let root = dom.document_element(doc)
let body = root.last_child
let seeded = body.first_child
let empty = seeded.next_sibling

{
    // before any write, the attribute is what the state reports
    seeded_default: dom.get_state(seeded, "value"),
    empty_default:  dom.get_state(empty, "value"),

    // a write goes to the live buffer
    write_ok:       dom.set_state(seeded, "value", "typed"),
    seeded_live:    dom.get_state(seeded, "value"),

    // and the attribute still holds the default, unchanged
    attr_unchanged: dom.get_attribute(seeded, "value"),

    // writing the empty control works the same way
    write_empty:    dom.set_state(empty, "value", "hello"),
    empty_live:     dom.get_state(empty, "value"),

    free: radiant.free(doc)
}
