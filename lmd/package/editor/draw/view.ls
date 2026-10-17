// S12.1.3 / D4.5.3v2: retain the owner and capture node; handlers publish only paint/controls.
import dom
import draw: lambda.editor.draw
import model: .model
import shapes: .shapes
import scene: .scene
import interaction: .interaction
import viewport: .viewport
import widgets: .widgets
import common: lambda.slide.common

fn owner(evt) => dom.closest(evt.target,".lambda-draw")
pn point(root,st,x,y) { return viewport.point(dom.bounding_box(dom.query_selector(root,".draw-paper")),st.zoom,x,y) }
pn publish(root,st,prefix) {
    let paper = dom.query_selector(root,".draw-paper")
    let page = model.page(st.doc)
    dom.style_set_property(paper,"width",string(page.width*st.zoom) ++ "px")
    dom.style_set_property(paper,"height",string(page.height*st.zoom) ++ "px")
    dom.set_inner_html(paper,format(scene.svg(st.doc,st.selected,st.source,false,false,model.selected_ids(st),st.zoom,
        if (st.drag.mode == "marquee") {x:min(st.drag.start.x,st.drag.current.x),y:min(st.drag.start.y,st.drag.current.y),
            width:abs(st.drag.current.x-st.drag.start.x),height:abs(st.drag.current.y-st.drag.start.y)} else null,prefix),'xml'))
    for (key,value in {'data-selection':st.selected,'data-history':string(len(st.hist.undo)),'data-error':st.error})
        dom.set_attribute(root,string(key),value)
    let chosen = model.lookup(st.doc,st.selected).shape
    for (key in ["label","rotate"]) {
        let control = dom.get_element_by_id(root,prefix ++ "field-" ++ key)
        if (st.editing.key != key) dom.set_state(control,"value",if (chosen == null) "" else
            string(model.attr(chosen,if (key == "rotate") "rotation" else key)))
    }
    st
}

view draw_surface: <draw.surface> state session:~.initial { ~.rendered }
on pointerdown(evt) {
    let root = owner(evt)
    if (evt.button != 0 or session.drag != null or dom.closest(evt.target,".draw-paper") == null) { return 'pass' }
    session = interaction.finish_edit(session)
    dom.focus_set(dom.query_selector(root,".draw-viewport"),false)
    let p = point(root,session,evt.clientX,evt.clientY)
    if (contains(shapes.kinds,session.tool)) {
        let size = model.page(session.doc)
        let id = "v" ++ string(session.next_id)
        let vertex = draw.vertex(id,session.tool,{x:model.clamp(p.x-80,0,size.width-160),y:model.clamp(p.y-40,0,size.height-80),width:160.0,height:80.0})^
        session = model.select_ids(draw.exec(session,{kind:'insert',objects:[vertex]})^,[id])
        session = {*:session,tool:"select",next_id:session.next_id+1}
    } else {
        session = draw.update(session,{kind:'begin',x:evt.clientX,y:evt.clientY,point:p,pointer:evt.pointerId,shift:evt.shiftKey})
        if (session.drag != null) dom.set_pointer_capture(dom.query_selector(root,".draw-viewport"),evt.pointerId)
    }
    session = publish(root,session,~.prefix)
    'prevent-default'
}
on mousedown(evt) {
    if (dom.closest(evt.target,".draw-paper") == null) { return 'pass' }
    dom.focus_set(dom.query_selector(owner(evt),".draw-viewport"),false)
    'prevent-default'
}
on pointermove(evt) {
    if (session.drag == null or evt.pointerId != session.drag.pointer) { return 'pass' }
    let root = owner(evt)
    session = publish(root,draw.update(session,{kind:'preview',x:evt.clientX,y:evt.clientY,
        point:point(root,session,evt.clientX,evt.clientY),shift:evt.shiftKey}),~.prefix)
    'prevent-default'
}
on pointerup(evt) {
    if (session.drag == null or evt.pointerId != session.drag.pointer) { return 'pass' }
    let root = owner(evt)
    session = draw.update(session,{kind:'commit',x:evt.clientX,y:evt.clientY,
        point:point(root,session,evt.clientX,evt.clientY),shift:evt.shiftKey})
    dom.release_pointer_capture(dom.query_selector(root,".draw-viewport"),evt.pointerId)
    session = publish(root,session,~.prefix)
    emit("draw_change",{id:~.prefix,document:session.doc})
    'prevent-default'
}
on pointercancel(evt) {
    if (session.drag == null or evt.pointerId != session.drag.pointer) { return 'pass' }
    let root = owner(evt)
    dom.release_pointer_capture(dom.query_selector(root,".draw-viewport"),evt.pointerId)
    session = publish(root,interaction.cancel(session),~.prefix)
    'prevent-default'
}
on click(evt) {
    let root = owner(evt)
    let action = dom.get_attribute(dom.closest(evt.target,"[data-draw-action]"),"data-draw-action")
    if (action == null) { return 'pass' }
    session = interaction.finish_edit(session)
    if (action == "select" or action == "connect") { session = {*:session,tool:action,source:""} }
    else if (starts_with(action,"add-")) { session = {*:session,tool:slice(action,4)} }
    else if (action == "zoom-in" or action == "zoom-out") {
        session = {*:session,zoom:model.clamp(session.zoom*(if (action == "zoom-in") 1.2 else 1/1.2),0.1,3.0)}
    } else {
        session = draw.exec(session,{kind:symbol(action),ids:model.selected_ids(session)}) ^ { {*:session,error:^.message} }
    }
    session = publish(root,session,~.prefix)
    emit("draw_change",{id:~.prefix,document:session.doc})
    'handled'
}
on input(evt) {
    let key = dom.get_attribute(evt.target,"data-draw-property")
    if (key == null or session.selected == "" or session.drag != null) { return 'pass' }
    let root = owner(evt)
    if (session.editing.key != key) { session = interaction.finish_edit(session) }
    session = {*:session,editing:if (session.editing == null) {key:key,before:session.doc} else session.editing}
    let raw = dom.get_state(evt.target,"value");
    (if (key == "rotate") float(raw) else raw) ^ { session = {*:session,error:"Draw: enter a valid value"} } ~ {
        draw.transaction(session.doc,null,{kind:'set',id:session.selected,properties:{[key]:~}}) ^ { session = {*:session,error:^.message} } ~ {
            if (~ != null) { session = {*:session,doc:~.doc_after,error:""} }
        }
    }
    session = publish(root,session,~.prefix)
    'handled'
}
on focusout(evt) {
    if (session.editing == null or dom.get_attribute(evt.target,"data-draw-property") == null) { return 'pass' }
    session = publish(owner(evt),interaction.finish_edit(session),~.prefix)
    emit("draw_change",{id:~.prefix,document:session.doc})
    'pass'
}
on keydown(evt) {
    let root = owner(evt)
    if (dom.closest(evt.target,"input,textarea,select") != null) { return 'pass' }
    if (evt.key == "Escape") {
        if (session.drag != null) dom.release_pointer_capture(dom.query_selector(root,".draw-viewport"),session.drag.pointer)
        session = publish(root,interaction.cancel(session),~.prefix)
        return 'prevent-default'
    }
    if (session.drag != null) { return 'pass' }
    let key = lower(evt.key)
    let modifier = evt.ctrlKey or evt.metaKey
    let action = if (modifier and key == "z") (if (evt.shiftKey) 'redo' else 'undo')
        else if (modifier and key == "y") 'redo' else if (modifier and key == "d") 'duplicate'
        else if (key == "delete" or key == "backspace") 'delete' else null
    if (modifier and key == "a") { session = model.select_ids(session,[for (n in model.objects(session.doc)) model.id(n)]) }
    else if (contains(["arrowleft","arrowright","arrowup","arrowdown"],key)) {
        let amount = if (evt.shiftKey) 10 else 1
        session = draw.exec(session,{kind:'move',ids:model.selected_ids(session),
            dx:if (key == "arrowleft") -amount else if (key == "arrowright") amount else 0,
            dy:if (key == "arrowup") -amount else if (key == "arrowdown") amount else 0})^
    } else if (action != null) { session = draw.exec(session,{kind:action,ids:model.selected_ids(session)})^ }
    else { return 'pass' }
    session = publish(root,session,~.prefix)
    emit("draw_change",{id:~.prefix,document:session.doc})
    'prevent-default'
}
on closerequest(evt) {
    if (session.drag != null) dom.release_pointer_capture(dom.query_selector(owner(evt),".draw-viewport"),session.drag.pointer)
    session = interaction.cancel(session)
    'pass'
}

pub let css = ".lambda-draw{border:1px solid #c9d5e5;background:#f4f6fa;font:13px Arial;color:#29354c}.lambda-draw .draw-toolbar,.lambda-draw .draw-palette,.lambda-draw .draw-inspector{display:flex;flex-wrap:wrap;gap:6px;padding:6px}.lambda-draw button{padding:4px 8px;background:white;border:1px solid #c9d5e5;border-radius:4px}.lambda-draw .draw-viewport{overflow:auto;position:relative;outline:none}.lambda-draw .draw-paper{position:relative;background:white}.lambda-draw .draw-paper>svg{display:block;width:100%;height:100%}.lambda-draw input{width:120px;margin-left:6px}.lambda-draw .draw-inspector label{display:flex;align-items:center}"
pub fn surface(document,options) any^ {
    let prefix = options.id
    let checked = if (not common.instance_id(prefix))
        raise error("Draw: surface requires a unique id using letters, digits, underscore or hyphen")
    let zoom = if (options.zoom == null) 1.0 else options.zoom
    let checked_zoom = if (not (zoom is number) or zoom<0.1 or zoom>3.0) raise error("Draw: zoom must be between 0.1 and 3")
    let initial = draw.open_session(document,{zoom:zoom})^;
    apply(<draw.surface prefix:prefix ++ "-",initial:{*:initial,zoom:zoom,grid:false},
        rendered:<section id:prefix,class:"lambda-draw",'data-selection':"",'data-history':"0",'data-error':"",
            <style css>;
            (widgets.toolbar());(widgets.palette());(widgets.inspector(prefix ++ "-"));
            <div class:"draw-viewport",tabindex:0,role:"application",'aria-label':if (options.label == null) "Drawing editor" else options.label,
                style:"width:" ++ string(if (options.width == null) 600 else options.width) ++ "px;height:" ++
                    string(if (options.height == null) 360 else options.height) ++ "px",
                <div class:"draw-paper",style:"width:" ++ string(model.page(document).width*zoom) ++ "px;height:" ++ string(model.page(document).height*zoom) ++ "px",
                    (scene.svg(document,"","",false,false,[],zoom,null,prefix ++ "-"))>>
        >>)
}
