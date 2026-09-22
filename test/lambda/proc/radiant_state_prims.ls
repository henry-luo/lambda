// F0b: interaction-state primitives (get_state / set_state) over canonical
// engine state. Storage stays native; these are the behavior-template waist.
import radiant
import dom

let doc = dom.load("test/lambda/dom/state_prims.html")
let laid = radiant.layout(doc)
let root = dom.document_element(doc)
let body = root.last_child
let cb = body.first_child

{
    // a generic pseudo-class round-trips through the state map
    generic_before: dom.get_state(root, "visited"),
    generic_set:    dom.set_state(root, "visited", true),
    generic_after:  dom.get_state(root, "visited"),

    // form state routes to the form-control writer on a laid-out control
    control_tag:    cb.tag_name,
    checked_before: dom.get_state(cb, "checked"),
    checked_set:    dom.set_state(cb, "checked", true),
    checked_after:  dom.get_state(cb, "checked"),
    uncheck:        dom.set_state(cb, "checked", false),
    checked_end:    dom.get_state(cb, "checked"),

    // engine-owned hot states are readable but not writable from script
    hover_read:     dom.get_state(cb, "hover"),
    hover_write:    dom.set_state(cb, "hover", true),

    // an unknown state name reads null (not false, which would be
    // indistinguishable from a legitimately-false state) and is rejected on write
    unknown_read:   dom.get_state(cb, "not_a_state"),
    unknown_write:  dom.set_state(cb, "not_a_state", true),

    free: radiant.free(doc)
}
