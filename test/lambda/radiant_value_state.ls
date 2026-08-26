// `value` is engine-backed state (ES4): the HTML attribute is the default and
// the live buffer answers once the control has one — the same seed-then-live
// shape `checked` has with the `checked` attribute.
import radiant

let doc = radiant.load("test/lambda/dom/value_state.html")
let laid = radiant.layout(doc)
let root = radiant.root(doc)
let body = root.last_child
let seeded = body.first_child
let empty = seeded.next_sibling

{
    // before any write, the attribute is what the state reports
    seeded_default: radiant.get_state(seeded, "value"),
    empty_default:  radiant.get_state(empty, "value"),

    // a write goes to the live buffer
    write_ok:       radiant.set_state(seeded, "value", "typed"),
    seeded_live:    radiant.get_state(seeded, "value"),

    // and the attribute still holds the default, unchanged
    attr_unchanged: radiant.attr(seeded, "value"),

    // writing the empty control works the same way
    write_empty:    radiant.set_state(empty, "value", "hello"),
    empty_live:     radiant.get_state(empty, "value"),

    free: radiant.free(doc)
}
