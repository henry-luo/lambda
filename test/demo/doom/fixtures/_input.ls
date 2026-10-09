import dom
let TREE = <main id:"input-probe", tabindex:"0",
    <button id:"lock", "Capture relative mouse">
    <button id:"release", "Release">
    <div id:"drag", style:"width:100px;height:50px;background:red;margin-top:20px;", "Drag">
>
view <input_probe> { ~.rendered }
on click(evt) {
    let owner = dom.closest(evt.target, "#input-probe")
    let id = dom.get_attribute(evt.target, "id")
    if (id == "lock" or id == "release") {
        dom.set_attribute(owner, "data-accepted", string(dom.set_relative_mouse(owner, id == "lock")))
        dom.set_attribute(owner, "data-relative", string(dom.relative_mouse_active(owner)))
    }
    dom.focus_set(owner, false)
    return 'handled'
}
on keydown(evt) {
    let owner = dom.closest(evt.target, "#input-probe")
    dom.set_attribute(owner, "data-key", evt.key ++ "/" ++ evt.code)
    if (evt.key == "p") {
        dom.set_relative_mouse(owner, false)
        dom.set_attribute(owner, "data-relative", string(dom.relative_mouse_active(owner)))
    }
    return 'prevent-default'
}
on mousemove(evt) {
    let owner = dom.closest(evt.target, "#input-probe")
    if (owner != null) dom.set_attribute(owner, "data-delta", string(evt.movementX) ++ "/" ++ string(evt.movementY))
    return 'pass'
}
on pointerdown(evt) {
    if (dom.get_attribute(evt.target, "id") != "drag") { return 'pass' }
    dom.set_attribute(evt.target, "data-capture", string(dom.set_pointer_capture(evt.target, evt.pointerId)))
    return 'prevent-default'
}
on pointermove(evt) {
    if (dom.has_pointer_capture(evt.target, evt.pointerId)) dom.set_attribute(evt.target, "data-x", string(evt.clientX))
    return 'pass'
}
on lostpointercapture(evt) {
    dom.set_attribute(evt.target, "data-capture", string(dom.has_pointer_capture(evt.target, evt.pointerId)))
    return 'pass'
}
view <input_document> { ~.rendered }
on blur(evt) {
    let owner = dom.get_element_by_id(dom.root_node(evt.target), "input-probe")
    dom.set_attribute(owner,"data-relative",string(dom.relative_mouse_active(owner)))
    dom.set_attribute(owner,"data-blurred","true")
    return 'pass'
}
<html <head <title "Native keyboard, relative motion and author pointer capture">>
    apply(<input_document rendered:<body apply(<input_probe rendered:TREE>)>>)>
