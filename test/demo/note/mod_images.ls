// Image chrome stays outside the source projection; dragging previews geometry until one model commit.
import dom
import controller: lambda.edit.controller
import model: .mod_model

pub fn image_index(editor,path) => [for (i,entry in model.images(editor.doc) where entry.path==path) i][0]
pub pn image_element(editor,node,path=null) {
    let selected = model.selected_image(editor)
    let index = image_index(editor,if (path!=null) path else selected.path)
    return if (index==null) null else dom.query_selector_all(controller.surface(node),"img")[index]
}
pub pn hit(editor,node) {
    let image = dom.closest(node,"#edit-surface img")
    if (image==null) { return null }
    let nodes = dom.query_selector_all(controller.surface(node),"img")
    let index = [for (i,n in nodes where n==image) i][0]
    return model.images(editor.doc)[index].path
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
    position(editor,node)
}
pn position(editor,node) {
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
        <div class:"image-actions",
            <button id:"image-smaller",type:"button",title:"Make image smaller",["aria-label"]:"Make image smaller","−">
            <button id:"image-larger",type:"button",title:"Make image larger",["aria-label"]:"Make image larger","+">
            <button id:"image-original",type:"button","Original size">>
        <button id:"image-resize",type:"button",class:"image-handle",title:"Drag to resize image",["aria-label"]:"Resize image">>
}
on render(evt) { if (evt.event_phase==2) { position(~.editor,evt.target) }; return 'pass' }
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
    if (evt.button==0 and dom.closest(evt.target,"#image-resize")!=null) {
        let box = dom.bounding_box(image_element(~.editor,evt.target))
        drag = {x:evt.clientX,y:evt.clientY,width:box.width,height:box.height,pointer:evt.pointerId}
        let host = dom.closest(evt.target,"#image-tools")
        dom.focus_set(host,true)
        host.set_pointer_capture(evt.pointerId)
        return 'prevent-default'
    }
    return 'pass'
}
on pointermove(evt) {
    if (drag!=null and drag.pointer==evt.pointerId) {
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
        let size = drag_dimensions(drag,evt.clientX,evt.clientY)
        emit("note_command",{id:"image.resize",params:{path:~.path,*:size},node:evt.target,event:evt})
        drag = null
        return 'prevent-default'
    }
    return 'pass'
}
on pointercancel(evt) {
    if (drag!=null) {
        clear_preview(~.editor,evt.target)
        drag = null
    }
    return 'pass'
}
on keydown(evt) {
    if (evt.key=="Escape" and drag!=null) {
        clear_preview(~.editor,evt.target)
        drag = null
        controller.focus(evt.target)
        return 'prevent-default'
    }
    return 'pass'
}
pub fn tools(editor) {
    let selected = model.selected_image(editor)
    if (selected==null) null else apply(<image_tools editor:editor,path:selected.path>)
}
