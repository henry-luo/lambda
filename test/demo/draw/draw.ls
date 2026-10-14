// S12.1.3 / D4.5.1v4: retain the shell and pointer owner; publish SVG from values.
import dom
import resources: ~~.mod_resources
import model: .mod_model
import scene: .mod_scene
import present: .mod_present

let BASE = url_resolve(resources.entry_uri()^,".")
fn owner_of(evt) => dom.get_element_by_id(dom.root_node(evt.target),"draw")
fn field(owner,target) => dom.get_state(dom.get_element_by_id(owner,target),"value")
fn cancel(st) => {*:st,doc:if (st.drag.before != null) st.drag.before else st.doc,
    pan:if (st.drag.pan != null) st.drag.pan else st.pan,drag:null,source:"",tool:"select",dialog:"",error:""}
pn publish(owner,st) { present.sync(owner,st); return st }
pn apply_properties(owner,st) any^ {
    let selected = model.lookup(st.doc,st.selected).shape
    if (selected == null) { return st }
    let label = field(owner,"prop-label")
    if (len(label)>200) raise error("Draw: labels may contain at most 200 characters")
    var next = model.change(st,"label",label)
    if (selected.tag == 'connector') { next = model.change(next,"routing",symbol(field(owner,"prop-routing"))) }
    if (selected.tag == 'shape') {
        let values = [for (key in ["x","y","width","height"]) float(field(owner,"prop-" ++ key))^]
        let x = values[0]
        let y = values[1]
        let w = values[2]
        let h = values[3]
        if (not all([for (v in values) v >= 0 and v <= max(model.width,model.height)]) or
            w<60 or h<40 or x+w>model.width or y+h>model.height) raise error("Draw: keep the shape on the page, at least 60 × 40")
        for (i,key in ["x","y","width","height"]) { next = model.change(next,key,values[i]) }
    }
    return {*:model.commit_edit(st,st.doc,next.doc),error:"",status:"Properties updated."}
}
pn command(owner,st,action) any^ {
    if (st.drag != null) { return st }
    if (starts_with(action,"tool-")) { return {*:st,tool:slice(action,5),source:"",status:"Tool selected."} }
    if (starts_with(action,"add-")) { return {*:st,tool:slice(action,4),source:"",status:"Click on the page to place a shape."} }
    if (action == "undo" or action == "redo") { return model.step_history(st,action == "redo") }
    if (action == "duplicate") { return model.duplicate(st) }
    if (action == "delete") { return model.remove(st) }
    if (action == "front" or action == "back") { return model.reorder(st,action == "front") }
    if (action == "apply-properties") { return apply_properties(owner,st)^ }
    if (action == "grid") { return {*:st,grid:not st.grid} }
    if (action == "fit") { return present.fit(owner,st) }
    if (action == "zoom-in" or action == "zoom-out") {
        return {*:st,zoom:model.clamp(st.zoom*(if (action == "zoom-in") 1.2 else 1/1.2),0.1,3)}
    }
    if (action == "blank" or action == "example") {
        let changed = model.commit_edit(st,st.doc,if (action == "blank") model.drawing([]) else model.starter())
        return {*:changed,selected:"",source:"",tool:"select",status:"New page. Undo restores your previous diagram."}
    }
    if (contains(["open","save","export"],action)) {
        // file controls keep their native buffers while the surrounding editor updates.
        dom.set_state(dom.get_element_by_id(owner,"file-path"),"value",if (action == "export") "temp/draw/diagram.svg" else "temp/draw/diagram.json")
        return {*:st,dialog:action,error:""}
    }
    if (action == "close-dialog") { return {*:st,dialog:"",error:""} }
    if (action == "file-confirm") {
        let path = field(owner,"file-path")
        let extension = if (st.dialog == "export") ".svg" else ".json"
        if (not ends_with(lower(path),extension)) raise error("Draw: choose a " ++ extension ++ " file path")
        if (st.dialog == "open") {
            let opened = model.restore(input(path,'json')^)^;
            return model.open_document(st,opened)
        }
        io.mkdir("temp") ^ { null }
        io.mkdir("temp/draw") ^ { null }
        if (st.dialog == "export") {
            output(format(scene.svg(st.doc,"","",false,true),'xml'),path,{atomic:true})^
        } else { output(model.archive(st.doc),path,{format:'json',atomic:true})^ }
        return {*:st,saved:if (st.dialog == "save") st.doc else st.saved,dialog:"",error:"",status:"Written to " ++ path}
    }
    return st
}
pn dispatch(owner,st,action) {
    var result = st
    command(owner,st,action) ^ { result = {*:st,error:^.message,status:^.message} } ~ { result = ~ }
    return result
}

view <draw_session> state session:model.initial() { ~.rendered }
on draw_activate(evt) {
    if (evt.event_phase != 2) { return 'pass' }
    session = publish(evt.target,present.fit(evt.target,session))
    return 'handled'
}
on click(evt) {
    let owner = owner_of(evt)
    let button = dom.closest(evt.target,"button")
    let action = dom.get_attribute(button,"data-action")
    let color = dom.get_attribute(button,"data-color")
    if (button == null or (session.dialog != "" and dom.closest(button,"#file-dialog") == null)) { return 'pass' }
    let previous_dialog = session.dialog
    if (color != null) { session = model.change(session,"fill",color) }
    else if (action != null) {
        session = dispatch(owner,session,action)
    } else { return 'pass' }
    session = publish(owner,session)
    if (session.dialog != "" and previous_dialog == "") dom.focus_set(dom.get_element_by_id(owner,"file-path"),false)
    if (session.dialog == "" and previous_dialog != "") dom.focus_set(dom.get_element_by_id(owner,previous_dialog),false)
    return 'handled'
}
on pointerdown(evt) {
    let owner = owner_of(evt)
    let stage = dom.get_element_by_id(owner,"stage")
    if (session.dialog != "" or evt.button != 0 or dom.closest(evt.target,"#stage") == null or session.drag != null) { return 'pass' }
    dom.focus_set(stage,false)
    let p = present.point(owner,session,evt.clientX,evt.clientY)
    let target = model.hit(session.doc,p,6/session.zoom)
    if (contains(model.kinds,session.tool)) {
        if (p.x>=0 and p.y>=0 and p.x<=model.width and p.y<=model.height) { session = publish(owner,model.add(session,session.tool,p.x-80,p.y-40)) }
        return 'prevent-default'
    }
    if (session.tool == "connect") {
        if (target != "") { session = publish(owner,model.connect(session,target)) }
        return 'prevent-default'
    }
    let chosen = model.lookup(session.doc,session.selected).shape
    let b = model.box(chosen)
    let resize = b != null and abs(p.x-b.x-b.width)<=10/session.zoom and abs(p.y-b.y-b.height)<=10/session.zoom
    if (session.tool == "pan" or evt.shiftKey) {
        session = {*:session,drag:{mode:"pan",x:evt.clientX,y:evt.clientY,pan:session.pan,pointer:evt.pointerId}}
    } else {
        let selected = if (resize) session.selected else target
        session = {*:session,selected:selected,drag:if (model.box(model.lookup(session.doc,selected).shape) == null) null else
            {mode:if (resize) "resize" else "move",x:evt.clientX,y:evt.clientY,before:session.doc,selected:selected,pointer:evt.pointerId}}
    }
    if (session.drag != null) dom.set_pointer_capture(stage,evt.pointerId)
    session = publish(owner,session)
    return 'prevent-default'
}
on mousedown(evt) {
    if (session.dialog != "" or dom.closest(evt.target,"#stage") == null) { return 'pass' }
    // the diagram owns mouse focus; document text-selection defaults would clear it.
    dom.focus_set(dom.get_element_by_id(owner_of(evt),"stage"),false)
    return 'prevent-default'
}
on pointermove(evt) {
    let drag = session.drag
    if (drag == null or evt.pointerId != drag.pointer) { return 'pass' }
    let dx = evt.clientX-drag.x
    let dy = evt.clientY-drag.y
    session = if (drag.mode == "pan") {*:session,pan:{x:drag.pan.x+dx,y:drag.pan.y+dy}}
        else {*:session,doc:model.transform(drag.before,drag.selected,drag.mode,dx/session.zoom,dy/session.zoom,session.grid)}
    session = publish(owner_of(evt),session)
    return 'prevent-default'
}
on pointerup(evt) {
    let drag = session.drag
    if (drag == null or evt.pointerId != drag.pointer) { return 'pass' }
    let next = if (drag.mode == "pan") session else model.commit_edit(session,drag.before,session.doc)
    dom.release_pointer_capture(dom.get_element_by_id(owner_of(evt),"stage"),evt.pointerId)
    session = publish(owner_of(evt),{*:next,drag:null})
    return 'prevent-default'
}
on pointercancel(evt) {
    if (session.drag == null or evt.pointerId != session.drag.pointer) { return 'pass' }
    session = publish(owner_of(evt),cancel(session))
    return 'prevent-default'
}
on wheel(evt) {
    if (session.dialog != "" or session.drag != null or dom.closest(evt.target,"#stage") == null) { return 'pass' }
    let owner = owner_of(evt)
    let b = present.viewport(owner)
    let zoom = model.clamp(session.zoom*(if (evt.deltaY<0) 1.1 else 1/1.1),0.1,3)
    let factor = zoom/session.zoom;
    // keep the world point under the pointer fixed while zooming.
    session = publish(owner,{*:session,zoom:zoom,pan:{x:(evt.clientX-b.x-b.width/2)*(1-factor)+session.pan.x*factor,
        y:(evt.clientY-b.y-b.height/2)*(1-factor)+session.pan.y*factor}})
    return 'prevent-default'
}
on keydown(evt) {
    let owner = owner_of(evt)
    if (evt.key == "Escape") {
        if (session.drag != null) dom.release_pointer_capture(dom.get_element_by_id(owner,"stage"),session.drag.pointer)
        let dialog = session.dialog
        session = publish(owner,cancel(session))
        if (dialog != "") dom.focus_set(dom.get_element_by_id(owner,dialog),false)
        return 'prevent-default'
    }
    if (session.dialog != "") {
        if (evt.key == "Tab") {
            let order = ["file-path","file-cancel","file-confirm"]
            let positions = [for (i,target in order where target == dom.get_attribute(evt.target,"id")) i]
            let index = if (len(positions)>0) positions[0] else 0
            dom.focus_set(dom.get_element_by_id(owner,order[(index+(if (evt.shiftKey) 2 else 1))%3]),false)
            return 'prevent-default'
        }
        return 'pass'
    }
    if (dom.closest(evt.target,"input,textarea,select") != null or session.drag != null) { return 'pass' }
    let key = lower(evt.key)
    let modifier = evt.ctrlKey or evt.metaKey
    let action = if (modifier and key == "z") (if (evt.shiftKey) "redo" else "undo")
        else if (modifier and key == "y") "redo" else if (modifier and key == "d") "duplicate"
        else if (modifier and key == "s") "save" else if (key == "delete" or key == "backspace") "delete"
        else if (key == "v") "tool-select" else if (key == "c") "tool-connect" else if (key == "h") "tool-pan"
        else if (key == "+" or key == "=") "zoom-in" else if (key == "-") "zoom-out" else null
    if (contains(["arrowleft","arrowright","arrowup","arrowdown"],key)) {
        let amount = if (evt.shiftKey) 10 else 1
        session = publish(owner,model.nudge(session,if (key == "arrowleft") -amount else if (key == "arrowright") amount else 0,
            if (key == "arrowup") -amount else if (key == "arrowdown") amount else 0))
        return 'prevent-default'
    }
    if (action == null) { return 'pass' }
    session = dispatch(owner,session,action)
    session = publish(owner,session)
    if (session.dialog != "") dom.focus_set(dom.get_element_by_id(owner,"file-path"),false)
    return 'prevent-default'
}
on resize(evt) { session = publish(owner_of(evt),session); return 'pass' }
on closerequest(evt) { session = {*:session,drag:null}; return 'pass' }
view <draw_document> { ~.rendered }
on load(evt) {
    let owner = dom.get_element_by_id(evt.target,"draw")
    if (owner != null) dom.request_frame(owner,"draw_activate")
    return 'handled'
}
<html lang:"en",<head <meta charset:"UTF-8"><title "Draw — ideas, connected">
    <link rel:"stylesheet",href:url_resolve(BASE,"draw.css")>>
    apply(<draw_document rendered:<body apply(<draw_session rendered:scene.tree()>)>>)
>
