// S12.1.3: retained editor shell; authoring and presentation have independent sessions.
import dom
import slide: lambda.slide
import slide_html: lambda.slide.html
import resources: ~~.mod_resources
import m: .mod_model
import v: .mod_view
import app: .mod_app

let BASE = url_resolve(resources.entry_uri()^,".")
fn owner_of(evt) => dom.get_element_by_id(dom.root_node(evt.target),"slides-editor")

view <editor_show> state shown:null {
    <div id:"show-host",if (shown == null) null else slide.player(shown.deck,
        {instance:"editor-show",width:shown.width,height:shown.height,base_uri:shown.base})^>
}
on editor_present(evt) {
    if (evt.event_phase != 2) { return 'pass' }
    shown = {*:evt.detail,deck:resources.slide_sources(evt.detail.deck,evt.detail.base)}
    dom.request_frame(evt.target,"editor_show_ready")
    return 'handled'
}
on editor_show_ready(evt) {
    if (evt.event_phase != 2) { return 'pass' }
    let player = app.node(dom.root_node(evt.target),"editor-show")
    if (player != null) {
        dom.dispatch(player,{type:"slide_command",bubbles:false,detail:{command:"jump",slide:shown.index}})
        dom.focus_set(player,false)
    }
    return 'handled'
}
on editor_end(evt) {
    if (evt.event_phase != 2) { return 'pass' }
    shown = null
    return 'handled'
}

view <slide_editor> state session:m.initial(null,BASE) { ~.rendered }
on editor_activate(evt) {
    if (evt.event_phase != 2) { return 'pass' }
    session = app.publish(evt.target,session)
    return 'handled'
}
on click(evt) {
    let owner = owner_of(evt)
    let button = dom.closest(evt.target,"button")
    if (button == null or session.drag != null) { return 'pass' }
    if (session.dialog != "" and dom.closest(button,"#file-dialog") == null) { return 'prevent-default' }
    let tab = dom.get_attribute(button,"data-tab")
    let at = dom.get_attribute(button,"data-slide")
    let action = dom.get_attribute(button,"data-action")
    let dialog = session.dialog
    if (session.show and action != "end-show") { return 'pass' }
    if (session.inline != null) { session = app.finish_inline(owner,session) }
    if (tab != null) { session = app.publish(owner,{*:session,tab:tab,editing:null,error:""}) }
    else if (at != null) { session = app.publish(owner,m.select_slide({*:session,editing:null},int(at))) }
    else if (action != null) { session = app.dispatch(owner,session,action,BASE) }
    else { return 'pass' }
    if (session.dialog != "" and dialog == "") dom.focus_set(app.node(owner,"file-path"),false)
    if (session.dialog == "" and dialog != "") dom.focus_set(app.node(owner,dialog),false)
    return 'handled'
}
on input(evt) {
    let key = dom.get_attribute(evt.target,"data-property")
    if (key == null or session.dialog != "" or session.show or dom.matches(evt.target,"select")) { return 'pass' }
    let target = dom.get_attribute(evt.target,"id")
    let old_edit = session.editing
    var next = session
    app.change(owner_of(evt),session,key,dom.get_state(evt.target,"value")) ^ {
        // incomplete numeric/Mark drafts belong to the native field, while the deck stays valid.
        next = {*:session,error:^.message,editing:if (old_edit.target == target) old_edit else {target:target,undo:null}}
    } ~ {
        // a continuous field edit is one undo step, while the native control keeps its draft.
        let grouped = old_edit.target == target and old_edit.undo != null;
        next = {*:~,error:"",undo:if (grouped) old_edit.undo else ~.undo,
            editing:{target:target,undo:if (grouped) old_edit.undo else ~.undo}}
    }
    session = app.publish(owner_of(evt),next)
    return 'handled'
}
on change(evt) {
    let owner = owner_of(evt)
    let id = dom.get_attribute(evt.target,"id")
    if (session.dialog != "" or session.show) { return 'pass' }
    if (id == "samples") {
        let sample = dom.get_state(evt.target,"value")
        if (sample == "") { return 'pass' }
        var next = session
        m.open_deck(session,input(url_resolve(BASE,sample),'slides')^,BASE) ^ { next = {*:session,error:^.message} } ~ { next = ~ }
        session = app.publish(owner,next)
        app.value(owner,"samples","")
        return 'handled'
    }
    if (id == "object-list") {
        session = app.publish(owner,{*:session,selected:dom.get_state(evt.target,"value"),editing:null})
        return 'handled'
    }
    let key = dom.get_attribute(evt.target,"data-property")
    if (key == null or not dom.matches(evt.target,"select")) { return 'pass' }
    var next = session
    app.change(owner,session,key,dom.get_state(evt.target,"value")) ^ { next = {*:session,error:^.message} } ~ { next = ~ }
    session = app.publish(owner,{*:next,editing:null})
    return 'handled'
}
on focusout(evt) {
    if (dom.get_attribute(evt.target,"id") == "canvas-text" and session.inline != null) {
        let current = session
        session = {*:session,inline:null}
        session = app.finish_inline(owner_of(evt),current)
        return 'handled'
    }
    if (session.editing.target != dom.get_attribute(evt.target,"id")) { return 'pass' }
    session = app.publish(owner_of(evt),{*:session,editing:null})
    return 'pass'
}
on pointerdown(evt) {
    if (dom.closest(evt.target,"#canvas-text") != null) { return 'pass' }
    let owner = owner_of(evt)
    let on_stage = dom.closest(evt.target,"#stage") != null
    let place = dom.closest(evt.target,".slide-object")
    let key = if (place == null) "" else slice(dom.get_attribute(place,"id"),11)
    let resize = dom.closest(evt.target,"[data-resize]") != null
    // commit the inline buffer before changing selection or replacing its canvas nodes.
    if (session.inline != null) { session = app.finish_inline(owner,session) }
    if (session.show or session.dialog != "" or evt.button != 0 or not on_stage) { return 'pass' }
    session = {*:session,selected:key,editing:null}
    dom.focus_set(app.node(owner,"stage"),false)
    if (key != "") {
        session = {*:session,drag:{x:evt.clientX,y:evt.clientY,object:m.chosen(session),
            resize:resize,pointer:evt.pointerId,values:null}}
        dom.set_pointer_capture(app.node(owner,"stage"),evt.pointerId)
    }
    session = app.publish(owner,session)
    return 'prevent-default'
}
on pointermove(evt) {
    if (session.drag == null or evt.pointerId != session.drag.pointer) { return 'pass' }
    session = app.preview_drag(owner_of(evt),session,evt.clientX,evt.clientY)
    return 'prevent-default'
}
on pointerup(evt) {
    if (session.drag == null or evt.pointerId != session.drag.pointer) { return 'pass' }
    let owner = owner_of(evt)
    let drag = session.drag
    session = {*:session,drag:null}
    dom.release_pointer_capture(app.node(owner,"stage"),evt.pointerId)
    var next = session
    if (drag.values != null) m.properties(session,drag.values) ^ { next = {*:session,error:^.message} } ~ { next = ~ }
    session = app.publish(owner,next)
    return 'prevent-default'
}
on pointercancel(evt) {
    if (session.drag == null) { return 'pass' }
    let owner = owner_of(evt)
    let place = app.node(owner,"edit-place-" ++ session.selected)
    for (key,value in slide_html.placement_properties(session.drag.object)) dom.style_set_property(place,string(key),value)
    dom.release_pointer_capture(app.node(owner,"stage"),session.drag.pointer)
    session = {*:session,drag:null}
    return 'handled'
}
on dblclick(evt) {
    if (session.show or dom.closest(evt.target,"#stage") == null or not contains(['text','content'],m.chosen(session).tag)) { return 'pass' }
    let owner = owner_of(evt)
    let obj = m.chosen(session)
    if (obj.tag == 'text' and m.plain(obj.source)) {
        let place = app.node(owner,"edit-place-" ++ session.selected)
        let fragment = dom.parse_fragment(place,format(v.inline_editor(obj,slide_html.layer_background(session.plan,m.scene(session))),'xml'))
        for (control in dom.child_nodes(fragment)) dom.append_child(place,control)
        session = {*:session,inline:{selected:session.selected}}
        dom.focus_set(app.node(owner,"canvas-text"),false)
        return 'prevent-default'
    }
    dom.focus_set(app.node(owner,"prop-text"),false)
    return 'prevent-default'
}
on keydown(evt) {
    let owner = owner_of(evt)
    let key = lower(evt.key)
    let modifier = evt.metaKey or evt.ctrlKey
    if (session.inline != null) {
        if (key == "escape" or (modifier and key == "enter")) {
            session = app.finish_inline(owner,session,key != "escape")
            dom.focus_set(app.node(owner,"stage"),false)
            return 'prevent-default'
        }
        return 'pass'
    }
    if (session.show) {
        // the player clears its overlays before Escape can end the editing host's show.
        if (evt.defaultPrevented == true) { return 'pass' }
        if (key == "escape") { session = app.dispatch(owner,session,"end-show",BASE); return 'prevent-default' }
        return 'pass'
    }
    if (session.dialog != "") {
        if (key == "escape") { session = app.dispatch(owner,session,"file-cancel",BASE); return 'prevent-default' }
        if (key == "enter") { session = app.dispatch(owner,session,"file-confirm",BASE); return 'prevent-default' }
        if (key == "tab") {
            let ids = ["file-path","file-cancel","file-confirm"]
            let found = [for (i,id in ids where id == dom.get_attribute(evt.target,"id")) i]
            let index = if (len(found)>0) found[0] else 0;
            dom.focus_set(app.node(owner,ids[(index+(if (evt.shiftKey) 2 else 1))%3]),false)
            return 'prevent-default'
        }
        return 'pass'
    }
    if (modifier and (key == "s" or key == "o")) {
        session = app.dispatch(owner,session,if (key == "s") "save" else "open",BASE)
        dom.focus_set(app.node(owner,"file-path"),false)
        return 'prevent-default'
    }
    if (key == "f5") { session = app.dispatch(owner,session,if (evt.shiftKey) "present-current" else "present",BASE); return 'prevent-default' }
    if (dom.closest(evt.target,"input,textarea,select,[contenteditable]") != null) { return 'pass' }
    if (session.drag != null) {
        if (key == "escape") dom.dispatch(app.node(owner,"stage"),{type:"pointercancel",bubbles:true})
        return 'prevent-default'
    }
    let obj = m.chosen(session)
    if (contains(["arrowleft","arrowright","arrowup","arrowdown"],key) and obj != null) {
        let step = if (evt.shiftKey) 10.0 else 1.0
        var next = session
        m.properties(session,{x:obj.x+(if (key == "arrowleft") -step else if (key == "arrowright") step else 0.0),
            y:obj.y+(if (key == "arrowup") -step else if (key == "arrowdown") step else 0.0)}) ^ { next = {*:session,error:^.message} } ~ { next = ~ }
        session = app.publish(owner,next)
        return 'prevent-default'
    }
    let action = if (modifier and key == "z") (if (evt.shiftKey) "redo" else "undo")
        else if (modifier and key == "y") "redo" else if (modifier and key == "d") "duplicate"
        else if (modifier and key == "c") "copy" else if (modifier and key == "v") "paste"
        else if (modifier and key == "m") "add-slide" else if (key == "delete" or key == "backspace") "delete"
        else if (key == "escape") "deselect" else null
    if (action == "deselect") { session = app.publish(owner,{*:session,selected:""}); return 'prevent-default' }
    if (action != null) {
        session = app.dispatch(owner,session,action,BASE)
        if (session.dialog != "") dom.focus_set(app.node(owner,"file-path"),false)
        return 'prevent-default'
    }
    return 'pass'
}
on resize(evt) {
    session = app.fit(owner_of(evt),session)
    return 'pass'
}

view <slides_document> { ~.rendered }
on load(evt) {
    let owner = app.node(evt.target,"slides-editor")
    if (owner != null) dom.request_frame(owner,"editor_activate")
    return 'handled'
}
<html lang:"en",<head <meta charset:"UTF-8"><title "Slides — create and present">
    <base href:BASE><style slide_html.css><link rel:"stylesheet",href:url_resolve(BASE,"slides.css")>>
    apply(<slides_document rendered:<body apply(<slide_editor rendered:v.shell(apply(<editor_show>))>)>>)
>
