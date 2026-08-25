// F0b: interaction-state primitives (get_state / set_state) over canonical
// engine state. Storage stays native; these are the behavior-template waist.
import radiant

let doc = radiant.load("test/lambda/dom/state_prims.html")
let laid = radiant.layout(doc)
let root = radiant.root(doc)
let body = root.last_child
let cb = body.first_child

{
    // a generic pseudo-class round-trips through the state map
    generic_before: radiant.get_state(root, "visited"),
    generic_set:    radiant.set_state(root, "visited", true),
    generic_after:  radiant.get_state(root, "visited"),

    // form state routes to the form-control writer on a laid-out control
    control_tag:    cb.tag_name,
    checked_before: radiant.get_state(cb, "checked"),
    checked_set:    radiant.set_state(cb, "checked", true),
    checked_after:  radiant.get_state(cb, "checked"),
    uncheck:        radiant.set_state(cb, "checked", false),
    checked_end:    radiant.get_state(cb, "checked"),

    // engine-owned hot states are readable but not writable from script
    hover_read:     radiant.get_state(cb, "hover"),
    hover_write:    radiant.set_state(cb, "hover", true),

    // an unknown state name is rejected rather than silently stored
    unknown_read:   radiant.get_state(cb, "not_a_state"),
    unknown_write:  radiant.set_state(cb, "not_a_state", true),

    free: radiant.free(doc)
}
