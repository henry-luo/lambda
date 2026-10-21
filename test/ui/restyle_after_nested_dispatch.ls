// Regression: a handler that dispatches to another view (which re-renders from
// its state change) and then inserts markup must still cascade the inserted
// nodes -- the template splice used to drop the handler's pending DOM writes.
import dom
let rows = "<div><button type=\"button\" class=\"place\" data-place=\"1\"><span class=\"place-name\">Morning ridge</span><small>1</small></button></div>"
let css = ".place { display: grid; grid-template-columns: minmax(0, 1fr) auto; text-align: left; color: rgb(10, 132, 255); }"
view <other> state shown:null { <div id:"other", (if (shown == null) null else <p "map " ++ shown>)> }
on show_other(evt) { if (evt.event_phase != 2) { return 'pass' }; shown = "on"; return 'handled' }
view <host> { <div id:"host", <button id:"go", "go">; <div id:"wrap", <div id:"o", apply(<other>)>; <div id:"slot", style:"width:300px">>> }
on click(evt) {
    if (dom.get_attribute(evt.target, "id") != "go") { return 'pass' }
    let host = dom.get_element_by_id(dom.root_node(evt.target), "host")
    dom.style_set_property(dom.get_element_by_id(host, "wrap"), "display", "flex")
    dom.request_frame(host, "fill")
    return 'handled'
}
on fill(evt) {
    if (evt.event_phase != 2) { return 'pass' }
    dom.dispatch(dom.get_element_by_id(evt.target, "other"), {type:"show_other", bubbles:false, detail:{}})
    dom.set_inner_html(dom.get_element_by_id(evt.target, "slot"), rows)
    return 'handled'
}
<html <head <style css>> <body (apply(<host>))>>
