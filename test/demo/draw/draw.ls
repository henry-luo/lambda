// S12.1.3 / D4.5.1v4: retain the shell and pointer owner; publish SVG from values.
import dom
import resources: ~~.mod_resources
import model: .mod_model
import scene: .mod_scene
import present: .mod_present
import geom: lambda.editor.draw.geometry
import interaction: lambda.editor.draw.interaction
import codec: lambda.editor.draw.codec
import viewport_math: lambda.editor.draw.viewport

let BASE = url_resolve(resources.entry_uri()^,".")
fn owner_of(evt) => dom.get_element_by_id(dom.root_node(evt.target),"draw")
fn field(owner,target) => dom.get_state(dom.get_element_by_id(owner,target),"value")
fn cancel(st) => interaction.cancel(st)
pn publish(owner,st) { present.sync(owner,st); return st }
fn finish_edit(st) => interaction.finish_edit(st)
fn arrow_trigger(st,key) => if (st.arrow_anchor == "connector-" ++ key) st.arrow_anchor else "prop-" ++ key
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
    if (action == "reset-bend") { return model.edit_properties(st,{waypoints:[],'bend-x':-1.0,'bend-y':-1.0})^ }
    if (action == "cycle-routing") {
        let current = string(model.attr(model.lookup(st.doc,st.selected).shape,"routing"))
        let indexes = [for (i,kind in model.routings where kind == current) i];
        return model.edit_properties(st,{routing:symbol(model.routings[(indexes[0]+1)%len(model.routings)])})^
    }
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
            let opened = model.restore_archive(input(path,'json')^)^;
            return model.open_document(st,opened)
        }
        io.mkdir("temp") ^ { null }
        io.mkdir("temp/draw") ^ { null }
        if (st.dialog == "export") {
            output(format(scene.svg(st.doc,"","",false,true),'xml'),path,{atomic:true})^
        } else { output(codec.encode(st.doc)^,path,{format:'json',atomic:true})^ }
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
    // native scroll positions adjust the cached transform without forcing layout on each move.
    let scroll = if (st.drag.page != null) dom.scroll_state(dom.get_element_by_id(owner,"stage")) else null
    let page = {x:st.drag.page.x+st.drag.scroll.x-scroll.x,y:st.drag.page.y+st.drag.scroll.y-scroll.y}
    let point = if (st.drag.page != null) viewport_math.point(page,st.zoom,x,y) else present.point(owner,st,x,y)
    return interaction.preview_drag(st,x,y,point,shift)
}

view <draw_session> state session:{*:model.initial(),arrow_menu:"",camera_dirty:false} { ~.rendered }
on draw_activate(evt) {
    if (evt.event_phase != 2) { return 'pass' }
    session = publish(evt.target,present.fit(evt.target,session))
    return 'handled'
}
on draw_camera(evt) {
    if (evt.event_phase != 2) { return 'pass' }
    session = present.apply_camera(evt.target,session)
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
        session = dispatch(owner,if (starts_with(action,"arrow-open-")) {*:session,arrow_anchor:dom.get_attribute(button,"id")} else session,action)
    } else { return 'pass' }
    session = publish(owner,session)
    if (starts_with(action,"arrow-open-") and session.arrow_menu != "") {
        let kind = model.attr(model.lookup(session.doc,session.selected).shape,session.arrow_menu)
        dom.focus_set(dom.get_element_by_id(owner,session.arrow_menu ++ "-option-" ++ kind),true)
    }
    if (starts_with(action,"arrow-set-")) dom.focus_set(dom.get_element_by_id(owner,arrow_trigger(session,split(slice(action,10),"/")[0])),false)
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
    // native navigation can precede its queued scroll notification in the same frame.
    if (session.camera_dirty == true) { session = present.camera(owner,session) }
    if (session.arrow_menu != "" and dom.closest(evt.target,"[data-arrow-key]") == null) { session = publish(owner,{*:session,arrow_menu:""}) }
    let stage = dom.get_element_by_id(owner,"stage")
    // the scroll surface fills the viewport; a direct stage hit belongs to native scrollbar chrome.
    if (evt.target == stage) {
        session = {*:session,camera_dirty:true}
        return 'pass'
    }
    if (session.dialog != "" or evt.button != 0 or dom.closest(evt.target,"#stage") == null or dom.closest(evt.target,"button") != null or session.drag != null) { return 'pass' }
    let viewport = present.viewport(owner)
    if (not geom.rect_contains(viewport,{x:evt.clientX,y:evt.clientY})) { return 'pass' }
    session = finish_edit(session)
    dom.focus_set(stage,false)
    let p = present.point(owner,session,evt.clientX,evt.clientY)
    if (contains(model.kinds,session.tool)) {
        if (p.x>=0 and p.y>=0 and p.x<=model.width and p.y<=model.height) { session = publish(owner,model.add(session,session.tool,p.x-80,p.y-40)) }
        return 'prevent-default'
    }
    if (session.tool == "connect") {
        session = interaction.begin_connection(session,{x:evt.clientX,y:evt.clientY,point:p,pointer:evt.pointerId})
        if (session.drag != null) {
            session = {*:session,drag:{*:session.drag,page:dom.bounding_box(dom.get_element_by_id(owner,"paper")),scroll:dom.scroll_state(stage)}}
            dom.set_pointer_capture(stage,evt.pointerId)
        }
        session = publish(owner,session)
        return 'prevent-default'
    }
    let ids = model.selected_ids(session)
    let chosen = if (len(ids)==1) model.lookup(session.doc,session.selected).shape else null
    let picked = model.handle_at(session.doc,chosen,p,session.zoom)
    let handle = picked.mode
    // selection handles take precedence over objects beneath them; avoid a full-scene pick.
    let target = if (handle != null) session.selected else model.hit(session.doc,p,6/session.zoom)
    let base_drag = {x:evt.clientX,y:evt.clientY,start:p,before:session.doc,selected:session.selected,ids:ids,pointer:evt.pointerId,
        targets:if (contains(["from","to"],handle)) model.connection_targets(session.doc) else null,
        segment:picked.segment,scroll:dom.scroll_state(stage),
        page:if (interaction.connector_drag(handle)) dom.bounding_box(dom.get_element_by_id(owner,"paper")) else null};
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
    if (interaction.connector_drag(session.drag.mode)) {
        dom.style_set_property(dom.get_element_by_id(owner,"connector-menu"),"display","none")
        dom.set_attribute(owner,"data-drag",session.drag.mode)
        session = present.sync_connector(owner,session)
    } else { session = publish(owner,session) }
    return 'prevent-default'
}
on mousedown(evt) {
    if (dom.get_attribute(evt.target,"id") == "stage" or session.dialog != "" or dom.closest(evt.target,"#stage") == null or dom.closest(evt.target,"button") != null) { return 'pass' }
    // the diagram owns mouse focus; document text-selection defaults would clear it.
    dom.focus_set(dom.get_element_by_id(owner_of(evt),"stage"),false)
    return 'prevent-default'
}
on pointermove(evt) {
    if (session.drag == null or evt.pointerId != session.drag.pointer) { return 'pass' }
    let owner = owner_of(evt)
    session = drag_to(owner,session,evt.clientX,evt.clientY,evt.shiftKey)
    if (interaction.connector_drag(session.drag.mode)) present.sync_connector(owner,session)
    else present.sync(owner,session)
    return 'prevent-default'
}
on pointerup(evt) {
    let drag = session.drag
    if (drag == null or evt.pointerId != drag.pointer) { return 'pass' }
    let owner = owner_of(evt)
    session = drag_to(owner,session,evt.clientX,evt.clientY,evt.shiftKey)
    let next = if (drag.mode == "connect") interaction.finish_connection(session)
        else if (contains(["pan","marquee"],drag.mode)) session else model.commit_edit(session,drag.before,session.doc)
    dom.release_pointer_capture(dom.get_element_by_id(owner,"stage"),evt.pointerId)
    session = publish(owner,{*:next,drag:null,hover:""})
    return 'prevent-default'
}
on pointercancel(evt) {
    if (session.drag == null or evt.pointerId != session.drag.pointer) { return 'pass' }
    dom.release_pointer_capture(dom.get_element_by_id(owner_of(evt),"stage"),evt.pointerId)
    session = publish(owner_of(evt),cancel(session))
    return 'prevent-default'
}
on canvas_scrolled(evt) {
    let connector = interaction.connector_drag(session.drag.mode)
    if ((session.drag != null and not connector) or (session.camera_dirty != true and not connector)) { return 'handled' }
    session = present.camera(owner_of(evt),session)
    present.sync_menu(owner_of(evt),session)
    return 'handled'
}
on wheel(evt) {
    if (session.dialog != "" or session.drag != null or dom.closest(evt.target,"#stage") == null) { return 'pass' }
    let owner = owner_of(evt)
    if (not evt.ctrlKey and not evt.metaKey) {
        session = {*:session,camera_dirty:true}
        return 'pass'
    }
    if (session.camera_dirty == true) { session = present.camera(owner,session) }
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
    if (session.camera_dirty == true) { session = present.camera(owner,session) }
    let picker = dom.closest(evt.target,"[data-arrow-key]")
    if (picker != null and session.dialog == "" and contains(["ArrowUp","ArrowDown","Home","End","Enter"," ","Escape","Tab"],evt.key)) {
        let property = dom.get_attribute(picker,"data-arrow-key")
        let focused = dom.get_attribute(dom.closest(evt.target,"[data-arrow-value]"),"data-arrow-value")
        let current = model.attr(model.lookup(session.doc,session.selected).shape,property)
        if (evt.key == "Tab" or evt.key == "Escape") {
            session = publish(owner,{*:session,arrow_menu:""})
            dom.focus_set(dom.get_element_by_id(owner,arrow_trigger(session,property)),true)
            return if (evt.key == "Tab") 'pass' else 'prevent-default'
        }
        if ((evt.key == "Enter" or evt.key == " ") and focused != null) {
            session = publish(owner,dispatch(owner,session,"arrow-set-" ++ property ++ "/" ++ focused))
            dom.focus_set(dom.get_element_by_id(owner,arrow_trigger(session,property)),true)
        } else {
            let positions = [for (i,kind in model.arrows where kind == (if (focused != null) focused else current)) i]
            let index = if (len(positions)>0) positions[0] else 0
            let next = if (evt.key == "Home") 0 else if (evt.key == "End") len(model.arrows)-1
                else if (session.arrow_menu != property or evt.key == "Enter" or evt.key == " ") index
                else (index+(if (evt.key == "ArrowUp") len(model.arrows)-1 else 1))%len(model.arrows)
            session = publish(owner,{*:finish_edit(session),arrow_menu:property,
                arrow_anchor:if (focused == null) dom.get_attribute(dom.closest(evt.target,"button"),"id") else session.arrow_anchor})
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
on resize(evt) {
    let owner = owner_of(evt)
    session = publish(owner,session)
    return 'pass'
}
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
