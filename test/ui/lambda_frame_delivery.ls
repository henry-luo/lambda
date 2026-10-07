import dom

view <probe> state token: 0, calls: 0, last_stamp: -1.0, later: 0, saved_selection: null {
    ~.tree
}
on click(evt) {
    if (dom.get_attribute(evt.target, "id") != "start") { return 'pass' }
    let owner = dom.closest(evt.target, "#owner")
    let other = dom.get_element_by_id(owner, "other")
    let victim = dom.get_element_by_id(owner, "victim")
    let cancelled = dom.request_frame(owner, "cancelled_frame")
    dom.set_attribute(owner, "data-cancelled", if (dom.cancel_frame(owner, cancelled)) "true" else "false")
    token = dom.request_frame(owner, "probe_frame")
    dom.set_attribute(owner, "data-wrong-owner", if (dom.cancel_frame(other, token)) "true" else "false")
    dom.request_frame(victim, "detached_frame")
    dom.remove_child(owner, victim)
    let label = dom.first_child(dom.get_element_by_id(owner, "start"))
    let selection = dom.document_selection(owner)
    dom.set_base_and_extent(selection, label, 0, label, 0)
    dom.request_frame(owner, "earlier_frame")
    later = dom.request_frame(owner, "later_frame")
    return 'handled'
}
on probe_frame(evt) {
    let owner = evt.target
    if (calls == 0) {
        saved_selection = evt.source_selection
        dom.set_attribute(owner, "data-source-present", if (saved_selection is map) "true" else "false")
    }
    if (calls > 0) {
        dom.set_attribute(owner, "data-source-retained", if (saved_selection != null and
            saved_selection.kind == 'text' and saved_selection.anchor.path == evt.source_selection.anchor.path and
            saved_selection.anchor.offset == evt.source_selection.anchor.offset) "true" else "false")
    }
    dom.set_attribute(owner, "data-token-matches", if (evt.detail == token) "true" else "false")
    dom.set_attribute(owner, "data-time-increases", if (evt.time_stamp > last_stamp) "true" else "false")
    last_stamp = evt.time_stamp
    calls = calls + 1
    dom.set_attribute(owner, "data-calls", string(calls))
    if (calls < 2) { token = dom.request_frame(owner, "probe_frame") }
    return 'handled'
}
on cancelled_frame(evt) { dom.set_attribute(evt.target, "data-cancelled", "false") }
on detached_frame(evt) { dom.set_attribute(evt.target, "data-detached-fired", "true") }
on earlier_frame(evt) { dom.set_attribute(evt.target, "data-cross-cancel", if (dom.cancel_frame(evt.target, later)) "true" else "false") }
on later_frame(evt) { dom.set_attribute(evt.target, "data-cross-cancel", "false") }

let source = <probe tree: <div id: "owner", ["data-cancelled"]: "false", ["data-detached-fired"]: "false",
    <button id: "start", "Start"> <div id: "other"> <div id: "victim">>>;
<html <body apply(source)>>
