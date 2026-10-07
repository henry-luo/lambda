// Temporary handler markup uses GC; the changed template body still publishes UI nodes.
import dom

view <allocation_probe> state calls: 0, total: 0, saved: null, held: null, held_value: "none", held_extra: "none" {
    <div id: "allocation-probe",
        <button id: "allocate", "Allocate">
        <output id: "allocation-result", string(calls) ++ ":" ++ string(total)>
        <output id: "held-value", held_value>
        <output id: "held-extra", held_extra>
        if (saved != null) saved
    >
}
on click(evt) {
    if (dom.get_attribute(evt.target, "id") != "allocate") { return 'pass' }
    dom.request_frame(dom.closest(evt.target, "#allocation-probe"), "allocate_frame")
    return 'handled'
}
on allocate_frame(evt) {
    // a detached wrapper retains its old attribute after later renders and collections.
    if (calls == 1) { held = dom.query_selector(evt.target, "#saved-markup") }
    let markup = [for (i in 0 to 999) format(<div ["data-index"]: i, <span "temporary">>, 'html')]
    total = sum([for (text in markup) len(text)])
    if (calls == 2) { dom.set_attribute(held, "data-extra", "edited") }
    if (held != null) {
        held_value = dom.get_attribute(held, "data-value")
        held_extra = dom.get_attribute(held, "data-extra")
    }
    calls = calls + 1
    saved = <span id: "saved-markup", ["data-value"]: "value " ++ string(calls), "retained " ++ string(calls)>
    return 'handled'
}

<html <body apply(<allocation_probe>)>>
