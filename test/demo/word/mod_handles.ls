// Object gestures retain a source path; previews never change history (D7.2.5).
import dom
import lambda.editor.mod_doc
import image: lambda.editor.mod_image
import controller: lambda.edit.controller
import transform: .mod_transform
import objects: .mod_objects
import object_layout: .mod_object_layout
import page: .mod_page
import tables: .mod_table
import lambda.editor.mod_step
import lambda.editor.mod_source_pos

pn selected_element(editor,node,path=null) { return image.image_element(editor,controller.surface(node),path) }
pub pn hit(editor,node) {
    if (dom.closest(node,"#object-handles")!=null) { return image.selected(editor).path }
    return image.hit(editor,node,controller.surface(node))
}
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
        for (handle in ["nw","ne","sw","se"])
            dom.style_set_property(dom.get_element_by_id(root,"object-" ++ handle),"cursor",transform.resize_cursor(handle,angle))
    }
}
pn end_gesture(editor,node,gesture,release=true) {
    if (gesture.handle=="table") controller.restore(tables.native_editor(editor))
    else {
        let target = selected_element(editor,node,gesture.path)
        dom.set_attribute(target,"style",gesture.style or "")
        dom.set_attribute(target,"src",gesture.src)
        position(editor,node,gesture.zoom)
    }
    if (release) dom.release_pointer_capture(dom.get_element_by_id(dom.root_node(node),"paper-wrap"),gesture.pointer)
}
pub pn fit_surface(node) {
    let root = dom.root_node(node)
    let frame = dom.get_element_by_id(root,"surface-frame")
    let height = dom.bounding_box(controller.surface(node)).height
    // the stable HTML shell retains the scaled flow height across editor projections, preserving scroll extent.
    if (abs(dom.bounding_box(frame).height-height)>0.1)
        dom.style_set_property(dom.parent_node(dom.get_element_by_id(root,"word")),"--word-surface-height",string(height) ++ "px")
}
view <word_object_handles> state drag:null {
    let g = page.geometry(~.editor.doc)
    let editor = if (drag.handle=="table" and drag.active) tables.preview(~.editor,drag) else ~.editor
    let selected = image.selected(if (drag==null or drag.handle=="table") editor else image.select_image(editor,drag.path));
    <div id:"paper-wrap",class:"paper-wrap",tabindex:"-1",style:if (~.layout=="web") null else
        "width:" ++ string(g.width*~.zoom) ++ "px; min-height:" ++ string(g.height*~.zoom) ++ "px;",
        if (~.layout=="web") <div id:"surface-frame",class:"surface-frame",~.rendered> else ~.rendered;
        // equations have no resize frame; the frame appears only to host their in-place caret.
        if (~.disabled or ~.read_only or selected==null or (transform.settings(selected.node)==null and ~.caret==null) or
            object_layout.settings(selected.node).hidden) null else
        <div id:"object-handles",class:"object-handles" ++ (if (~.caret!=null) " math-editing" else ""),contenteditable:"false",tabindex:"-1",["aria-label"]:"Object move, size and rotation",
            <div id:"object-move",class:"object-move",["data-object-handle"]:"move",title:"Drag to move the object.",
                style:if (drag.handle=="move" and drag.active) "cursor:grabbing" else null>
            <div class:"object-rotation-stem">;
            *[for (handle in ["nw","ne","sw","se","rotate"]) <button id:"object-" ++ handle,type:"button",
                class:"object-handle object-" ++ handle,["data-object-handle"]:handle,
                title:if (handle=="rotate") "Drag to rotate. Hold Shift to snap to 15°." else "Drag to resize proportionally.",
                ["aria-label"]:if (handle=="rotate") "Rotate object" else "Resize object " ++ handle>];
            if (~.caret==null) null else <div id:"math-caret",class:"math-caret",["aria-hidden"]:"true",
                style:"left:" ++ string(~.caret.left) ++ "%;top:" ++ string(~.caret.top) ++ "%;height:" ++ string(~.caret.height) ++ "%">>;
        if (drag.handle=="move" and drag.active and drag.drop!=null)
            <div id:"object-drop-caret",class:"object-drop-caret",style:"left:" ++ string(drag.drop.caret.left) ++
                "px;top:" ++ string(drag.drop.caret.top) ++ "px;height:" ++ string(drag.drop.caret.height) ++ "px"> else null>
}
on render(evt) {
    if (evt.event_phase==2) {
        if (~.layout=="web") fit_surface(evt.target)
        let editor = if (drag.handle=="table" and drag.active) tables.preview(~.editor,drag)
            else if (drag==null or drag.handle=="table") ~.editor else image.select_image(~.editor,drag.path)
        tables.paint(editor,evt.target)
        position(editor,evt.target,~.zoom)
        if (drag.handle=="move" and drag.active and not drag.floating) dom.style_set_property(selected_element(editor,evt.target,drag.path),"opacity","0.45")
    }
    return 'pass'
}
on selectionchange(evt) {
    // this event does not bubble past the workspace's state owner.
    if (drag.handle=="table" and drag.active) { return 'pass' }
    emit("word_selectionchange",{event:evt})
    return 'pass'
}
on mousedown(evt) {
    if (drag.handle=="table" and drag.active) { return 'prevent-default' }
    if (not ~.disabled and not ~.read_only and
        (dom.closest(evt.target,"#object-handles")!=null or image.hit(~.editor,evt.target,controller.surface(evt.target))!=null)) {
        return 'prevent-default'
    }
    return 'pass'
}
on pointerdown(evt) {
    if (evt.button!=0 or ~.disabled or ~.read_only) { return 'pass' }
    let table_gesture = if (image.hit(~.editor,evt.target,controller.surface(evt.target))==null)
        tables.gesture(~.editor,evt) else null
    if (table_gesture!=null) {
        drag = table_gesture
        if (drag.active) {
            dom.get_element_by_id(dom.root_node(evt.target),"paper-wrap").set_pointer_capture(evt.pointerId)
            return 'prevent-default'
        }
        return 'pass'
    }
    let handle = dom.get_attribute(dom.closest(evt.target,"[data-object-handle]"),"data-object-handle")
    let path = if (handle!=null) image.selected(~.editor).path else image.hit(~.editor,evt.target,controller.surface(evt.target))
    if (path==null) { return 'pass' }
    let selected = image.select_image(~.editor,path)
    let target = selected_element(selected,evt.target,path)
    let metrics = dom.scroll_metrics(target)
    let box = dom.bounding_box(target)
    let kind = handle or "move"
    let value = transform.settings(image.selected(selected).node,{width:metrics.client_width,height:metrics.client_height})
    if (kind!="move" and (value==null or value.width<=0 or value.height<=0)) { return 'prevent-default' }
    // the stable workspace owns capture even when the first press selects an unselected image.
    drag = {*:if (value==null) {} else value,handle:kind,path:path,x:evt.clientX,y:evt.clientY,cx:box.x+box.width/2,cy:box.y+box.height/2,
        layout:object_layout.settings(image.selected(selected).node),floating:object_layout.floating(image.selected(selected).node),
        zoom:~.zoom,pointer:evt.pointerId,box:box,style:dom.get_attribute(target,"style"),src:dom.get_attribute(target,"src"),active:false,drop:null}
    let host = dom.get_element_by_id(dom.root_node(evt.target),"paper-wrap")
    dom.focus_set(host,true)
    host.set_pointer_capture(evt.pointerId)
    return 'prevent-default'
}
on pointermove(evt) {
    if (drag==null or drag.pointer!=evt.pointerId) { return 'pass' }
    if (drag.handle=="table") {
        let head = tables.point(~.editor,evt.target,evt.clientX,evt.clientY)
        if (head!=null and tables.context({*:~.editor,selection:node_selection(head)}).table_path==
            tables.context({*:~.editor,selection:node_selection(drag.path)}).table_path) {
            let active = drag.active or head!=drag.path
            if (active and not drag.active) {
                // end the text range before the rectangular cell preview takes over.
                dom.selection_remove_all_ranges(dom.document_selection(evt.target))
                dom.get_element_by_id(dom.root_node(evt.target),"paper-wrap").set_pointer_capture(evt.pointerId)
            }
            drag = {*:drag,head:head,active:active}
        }
        return if (drag.active) 'prevent-default' else 'pass'
    }
    let target = selected_element(~.editor,evt.target,drag.path)
    if (drag.handle=="move") {
        let dx = evt.clientX-drag.x
        let dy = evt.clientY-drag.y
        if (drag.active or dx*dx+dy*dy>25) {
            drag = {*:drag,active:true,drop:if (drag.floating) null else image.drop_target(~.editor,drag.path,evt,controller.surface(evt.target))}
            if (drag.floating) {
                dom.style_set_property(target,"left",string(drag.layout.x+dx/drag.zoom) ++ "px")
                dom.style_set_property(target,"top",string(drag.layout.y+dy/drag.zoom) ++ "px")
                position(image.select_image(~.editor,drag.path),evt.target,~.zoom)
            } else dom.style_set_property(target,"opacity","0.45")
        }
        return 'prevent-default'
    }
    let value = transform.gesture_value(drag,evt.clientX,evt.clientY,evt.shiftKey==true)
    let selected = image.select_image(~.editor,drag.path)
    let src = transform.preview_source(image.selected(selected).node,{*:value,scale_text:drag.handle!="rotate"})
    if (src!=null) dom.set_attribute(target,"src",src)
    dom.style_set_property(target,"width",string(value.width) ++ "px")
    dom.style_set_property(target,"height",string(value.height) ++ "px")
    dom.style_set_property(target,"transform",objects.transform_style(node_at(~.editor.doc,drag.path),value.rotation) or "")
    position(selected,evt.target,~.zoom)
    dom.style_set_property(dom.get_element_by_id(dom.root_node(evt.target),"object-handles"),"transform",transform.rotation_style(value.rotation))
    return 'prevent-default'
}
on pointerup(evt) {
    if (drag==null and evt.button==0 and not evt.shiftKey and tables.structural(~.editor) and
        tables.hit(~.editor,evt.target)!=null and evt.source_selection!=null) {
        // the projected caret may already be here, so a plain click still leaves structural selection mode.
        emit("word_table_select",{selection:evt.source_selection,node:evt.target})
        return 'pass'
    }
    if (drag==null or drag.pointer!=evt.pointerId) { return 'pass' }
    if (drag.handle=="table") {
        let gesture = drag
        drag = null
        if (not gesture.active) {
            if (gesture.was_structural and not evt.shiftKey and tables.structural(~.editor) and evt.source_selection!=null)
                emit("word_table_select",{selection:evt.source_selection,node:evt.target})
            return 'pass'
        }
        let chosen = tables.preview(~.editor,gesture)
        dom.release_pointer_capture(dom.get_element_by_id(dom.root_node(evt.target),"paper-wrap"),evt.pointerId)
        emit("word_table_select",{selection:chosen.selection,range:chosen.table_range,node:evt.target})
        return 'prevent-default'
    }
    let gesture = drag
    let move = gesture.handle=="move"
    let drop = if (move and gesture.active and not gesture.floating) image.drop_target(~.editor,gesture.path,evt,controller.surface(evt.target)) else null
    let value = if (move) null else transform.gesture_value(gesture,evt.clientX,evt.clientY,evt.shiftKey==true)
    drag = null
    end_gesture(~.editor,evt.target,gesture)
    if (move and gesture.active and gesture.floating) emit("word_command",{id:"object.layout",params:{values:{
        x:gesture.layout.x+(evt.clientX-gesture.x)/gesture.zoom,y:gesture.layout.y+(evt.clientY-gesture.y)/gesture.zoom}},
        selection:node_selection(gesture.path),node:evt.target,event:evt})
    else if (move and drop!=null) emit("word_command",{id:"object.move",params:{path:gesture.path,target:drop.target},
        selection:node_selection(gesture.path),node:evt.target,event:evt})
    else if (not move and (value.width!=gesture.width or value.height!=gesture.height or value.rotation!=gesture.rotation))
        emit("word_command",{id:"object.transform",params:{*:value,path:gesture.path,scale_text:gesture.handle!="rotate"},
            selection:node_selection(gesture.path),node:evt.target,event:evt})
    // a plain click reports where it landed so an equation can place its caret there.
    else emit("word_object_select",{path:gesture.path,node:evt.target,point:if (gesture.box.width>0 and gesture.box.height>0)
        {x:(evt.clientX-gesture.box.x)/gesture.box.width,y:(evt.clientY-gesture.box.y)/gesture.box.height} else null})
    return 'prevent-default'
}
on pointercancel(evt) {
    if (drag==null) { return 'pass' }
    let gesture = drag
    drag = null
    end_gesture(~.editor,evt.target,gesture)
    if (gesture.handle!="table") emit("word_object_select",{path:gesture.path,node:evt.target})
    return 'prevent-default'
}
on lostpointercapture(evt) {
    if (drag!=null) {
        let gesture = drag
        drag = null
        end_gesture(~.editor,evt.target,gesture,false)
    }
    return 'pass'
}
on keydown(evt) {
    if (drag==null) { return 'pass' }
    if (evt.key=="Escape") {
        let gesture = drag
        drag = null
        end_gesture(~.editor,evt.target,gesture)
        if (gesture.handle!="table") emit("word_object_select",{path:gesture.path,node:evt.target})
    }
    return 'prevent-default'
}
// the parent projects the document once; gesture redraws only reuse that rendered body.
pub fn workspace(editor,zoom,read_only,disabled,rendered,layout="print",caret=null) =>
    apply(<word_object_handles editor:editor,zoom:zoom,read_only:read_only,disabled:disabled,rendered:rendered,layout:layout,caret:caret>)
