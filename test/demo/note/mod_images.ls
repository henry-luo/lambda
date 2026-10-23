// Image chrome stays outside the source projection; dragging previews geometry until one model commit.
import dom
import controller: lambda.edit.controller
import model: .mod_model
import adapter: lambda.editor.mod_dom_adapter
import image: lambda.editor.mod_image

pub pn image_element(editor,node,path=null) {
    return image.image_element(editor,controller.surface(node),path)
}
pub pn hit(editor,node) {
    return image.hit(editor,node,controller.surface(node))
}
fn dimensions(box,target) {
    let width = round(max(16,min(4096,target)))
    {width:width,height:round(max(1,box.height*width/box.width))}
}
fn drag_dimensions(gesture,x,y) {
    let dx = (x-gesture.x)/gesture.width
    let dy = (y-gesture.y)/gesture.height
    dimensions(gesture,gesture.width*(1+if (abs(dx)>=abs(dy)) dx else dy))
}
pn clear_preview(editor,node) {
    let image = image_element(editor,node)
    dom.style_set_property(image,"width","")
    dom.style_set_property(image,"height","")
    dom.style_set_property(image,"opacity","")
    position(editor,node)
}
pn drop_target(editor,path,evt) {
    let surface = controller.surface(evt.target)
    let box = dom.bounding_box(surface)
    if (evt.clientX<box.x or evt.clientX>=box.x+box.width or evt.clientY<box.y or evt.clientY>=box.y+box.height) { return null }
    let target = adapter.normalize_source_pos(editor.doc,evt.source_pos)
    if (model.image_move_transaction(editor,path,target)==null) { return null }
    let boundary = dom.boundary_from_point(surface,int(evt.clientX),int(evt.clientY),null)
    if (boundary==null or not dom.contains(surface,boundary.node)) { return null }
    // a collapsed Range measures the drop caret without changing the image bookmark.
    let caret_range = dom.document_create_range(dom.root_node(surface))
    dom.range_set_start(caret_range,boundary.node,boundary.offset)
    dom.range_collapse(caret_range,true)
    let caret = dom.range_get_bounding_client_rect(caret_range)
    return if (caret.height>0) {target:target,caret:caret} else null
}
pub pn position(editor,node) {
    let image = image_element(editor,node)
    let root = dom.root_node(node)
    let frame = dom.get_element_by_id(root,"image-tools")
    let paper = dom.bounding_box(dom.get_element_by_id(root,"note-paper"))
    let box = dom.bounding_box(image)
    if (frame!=null and box.width>0) {
        dom.style_set_property(frame,"left",string(box.x-paper.x) ++ "px")
        dom.style_set_property(frame,"top",string(box.y-paper.y) ++ "px")
        dom.style_set_property(frame,"width",string(box.width) ++ "px")
        dom.style_set_property(frame,"height",string(box.height) ++ "px")
    }
}
view <image_tools> state drag:null {
    <div id:"image-tools",class:"image-tools",contenteditable:"false",tabindex:"-1",
        <div id:"image-move",class:"image-move",title:"Drag image to move it",style:if (drag.kind=="move" and drag.active) "cursor:grabbing" else null>
        <div class:"image-actions",
            <button id:"image-smaller",type:"button",title:"Make image smaller",["aria-label"]:"Make image smaller","−">
            <button id:"image-larger",type:"button",title:"Make image larger",["aria-label"]:"Make image larger","+">
            <button id:"image-original",type:"button","Original size">>
        <button id:"image-resize",type:"button",class:"image-handle",title:"Drag to resize image",["aria-label"]:"Resize image">
        if (drag.kind=="move" and drag.active and drag.drop!=null)
            <div id:"image-drop-caret",class:"image-drop-caret",style:"left:" ++ string(drag.drop.caret.left) ++ "px;top:" ++ string(drag.drop.caret.top) ++ "px;height:" ++ string(drag.drop.caret.height) ++ "px">
        else null>
}
on render(evt) {
    if (evt.event_phase==2) emit("note_image_position",{node:evt.target})
    return 'pass'
}
on mousedown() { return 'prevent-default' }
on click(evt) {
    let id = dom.get_attribute(dom.closest(evt.target,"button"),"id")
    if (contains(["image-smaller","image-larger","image-original"],id)) {
        let box = dom.bounding_box(image_element(~.editor,evt.target))
        let factor = if (id=="image-smaller") 0.8 else 1.25
        let size = if (id=="image-original") {width:null,height:null} else dimensions(box,box.width*factor)
        emit("note_command",{id:"image.resize",params:{path:~.path,*:size},
            node:evt.target,event:evt})
    }
    return 'prevent-default'
}
on pointerdown(evt) {
    let kind = if (dom.closest(evt.target,"#image-resize")!=null) "resize"
        else if (dom.closest(evt.target,"#image-move")!=null) "move" else null
    if (evt.button==0 and kind!=null) {
        let box = dom.bounding_box(image_element(~.editor,evt.target))
        drag = {kind:kind,x:evt.clientX,y:evt.clientY,width:box.width,height:box.height,pointer:evt.pointerId,active:false,drop:null}
        let host = dom.closest(evt.target,"#image-tools")
        dom.focus_set(host,true)
        host.set_pointer_capture(evt.pointerId)
        return 'prevent-default'
    }
    return 'pass'
}
on pointermove(evt) {
    if (drag!=null and drag.pointer==evt.pointerId) {
        if (drag.kind=="move") {
            let dx = evt.clientX-drag.x
            let dy = evt.clientY-drag.y
            let active = drag.active or dx*dx+dy*dy>25
            if (active) {
                let drop = drop_target(~.editor,~.path,evt)
                drag = {*:drag,active:true,drop:drop}
                dom.style_set_property(image_element(~.editor,evt.target),"opacity","0.45")
            }
            return 'prevent-default'
        }
        let size = drag_dimensions(drag,evt.clientX,evt.clientY)
        let image = image_element(~.editor,evt.target)
        let frame = dom.closest(evt.target,"#image-tools")
        let fitted = dimensions(drag,min(size.width,dom.bounding_box(dom.parent_node(image)).width))
        // the displayed image can be narrower than its saved size when constrained by the note pane.
        dom.style_set_property(image,"width",string(size.width) ++ "px")
        dom.style_set_property(image,"height","auto")
        for (entry in [{key:"width",value:fitted.width},{key:"height",value:fitted.height}]) {
            dom.style_set_property(frame,entry.key,string(entry.value) ++ "px")
        }
        return 'prevent-default'
    }
    return 'pass'
}
on pointerup(evt) {
    if (drag!=null and drag.pointer==evt.pointerId) {
        let gesture = drag
        let drop = if (gesture.kind=="move" and gesture.active) drop_target(~.editor,~.path,evt) else null
        drag = null
        clear_preview(~.editor,evt.target)
        dom.release_pointer_capture(dom.closest(evt.target,"#image-tools"),evt.pointerId)
        if (gesture.kind=="resize") {
            let size = drag_dimensions(gesture,evt.clientX,evt.clientY)
            emit("note_command",{id:"image.resize",params:{path:~.path,*:size},node:evt.target,event:evt})
        } else if (drop!=null) {
            emit("note_command",{id:"image.move",params:{path:~.path,target:drop.target},node:evt.target,event:evt})
        } else { controller.focus(evt.target) }
        return 'prevent-default'
    }
    return 'pass'
}
on pointercancel(evt) {
    if (drag!=null) {
        let pointer = drag.pointer
        drag = null
        clear_preview(~.editor,evt.target)
        dom.release_pointer_capture(dom.closest(evt.target,"#image-tools"),pointer)
        controller.focus(evt.target)
    }
    return 'pass'
}
on lostpointercapture(evt) {
    if (drag!=null) {
        drag = null
        clear_preview(~.editor,evt.target)
    }
    return 'pass'
}
on keydown(evt) {
    if (evt.key=="Escape" and drag!=null) {
        let pointer = drag.pointer
        drag = null
        clear_preview(~.editor,evt.target)
        dom.release_pointer_capture(dom.closest(evt.target,"#image-tools"),pointer)
        controller.focus(evt.target)
        return 'prevent-default'
    }
    return 'pass'
}
pub fn tools(editor) {
    let selected = model.selected_image(editor)
    if (selected==null) null else apply(<image_tools editor:editor,path:selected.path>)
}
