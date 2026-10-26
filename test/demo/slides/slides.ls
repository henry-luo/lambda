// S12.1.3: retained editor shell; authoring and presentation have independent sessions.
import dom
import slide: lambda.slide
import slide_html: lambda.slide.html
import resources: ~~.mod_resources
import m: .mod_model
import v: .mod_view
import app: .mod_app
import menus: ~~.mod_context_menu
import ins: .mod_insert
import insert_data: .mod_insert_data
import splitter: ~~.mod_splitter
import inline_text: .mod_inline
import slide_drag: .mod_slide_drag
import spelling: lambda.edit.spelling
import ribbon_overflow: ~~.mod_ribbon_overflow

let BASE = url_resolve(resources.entry_uri()^,".")
let DEFAULT_DECK = input(url_resolve(BASE,"northstar.slides"),'slides')^
let MIN_WINDOW_WIDTH = 600
fn owner_of(evt) => dom.get_element_by_id(dom.root_node(evt.target),"slides-editor")

view <editor_show> state shown:null {
    <div id:"show-host",if (shown == null) null else slide.player(shown.deck,
        {instance:"editor-show",width:shown.width,height:shown.height,base_uri:shown.base,controls:shown.preview != true,start_slide:shown.index})^>
}
on editor_present(evt) {
    if (evt.event_phase != 2) { return 'pass' }
    shown = {*:evt.detail,deck:resources.slide_sources(evt.detail.deck,evt.detail.base)}
    return 'handled'
}
on render(evt) {
    // the nested player must be projected before its activation frame is scheduled.
    if (evt.event_phase==2 and shown!=null) dom.request_frame(evt.target,"editor_show_ready")
    return 'pass'
}
on editor_show_ready(evt) {
    if (evt.event_phase != 2) { return 'pass' }
    let player = app.node(dom.root_node(evt.target),"editor-show")
    if (player != null) {
        dom.dispatch(player,{type:"slide_command",bubbles:false,detail:{command:if (shown.preview_kind == "transition") "next" else "activate"}})
        if (shown.preview == true) dom.dispatch(app.node(dom.root_node(evt.target),"slides-editor"),{type:"transition_preview_started",bubbles:false})
        dom.focus_set(player,false)
    }
    return 'handled'
}
on editor_end(evt) {
    if (evt.event_phase != 2) { return 'pass' }
    shown = null
    return 'handled'
}

view <slide_editor> state session:m.initial(DEFAULT_DECK,BASE) { ~.rendered }
on editor_layout(evt) {
    if (evt.event_phase != 2 or evt.detail != session.layout_token) { return 'pass' }
    // read the committed flex widths before refitting previews and the editing canvas.
    session = app.fit_layout(evt.target,{*:session,layout_token:0})
    return 'handled'
}
on slide_drag_frame(evt) {
    if (evt.event_phase!=2) { return 'pass' }
    session = slide_drag.frame(evt.target,session,evt.detail)
    return 'handled'
}
on slide_plain_ready(evt) {
    if (evt.event_phase!=2) { return 'pass' }
    session = app.plain_ready(evt.target,session,evt.detail)
    return 'handled'
}
on transition_preview_started(evt) {
    if (evt.event_phase != 2) { return 'pass' }
    session = {*:session,preview_token:dom.request_frame(evt.target,"editor_preview_frame")}
    return 'handled'
}
on editor_preview_frame(evt) {
    if (evt.detail != session.preview_token or session.preview != true) { return 'pass' }
    let player = app.node(evt.target,"editor-show")
    if (dom.get_attribute(player,"data-slide-phase") in ["transition","cue"] or dom.get_attribute(player,"data-slide-sound") in ["playing","paused","ready"]) {
        session = {*:session,preview_token:dom.request_frame(evt.target,"editor_preview_frame")}
    } else {
        let sound_error = dom.get_attribute(player,"data-slide-sound-error")
        session = app.dispatch(evt.target,session,"end-show",BASE)
        session = app.publish(evt.target,{*:session,status:if (sound_error == null) (if (session.preview_kind == "animation") "Animation preview complete." else "Transition preview complete.") else sound_error})
        dom.focus_set(app.node(evt.target,if (session.preview_kind == "animation") "animation-preview" else "transition-preview"),false)
    }
    return 'handled'
}
on editor_activate(evt) {
    if (evt.event_phase != 2) { return 'pass' }
    // install the host limit on the first document frame, before ribbon layout work.
    if (dom.get_attribute(evt.target,"data-window-min")!="set" and
        dom.set_window_min_width(evt.target,MIN_WINDOW_WIDTH))
        dom.set_attribute(evt.target,"data-window-min","set")
    // window resize does not bubble to the editor's retained DOM owner.
    dom.watch_window(evt.target,"resize",true)
    let fonts = sort(unique([for (face in (dom.query_local_fonts(null) ^ {[]})) face.family]),(family) => lower(family))
    session = app.publish(evt.target,{*:session,fonts:fonts})
    return 'handled'
}
on slide_ribbon_ready(evt) {
    if (evt.event_phase != 2) { return 'pass' }
    let owner = evt.target
    ribbon_overflow.update(dom.query_selector(owner,".editor-ribbon"),session.tab,"")
    return 'handled'
}
on insert_dialog_ready(evt) {
    if (evt.event_phase != 2 or session.insert_form == null) { return 'pass' }
    app.focus_insert(evt.target)
    return 'handled'
}
on home_menu_fit(evt) {
    if (evt.event_phase != 2 or session.menu == null or session.menu == "") { return 'pass' }
    menus.fit(evt.target,"home-menu","slides-editor")
    menus.focus(evt.target,"home-menu")
    return 'handled'
}
on contextmenu(evt) {
    let thumbnail = dom.closest(evt.target,"#thumbnails [data-slide]")
    if (thumbnail==null or session.show or session.dialog!="" or session.insert_form!=null or
        session.slide_drag!=null or session.navigator_drag!=null or session.drag!=null) { return 'pass' }
    session = app.open_slide_context(owner_of(evt),session,int(dom.get_attribute(thumbnail,"data-slide")),
        {x:evt.clientX,y:evt.clientY})
    return 'prevent-default'
}
on click(evt) {
    if (evt.button!=null and evt.button!=0) { return 'pass' }
    let owner = owner_of(evt)
    if (session.slide_drag!=null) { return 'prevent-default' }
    let group_trigger = dom.closest(evt.target,".ribbon-group-trigger")
    if (group_trigger!=null and session.dialog=="" and session.insert_form==null and not session.show) {
        ribbon_overflow.toggle(group_trigger)
        return 'prevent-default'
    }
    // the completed gesture owns selection; its following click can hit the new drop neighbor.
    if (session.slide_click==true and dom.closest(evt.target,"#thumbnails [data-slide]")!=null) {
        session = {*:session,slide_click:false}
        return 'prevent-default'
    }
    // authoring selects embedded controls; their links and playback belong to Slide Show.
    if (not session.show and dom.closest(evt.target,"#stage a,#stage button,#stage audio,#stage video") != null) { return 'prevent-default' }
    let button = dom.closest(evt.target,"button,[data-slide]")
    if (button == null or session.drag != null) { return 'pass' }
    if (session.dialog != "" and dom.closest(button,"#file-dialog") == null) { return 'prevent-default' }
    let tab = dom.get_attribute(button,"data-tab")
    let at = dom.get_attribute(button,"data-slide")
    let action = dom.get_attribute(button,"data-action")
    if (session.insert_form != null and dom.closest(button,"#insert-dialog") == null) { return 'prevent-default' }
    let insert_form = session.insert_form
    let dialog = session.dialog
    let menu = session.menu
    if (session.show and action != "end-show") { return 'pass' }
    if (session.inline != null) { session = app.finish_inline(owner,session) }
    if (tab != null) { session = app.publish(owner,{*:session,tab:tab,editing:null,error:"",menu:""}) }
    else if (at != null) { session = app.publish(owner,m.select_slide({*:session,editing:null},int(at))) }
    else if (action != null) { session = app.dispatch(owner,session,action,BASE) }
    else { return 'pass' }
    if (action!=null and not starts_with(action,"menu-"))
        ribbon_overflow.close(dom.query_selector(owner,".editor-ribbon"))
    if (session.dialog != "" and dialog == "") dom.focus_set(app.node(owner,"file-path"),false)
    if (menu != null and menu != "" and menu!="slide-context" and session.menu == "" and action != "selection-pane") dom.focus_set(app.node(owner,menu),false)
    if (insert_form != null and session.insert_form == null) dom.focus_set(app.node(owner,"stage"),false)
    return 'handled'
}
on input(evt) {
    if (dom.get_attribute(evt.target,"id")=="canvas-text") {
        spelling.update(evt.target,"slides-text",dom.get_state(evt.target,"value"),evt.isComposing==true)
        return 'pass'
    }
    let key = dom.get_attribute(evt.target,"data-property")
    if (key == null or session.insert_form != null or session.dialog != "" or session.show or dom.matches(evt.target,"select,input[type=checkbox]")) { return 'pass' }
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
on compositionstart(evt) {
    if (dom.get_attribute(evt.target,"id")=="canvas-text")
        spelling.update(evt.target,"slides-text",dom.get_state(evt.target,"value"),true)
    return 'pass'
}
on compositionend(evt) {
    if (dom.get_attribute(evt.target,"id")=="canvas-text")
        spelling.update(evt.target,"slides-text",dom.get_state(evt.target,"value"))
    return 'pass'
}
on change(evt) {
    let owner = owner_of(evt)
    let id = dom.get_attribute(evt.target,"id")
    if (session.insert_form != null) {
        if (id == "insert-example" and session.insert_form.kind == "equation") {
            let example = [for (entry in insert_data.equations where entry.id == dom.get_state(evt.target,"value")) entry][0]
            if (example != null) app.value(owner,"insert-field-source",example.source)
            return 'handled'
        }
        return 'pass'
    }
    if (session.dialog != "" or session.show) { return 'pass' }
    if (id == "samples") {
        let sample = dom.get_state(evt.target,"value")
        if (sample == "") { return 'pass' }
        var next = session
        m.open_deck(session,input(url_resolve(BASE,sample),'slides')^,BASE) ^ { next = {*:session,error:^.message} } ~ { next = ~ }
        session = app.publish(owner,next,true,next.doc!=session.doc)
        app.value(owner,"samples","")
        return 'handled'
    }
    if (id == "object-list") {
        let next = {*:session,selected:dom.get_state(evt.target,"value"),editing:null}
        session = if (next.painter == true) app.dispatch(owner,next,"paint-format",BASE) else app.publish(owner,next)
        return 'handled'
    }
    let key = dom.get_attribute(evt.target,"data-property")
    if (key == null or not dom.matches(evt.target,"select,input[type=checkbox]")) { return 'pass' }
    var next = session
    app.change(owner,session,key,dom.get_state(evt.target,if (dom.matches(evt.target,"input[type=checkbox]")) "checked" else "value")) ^ { next = {*:session,error:^.message} } ~ { next = ~ }
    session = app.publish(owner,{*:next,editing:null})
    return 'handled'
}
on focusout(evt) {
    if (dom.get_attribute(evt.target,"data-inline-finishing")=="true") { return 'pass' }
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
    if (session.insert_form != null) { return 'pass' }
    // native contextmenu precedes the secondary press; that press must not dismiss its new menu.
    if (evt.button!=0) { return 'pass' }
    if (session.slide_drag!=null) { return 'prevent-default' }
    if (session.slide_click==true) { session = {*:session,slide_click:false} }
    if (dom.closest(evt.target,"#canvas-text,#canvas-rich-text") != null) { return 'pass' }
    let owner = owner_of(evt)
    if (dom.closest(evt.target,".ribbon-group[data-open=true],.ribbon-group-trigger")==null and
        (session.menu==null or session.menu=="" or dom.closest(evt.target,"#home-menu")==null))
        ribbon_overflow.close(dom.query_selector(owner,".editor-ribbon"))
    let divider = dom.closest(evt.target,"#navigator-divider")
    if (divider != null) {
        if (session.show or session.dialog != "" or evt.button != 0 or session.drag != null or session.navigator_drag != null) { return 'pass' }
        if (session.inline != null) { session = app.finish_inline(owner,session) }
        if (session.menu != null and session.menu != "") { session = app.publish(owner,{*:session,menu:""}) }
        session = {*:session,editing:null,navigator_drag:splitter.start(app.node(owner,"slide-navigator"),divider,evt)}
        dom.set_attribute(owner,"data-navigator-resizing","true")
        return 'prevent-default'
    }
    let on_stage = dom.closest(evt.target,"#stage") != null
    let thumbnail = dom.closest(evt.target,"#thumbnails [data-slide]")
    let slide_index = if (thumbnail==null) null else int(dom.get_attribute(thumbnail,"data-slide"))
    let place = dom.closest(evt.target,".slide-object[id^=\"edit-place-\"]")
    let key = if (place == null) "" else slice(dom.get_attribute(place,"id"),11)
    let resize = dom.closest(evt.target,"[data-resize]") != null
    let editing_click = session.selected==key and not resize and dom.closest(evt.target,"svg,math,canvas,video,audio")==null
    // commit the inline buffer before changing selection or replacing its canvas nodes.
    if (session.inline != null) { session = app.finish_inline(owner,session) }
    if (session.menu != null and session.menu != "" and dom.closest(evt.target,"#home-menu,.home-menu-trigger") == null) {
        session = app.publish(owner,{*:session,menu:""})
    }
    if (thumbnail!=null and not session.show and session.dialog=="" and evt.button==0 and session.navigator_drag==null and session.drag==null) {
        session = slide_drag.start(owner,session,evt,slide_index)
        return 'prevent-default'
    }
    if (session.show or session.dialog != "" or evt.button != 0 or not on_stage) { return 'pass' }
    session = {*:session,selected:key,editing:null}
    dom.focus_set(app.node(owner,"stage"),false)
    if (key != "" and session.animation_painter == true) {
        session = app.dispatch(owner,session,"paint-animation",BASE)
        return 'prevent-default'
    }
    if (key != "" and session.painter == true) {
        session = app.dispatch(owner,session,"paint-format",BASE)
        return 'prevent-default'
    }
    if (key != "") {
        session = {*:session,drag:{x:evt.clientX,y:evt.clientY,object:m.chosen(session),
            resize:resize,pointer:evt.pointerId,values:null,text_edit:editing_click}}
        dom.set_pointer_capture(app.node(owner,"stage"),evt.pointerId)
    }
    // selection keeps the hit text node alive so ordinary double-clicks can reach the editor.
    session = app.publish(owner,session,false)
    return 'prevent-default'
}
on pointermove(evt) {
    if (session.slide_drag!=null) {
        if (evt.pointerId!=session.slide_drag.pointer) { return 'pass' }
        session = slide_drag.preview(owner_of(evt),session,evt.clientX,evt.clientY)
        return 'prevent-default'
    }
    if (session.navigator_drag != null) {
        if (evt.pointerId != session.navigator_drag.pointer) { return 'pass' }
        session = app.resize_navigation(owner_of(evt),session,splitter.pointer_width(session.navigator_drag,evt.clientX))
        return 'prevent-default'
    }
    if (session.drag == null or evt.pointerId != session.drag.pointer) { return 'pass' }
    session = app.preview_drag(owner_of(evt),session,evt.clientX,evt.clientY)
    return 'prevent-default'
}
on pointerup(evt) {
    if (session.slide_drag!=null) {
        if (evt.pointerId!=session.slide_drag.pointer) { return 'pass' }
        let owner = owner_of(evt)
        session = slide_drag.preview(owner,session,evt.clientX,evt.clientY)
        session = app.finish_slide_drag(owner,session)
        return 'prevent-default'
    }
    if (session.navigator_drag != null) {
        if (evt.pointerId != session.navigator_drag.pointer) { return 'pass' }
        session = app.finish_navigation_resize(owner_of(evt),session)
        return 'prevent-default'
    }
    if (session.drag == null or evt.pointerId != session.drag.pointer) { return 'pass' }
    let owner = owner_of(evt)
    let drag = session.drag
    session = {*:session,drag:null}
    dom.release_pointer_capture(app.node(owner,"stage"),evt.pointerId)
    var next = session
    let moved = drag.values!=null and any([for (key,value in drag.values) value!=drag.object[key]])
    if (moved) m.properties(session,drag.values) ^ { next = {*:session,error:^.message} } ~ { next = ~ }
    session = if (not moved and drag.text_edit==true and inline_text.can_edit(m.chosen(next)))
        app.begin_inline(owner,next,{x:evt.clientX,y:evt.clientY}) else app.publish(owner,next,moved)
    return 'prevent-default'
}
on pointercancel(evt) {
    if (session.slide_drag!=null) {
        if (evt.pointerId!=session.slide_drag.pointer) { return 'pass' }
        session = app.finish_slide_drag(owner_of(evt),session,true)
        return 'handled'
    }
    if (session.navigator_drag != null) {
        if (evt.pointerId != session.navigator_drag.pointer) { return 'pass' }
        session = app.finish_navigation_resize(owner_of(evt),session,true)
        return 'handled'
    }
    if (session.drag == null) { return 'pass' }
    let owner = owner_of(evt)
    let place = app.node(owner,"edit-place-" ++ session.selected)
    for (key,value in slide_html.placement_properties(session.drag.object)) dom.style_set_property(place,string(key),value)
    dom.release_pointer_capture(app.node(owner,"stage"),session.drag.pointer)
    session = {*:session,drag:null}
    return 'handled'
}
on lostpointercapture(evt) {
    if (session.slide_drag!=null and evt.pointerId==session.slide_drag.pointer) {
        session = app.finish_slide_drag(owner_of(evt),session,true)
        return 'pass'
    }
    if (session.navigator_drag == null or evt.pointerId != session.navigator_drag.pointer) { return 'pass' }
    session = app.finish_navigation_resize(owner_of(evt),session)
    return 'pass'
}
on dblclick(evt) {
    if (session.insert_form != null or session.show or dom.closest(evt.target,"#stage") == null) { return 'pass' }
    if (session.inline!=null) { return 'prevent-default' }
    let owner = owner_of(evt)
    let obj = m.chosen(session)
    if (obj == null) { return 'pass' }
    if (inline_text.can_edit(obj)) {
        session = app.begin_inline(owner,session,{x:evt.clientX,y:evt.clientY})
        return 'prevent-default'
    }
    if (ins.settings(obj.source) != null) {
        session = app.dispatch(owner,session,"insert-edit",BASE)
        return 'prevent-default'
    }
    if (not contains(['text','content'],obj.tag)) { return 'pass' }
    dom.focus_set(app.node(owner,"prop-text"),false)
    return 'prevent-default'
}
on keydown(evt) {
    let owner = owner_of(evt)
    let key = lower(evt.key)
    let modifier = evt.metaKey or evt.ctrlKey
    if (session.slide_click==true) { session = {*:session,slide_click:false} }
    if (session.slide_drag!=null) {
        if (key=="escape") { session = app.finish_slide_drag(owner,session,true) }
        return 'prevent-default'
    }
    if (session.navigator_drag != null and key == "escape") {
        session = app.finish_navigation_resize(owner,session,true)
        return 'prevent-default'
    }
    if (session.insert_form != null) {
        if (key == "escape") {
            session = app.dispatch(owner,session,"insert-cancel",BASE)
            dom.focus_set(app.node(owner,"stage"),false)
            return 'prevent-default'
        }
        if (key == "enter" and (modifier or dom.matches(evt.target,"input[type=text],input[type=number]"))) {
            session = app.dispatch(owner,session,"insert-confirm",BASE)
            if (session.insert_form == null) dom.focus_set(app.node(owner,"stage"),false)
            return 'prevent-default'
        }
        if (key == "tab") {
            let controls = dom.query_selector_all(app.node(owner,"insert-dialog"),"input,select,textarea,button")
            let found = [for (i,control in controls where dom.same_node(control,evt.target)) i]
            let index = if (len(found)>0) found[0] else 0;
            dom.focus_set(controls[(index+(if (evt.shiftKey) len(controls)-1 else 1))%len(controls)],false)
            return 'prevent-default'
        }
        return 'pass'
    }
    if (session.menu != null and session.menu != "") {
        if (key == "escape" or key == "tab") {
            let trigger = session.menu
            session = app.publish(owner,{*:session,menu:""})
            app.restore_menu_focus(owner,session,trigger)
            return if (key == "escape") 'prevent-default' else 'pass'
        }
        if (dom.closest(evt.target,"#home-menu") != null and evt.key in ["ArrowUp","ArrowDown","Home","End"]) {
            menus.navigate(evt.target,"home-menu",evt.key)
            return 'prevent-default'
        }
    }
    if (key in ["enter"," "] and dom.matches(evt.target,"[data-slide]")) {
        session = app.publish(owner,m.select_slide(session,int(dom.get_attribute(evt.target,"data-slide"))))
        return 'prevent-default'
    }
    if (evt.key == "ArrowDown" and dom.matches(evt.target,".home-menu-trigger")) {
        session = app.dispatch(owner,session,"menu-" ++ dom.get_attribute(evt.target,"id"),BASE)
        return 'prevent-default'
    }
    if (key == "escape" and (session.painter == true or session.animation_painter == true)) {
        session = app.publish(owner,{*:session,painter:false,animation_painter:false,status:"Painter canceled."})
        return 'prevent-default'
    }
    if (session.inline != null) {
        if (modifier and key in ["s","o","p"]) {
            session = app.file_shortcut(owner,session,key,BASE)
            return 'prevent-default'
        }
        if (key == "escape" or key=="tab" or (modifier and key == "enter")) {
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
    if (key=="escape") {
        let opened = dom.query_selector(owner,".ribbon-group[data-open=true]")
        if (opened!=null) {
            let trigger = dom.query_selector(opened,".ribbon-group-trigger")
            ribbon_overflow.close(dom.query_selector(owner,".editor-ribbon"))
            dom.focus_set(trigger,false)
            return 'prevent-default'
        }
    }
    if (modifier and key in ["s","o","p"]) {
        session = app.file_shortcut(owner,session,key,BASE)
        return 'prevent-default'
    }
    if (key == "f5") { session = app.dispatch(owner,session,if (evt.shiftKey) "present-current" else "present",BASE); return 'prevent-default' }
    if (dom.closest(evt.target,"#navigator-divider") != null) {
        if (evt.key in ["ArrowLeft","ArrowRight","Home","End"]) {
            let limits = app.navigation_limits(owner,session)
            let width = dom.bounding_box(app.node(owner,"slide-navigator")).width
            session = app.resize_navigation(owner,session,splitter.key_width(width,evt.key,limits,1.0,if (evt.shiftKey) 50.0 else 20.0))
            return 'prevent-default'
        }
        if (key in ["delete","backspace"]) { return 'prevent-default' }
    }
    if (dom.closest(evt.target,"input,textarea,select,[contenteditable]") != null) { return 'pass' }
    if (session.drag != null) {
        if (key == "escape") dom.dispatch(app.node(owner,"stage"),{type:"pointercancel",bubbles:true})
        return 'prevent-default'
    }
    if ((key=="contextmenu" or (key=="f10" and evt.shiftKey)) and dom.closest(evt.target,"#slide-navigator")!=null) {
        let thumbnail = dom.closest(evt.target,"[data-slide]")
        session = app.open_slide_context(owner,session,if (thumbnail==null) session.index else int(dom.get_attribute(thumbnail,"data-slide")))
        return 'prevent-default'
    }
    // navigation owns slide deletion; a keyboard-focused thumbnail may differ from the active canvas.
    if (key in ["delete","backspace"] and dom.closest(evt.target,"#slide-navigator")!=null) {
        let thumbnail = dom.closest(evt.target,"[data-slide]")
        let target = if (thumbnail==null) session else m.select_slide(session,int(dom.get_attribute(thumbnail,"data-slide")))
        session = app.dispatch(owner,target,"delete-slide",BASE)
        if (thumbnail==null) slide_drag.focus(owner,session.index)
        return 'prevent-default'
    }
    let obj = m.chosen(session)
    if (key in ["f2","enter"] and not modifier and dom.closest(evt.target,"#stage")!=null and inline_text.can_edit(obj)) {
        session = app.begin_inline(owner,session)
        return 'prevent-default'
    }
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
        else if (modifier and key == "c") "copy" else if (modifier and key == "x") "cut" else if (modifier and key == "v") "paste"
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
    let owner = owner_of(evt)
    if (session.slide_drag!=null) { session = app.finish_slide_drag(owner,session,true) }
    session = app.finish_navigation_resize(owner,session)
    // Native live resize can paint before another requested frame runs.
    let ribbon = dom.query_selector(owner,".editor-ribbon")
    let handoff = ribbon_overflow.begin_resize(ribbon,app.node(owner,"stage"))
    ribbon_overflow.update(ribbon,session.tab,"")
    ribbon_overflow.end_resize(handoff)
    // Fit the canvas from this width before the native resize callback paints it.
    if (session.layout_token>0) dom.cancel_frame(owner,session.layout_token)
    session = app.fit_layout(owner,{*:session,layout_token:0},false)
    spelling.reposition(owner,"slides-notes")
    spelling.reposition(owner,"slides-text")
    spelling.reposition(owner,"slides-canvas")
    return 'pass'
}
on scroll(evt) {
    spelling.reposition(evt.target,"slides-notes")
    spelling.reposition(evt.target,"slides-text")
    spelling.reposition(evt.target,"slides-canvas")
    return 'pass'
}

view <slides_document> { ~.rendered }
on load(evt) {
    let owner = app.node(evt.target,"slides-editor")
    if (owner != null) dom.request_frame(owner,"editor_activate")
    return 'handled'
}
<html lang:"en",<head <meta charset:"UTF-8"><title "Slides — create and present">
    <base href:BASE><style slide_html.css ++ spelling.css><link rel:"stylesheet",href:url_resolve(BASE,"slides.css")>>
    apply(<slides_document rendered:<body apply(<slide_editor rendered:v.shell(apply(<editor_show>),inline_text.widget())>)>>)
>
