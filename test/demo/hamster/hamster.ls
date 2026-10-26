import dom
import camera: lambda.scene3d.camera
import scene_dom: lambda.scene3d.dom
import cage: .mod_cage
import model: .mod_model
import sim: .mod_sim
import ui: .mod_ui
import present: .mod_present

fn owner_of(evt) => dom.get_element_by_id(dom.root_node(evt.target),"hamster")
pn publish(owner,previous,st) {
    if (not st.playing and st.token>0) dom.cancel_frame(owner,st.token)
    let next = {*:st,token:if (st.playing) (if (st.token>0) st.token else dom.request_frame(owner,"hamster_frame")) else 0,
        last_ms:if (st.playing) st.last_ms else null}
    present.sync(owner,previous,next)
    return next
}
pn command(owner,st,action) any^ {
    if (action == "play") { return {*:st,playing:not st.playing,last_ms:null} }
    if (action == "speed") { return {*:st,speed:if (st.speed == 1) 4 else 1} }
    if (action == "skip") { return sim.advance(st,30.0) }
    if (action == "select") { return {*:st,tool:"select",item_tool:"",moving:false} }
    if (action == "delete") { return {*:st,tool:"delete",item_tool:"",moving:false,status:"Click a module on the floor plan or in the scene to delete it."} }
    if (action == "tab-modules" or action == "tab-furnishings") {
        return {*:st,tab:if (action == "tab-modules") "modules" else "furnishings",tool:"select",item_tool:"",moving:false}
    }
    if (action == "floor-0" or action == "floor-1") { return {*:st,level:if (action == "floor-0") 0 else 1,selected:""} }
    if (action == "rotate") { return model.rotate_selected(st) }
    if (action == "move") { return {*:st,tab:"modules",tool:"select",item_tool:"",moving:cage.lookup(st.modules,st.selected) != null} }
    if (action == "remove") { return model.remove(st) }
    if (action == "undo" or action == "redo") { return model.undo(st,action == "redo") }
    if (contains(["feed","water","clean","treat"],action)) { return model.care(st,action) }
    if (action == "adopt") { return model.adopt(st) }
    if (action == "rehome") {
        let pets = [for (h in st.pets where h.id != st.pet_selected) h]
        return {*:st,pets:pets,pet_selected:if (len(pets)>0) pets[0].id else "",status:"A tiny neighbor has moved on."}
    }
    if (action == "starter") { return {*:model.initial(),token:st.token,last_ms:st.last_ms,status:"The Cardboard Commons is ready for a new day."} }
    if (action == "blank") { return {*:model.initial(),modules:[],pets:[],saved:[],selected:"",pet_selected:"",next_id:1,next_pet:1,
        token:st.token,last_ms:st.last_ms,playing:st.playing,status:"A fresh beginning. Place a room, then adopt your first resident."} }
    if (action == "home") { return {*:st,camera:model.initial().camera,follow:false} }
    if (action == "top") { return {*:st,follow:false,camera:camera.prepare({position:[0.0,25.0,0.0],target:[0.0,0.0,0.0],up:[0.0,0.0,-1.0],fov:40.0})} }
    if (action == "follow") {
        let h = model.pet(st,st.pet_selected)
        if (h == null) { return st }
        let target = sim.position(st.modules,h)
        return {*:st,follow:not st.follow,camera:if (st.follow) model.camera_state(st)
            else camera.prepare({position:camera.add(target,[2.0,1.35,1.8]),target:target,fov:36.0})}
    }
    if (action == "zoom-in" or action == "zoom-out") {
        let cam = model.camera_state(st)
        let delta = camera.subtract(cam.position,cam.target)
        let distance = sqrt(camera.dot(delta,delta))
        let wanted = min(45.0,max(4.0,distance*(if (action == "zoom-in") 0.88 else 1/0.88)))
        return {*:st,camera:camera.control(cam,{type:"zoom",factor:wanted/distance})}
    }
    if (action == "open" or action == "save") {
        let path = trim(dom.get_state(scene_dom.node(owner,"cage-file"),"value"))
        if (path == "" or not ends_with(lower(path),".json")) raise error("Hamster: choose a .json cage file.")
        if (action == "open") { return model.restore(st,input(path,'json')^)^ }
        io.mkdir("temp") ^ { null }
        output(model.archive(st),path,{format:'json',atomic:true})^
        return {*:st,saved:st.modules,status:"Cage and resident progress saved to " ++ path}
    }
    return st
}
pn dispatch(owner,st,action) {
    var next = st
    command(owner,st,action) ^ { next = {*:st,status:^.message} } ~ { next = ~ }
    return publish(owner,st,next)
}
view <hamster_session> state session:model.initial() { ~.rendered }
on hamster_activate(evt) {
    if (evt.event_phase != 2) { return 'pass' }
    session = publish(evt.target,session,session)
    return 'handled'
}
on click(evt) {
    let button = dom.closest(evt.target,"button")
    if (button == null) { return 'pass' }
    let owner = owner_of(evt)
    let tool = dom.get_attribute(button,"data-tool")
    let x = dom.get_attribute(button,"data-x")
    let pet = dom.get_attribute(button,"data-pet")
    let item = dom.get_attribute(button,"data-item")
    let removed = dom.get_attribute(button,"data-remove-item")
    session = if (tool != null) publish(owner,session,{*:session,tool:tool,item_tool:"",tab:"modules",moving:false,selected:"",turn:0})
        else if (item != null) publish(owner,session,{*:session,item_tool:item,tool:"select",moving:false})
        else if (removed != null) publish(owner,session,model.furnish(session,removed,true))
        else if (x != null) publish(owner,session,model.build(session,int(x),int(dom.get_attribute(button,"data-z"))))
        else if (pet != null) publish(owner,session,{*:session,pet_selected:pet})
        else dispatch(owner,session,dom.get_attribute(button,"data-action"))
    return 'handled'
}
on change(evt) {
    if (dom.get_attribute(evt.target,"id") != "pet-name") { return 'pass' }
    session = publish(owner_of(evt),session,model.rename(session,dom.get_state(evt.target,"value")))
    return 'handled'
}
on pointerdown(evt) {
    let viewport = dom.closest(evt.target,"scene3d")
    if (viewport == null or evt.button != 0) { return 'pass' }
    let measured = scene_dom.measured_camera(viewport,model.camera_state(session))
    session = {*:session,drag:{x:evt.clientX,y:evt.clientY,bounds:measured.bounds,camera:measured.camera,moved:false}}
    dom.focus_set(viewport,false)
    dom.set_pointer_capture(viewport,evt.pointerId)
    return 'prevent-default'
}
on pointermove(evt) {
    if (session.drag == null) { return 'pass' }
    let drag = session.drag
    let dx = evt.clientX-drag.x
    let dy = evt.clientY-drag.y
    if (drag.moved or abs(dx)+abs(dy)>4) {
        session = {*:session,drag:{*:drag,moved:true},camera:camera.control(drag.camera,{type:"orbit",yaw:0-dx*0.006,pitch:0-dy*0.006})}
        present.camera_sync(owner_of(evt),session)
    }
    return 'prevent-default'
}
on pointerup(evt) {
    if (session.drag == null) { return 'pass' }
    let owner = owner_of(evt)
    let drag = session.drag
    dom.release_pointer_capture(scene_dom.node(owner,"cage-scene"),evt.pointerId)
    let x = evt.clientX-drag.bounds.x
    let y = evt.clientY-drag.bounds.y
    let pet = if (session.tool == "select" and session.item_tool == "" and session.tab == "modules" and not session.moving)
        model.pick_pet(session,drag.camera,x,y) else null
    let cell = cage.grid_point(drag.camera,x,y,session.level)
    let next = if (drag.moved) session else if (pet != null) {*:session,pet_selected:pet}
        else if (cell != null) model.build(session,cell.x,cell.z) else session
    session = publish(owner,session,{*:next,drag:null})
    return 'prevent-default'
}
on pointercancel(evt) { session = {*:session,drag:null}; return 'handled' }
on wheel(evt) {
    if (dom.closest(evt.target,"scene3d") == null) { return 'pass' }
    session = dispatch(owner_of(evt),session,if (evt.deltaY<0) "zoom-in" else "zoom-out")
    return 'prevent-default'
}
on keydown(evt) {
    if (dom.closest(evt.target,"input") != null) { return 'pass' }
    let key = lower(evt.key)
    let modifier = evt.ctrlKey or evt.metaKey
    let action = if (modifier and key == "z") (if (evt.shiftKey) "redo" else "undo")
        else if (modifier and key == "s") "save" else if (key == " ") "play" else if (key == "r") "rotate"
        else if (key == "delete" or key == "backspace") "remove" else if (key == "escape") "select"
        else if (key == "+" or key == "=") "zoom-in" else if (key == "-") "zoom-out" else null
    if (action == null) { return 'pass' }
    session = dispatch(owner_of(evt),session,action)
    return 'prevent-default'
}
on hamster_frame(evt) {
    if (evt.event_phase != 2 or evt.detail != session.token or not session.playing) { return 'pass' }
    let dt = if (session.last_ms == null) 0.0 else min(0.1,max(0.0,(evt.time_stamp-session.last_ms)/1000.0))*session.speed
    let previous = session
    session = {*:sim.advance(session,dt),last_ms:evt.time_stamp,token:dom.request_frame(evt.target,"hamster_frame")}
    present.animate(evt.target,session)
    if (int(session.elapsed*2) != int(previous.elapsed*2)) present.life(evt.target,session)
    return 'handled'
}
on closerequest(evt) {
    if (session.token>0) dom.cancel_frame(owner_of(evt),session.token)
    session = {*:session,token:0,playing:false,drag:null}
    return 'pass'
}
view <hamster_document> { ~.rendered }
on load(evt) {
    let owner = dom.get_element_by_id(evt.target,"hamster")
    if (owner != null) dom.request_frame(owner,"hamster_activate")
    return 'handled'
}
<html lang:"en",<head <meta charset:"UTF-8"><meta name:"viewport",content:"width=device-width,initial-scale=1">
    <title "Hamlet / Lambda Hamster"><link rel:"stylesheet",href:"hamster.css">>
    apply(<hamster_document rendered:<body apply(<hamster_session rendered:ui.tree()>)>>)
>
