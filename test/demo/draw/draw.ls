// S12.1.3 / D4.5.1v4: retain the shell and pointer owner; publish SVG from values.
import dom
import resources: ~~.mod_resources
import model: .mod_model
import scene: .mod_scene
import present: .mod_present
import geom: lambda.editor.mod_geom

let BASE = url_resolve(resources.entry_uri()^,".")
fn owner_of(evt) => dom.get_element_by_id(dom.root_node(evt.target),"draw")
fn field(owner,target) => dom.get_state(dom.get_element_by_id(owner,target),"value")
fn cancel(st) => {*:st,doc:if (st.drag.before != null) st.drag.before else if (st.editing.before != null) st.editing.before else st.doc,
    pan:if (st.drag.pan != null) st.drag.pan else st.pan,
    selection:if (st.drag.mode == "marquee") st.drag.previous else st.selection,
    selected:if (st.drag.mode == "marquee") st.drag.primary else st.selected,
    arrow_menu:"",editing:null,drag:null,source:"",tool:"select",dialog:"",error:"",hover:""}
pn publish(owner,st) { present.sync(owner,st); return st }
fn finish_edit(st) {
    if (st.editing == null) st else (
        let committed = model.commit_edit(st,st.editing.before,st.doc),
        {*:committed,editing:null})
}
pn update_property(owner,st,control,final=false) {
    let key = dom.get_attribute(control,"data-property")
    let base = if (st.editing.key != key) finish_edit(st) else st
    // preserve the last valid preview while the native field owns an incomplete draft.
    let editing = if (base.editing != null) base.editing else {key:key,before:base.doc}
    var result = {*:base,editing:editing}
    let input_value = dom.get_state(control,"value")
    let requested = (if (model.numeric_fields[key] != null) float(input_value) else if (key == "routing") symbol(input_value) else input_value) ^ {
        result = {*:result,error:"Draw: enter a valid number",status:"Enter a valid number."}
    } ~ {
        model.preview_properties(result,{[key]:~}) ^ { result = {*:result,error:^.message,status:^.message} }
            ~ { result = {*:~,status:"Properties updated."} }
    }
    if (final or dom.matches(control,"select")) { result = finish_edit(result) }
    return result
}
pn command(owner,st,action) any^ {
    if (st.drag != null) { return st }
    if (starts_with(action,"arrow-open-")) {
        let key = slice(action,11)
        return {*:st,arrow_menu:if (st.arrow_menu == key) "" else key}
    }
    if (starts_with(action,"arrow-set-")) {
        let choice = split(slice(action,10),"/")
        return {*:model.edit_properties(st,{[choice[0]]:choice[1]})^,arrow_menu:""}
    }
    if (starts_with(action,"tool-")) { return {*:st,tool:slice(action,5),source:"",status:"Tool selected."} }
    if (starts_with(action,"add-")) { return {*:st,tool:slice(action,4),source:"",status:"Click on the page to place a shape."} }
    if (action == "undo" or action == "redo") { return model.step_history(st,action == "redo") }
    if (action == "duplicate") { return model.duplicate(st) }
    if (action == "delete") { return model.remove(st) }
    if (action == "front" or action == "back") { return model.reorder(st,action == "front") }
    if (starts_with(action,"fill-")) { return model.edit_properties(st,{fill:slice(action,5)})^ }
    if (action == "copy-style") { return model.copy_style(st) }
    if (action == "paste-style") { return model.paste_style(st)^ }
    if (action == "reverse-edge") { return model.reverse_edge(st)^ }
    if (action == "reset-bend") { return model.edit_properties(st,{'bend-x':-1.0,'bend-y':-1.0})^ }
    if (starts_with(action,"align-")) { return model.align(st,slice(action,6))^ }
    if (contains(["rotate","flip-x","flip-y"],action)) { return model.orient(st,action) }
    if (action == "grid") { return {*:st,grid:not st.grid} }
    if (action == "fit") { return present.fit(owner,st) }
    if (action == "zoom-in" or action == "zoom-out") {
        return {*:st,zoom:model.clamp(st.zoom*(if (action == "zoom-in") 1.2 else 1/1.2),0.1,3)}
    }
    if (contains(["blank","example","showcase"],action)) {
        let changed = model.commit_edit(st,st.doc,if (action == "blank") model.drawing([]) else if (action == "showcase") model.showcase() else model.starter())
        return {*:changed,selected:"",selection:[],source:"",tool:"select",status:"New page. Undo restores your previous diagram."}
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
    let base = finish_edit(st)
    var result = base
    command(owner,base,action) ^ { result = {*:base,error:^.message,status:^.message} } ~ { result = ~ }
    return if (starts_with(action,"arrow-open-")) result else {*:result,arrow_menu:""}
}

pn drag_to(owner,st,x,y,shift=false) {
    let drag = st.drag
    let dx = x-drag.x
    let dy = y-drag.y
    let point = present.point(owner,st,x,y)
    let snapped = {x:model.snap(point.x,st.grid),y:model.snap(point.y,st.grid)}
    let drop = model.hit_shape(drag.before,point)
    if (dx == 0 and dy == 0 and not contains(["pan","marquee"],drag.mode)) { return {*:st,doc:drag.before} }
    if (drag.mode == "pan") { return {*:st,pan:{x:drag.pan.x+dx,y:drag.pan.y+dy}} }
    if (drag.mode == "marquee") {
        let ids = model.enclosed(st.doc,geom.rect_from_points(drag.start,point))
        return {*:model.select_ids(st,if (drag.additive) [*drag.previous,*ids] else ids),drag:{*:drag,current:point}}
    }
    if (contains(["from","to","bend"],drag.mode)) { return {*:st,hover:if (drag.mode == "bend") "" else drop,
        doc:model.edit_edge_document(drag.before,drag.selected,drag.mode,snapped,drop)} }
    if (drag.mode == "rotate") { return {*:st,doc:model.rotate_document(drag.before,drag.selected,point,shift)} }
    if (drag.mode == "resize") { return {*:st,doc:model.resize_document(drag.before,drag.selected,dx/st.zoom,dy/st.zoom,st.grid)} }
    return {*:st,doc:model.move_selection(drag.before,drag.ids,dx/st.zoom,dy/st.zoom,st.grid)}
}

view <draw_session> state session:{*:model.initial(),arrow_menu:""} { ~.rendered }
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
    if (color != null) { session = dispatch(owner,finish_edit(session),"fill-" ++ color) }
    else if (action != null) {
        session = dispatch(owner,session,action)
    } else { return 'pass' }
    session = publish(owner,session)
    if (starts_with(action,"arrow-open-") and session.arrow_menu != "") {
        let kind = model.attr(model.lookup(session.doc,session.selected).shape,session.arrow_menu)
        dom.focus_set(dom.get_element_by_id(owner,session.arrow_menu ++ "-option-" ++ kind),true)
    }
    if (starts_with(action,"arrow-set-")) dom.focus_set(dom.get_element_by_id(owner,"prop-" ++ split(slice(action,10),"/")[0]),false)
    if (session.dialog != "" and previous_dialog == "") dom.focus_set(dom.get_element_by_id(owner,"file-path"),false)
    if (session.dialog == "" and previous_dialog != "") dom.focus_set(dom.get_element_by_id(owner,previous_dialog),false)
    return 'handled'
}
on input(evt) {
    // selects emit input and change together; validate their committed choice once.
    if (session.dialog != "" or session.drag != null or dom.matches(evt.target,"select") or dom.get_attribute(evt.target,"data-property") == null) { return 'pass' }
    session = publish(owner_of(evt),update_property(owner_of(evt),session,evt.target))
    return 'handled'
}
on change(evt) {
    if (session.dialog != "" or session.drag != null) { return 'pass' }
    let owner = owner_of(evt)
    if (dom.get_attribute(evt.target,"id") == "object-list") {
        session = publish(owner,{*:model.select_ids(finish_edit(session),[dom.get_state(evt.target,"value")]),tool:"select",source:"",error:""})
    } else if (dom.get_attribute(evt.target,"data-property") != null) {
        session = publish(owner,update_property(owner,session,evt.target,true))
    } else { return 'pass' }
    return 'handled'
}
on focusout(evt) {
    if (session.arrow_menu != "" and dom.get_attribute(dom.closest(evt.relatedTarget,"[data-arrow-key]"),"data-arrow-key") != session.arrow_menu) {
        session = publish(owner_of(evt),{*:session,arrow_menu:""})
    }
    if (session.editing == null or dom.get_attribute(evt.target,"data-property") != session.editing.key) { return 'pass' }
    session = publish(owner_of(evt),finish_edit(session))
    return 'handled'
}
on pointerdown(evt) {
    let owner = owner_of(evt)
    if (session.arrow_menu != "" and dom.closest(evt.target,"[data-arrow-key]") == null) { session = publish(owner,{*:session,arrow_menu:""}) }
    let stage = dom.get_element_by_id(owner,"stage")
    if (session.dialog != "" or evt.button != 0 or dom.closest(evt.target,"#stage") == null or session.drag != null) { return 'pass' }
    let viewport = present.viewport(owner)
    if (not geom.rect_contains(viewport,{x:evt.clientX,y:evt.clientY})) { return 'pass' }
    session = finish_edit(session)
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
    let ids = model.selected_ids(session)
    let chosen = if (len(ids)==1) model.lookup(session.doc,session.selected).shape else null
    let handles = if (chosen.tag == 'connector') model.edge_handles(session.doc,chosen)
        else if (chosen.tag == 'shape') model.shape_handles(chosen,session.zoom) else {}
    let handle_hits = [for (key,position in handles where geom.v2_dist(position,p)<=10/session.zoom) string(key)]
    let handle = handle_hits[0]
    let base_drag = {x:evt.clientX,y:evt.clientY,start:p,before:session.doc,selected:session.selected,ids:ids,pointer:evt.pointerId};
    if (session.tool == "pan") {
        session = {*:session,drag:{*:base_drag,mode:"pan",pan:session.pan}}
    } else if (session.tool == "marquee" or (target == "" and handle == null)) {
        session = {*:session,drag:{*:base_drag,mode:"marquee",current:p,previous:ids,primary:session.selected,additive:evt.shiftKey},
            status:"Drag a rectangle around objects. Shift adds to the selection."}
    } else if (handle != null) {
        session = {*:session,drag:{*:base_drag,mode:handle}}
    } else if (evt.shiftKey) {
        let toggled = if (contains(ids,target)) [for (value in ids where value != target) value] else [*ids,target]
        session = model.select_ids(session,toggled)
    } else {
        session = if (contains(ids,target)) session else model.select_ids(session,[target])
        session = {*:session,drag:{*:base_drag,mode:"move",ids:model.selected_ids(session),selected:session.selected}}
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
    if (session.drag == null or evt.pointerId != session.drag.pointer) { return 'pass' }
    session = publish(owner_of(evt),drag_to(owner_of(evt),session,evt.clientX,evt.clientY,evt.shiftKey))
    return 'prevent-default'
}
on pointerup(evt) {
    let drag = session.drag
    if (drag == null or evt.pointerId != drag.pointer) { return 'pass' }
    let owner = owner_of(evt)
    session = drag_to(owner,session,evt.clientX,evt.clientY,evt.shiftKey)
    let next = if (contains(["pan","marquee"],drag.mode)) session else model.commit_edit(session,drag.before,session.doc)
    dom.release_pointer_capture(dom.get_element_by_id(owner,"stage"),evt.pointerId)
    session = publish(owner,{*:next,drag:null,hover:""})
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
    let picker = dom.closest(evt.target,"[data-arrow-key]")
    if (picker != null and session.dialog == "" and contains(["ArrowUp","ArrowDown","Home","End","Enter"," ","Escape","Tab"],evt.key)) {
        let property = dom.get_attribute(picker,"data-arrow-key")
        let focused = dom.get_attribute(dom.closest(evt.target,"[data-arrow-value]"),"data-arrow-value")
        let current = model.attr(model.lookup(session.doc,session.selected).shape,property)
        if (evt.key == "Tab" or evt.key == "Escape") {
            session = publish(owner,{*:session,arrow_menu:""})
            dom.focus_set(dom.get_element_by_id(owner,"prop-" ++ property),true)
            return if (evt.key == "Tab") 'pass' else 'prevent-default'
        }
        if ((evt.key == "Enter" or evt.key == " ") and focused != null) {
            session = publish(owner,dispatch(owner,session,"arrow-set-" ++ property ++ "/" ++ focused))
            dom.focus_set(dom.get_element_by_id(owner,"prop-" ++ property),true)
        } else {
            let positions = [for (i,kind in model.arrows where kind == (if (focused != null) focused else current)) i]
            let index = if (len(positions)>0) positions[0] else 0
            let next = if (evt.key == "Home") 0 else if (evt.key == "End") len(model.arrows)-1
                else if (session.arrow_menu != property or evt.key == "Enter" or evt.key == " ") index
                else (index+(if (evt.key == "ArrowUp") len(model.arrows)-1 else 1))%len(model.arrows)
            session = publish(owner,{*:finish_edit(session),arrow_menu:property})
            dom.focus_set(dom.get_element_by_id(owner,property ++ "-option-" ++ model.arrows[next]),true)
        }
        return 'prevent-default'
    }
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
    session = finish_edit(session)
    let key = lower(evt.key)
    let modifier = evt.ctrlKey or evt.metaKey
    if (modifier and key == "a") {
        session = publish(owner,model.select_ids(session,[for (node in model.objects(session.doc)) model.id(node)]))
        return 'prevent-default'
    }
    let action = if (modifier and evt.shiftKey and key == "c") "copy-style"
        else if (modifier and evt.shiftKey and key == "v") "paste-style"
        else if (modifier and key == "z") (if (evt.shiftKey) "redo" else "undo")
        else if (modifier and key == "y") "redo" else if (modifier and key == "d") "duplicate"
        else if (modifier and key == "s") "save" else if (key == "delete" or key == "backspace") "delete"
        else if (not modifier and key == "v") "tool-select" else if (not modifier and key == "c") "tool-connect" else if (not modifier and key == "h") "tool-pan"
        else if (not modifier and key == "m") "tool-marquee"
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
