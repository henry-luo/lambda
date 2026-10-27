// native illustration demo. Views are pure; all DOM/file effects live in procedures (S12.1.3, D7.4.6).
import dom
import resources: ~~.mod_resources
import m: .mod_model
import art: .mod_artwork
import scene: .mod_scene
import present: .mod_present
import raster: .mod_raster
import menus: ~~.mod_context_menu
import keyboard: lambda.dom.keyboard

let BASE = url_resolve(resources.entry_uri()^,".")
fn owner_of(evt) => dom.get_element_by_id(dom.root_node(evt.target),"sketch")
pn publish(owner,previous,next) { return present.sync(owner,next,previous) }
pn focus_canvas(owner) { dom.focus_set(present.node(owner,"stage"),false) }
fn paste_source(st,snapshot) => if (len(st.clipboard)>0 and st.clipboard_snapshot==snapshot) null else raster.clipboard_source(snapshot)
pn open_context(owner,st,x,y,pick=true) {
    let p = present.point(owner,st,x,y)
    let base = m.finish_edit(st)
    let selected = if (pick and len(st.pen)==0) m.context_select(base,p) else base
    let snapshot = dom.clipboard_read()
    let next = {*:selected,context:{x:x,y:y,point:p,image:paste_source(st,snapshot)!=null},tool:if (len(st.pen)>0) st.tool else "select"}
    dom.request_frame(owner,"sketch_menu_ready")
    return next
}
pn command(owner,st,action,shift=false) any^ {
    if (st.drag != null) { return st }
    if (contains(["dock-properties","dock-layers"],action)) { return {*:st,dock_tab:slice(action,5)} }
    if (starts_with(action,"tool-")) { return {*:st,tool:slice(action,5),pen:[],error:"",status:"Tool selected."} }
    if (starts_with(action,"color-")) {
        let color = slice(action,6)
        let text_ink = m.chosen(st).kind=="text"
        let next = m.finish_edit(m.property(st,if (shift or text_ink) "fill" else "stroke",color)^)
        return if (text_ink and not shift) {*:next,stroke:color} else next
    }
    if (starts_with(action,"layer-")) {
        let parts = split(slice(action,6),"/")
        return m.layer_action(st,parts[0],if (len(parts)>1) parts[1] else "")
    }
    if (action=="undo" or action=="redo") { return m.step_history(st,action=="redo") }
    if (action=="delete") { return m.remove(st) }
    if (action=="duplicate") { return m.duplicate(st) }
    if (action=="copy" or action=="cut") {
        if (len(m.selected_ids(st))==0) { return st }
        // remember the external snapshot so an older OS image cannot override a fresh internal copy.
        return {*:m.copy_selection(st,action=="cut"),clipboard_snapshot:dom.clipboard_read()}
    }
    if (action=="paste" or action=="paste-here") {
        let point = if (action=="paste-here") st.context.point else null
        let src = paste_source(st,dom.clipboard_read())
        return if (src!=null) m.add_image(st,raster.from_uri(src)^,point) else m.paste(st,point)
    }
    if (action=="select-all") { return m.select_ids(st,[for (o in m.objects(st.doc)) o.id]) }
    if (action=="deselect") { return m.select_ids(st,[]) }
    if (action=="front" or action=="back") { return m.arrange(st,action=="front") }
    if (action=="finish-path" or action=="close-path") { return m.finish_pen(st,action=="close-path") }
    if (action=="grid") { return {*:st,grid:not st.grid} }
    if (action=="fit") { return present.fit(owner,st) }
    if (action=="zoom-in" or action=="zoom-out") { return {*:st,zoom:m.clamp(st.zoom*(if (action=="zoom-in") 1.2 else 1/1.2),.1,8)} }
    if (action=="blank" or action=="example") {
        let doc = if (action=="blank") m.blank() else art.starter()
        return present.fit(owner,{*:m.record_edit(st,doc,"New illustration. Undo restores your previous work."),active:doc.layers[last].id,selected:"",selection:[],pen:[]})
    }
    if (contains(["save","open","export","import-image"],action)) {
        dom.set_state(present.node(owner,"file-path"),"value",if (action=="import-image") "" else if (action=="export") "temp/sketch/illustration.svg" else "temp/sketch/illustration.sketch")
        return {*:st,dialog:action,error:""}
    }
    if (action=="close-dialog") { return {*:st,dialog:"",error:""} }
    if (action=="file-confirm") {
        let path = dom.get_state(present.node(owner,"file-path"),"value")
        if (st.dialog=="import-image") {
            let next = m.add_image(st,raster.read(path)^)
            return {*:next,dialog:if (next.error=="") "" else st.dialog}
        }
        let ext = if (st.dialog=="export") ".svg" else ".sketch"
        if (not ends_with(lower(path),ext)) raise error("Sketch: choose a " ++ ext ++ " file path.")
        if (st.dialog=="open") { return present.fit(owner,m.open_document(st,m.decode(input(path,'json')^)^)) }
        io.mkdir("temp") ^ { null }
        io.mkdir("temp/sketch") ^ { null }
        if (st.dialog=="export") { output(format(scene.svg(st.doc),'xml'),path,{atomic:true})^ }
        else { output(m.encode(st.doc),path,{format:'json',atomic:true})^ }
        return {*:st,saved:if (st.dialog=="save") st.doc else st.saved,dialog:"",error:"",status:"Written to " ++ path}
    }
    return st
}
pn dispatch(owner,st,action,shift=false) {
    let base = m.finish_edit(st)
    var result = base
    command(owner,base,action,shift) ^ { result = {*:base,error:^.message,status:^.message} } ~ { result = ~ }
    return {*:result,context:null}
}
pn update_property(st,control,final=false) {
    let key = dom.get_attribute(control,"data-property")
    var result = st
    m.property(st,key,dom.get_state(control,"value")) ^ { result = {*:st,error:^.message} } ~ { result = ~ }
    return if (final) m.finish_edit(result) else result
}

view <sketch_session> state session:{*:m.initial(art.starter()),dock_tab:"properties",context:null,clipboard_snapshot:null} { ~.rendered }
on sketch_activate(evt) {
    if (evt.event_phase != 2) { return 'pass' }
    dom.watch_window(evt.target,"resize",true)
    session = publish(evt.target,null,present.fit(evt.target,session))
    return 'handled'
}
on sketch_menu_ready(evt) {
    if (evt.event_phase!=2 or session.context==null) { return 'pass' }
    // placement and focus wait for the newly visible menu's committed layout (D7.4.6).
    menus.fit(evt.target,"sketch-context-menu","sketch")
    menus.focus(evt.target,"sketch-context-menu")
    return 'handled'
}
on contextmenu(evt) {
    if (session.dialog!="" or session.drag!=null or dom.closest(evt.target,"#stage")==null or dom.closest(evt.target,"button")!=null) { return 'pass' }
    let owner = owner_of(evt)
    session = publish(owner,session,open_context(owner,session,evt.clientX,evt.clientY))
    return 'prevent-default'
}
on click(evt) {
    if (evt.button!=null and evt.button!=0) { return 'pass' }
    let owner = owner_of(evt)
    let button = dom.closest(evt.target,"button[data-action]")
    if (button==null or (session.dialog!="" and dom.closest(button,"#file-dialog")==null)) { return 'pass' }
    let action = dom.get_attribute(button,"data-action")
    let before = session
    session = publish(owner,before,dispatch(owner,session,action,evt.shiftKey))
    if (session.dialog!="" and before.dialog=="") dom.focus_set(present.node(owner,"file-path"),false)
    else if (session.dialog=="" and before.dialog!="") dom.focus_set(present.node(owner,before.dialog),false)
    else if (starts_with(action,"dock-")) dom.focus_set(button,false)
    else if (session.dialog=="") focus_canvas(owner)
    return 'handled'
}
on input(evt) {
    if (session.dialog!="" or session.drag!=null or dom.get_attribute(evt.target,"data-property")==null) { return 'pass' }
    session = publish(owner_of(evt),session,update_property(session,evt.target))
    return 'handled'
}
on change(evt) {
    if (session.dialog!="" or session.drag!=null or dom.get_attribute(evt.target,"data-property")==null) { return 'pass' }
    session = publish(owner_of(evt),session,update_property(session,evt.target,true))
    return 'handled'
}
on focusout(evt) {
    if (session.context!=null and dom.closest(evt.target,"#sketch-context-menu")!=null and evt.relatedTarget!=null and
        dom.closest(evt.relatedTarget,"#sketch-context-menu")==null) {
        session = publish(owner_of(evt),session,{*:session,context:null})
    }
    if (dom.get_attribute(evt.target,"data-property")==null) { return 'pass' }
    let before = session
    session = m.finish_edit(session)
    // blur restores an invalid draft to the last valid value after native focus has moved.
    let key = dom.get_attribute(evt.target,"data-property")
    let source = if (key=="layer_name") m.layer_of(session.doc,session.active).name
        else if (key=="layer_opacity") m.layer_of(session.doc,session.active).opacity
        else if (m.chosen(session)!=null) m.chosen(session)[key] else session[key]
    dom.set_state(evt.target,"value",string(if (source==null) "" else source))
    session = publish(owner_of(evt),before,session)
    return 'handled'
}
on pointerdown(evt) {
    let owner = owner_of(evt)
    // native contextmenu precedes the secondary press; only a primary outside press dismisses it.
    if (evt.button==0 and session.context!=null and dom.closest(evt.target,"#sketch-context-menu")==null) {
        session = publish(owner,session,{*:session,context:null})
    }
    if (session.dialog!="" or evt.button!=0 or session.drag!=null or dom.closest(evt.target,"#stage")==null or dom.closest(evt.target,"button")!=null) { return 'pass' }
    let before = session
    session = m.finish_edit(session)
    focus_canvas(owner)
    let p = present.point(owner,session,evt.clientX,evt.clientY)
    if (session.tool=="pan") {
        session = {*:session,drag:{mode:"pan",before:session.doc,pointer:evt.pointerId,
            start:{x:evt.clientX,y:evt.clientY},pan:session.pan}}
    } else if (m.inside(session.doc,p) or contains(["select","marquee","nodes"],session.tool)) {
        // selection may start in the pasteboard around the artboard and finish inside it.
        session = if (session.tool=="pen") m.pen_point(session,p) else m.begin(session,p,evt.pointerId,evt.shiftKey)
    }
    if (session.drag!=null) dom.set_pointer_capture(present.node(owner,"stage"),evt.pointerId)
    session = publish(owner,before,session)
    return 'prevent-default'
}
on pointermove(evt) {
    if (session.drag==null or session.drag.pointer!=evt.pointerId) { return 'pass' }
    let owner = owner_of(evt)
    let next = if (session.drag.mode=="pan") {*:session,pan:{x:session.drag.pan.x-(evt.clientX-session.drag.start.x)/session.zoom,
        y:session.drag.pan.y-(evt.clientY-session.drag.start.y)/session.zoom}}
        else m.preview(session,present.point(owner,session,evt.clientX,evt.clientY),evt.shiftKey)
    session = publish(owner,session,next)
    return 'prevent-default'
}
on pointerup(evt) {
    if (session.drag==null or session.drag.pointer!=evt.pointerId) { return 'pass' }
    let owner = owner_of(evt)
    let previewed = if (session.drag.mode=="pan") session else m.preview(session,present.point(owner,session,evt.clientX,evt.clientY),evt.shiftKey)
    dom.release_pointer_capture(present.node(owner,"stage"),evt.pointerId)
    let finished = if (session.drag.mode=="pan") {*:previewed,drag:null} else m.finish(previewed)
    // brush color/size choices configure the next mark; selection tools edit completed ink.
    session = publish(owner,session,if (contains(["brush","pencil"],session.tool)) {*:finished,selected:"",selection:[]} else finished)
    return 'prevent-default'
}
on pointercancel(evt) {
    if (session.drag==null or session.drag.pointer!=evt.pointerId) { return 'pass' }
    dom.release_pointer_capture(present.node(owner_of(evt),"stage"),evt.pointerId)
    session = publish(owner_of(evt),session,m.cancel(session))
    return 'handled'
}
on wheel(evt) {
    if (session.dialog!="" or session.drag!=null or dom.closest(evt.target,"#stage")==null) { return 'pass' }
    let owner = owner_of(evt)
    var next = {*:session,context:null}
    if (evt.ctrlKey or evt.metaKey) {
        let p = present.point(owner,session,evt.clientX,evt.clientY)
        let zoom = m.clamp(session.zoom*(if (evt.deltaY<0) 1.1 else 1/1.1),.1,8)
        let box = dom.bounding_box(present.node(owner,"stage"))
        next = {*:next,zoom:zoom,pan:{x:p.x-session.doc.width/2-(evt.clientX-box.x-box.width/2)/zoom,
            y:p.y-session.doc.height/2-(evt.clientY-box.y-box.height/2)/zoom}}
    } else { next = {*:next,pan:{x:session.pan.x+evt.deltaX/session.zoom,y:session.pan.y+evt.deltaY/session.zoom}} }
    session = publish(owner,session,next)
    return 'prevent-default'
}
on keydown(evt) {
    let owner = owner_of(evt)
    let key = lower(evt.key)
    if (session.context!=null and contains(["escape","tab"],key)) {
        session = publish(owner,session,{*:session,context:null})
        focus_canvas(owner)
        return 'prevent-default'
    }
    if (session.context!=null) {
        if (contains(["ArrowUp","ArrowDown","Home","End"],evt.key)) {
            menus.navigate(evt.target,"sketch-context-menu",evt.key)
            let focused = dom.query_selector(present.node(owner,"sketch-context-menu"),"button:focus")
            if (focused!=null) dom.scroll_into_view(focused)
            return 'prevent-default'
        }
        if (key=="enter" or key==" ") {
            let item = dom.closest(evt.target,"[role=menuitem]")
            if (item!=null) { return keyboard.click(item) }
        }
        if (not (evt.ctrlKey or evt.metaKey)) { return 'prevent-default' }
    }
    if (key=="escape") {
        let before = session
        if (session.drag!=null) dom.release_pointer_capture(present.node(owner,"stage"),session.drag.pointer)
        session = publish(owner,before,m.cancel(session))
        if (before.dialog!="") dom.focus_set(present.node(owner,before.dialog),false)
        return 'prevent-default'
    }
    if (session.dialog!="") {
        if (key=="tab") {
            let order = ["file-path","file-cancel","file-confirm"]
            let indexes = [for (i,id in order where id==dom.get_attribute(evt.target,"id")) i]
            let at = if (len(indexes)>0) indexes[0] else 0
            dom.focus_set(present.node(owner,order[(at+(if (evt.shiftKey) 2 else 1))%3]),false)
            return 'prevent-default'
        }
        if (key=="enter") { session = publish(owner,session,dispatch(owner,session,"file-confirm")); return 'prevent-default' }
        return 'pass'
    }
    if (dom.closest(evt.target,".dock-tabs [role=tab]")!=null and not (evt.ctrlKey or evt.metaKey or evt.altKey) and
        contains(["arrowleft","arrowright","home","end"],key)) {
        let panel = if (key=="home") "properties" else if (key=="end") "layers" else if (session.dock_tab=="layers") "properties" else "layers"
        session = publish(owner,session,dispatch(owner,session,"dock-" ++ panel))
        dom.focus_set(present.node(owner,"dock-tab-" ++ panel),false)
        return 'prevent-default'
    }
    if (dom.closest(evt.target,"input,textarea,select")!=null or session.drag!=null or evt.altKey) { return 'pass' }
    if ((key=="contextmenu" or (key=="f10" and evt.shiftKey)) and dom.closest(evt.target,"#stage")!=null) {
        let box = m.selection_box(session)
        let anchor = if (box!=null) present.client_point(owner,session,{x:box.x+box.width/2,y:box.y+box.height/2})
            else (let stage = dom.bounding_box(present.node(owner,"stage")),{x:stage.x+24,y:stage.y+24})
        session = publish(owner,session,open_context(owner,session,anchor.x,anchor.y,false))
        return 'prevent-default'
    }
    let modifier = evt.ctrlKey or evt.metaKey
    let tools = [for (tool in m.TOOLS where lower(tool.key)==key) tool.id]
    let action = if (modifier and key=="z") (if (evt.shiftKey) "redo" else "undo")
        else if (modifier and key=="y") "redo" else if (modifier and key=="d") "duplicate"
        else if (modifier and key=="c") "copy" else if (modifier and key=="x") "cut" else if (modifier and key=="v") "paste"
        else if (modifier and key=="a") "select-all"
        else if (modifier and evt.shiftKey and key=="i") "import-image"
        else if (modifier and key=="s") (if (evt.shiftKey) "export" else "save") else if (modifier and key=="o") "open"
        else if (modifier) null else if (len(tools)>0) "tool-" ++ tools[0]
        else if (key=="delete" or key=="backspace") "delete" else if (key=="enter") "finish-path"
        else if (key=="0") "fit" else if (key=="=" or key=="+") "zoom-in" else if (key=="-") "zoom-out" else null
    let before = session
    if (contains(["arrowleft","arrowright","arrowup","arrowdown"],key) and not modifier) {
        let amount = if (evt.shiftKey) 10.0 else 1.0
        session = publish(owner,before,m.nudge(m.finish_edit(session),if (key=="arrowleft") -amount else if (key=="arrowright") amount else 0.0,
            if (key=="arrowup") -amount else if (key=="arrowdown") amount else 0.0)^)
    } else if (key=="[" or key=="]") {
        session = publish(owner,before,dispatch(owner,session,"",false))
        session = publish(owner,session,m.finish_edit(m.property(session,"size",m.clamp(session.size+(if (key=="[") -1 else 1),.1,4096))^))
    } else if (action!=null) {
        session = publish(owner,before,dispatch(owner,session,action))
        if (session.dialog!="") dom.focus_set(present.node(owner,"file-path"),false)
    } else { return 'pass' }
    return 'prevent-default'
}
on resize(evt) { session = publish(owner_of(evt),session,{*:session,context:null}); return 'pass' }
view <sketch_document> { ~.rendered }
on load(evt) {
    let owner = dom.get_element_by_id(evt.target,"sketch")
    if (owner!=null) dom.request_frame(owner,"sketch_activate")
    return 'handled'
}
<html lang:"en",<head <meta charset:"UTF-8"><title "Sketch — a little space for big ideas">
    <link rel:"stylesheet",href:url_resolve(BASE,"sketch.css")>>
    apply(<sketch_document rendered:<body apply(<sketch_session rendered:scene.tree()>)>>)
>
