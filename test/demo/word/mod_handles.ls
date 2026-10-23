// Chrome lives outside the document; a preview never changes history or the saved checkpoint (D7.2.5).
import dom
import image: lambda.editor.mod_image
import controller: lambda.edit.controller
import transform: .mod_transform
import lambda.editor.mod_step

pn selected_element(editor,node) { return image.image_element(editor,controller.surface(node)) }
pub pn position(editor,node,zoom) {
    let target = selected_element(editor,node)
    let root = dom.root_node(node)
    let frame = dom.get_element_by_id(root,"object-handles")
    let origin = dom.bounding_box(dom.get_element_by_id(root,"paper-wrap"))
    let box = dom.bounding_box(target)
    let metrics = dom.scroll_metrics(target)
    let width = metrics.client_width*zoom
    let height = metrics.client_height*zoom
    let angle = transform.rotation(image.selected(editor).node)
    if (frame!=null and width>0 and height>0) {
        for (key,value in {left:box.x+box.width/2-origin.x-width/2,top:box.y+box.height/2-origin.y-height/2,width:width,height:height})
            dom.style_set_property(frame,string(key),string(value) ++ "px")
        dom.style_set_property(frame,"transform",transform.rotation_style(angle))
        dom.style_set_property(frame,"visibility","visible")
    }
}
pn clear(editor,node,drag) {
    let target = selected_element(editor,node)
    dom.set_attribute(target,"style",drag.style or "")
    dom.set_attribute(target,"src",drag.src)
    position(editor,node,drag.zoom)
}
view <word_object_handles> state drag:null {
    <div id:"object-handles",class:"object-handles",contenteditable:"false",tabindex:"-1",["aria-label"]:"Object size and rotation",
        <div class:"object-rotation-stem">;
        *[for (handle in ["nw","ne","sw","se","rotate"]) <button id:"object-" ++ handle,type:"button",
            class:"object-handle object-" ++ handle,["data-object-handle"]:handle,
            title:if (handle=="rotate") "Drag to rotate. Hold Shift to snap to 15°." else "Drag to resize proportionally.",
            ["aria-label"]:if (handle=="rotate") "Rotate object" else "Resize object " ++ handle>]>
}
on render(evt) {
    if (evt.event_phase==2) emit("word_object_position",{node:evt.target})
    return 'pass'
}
on mousedown() { return 'prevent-default' }
on pointerdown(evt) {
    let handle = dom.get_attribute(dom.closest(evt.target,"[data-object-handle]"),"data-object-handle")
    if (evt.button!=0 or handle==null) { return 'pass' }
    let target = selected_element(~.editor,evt.target)
    let metrics = dom.scroll_metrics(target)
    let box = dom.bounding_box(target)
    let value = transform.settings(image.selected(~.editor).node,{width:metrics.client_width,height:metrics.client_height})
    if (value==null or value.width<=0 or value.height<=0) { return 'prevent-default' }
    drag = {*:value,handle:handle,x:evt.clientX,y:evt.clientY,cx:box.x+box.width/2,cy:box.y+box.height/2,
        zoom:~.zoom,pointer:evt.pointerId,style:dom.get_attribute(target,"style"),src:dom.get_attribute(target,"src")}
    let host = dom.closest(evt.target,"#object-handles")
    dom.focus_set(host,true)
    host.set_pointer_capture(evt.pointerId)
    return 'prevent-default'
}
on pointermove(evt) {
    if (drag==null or drag.pointer!=evt.pointerId) { return 'pass' }
    let value = transform.gesture_value(drag,evt.clientX,evt.clientY,evt.shiftKey==true)
    let target = selected_element(~.editor,evt.target)
    let src = transform.preview_source(image.selected(~.editor).node,{*:value,scale_text:drag.handle!="rotate"})
    if (src!=null) dom.set_attribute(target,"src",src)
    dom.style_set_property(target,"width",string(value.width) ++ "px")
    dom.style_set_property(target,"height",string(value.height) ++ "px")
    dom.style_set_property(target,"transform",transform.rotation_style(value.rotation))
    // position reads the preview's used size, while its angle remains outside document state.
    let frame = dom.closest(evt.target,"#object-handles")
    position(~.editor,evt.target,~.zoom)
    dom.style_set_property(frame,"transform",transform.rotation_style(value.rotation))
    return 'prevent-default'
}
on pointerup(evt) {
    if (drag==null or drag.pointer!=evt.pointerId) { return 'pass' }
    let gesture = drag
    let value = transform.gesture_value(gesture,evt.clientX,evt.clientY,evt.shiftKey==true)
    drag = null
    clear(~.editor,evt.target,gesture)
    dom.release_pointer_capture(dom.closest(evt.target,"#object-handles"),evt.pointerId)
    if (value.width!=gesture.width or value.height!=gesture.height or value.rotation!=gesture.rotation)
        emit("word_command",{id:"object.transform",params:{*:value,path:~.path,scale_text:gesture.handle!="rotate"},node:evt.target,event:evt})
    else controller.focus(evt.target)
    return 'prevent-default'
}
on pointercancel(evt) {
    if (drag==null) { return 'pass' }
    let gesture = drag
    drag = null
    clear(~.editor,evt.target,gesture)
    dom.release_pointer_capture(dom.closest(evt.target,"#object-handles"),gesture.pointer)
    controller.focus(evt.target)
    return 'prevent-default'
}
on lostpointercapture(evt) {
    if (drag!=null) {
        let gesture = drag
        drag = null
        clear(~.editor,evt.target,gesture)
    }
    return 'pass'
}
on keydown(evt) {
    if (drag==null) { return 'pass' }
    if (evt.key=="Escape") {
        let gesture = drag
        drag = null
        clear(~.editor,evt.target,gesture)
        dom.release_pointer_capture(dom.closest(evt.target,"#object-handles"),gesture.pointer)
        controller.focus(evt.target)
    }
    return 'prevent-default'
}
pub fn tools(editor,zoom) {
    let selected = image.selected(editor)
    if (selected==null or transform.settings(selected.node)==null) null
    else apply(<word_object_handles editor:editor,path:selected.path,zoom:zoom>)
}
