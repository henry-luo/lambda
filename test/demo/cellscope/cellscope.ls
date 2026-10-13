// S12.1.3: values describe the scene; only event handlers mutate retained DOM.
import dom
import resources: ~~.mod_resources
import data: .mod_data
import model: .mod_model
import ui: .mod_ui
import present: .mod_present

let BASE = url_resolve(resources.entry_uri()^,".")
fn owner_of(evt) => dom.get_element_by_id(dom.root_node(evt.target),"cellscope")

pn publish(owner,previous,next) {
    if (previous.token > 0 and not next.rotating) dom.cancel_frame(owner,previous.token)
    let token = if (next.rotating) (if (previous.token > 0) previous.token else dom.request_frame(owner,"cell_frame")) else 0
    let updated = {*:next,token:token,last_ms:if (next.rotating) next.last_ms else null}
    present.sync(owner,updated,BASE)
    return updated
}
fn command(st,action) =>
    if (action == "mesh" or action == "focus") {*:st,focus:action == "focus" and model.procedural(st.cell)}
    else if (action == "cut" and model.procedural(st.cell)) {*:st,cut:not st.cut}
    else if (action == "compare") {*:st,compare:not st.compare}
    else if (action == "rotate") {*:st,rotating:not st.rotating,last_ms:null}
    else if (action == "favorite") model.favorite(st)
    else if (action == "library" or action == "gallery") {*:st,library:action == "library"}
    else if (action == "zoom-in" or action == "zoom-out") model.zoom(st,if (action == "zoom-in") 0.85 else 1/0.85)
    else if (action == "reset") model.reset(st) else st

view <cell_session> state session:model.initial() { ~.rendered }
on cell_activate(evt) {
    if (evt.event_phase != 2) { return 'pass' }
    session = publish(evt.target,session,session)
    return 'handled'
}
on click(evt) {
    let button = dom.closest(evt.target,"button")
    if (button == null) { return 'pass' }
    let cell = dom.get_attribute(button,"data-cell")
    let part = dom.get_attribute(button,"data-part")
    let action = dom.get_attribute(button,"data-action")
    if (action == "retry") {
        for (slot in dom.query_selector_all(owner_of(evt),"#imported-specimen")) dom.remove_attribute(slot,"data-model")
    }
    let next = if (cell != null) model.select_cell(session,cell)
        else if (part != null) {*:session,part:part} else command(session,action)
    session = publish(owner_of(evt),session,next)
    return 'handled'
}
on input(evt) {
    if (dom.get_attribute(evt.target,"id") != "search") { return 'pass' }
    session = publish(owner_of(evt),session,{*:session,filter:dom.get_state(evt.target,"value")})
    return 'handled'
}
on change(evt) {
    if (dom.get_attribute(evt.target,"id") != "peer") { return 'pass' }
    session = publish(owner_of(evt),session,{*:session,peer:dom.get_state(evt.target,"value")})
    return 'handled'
}
on pointerdown(evt) {
    if (evt.button != 0 or dom.closest(evt.target,"scene3d") == null) { return 'pass' }
    session = publish(owner_of(evt),session,{*:session,rotating:false,
        drag:{x:evt.clientX,y:evt.clientY,yaw:session.yaw,pitch:session.pitch,
            pan_x:session.pan_x,pan_y:session.pan_y,pan:evt.shiftKey}})
    dom.focus_set(evt.target,false)
    dom.set_pointer_capture(evt.target,evt.pointerId)
    return 'prevent-default'
}
on pointermove(evt) {
    if (session.drag == null) { return 'pass' }
    let drag = session.drag
    let x = evt.clientX-drag.x
    let y = evt.clientY-drag.y
    session = if (drag.pan) {*:session,pan_x:drag.pan_x-x*session.distance/700,pan_y:drag.pan_y+y*session.distance/700}
        else model.orbit({*:session,yaw:drag.yaw,pitch:drag.pitch},-x*0.008,y*0.008)
    present.camera(owner_of(evt),session)
    return 'prevent-default'
}
on pointerup(evt) {
    if (session.drag == null) { return 'pass' }
    dom.release_pointer_capture(evt.target,evt.pointerId)
    session = {*:session,drag:null}
    return 'prevent-default'
}
on pointercancel(evt) { session = {*:session,drag:null}; return 'pass' }
on wheel(evt) {
    if (dom.closest(evt.target,"scene3d") == null) { return 'pass' }
    session = model.zoom(session,if (evt.deltaY < 0) 0.9 else 1/0.9)
    present.camera(owner_of(evt),session)
    return 'prevent-default'
}
on keydown(evt) {
    if (dom.closest(evt.target,"scene3d") == null) { return 'pass' }
    let key = evt.key
    let next = if (key == "ArrowLeft") model.orbit(session,-0.12,0.0)
        else if (key == "ArrowRight") model.orbit(session,0.12,0.0)
        else if (key == "ArrowUp") model.orbit(session,0.0,0.12)
        else if (key == "ArrowDown") model.orbit(session,0.0,-0.12)
        else if (key == "+" or key == "=") command(session,"zoom-in")
        else if (key == "-" or key == "_") command(session,"zoom-out")
        else if (lower(key) == "r") command(session,"reset")
        else if (key == " ") command(session,"rotate") else null
    if (next == null) { return 'pass' }
    session = publish(owner_of(evt),session,next)
    return 'prevent-default'
}
on cell_frame(evt) {
    if (evt.event_phase != 2 or evt.detail != session.token or not session.rotating) { return 'pass' }
    let elapsed = if (session.last_ms == null) 0.0 else min(0.1,max(0.0,(evt.time_stamp-session.last_ms)/1000))
    session = {*:model.orbit(session,elapsed*0.28,0.0),last_ms:evt.time_stamp,
        token:dom.request_frame(evt.target,"cell_frame")}
    present.camera(evt.target,session)
    return 'handled'
}
on closerequest(evt) {
    if (session.token > 0) dom.cancel_frame(owner_of(evt),session.token)
    session = {*:session,token:0,rotating:false,drag:null}
    return 'pass'
}
view <cell_document> { ~.rendered }
on load(evt) {
    let owner = dom.get_element_by_id(evt.target,"cellscope")
    if (owner != null) dom.request_frame(owner,"cell_activate")
    return 'handled'
}
<html lang:"en",<head <meta charset:"UTF-8"><title "CellScope — Lambda">
    <link rel:"stylesheet",href:url_resolve(BASE,"cellscope.css")>>
    apply(<cell_document rendered:<body apply(<cell_session rendered:ui.tree()^>)>>)
>
