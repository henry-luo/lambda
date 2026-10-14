import dom

view <frame_focus_probe> { ~.tree }
on prepare_frame(evt) {
    let owner = dom.get_element_by_id(dom.root_node(evt.target),"result")
    let focused = dom.focus_set(dom.get_element_by_id(owner,"focused"),false)
    dom.set_attribute(owner,"data-prepared",string(focused))
    dom.request_frame(owner,"hide_frame")
    return 'handled'
}
on hide_frame(evt) {
    dom.style_set_property(dom.get_element_by_id(evt.target,"panel"),"display","none")
    dom.set_attribute(evt.target,"data-finished","true")
    return 'handled'
}

<html apply(<frame_focus_probe tree:<body id:"result",
    <div id:"panel",<button id:"focused","Apply">>>>)>
