// lambda demo aquarium — native scene, editor state and frame callbacks in Lambda.
import dom
import camera: lambda.scene3d.camera
import model: .mod_model
import present: .mod_present
import ui: .mod_ui

fn owner_of(evt) => dom.get_element_by_id(dom.root_node(evt.target),"aquarium")
pn publish(owner,st) {
    if (not st.playing and st.token > 0) dom.cancel_frame(owner,st.token)
    let next = {*:st,token:if (st.playing) (if (st.token > 0) st.token else dom.request_frame(owner,"aquarium_frame")) else 0,
        last_ms:if (st.playing) st.last_ms else null}
    present.sync(owner,next)
    return next
}
pn command(owner,st,action) any^ {
    if (action == "play") { return {*:st,playing:not st.playing,last_ms:null} }
    if (action == "remove") { return model.remove(st) }
    if (action == "duplicate") { return model.duplicate(st) }
    if (action == "undo" or action == "redo") { return model.undo(st,action == "redo") }
    if (action == "clear" or action == "starter") {
        return {*:model.record(st,st.doc,if (action == "clear") [] else model.starter()),selected:"",status:"Tank updated. Undo restores your previous aquascape."}
    }
    if (action == "home") { return {*:st,camera:model.initial().camera} }
    if (action == "front") { return {*:st,camera:camera.prepare({position:[0.0,3.5,22.0],target:[0.0,3.5,0.0],fov:40.0})} }
    if (action == "zoom-in" or action == "zoom-out") {
        let factor = if (action == "zoom-in") 0.9 else 1/0.9
        let distance = sqrt(camera.dot(camera.subtract(st.camera.position,st.camera.target),camera.subtract(st.camera.position,st.camera.target)))
        return {*:st,camera:camera.control(st.camera,{type:"zoom",factor:model.clamp(distance*factor,12.0,45.0)/distance})}
    }
    if (action == "open" or action == "save") {
        let path = trim(dom.get_state(present.node(owner,"tank-file"),"value"))
        if (path == "" or not ends_with(lower(path),".json")) raise error("Aquarium: choose a .json tank file.")
        if (action == "open") { return model.restore(st,input(path,'json')^)^ }
        io.mkdir("temp") ^ { null }
        output(model.archive(st),path,{format:'json',atomic:true})^
        return {*:st,saved:st.doc,status:"Tank saved to " ++ path}
    }
    return st
}
pn dispatch(owner,st,action) {
    var next = model.finish(st)
    command(owner,next,action) ^ { next = {*:next,status:^.message} } ~ { next = ~ }
    return publish(owner,next)
}

view <aquarium_session> state session:model.initial() { ~.rendered }
on aquarium_activate(evt) {
    if (evt.event_phase != 2) { return 'pass' }
    session = publish(evt.target,session)
    return 'handled'
}
on click(evt) {
    let button = dom.closest(evt.target,"button")
    if (button == null or session.drag != null) { return 'pass' }
    let owner = owner_of(evt)
    let added = dom.get_attribute(button,"data-add")
    let selected = dom.get_attribute(button,"data-select")
    let category = dom.get_attribute(button,"data-category")
    session = if (added != null) publish(owner,model.add(model.finish(session),added))
        else if (selected != null) publish(owner,model.select(model.finish(session),selected))
        else if (category != null) publish(owner,{*:session,category:category})
        else dispatch(owner,session,dom.get_attribute(button,"data-action"))
    return 'handled'
}
on input(evt) {
    let key = dom.get_attribute(evt.target,"data-property")
    if (key == null) { return 'pass' }
    let value = float(dom.get_state(evt.target,"value"))
    let base = if (session.editing == null) {*:session,editing:session.doc} else session
    session = publish(owner_of(evt),model.property(base,key,value))
    return 'handled'
}
on change(evt) {
    if (dom.get_attribute(evt.target,"data-property") == null) { return 'pass' }
    session = publish(owner_of(evt),model.finish(model.property(session,dom.get_attribute(evt.target,"data-property"),float(dom.get_state(evt.target,"value")))))
    return 'handled'
}
on pointerdown(evt) {
    let viewport = dom.closest(evt.target,"scene3d")
    if (viewport == null or evt.button != 0) { return 'pass' }
    let owner = owner_of(evt)
    let measured = present.camera_for(owner,session)
    let x = evt.clientX-measured.bounds.x
    let y = evt.clientY-measured.bounds.y
    let hit = if (evt.altKey) null else model.pick(session,measured.camera,x,y)
    let base = model.finish(session)
    let chosen = model.lookup(base.doc,hit)
    // grab the visible swimming position so selecting a moving fish never jumps to its home.
    let visible = if (chosen != null) model.pose(chosen,base.time,chosen.id == base.selected).position else null
    let selected = if (chosen != null) model.preview(model.select(base,hit),{*:chosen,position:visible}) else model.select(base,"")
    let ground = chosen != null and (model.species(chosen.kind).category != "fish" or evt.shiftKey)
    session = publish(owner,{*:selected,drag:{mode:if (chosen == null) "camera" else "object",before:base.doc,
        object:if (chosen != null) model.lookup(selected.doc,hit) else null,
        start:if (chosen != null) model.drag_point(measured.camera,x,y,visible,ground) else null,
        camera:measured.camera,bounds:measured.bounds,x:evt.clientX,y:evt.clientY,ground:ground}})
    dom.focus_set(viewport,false)
    dom.set_pointer_capture(viewport,evt.pointerId)
    return 'prevent-default'
}
on pointermove(evt) {
    if (session.drag == null) { return 'pass' }
    let owner = owner_of(evt)
    let drag = session.drag
    if (drag.mode == "camera") {
        session = {*:session,camera:camera.control(drag.camera,{type:"orbit",yaw:(drag.x-evt.clientX)*0.006,pitch:(drag.y-evt.clientY)*0.006})}
        present.camera_sync(owner,session)
    } else {
        let point = model.drag_point(drag.camera,evt.clientX-drag.bounds.x,evt.clientY-drag.bounds.y,drag.object.position,drag.ground)
        session = model.preview(session,{*:drag.object,position:camera.add(drag.object.position,camera.subtract(point,drag.start))})
        present.sync(owner,session)
    }
    return 'prevent-default'
}
on pointerup(evt) {
    if (session.drag == null) { return 'pass' }
    let owner = owner_of(evt)
    dom.release_pointer_capture(present.node(owner,"tank-scene"),evt.pointerId)
    let next = if (session.drag.mode == "object") model.record(session,session.drag.before,session.doc) else session
    session = publish(owner,{*:next,drag:null})
    return 'prevent-default'
}
on pointercancel(evt) {
    if (session.drag == null) { return 'pass' }
    session = publish(owner_of(evt),{*:session,doc:session.drag.before,drag:null})
    return 'handled'
}
on wheel(evt) {
    if (dom.closest(evt.target,"scene3d") == null) { return 'pass' }
    session = dispatch(owner_of(evt),session,if (evt.deltaY < 0) "zoom-in" else "zoom-out")
    return 'prevent-default'
}
on keydown(evt) {
    if (dom.closest(evt.target,"input") != null) { return 'pass' }
    let key = lower(evt.key)
    let modifier = evt.ctrlKey or evt.metaKey
    let action = if (modifier and key == "z") (if (evt.shiftKey) "redo" else "undo")
        else if (modifier and key == "s") "save" else if (modifier and key == "d") "duplicate"
        else if (key == "delete" or key == "backspace") "remove" else if (key == " ") "play"
        else if (key == "escape") "deselect" else if (key == "+" or key == "=") "zoom-in" else if (key == "-") "zoom-out" else null
    if (action == "deselect") { session = publish(owner_of(evt),model.select(session,"")); return 'prevent-default' }
    if (action != null) { session = dispatch(owner_of(evt),session,action); return 'prevent-default' }
    return 'pass'
}
on aquarium_frame(evt) {
    if (evt.event_phase != 2 or evt.detail != session.token or not session.playing) { return 'pass' }
    let elapsed = if (session.last_ms == null) 0.0 else model.clamp((evt.time_stamp-session.last_ms)/1000.0,0.0,0.1)
    session = {*:session,time:session.time+elapsed,last_ms:evt.time_stamp,token:dom.request_frame(evt.target,"aquarium_frame")}
    present.animate(evt.target,session)
    return 'handled'
}
on closerequest(evt) {
    if (session.token > 0) dom.cancel_frame(owner_of(evt),session.token)
    session = {*:session,token:0,playing:false,drag:null}
    return 'pass'
}
view <aquarium_document> { ~.rendered }
on load(evt) {
    let owner = dom.get_element_by_id(evt.target,"aquarium")
    if (owner != null) dom.request_frame(owner,"aquarium_activate")
    return 'handled'
}
<html lang:"en",<head <meta charset:"UTF-8"><meta name:"viewport",content:"width=device-width,initial-scale=1">
    <title "Aquarium / Lambda"><link rel:"stylesheet",href:"aquarium.css">>
    apply(<aquarium_document rendered:<body apply(<aquarium_session rendered:ui.tree()>)>>)
>
