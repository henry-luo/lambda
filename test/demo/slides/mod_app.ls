// S12.1.3 / D7.5.3: editor commands publish through the existing DOM host boundary.
import dom
import m: .mod_model
import v: .mod_view
import c: lambda.slide.common
import html: lambda.slide.html
import u: ~~.mod_publish
import geometry: lambda.editor.draw.geometry
import slide: lambda.slide
import resources: ~~.mod_resources
import home: .mod_home
import home_ui: .mod_home_ui
import menus: ~~.mod_context_menu
import insert_model: .mod_insert
import insert_ui: .mod_insert_ui
import insert_data: .mod_insert_data
import media: lambda.doc.media
import transitions: .mod_transitions
import transitions_ui: .mod_transitions_ui
import effects: lambda.slide.transitions
import animations: .mod_animations
import animations_ui: .mod_animations_ui
import animation_catalog: lambda.slide.animation
import splitter: ~~.mod_splitter
import exports: ~~.mod_export
import inline_text: .mod_inline
import spelling: lambda.edit.spelling
import text_offsets: lambda.dom.edit_history
import slide_drag: .mod_slide_drag

pub fn node(owner,id) => dom.get_element_by_id(owner,id)
pub pn field(owner,id) { return dom.get_state(node(owner,id),"value") }
pub pn value(owner,id,value,editing=null) {
    let target = node(owner,id)
    let text = c.as_text(value)
    if (target == null or editing == id) { return null }
    if (dom.matches(target,"select")) {
        let options = dom.query_selector_all(target,"option")
        let matches = [for (i,option in options where dom.get_attribute(option,"value") == text) i]
        if (len(matches)>0 and dom.get_state(target,"value") != text) dom.set_selected_index(target,matches[0])
    } else if (dom.get_state(target,"value") != text) dom.set_state(target,"value",text)
}
pub pn fit(owner,st) {
    let box = dom.bounding_box(node(owner,"stage"))
    let scale = max(0.05,min((box.width-64.0)/st.plan.width,(box.height-48.0)/st.plan.height))*st.zoom
    let w = st.plan.width*scale
    let h = st.plan.height*scale
    let paper = node(owner,"paper");
    for (key,n in {width:w,height:h,left:max(24.0,(box.width-w)/2.0),top:max(24.0,(box.height-h)/2.0)})
        u.style(paper,string(key),c.px(n))
    u.style(node(owner,"slide-render"),"transform","scale(" ++ c.fmt(scale) ++ ")")
    u.text(owner,"zoom-label",string(round(scale*100.0)) ++ "%")
    return {*:st,scale:scale}
}
pub pn request_layout(owner,st) {
    return if (st.layout_token>0) st else {*:st,layout_token:dom.request_frame(owner,"editor_layout")}
}
pub pn navigation_limits(owner,st) {
    let width = dom.bounding_box(dom.query_selector(owner,".workspace")).width
    let inspector = dom.bounding_box(dom.query_selector(owner,".inspector")).width
    let animations = if (st.animation_pane == true) dom.bounding_box(node(owner,"animation-pane")).width else 0.0
    // reserve the studio's CSS minimum and the divider's own width.
    return {min:160.0,max:max(160.0,min(480.0,width-inspector-animations-250.0-6.0))}
}
pub pn resize_navigation(owner,st,width,defer_layout=true) {
    let limits = navigation_limits(owner,st)
    let size = round(max(limits.min,min(limits.max,width)))
    let changed = splitter.publish(node(owner,"slide-navigator"),node(owner,"navigator-divider"),size,limits)
    let next = {*:st,navigator_width:size};
    return if (changed and defer_layout) request_layout(owner,next) else next
}
pub pn finish_navigation_resize(owner,st,cancel=false) {
    if (st.navigator_drag == null) { return st }
    let gesture = st.navigator_drag
    let next = {*:st,navigator_drag:null}
    let restored = if (cancel) resize_navigation(owner,next,gesture.width) else next
    u.attr(owner,"data-navigator-resizing",false)
    splitter.release(node(owner,"navigator-divider"),gesture)
    return request_layout(owner,restored)
}
pub pn fit_layout(owner,st,defer_layout=true) {
    let width = c.value(st.navigator_width,dom.bounding_box(node(owner,"slide-navigator")).width)
    let resized = resize_navigation(owner,st,width,defer_layout)
    let papers = dom.query_selector_all(node(owner,"thumbnails"),".thumb-paper")
    let preview = if (len(papers)>0) dom.bounding_box(papers[0]).width else st.thumbnail_width
    for (paper in papers) {
        u.style(paper,"height",c.px(preview*st.plan.height/st.plan.width))
        u.style(dom.query_selector(paper,".thumb-render"),"transform","scale(" ++ c.fmt(preview/st.plan.width) ++ ")")
    }
    return fit(owner,{*:resized,thumbnail_width:preview})
}
pub pn finish_slide_drag(owner,st,cancel=false) {
    let ended = slide_drag.finish(owner,st,cancel)
    let next = publish(owner,{*:ended,slide_click:true})
    slide_drag.focus(owner,next.index)
    return next
}
pub pn selection(owner,st) {
    for (old in dom.query_selector_all(node(owner,"slide-render"),".editor-selected")) {
        dom.set_attribute(old,"class","slide-object")
        for (handle in dom.query_selector_all(old,".editor-handle")) dom.remove(handle)
    }
    let place = node(owner,"edit-place-" ++ st.selected)
    if (place != null) {
        let editable = inline_text.can_edit(m.chosen(st));
        dom.set_attribute(place,"class","slide-object editor-selected" ++ (if (editable) " editor-text-target" else ""))
        if (editable) dom.set_attribute(place,"title","Double-click or press F2 to edit text")
        let fragment = dom.parse_fragment(place,"<div class=\"editor-handle\" data-resize=\"true\"></div>")
        for (handle in dom.child_nodes(fragment)) dom.append_child(place,handle)
    }
}
pub pn publish(owner,st,canvas=true,opened=false) {
    // a slide switch replaces the notes and painted canvas, even when editing never began.
    let previous_index = dom.get_attribute(owner,"data-index")
    let replaced = opened or (previous_index!=null and previous_index!=string(st.index))
    spelling.update(owner,"slides-notes",m.notes(st),st.show==true,null,replaced)
    if (st.inline==null) spelling.update(owner,"slides-text",null)
    spelling.reposition(owner,"slides-notes")
    spelling.reposition(owner,"slides-text")
    let thumbnail_focus = canvas and any([for (item in dom.query_selector_all(node(owner,"thumbnails"),"[data-slide]")) dom.focused(item)])
    let obj = m.chosen(st)
    let editing = st.editing.target
    let ribbon = node(owner,"ribbon")
    u.markup(ribbon,format(v.ribbon(st.tab,c.value(st.fonts,[])),'html'))
    let group_row = dom.query_selector(ribbon,".editor-ribbon")
    // Markup replacement discards group measurements; measure in the next layout frame.
    if (group_row!=null and dom.get_attribute(group_row,"data-layout-key")==null)
        dom.request_frame(owner,"slide_ribbon_ready")
    let menu_tree = if (st.menu=="slide-context") home_ui.slide_context(st)
        else if (starts_with(c.value(st.menu,""),"animation-")) animations_ui.menu_tree(st,st.menu) else if (st.menu == "transition-options") transitions_ui.menu_tree(st) else if (starts_with(c.value(st.menu,""),"insert-")) insert_ui.menu_tree(st.menu)
        else home_ui.menu_tree(c.value(st.menu,""));
    u.markup(node(owner,"home-menu"),if (len(menu_tree) == 0) "" else format(menu_tree,'html'))
    u.show(owner,"home-menu",st.menu != null and st.menu != "")
    u.attr(node(owner,"home-menu"),"aria-label",if (st.menu=="slide-context") "Slide actions" else "Home ribbon options")
    for (trigger in dom.query_selector_all(owner,".home-menu-trigger"))
        u.attr(trigger,"aria-expanded",dom.get_attribute(trigger,"id") == st.menu)
    let display = if (st.display_doc == st.doc and st.display_base == st.base) st else
        {*:st,display_doc:st.doc,display_base:st.base,display_plan:slide.compile(resources.slide_sources(st.doc,st.base))^}
    let visual_state = {*:st,plan:display.display_plan}
    if (canvas) {
        u.markup(node(owner,"thumbnails"),format(v.thumbnails(visual_state)^,'html'))
        u.markup(node(owner,"slide-render"),format([v.canvas(display.display_plan,st.index,"edit")^],'html'))
    }
    u.markup(node(owner,"object-list"),join([for (option in v.object_options(st)) format(option,'xml')],""))
    selection(owner,st)
    if (canvas) spelling.update_canvas(owner,if (st.inline==null) null else st.selected,st.show==true,replaced)
    u.attr(node(owner,"paper"),"class",if (st.grid) "grid" else "")
    for (key,val in {'data-index':st.index,'data-slides':len(st.plan.slides),'data-selected':st.selected,
        'data-insert-dialog':c.value(st.insert_form.kind,""),'data-dirty':st.doc != st.saved,'data-error':st.error,'data-dialog':st.dialog,'data-mode':if (st.show) "present" else "edit",'data-undo':len(st.undo),
        'data-x':obj.x,'data-y':obj.y,'data-width':obj.width,'data-height':obj.height,'data-editing':editing,'data-painter':st.painter == true}) u.attr(owner,string(key),val)
    for (key,text in {'save-state':if (st.doc == st.saved) "Saved" else "Unsaved changes",
        'slide-count':string(len(st.plan.slides)) ++ " slides",'slide-heading':c.value(m.scene(st).source.title,"Slide " ++ string(st.index+1)),
        position:"Slide " ++ string(st.index+1) ++ " of " ++ string(len(st.plan.slides)),'canvas-size':c.fmt(st.plan.width) ++ " × " ++ c.fmt(st.plan.height),status:if (st.error != "") st.error else st.status,
        'object-kind':if (obj == null) "Slide" else string(obj.tag),'text-label':if (obj != null and not m.plain(obj.source)) "Content (Mark)" else "Text",
        'dialog-title':if (st.dialog == "open") "Open presentation" else if (st.dialog == "export") "Export handout" else if (st.dialog == "print") "Print presentation" else "Save presentation",
        'dialog-copy':if (st.dialog == "open") "Open a .slides file. Your current deck remains available through Undo."
            else if (st.dialog == "export") "Export final slides and speaker notes to an HTML handout."
            else if (st.dialog == "print") "Save as PDF, with one final slide per page." else "Save an editable .slides presentation.",
        'dialog-error':st.error}) u.text(owner,string(key),text)
    u.text(owner,"file-confirm",if (st.dialog == "open") "Open" else if (st.dialog == "export") "Export" else "Save")
    for (id,shown in {'object-properties':obj != null,'slide-properties':obj == null,
        'text-properties':obj.tag == 'text' or obj.tag == 'content','font-properties':obj.tag == 'text',
        'shape-properties':obj.tag == 'shape','image-properties':obj.tag == 'image'}) u.show(owner,string(id),shown)
    u.show(owner,"file-dialog",st.dialog != "","flex")
    u.show(owner,"show-layer",st.show)
    u.show(owner,"insert-dialog",st.insert_form != null,"flex")
    if (st.insert_form != null) {
        u.markup(node(owner,"insert-dialog"),format([insert_ui.dialog(st)],'html'))
        u.text(owner,"insert-error",st.error)
    }
    disable(node(owner,"edit-inserted"),insert_model.settings(obj.source) == null)
    for (button in dom.query_selector_all(owner,"[data-tab]")) u.attr(button,"aria-selected",dom.get_attribute(button,"data-tab") == st.tab)
    for (id,disabled in {undo:len(st.undo)==0,redo:len(st.redo)==0,copy:obj==null,paste:st.clipboard==null,duplicate:obj==null,
        delete:obj==null,'apply-text':obj==null or not contains(['text','content'],obj.tag),front:obj==null,back:obj==null}) {
        for (control in dom.query_selector_all(owner,"[data-action=" ++ string(id) ++ "]")) disable(control,disabled)
    }
    for (id,val in {'deck-title':st.plan.title,'speaker-notes':m.notes(st),'prop-slide-title':m.scene(st).source.title,
        'prop-slide-background':html.layer_background(st.plan,m.scene(st)),layout:c.value(m.scene(st).source.layout,'blank'),
        transition:if (m.scene(st).transition == 'slide') 'cover' else m.scene(st).transition,'prop-slide-transition_duration':m.scene(st).transition_duration,
        'object-list':st.selected}) value(owner,string(id),val,editing)
    if (obj != null) {
        for (key in ["x","y","width","height","rotation","opacity","font_size"]) value(owner,"prop-" ++ key,obj[key],editing)
        for (key in ["color","fill","stroke","stroke_width","radius","src","fit","style"])
            value(owner,"prop-" ++ key,if (key == "color" or key == "fill") obj.paint else c.value(obj.source[key],if (key == "fit") "contain" else ""),editing)
        value(owner,"prop-text",if (m.plain(obj.source)) m.text(obj.source) else join([for (child in content(obj.source)) format(child,'mark')],"\n"),editing)
    }
    sync_animations(owner,st)
    sync_transitions(owner,st)
    sync_home(owner,st)
    // history and navigation replace thumbnails; keep keyboard input on the active slide's live node.
    if (thumbnail_focus) slide_drag.focus(owner,st.index)
    return request_layout(owner,fit(owner,display))
}
pn disable(control,disabled) {
    if (control == null) { return }
    if (disabled) dom.set_attribute(control,"disabled","")
    else if (dom.has_attribute(control,"disabled")) dom.remove_attribute(control,"disabled")
}
pn sync_home(owner,st) {
    if (st.tab != "Home") { return }
    let obj = m.chosen(st)
    let text = obj.tag == 'text'
    let inserted_shape = insert_model.settings(obj.source)
    let shape = obj.tag == 'shape' or inserted_shape.kind == "shape"
    for (id in ["home-font-family","home-font-size","grow-font","shrink-font","change-case","clear-format","bold","italic","underline","strike","subscript","superscript",
        "character-spacing","text-highlight","font-color","bullets","numbering","list-options","indent","outdent","line-spacing","align-left","align-center","align-right","align-justify","vertical-align"])
        disable(node(owner,id),not text)
    for (id in ["shape-outline","quick-styles"]) disable(node(owner,id),not shape)
    disable(node(owner,"shape-fill"),not shape or obj.source.kind == 'line' or inserted_shape.params.kind == "line")
    disable(node(owner,"reset-layout"),not home.has_placeholders(st))
    for (id in ["cut","copy","arrange"]) disable(node(owner,id),obj == null)
    disable(node(owner,"format-painter"),not (text or obj.tag == 'shape'))
    disable(node(owner,"paste"),st.clipboard == null)
    disable(node(owner,"paste-options"),st.clipboard == null)
    disable(node(owner,"paste-text"),name(st.clipboard) != 'text')
    u.attr(node(owner,"format-painter"),"aria-pressed",st.painter == true)
    for (id,color in {'font-color-preview':if (text) obj.paint else st.plan.palette.foreground,
        'text-highlight-preview':c.value(home.highlight_color(obj.source),"transparent"),
        'shape-fill-preview':if (inserted_shape.kind == "shape") inserted_shape.params.fill else if (shape and obj.paint != "none") obj.paint else "transparent",
        'shape-outline-preview':if (inserted_shape.kind == "shape") inserted_shape.params.stroke else if (shape and obj.source.stroke != "none") c.value(obj.source.stroke,"transparent") else "transparent"})
        u.style(node(owner,string(id)),"background-color",color)
    for (action in ["bold","italic","underline","strike","subscript","superscript","align-left","align-center","align-right","align-justify"])
        u.attr(node(owner,action),"aria-pressed",home.pressed(st,action))
    u.attr(node(owner,"bullets"),"aria-pressed",home.list_kind(st) == 'ul')
    u.attr(node(owner,"numbering"),"aria-pressed",home.list_kind(st) == 'ol')
    let family = home.family(st)
    let control = node(owner,"home-font-family")
    let found = [for (option in dom.query_selector_all(control,"option") where dom.get_attribute(option,"value") == family) option]
    if (len(found) == 0) {
        let fragment = dom.parse_fragment(control,format(<option value:family,family>,'xml'))
        for (option in dom.child_nodes(fragment)) dom.append_child(control,option)
    }
    value(owner,"home-font-family",family,st.editing.target)
    value(owner,"home-font-size",c.value(obj.font_size,st.plan.palette.body_size),st.editing.target)
}
pn checked(owner,id,enabled) {
    let control = node(owner,id)
    if (control != null and dom.get_state(control,"checked") != enabled) dom.set_state(control,"checked",enabled)
}
pn sync_animations(owner,st) {
    let row = animations.selected(st)
    let obj = m.chosen(st)
    u.show(owner,"animation-pane",st.animation_pane == true)
    let items = animations_ui.rows(st)
    u.markup(node(owner,"animation-list"),if (len(items)==0) "" else format(items,'html'))
    u.show(owner,"animation-empty",len(items)==0)
    u.show(owner,"animation-details",row != null)
    u.attr(owner,"data-animation",c.value(row.key,""))
    u.attr(owner,"data-animation-painter",st.animation_painter == true)
    for (entry in animation_catalog.catalog) {
        disable(node(owner,"animate-" ++ string(entry.id)),not animations.compatible(obj,entry.id))
        u.attr(node(owner,"animate-" ++ string(entry.id)),"aria-pressed",row.effect.kind == entry.id)
    }
    for (id in ["animation-start","animation-duration","animation-delay","animation-trigger","animation-options","animation-remove","animation-up","animation-down"])
        disable(node(owner,id),row == null)
    disable(node(owner,"animation-painter"),len(animations.copy(st))==0)
    disable(node(owner,"animation-preview"),len(items)==0)
    u.attr(node(owner,"animation-painter"),"aria-pressed",st.animation_painter == true)
    u.attr(node(owner,"animation-pane-toggle"),"aria-pressed",st.animation_pane == true)
    if (row == null) { return }
    for (key,val in {start:c.value(row.cue.start,'click'),duration:c.num(row.effect,"duration",400.0)/1000.0,
        delay:c.num(row.effect,"delay",0.0)/1000.0,from:c.value(row.effect.from,'bottom'),color:c.value(row.effect.color,"#f5b942"),
        scale:c.num(row.effect,"scale",1.15),angle:c.num(row.effect,"angle",360.0),distance:c.num(row.effect,"distance",40.0),
        opacity:c.num(row.effect,"opacity",0.5),easing:if (row.effect.easing is array) "custom" else c.value(row.effect.easing,'linear'),
        'path-preset':c.value(animation_catalog.path_name(row.effect.path),"custom"),orient:if (row.effect.orient == 'auto') "auto" else "none",repeat:c.num(row.effect,"repeat",1.0)})
        value(owner,"animation-" ++ string(key),val,st.editing.target)
    let kind = row.effect.kind
    for (key,shown in {from:kind in ['fly-in','fly-out','wipe-in','wipe-out','blinds-in','checkerboard-in'],
        color:kind in ['highlight','line-color'],scale:kind in ['zoom-in','zoom-out','grow','pulse'],angle:kind == 'spin',
        distance:kind in ['fly-in','fly-out'],opacity:kind == 'transparency'}) u.show(owner,"animation-field-" ++ string(key),shown,"flex")
    u.show(owner,"animation-field-path-preset",kind == 'motion',"flex")
    u.show(owner,"animation-field-orient",kind == 'motion',"flex")
    u.text(owner,"animation-group-help","Start, Trigger and order apply to this cue group; effect options apply to this effect.")
    u.text(owner,"animation-trigger-label",if (row.cue.trigger == null) "Plays in the slide build sequence." else "Triggered by clicking " ++ string(row.cue.trigger))
}
pn sync_transitions(owner,st) {
    let scene = m.scene(st)
    let custom = st.transition_custom == st.index or transitions.sound_kind(st) == "custom"
    u.show(owner,"transition-sound-file",st.tab == "Transitions" and custom)
    value(owner,"transition-sound-path",if (transitions.sound_kind(st) == "custom") scene.transition_sound.src else "",st.editing.target)
    if (st.tab != "Transitions") { return }
    for (entry in effects.catalog) u.attr(node(owner,"transition-" ++ string(entry.id)),"aria-pressed",transitions.kind(st) == entry.id)
    value(owner,"transition-duration",scene.transition_duration/1000.0,st.editing.target)
    value(owner,"transition-after",c.value(scene.advance_after,c.value(st.after_draft,2000.0))/1000.0,st.editing.target)
    value(owner,"transition-sound",if (custom) "custom" else transitions.sound_kind(st))
    checked(owner,"transition-click",scene.advance_on_click)
    checked(owner,"transition-automatic",scene.advance_after != null)
    disable(node(owner,"transition-after"),scene.advance_after == null)
    disable(node(owner,"transition-duration"),not effects.animated({*:scene,transition_duration:1.0}))
    disable(node(owner,"transition-options"),len(transitions_ui.menu_tree(st)) == 0)
}
pub pn change(owner,st,key,draft) any^ {
    if (starts_with(key,"animation-")) { return animations.property(st,slice(key,10),draft)^ }
    if (key == "transition-sound") {
        if (draft == "custom") { return {*:st,transition_custom:st.index} }
        if (draft == "none") { return {*:transitions.sound(st,null)^,transition_custom:false} }
        let choice = [for (entry in transitions.sounds where entry.id == draft) entry][0]
        if (choice == null) raise error("Slides: choose a transition sound")
        let bytes = input(url_resolve(resources.entry_uri()^,"assets/sounds/" ++ draft ++ ".wav"),'binary')^
        let src = media.data_uri(bytes,"audio/wav")^;
        return {*:transitions.sound(st,{name:choice.label,src:src})^,transition_custom:false}
    }
    if (key == "transition-sound-path") {
        if (trim(draft) == "") { return {*:transitions.sound(st,null)^,transition_custom:st.index} }
        return transitions.sound(st,{name:"Custom",src:url_resolve(st.base,trim(draft))^})^
    }
    if (starts_with(key,"transition-")) {
        let changed = transitions.property(st,slice(key,11),draft)^;
        return {*:changed,after_draft:if (slice(key,11) in ["automatic","after"]) c.value(m.scene(changed).advance_after,st.after_draft) else st.after_draft}
    }
    if (starts_with(key,"home-")) { return home.property(st,slice(key,5),draft)^ }
    if (key == "deck-title") { return m.commit_doc(st,<presentation *:map(st.doc),title:draft,*content(st.doc)>)^ }
    if (key == "notes") { return m.set_notes(st,draft)^ }
    if (key == "slide-transition") { return transitions.choose(st,draft)^ }
    if (starts_with(key,"slide-")) {
        let field = slice(key,6)
        let value = if (contains(["transition","layout"],field)) symbol(draft)
            else if (field == "transition_duration") float(draft)^ else draft;
        return m.slide_properties(st,{[field]:value})^
    }
    if (key == "text") { return m.set_text(st,draft,not m.plain(m.chosen(st).source))^ }
    let value = if (contains(["x","y","width","height","rotation","opacity","font_size","stroke_width","radius"],key)) float(draft)^
        else if (key == "fit") symbol(draft) else draft;
    return m.properties(st,{[key]:value})^
}
pub pn command(owner,st,action,base) any^ {
    let obj = m.chosen(st)
    if (starts_with(action,"animation-choose:") or starts_with(action,"animation-add:")) {
        let parts = split(action,":");
        return animations.choose(st,symbol(parts[1]),parts[0] == "animation-add")^
    }
    if (starts_with(action,"animation-select:")) { return animations.select(st,slice(action,17)) }
    if (starts_with(action,"animation-trigger:")) { return animations.property(st,"trigger",slice(action,18))^ }
    if (action == "animation-pane-toggle") { return {*:st,animation_pane:st.animation_pane != true} }
    if (action in ["animation-pane-close","animation-options"]) { return {*:st,animation_pane:action == "animation-options"} }
    if (action == "animation-remove") { return animations.remove(st)^ }
    if (action in ["animation-up","animation-down"]) { return animations.move(st,if (action == "animation-up") -1 else 1)^ }
    if (action == "animation-painter") { return {*:st,animation_clipboard:animations.copy(st),animation_painter:st.animation_painter != true,painter:false,status:"Click an object to copy these animations. Escape cancels."} }
    if (action == "paint-animation") { return animations.paint(st)^ }
    if (action == "animation-preview") { return start_show(owner,st,animations.preview(st),0,"animation")^ }
    if (starts_with(action,"transition-choose:")) { return {*:transitions.choose(st,slice(action,18))^,status:"Transition applied to this slide."} }
    if (starts_with(action,"transition-direction:") or starts_with(action,"transition-variant:")) {
        let parts = split(action,":");
        return transitions.property(st,slice(parts[0],11),parts[1])^
    }
    if (action == "transition-apply-all") { return {*:transitions.apply_all(st)^,status:"Transition and timing applied to all slides."} }
    if (action == "transition-preview") { return start_show(owner,st,transitions.preview(st),0,"transition")^ }
    if (starts_with(action,"insert-open-")) {
        let parts = split(slice(action,12),":");
        return open_insert(st,parts[0],parts[1])^
    }
    if (action == "insert-edit" or action == "edit-inserted") {
        let existing = insert_model.settings(obj.source);
        if (existing == null) raise error("Slides: select an inserted object to edit")
        return open_insert(st,existing.kind,null,true)^
    }
    if (action == "insert-cancel") { return {*:st,insert_form:null,error:""} }
    if (action == "insert-confirm") {
        let kind = st.insert_form.kind
        let values = read_insert(owner,st)^
        let resolved = resolve_insert(st,kind,values)^;
        return {*:insert_model.insert(st,kind,resolved,st.insert_form.target)^,insert_form:null,error:"",status:"Inserted " ++ insert_data.spec(kind).label}
    }
    if (starts_with(action,"insert-symbol:")) {
        let parts = split(action,":")
        let char = insert_data.symbols[int(parts[1])].chars[int(parts[2])]
        let values = read_insert(owner,st)^;
        return {*:st,insert_form:{*:st.insert_form,values:{*:values,text:char}}}
    }
    if (starts_with(action,"insert-edit-comment:")) {
        let existing = [for (comment in insert_model.comments(st) where comment.id == slice(action,20)) comment][0];
        return {*:st,insert_form:{kind:"comment",values:{author:existing.author,text:existing.text},target:existing.id},error:""}
    }
    if (starts_with(action,"insert-remove-comment:")) {
        let values = read_insert(owner,st)^;
        return {*:insert_model.remove_comment(st,slice(action,22))^,insert_form:{*:st.insert_form,values:values}}
    }
    if (action == "insert-image") { return open_insert(st,"picture")^ }
    if (starts_with(action,"menu-")) {
        let key = slice(action,5)
        let trigger = node(owner,key)
        let box = dom.bounding_box(trigger)
        let menu = node(owner,"home-menu");
        u.style(menu,"left",c.px(box.x))
        u.style(menu,"top",c.px(box.y+box.height+5.0))
        dom.request_frame(owner,"home_menu_fit")
        return {*:st,menu:if (st.menu == key) "" else key}
    }
    if (action == "selection-pane") {
        dom.focus_set(node(owner,"object-list"),false)
        return {*:st,status:"Choose an object in the Format selection list."}
    }
    if (home.handles(action)) { return home.command(if (action == "format-painter") {*:st,animation_painter:false} else st,action)^ }
    if (action == "undo" or action == "redo") { return {*:m.history(st,action == "redo"),transition_custom:null} }
    if (action == "new") { return {*:m.commit_doc(st,m.starter(),0,"")^,base:base,path:"",transition_custom:null,status:"New presentation. Undo restores your previous deck."} }
    if (action == "add-slide" or action == "duplicate-slide") { return m.add_slide(st,action == "duplicate-slide")^ }
    if (action == "delete-slide") { return m.delete_slide(st)^ }
    if (action == "slide-up" or action == "slide-down") { return m.move_slide(st,if (action == "slide-up") -1 else 1)^ }
    if (starts_with(action,"insert-")) { return {*:m.add_object(st,slice(action,7))^,status:"Object inserted. Edit its content and appearance in Format."} }
    if (action == "copy") { return {*:st,clipboard:m.copy_object(st),status:"Object copied."} }
    if (action == "paste" or action == "duplicate") { return m.paste(st,if (action == "duplicate") m.copy_object(st) else null)^ }
    if (action == "delete" or action == "remove-object") { return m.remove_object(st)^ }
    if (action == "front" or action == "back") { return m.reorder(st,action == "front")^ }
    if (action == "center-object" and obj != null) {
        let parent = m.parent(st);
        return m.properties(st,{x:(c.value(parent.width,st.plan.width)-obj.width)/2.0,y:(c.value(parent.height,st.plan.height)-obj.height)/2.0})^
    }
    if (action == "grid") { return {*:st,grid:not st.grid} }
    if (action == "fit" or action == "design-fit") { return {*:st,zoom:1.0} }
    if (action == "zoom-in" or action == "zoom-out") { return {*:st,zoom:min(3.0,max(0.25,st.zoom*(if (action == "zoom-in") 1.2 else 1.0/1.2)))} }
    if (starts_with(action,"theme-")) { return m.commit_doc(st,<presentation *:map(st.doc),theme:symbol(slice(action,6)),*content(st.doc)>)^ }
    if (action == "apply-text") { return m.set_text(st,field(owner,"prop-text"),not m.plain(obj.source))^ }
    if (starts_with(action,"animate-") and obj != null) { return animations.choose(st,symbol(slice(action,8)),true)^ }
    if (action == "clear-animations") { return animations.remove(st,true)^ }
    if (action == "present" or action == "present-current") {
        return start_show(owner,st,st.doc,if (action == "present-current") st.index else 0)^
    }
    if (action == "end-show") {
        let sound_error = dom.get_attribute(node(owner,"editor-show"),"data-slide-sound-error")
        dom.dispatch(node(owner,"editor-show"),{type:"slide_command",bubbles:false,detail:{command:"stop"}})
        if (st.preview_token > 0) dom.cancel_frame(owner,st.preview_token)
        dom.dispatch(node(owner,"show-host"),{type:"editor_end",bubbles:false})
        dom.focus_set(node(owner,"stage"),false)
        return {*:st,show:false,preview:false,status:c.value(sound_error,st.status)}
    }
    if (contains(["open","save","export","print"],action)) {
        value(owner,"file-path",if (action == "print") "temp/slides/print.pdf" else if (action == "export") "temp/slides/handout.html" else if (st.path != "" and action == "open") st.path else "temp/slides/presentation.slides")
        return {*:st,dialog:action,error:""}
    }
    if (action == "file-cancel") { return {*:st,dialog:"",error:""} }
    if (action == "file-confirm") {
        let path = trim(field(owner,"file-path"))
        let ext = if (st.dialog == "print") ".pdf" else if (st.dialog == "export") ".html" else ".slides"
        if (not ends_with(lower(path),ext)) raise error("Slides: choose a " ++ ext ++ " file path")
        let uri = url_resolve("file://" ++ sys.proc.self.cwd# ++ "/",path)^;
        if (st.dialog == "open") { return m.open_deck(st,input(path,'slides')^,url_resolve(uri,".")^,path)^ }
        io.mkdir("temp") ^ { null }
        io.mkdir("temp/slides") ^ { null }
        // freeze relative assets to their original base when Save As changes the deck directory.
        let deck = <presentation *:map(st.doc),base_uri:st.base,*content(st.doc)>;
        if (st.dialog == "print") {
            exports.pdf(format(v.printout(st,deck)^,'html'),st.plan.width,st.plan.height,"html",path,"Slides")^
        } else if (st.dialog == "export") {
            output(format(slide.handout(deck)^,'html'),path,{atomic:true})^
        } else { output(deck,path,{format:'mark',atomic:true})^ }
        return {*:st,saved:if (st.dialog == "save") st.doc else st.saved,path:if (st.dialog == "save") path else st.path,
            dialog:"",error:"",status:if (st.dialog == "print") "Saved " ++ path ++ " for printing" else "Written to " ++ path}
    }
    return st
}

pn start_show(owner,st,deck,index,preview=false) any^ {
    let screen = dom.viewport_size(owner)
    dom.dispatch(node(owner,"show-host"),{type:"editor_present",bubbles:false,detail:{deck:deck,base:st.base,
        width:screen.width,height:max(160.0,screen.height-150.0),index:index,preview:preview != false,preview_kind:preview}})
    return {*:st,show:true,preview:preview != false,preview_kind:preview}
}
pub pn restore_menu_focus(owner,st,key) {
    if (key=="slide-context") {
        if (not st.show) slide_drag.focus(owner,st.index)
    } else dom.focus_set(node(owner,key),false)
}
pub pn open_slide_context(owner,st,index,point=null) {
    let ready = finish_inline(owner,st)
    if (ready.inline!=null) { return ready }
    let next = publish(owner,{*:m.select_slide(ready,index),menu:"slide-context",editing:null,error:""})
    let box = dom.bounding_box(slide_drag.thumbnail(owner,next.index))
    let at = if (point==null) {x:box.x+12.0,y:box.y+min(32.0,box.height)} else point
    u.style(node(owner,"home-menu"),"left",c.px(at.x))
    u.style(node(owner,"home-menu"),"top",c.px(at.y))
    dom.request_frame(owner,"home_menu_fit")
    return next
}
pub pn dispatch(owner,st,action,base) {
    var result = {*:st,editing:null}
    command(owner,result,action,base) ^ { result = {*:result,error:^.message,status:^.message} } ~ { result = ~ }
    let published = publish(owner,if (starts_with(action,"menu-")) result else {*:result,menu:""},true,
        (action=="new" or (action=="file-confirm" and st.dialog=="open")) and result.doc!=st.doc);
    if (st.menu=="slide-context" and published.menu=="") restore_menu_focus(owner,published,st.menu)
    // both pointer and keyboard dismissal restore focus outside the hidden dialog.
    if (st.dialog != "" and published.dialog == "") {
        let trigger = dom.query_selector(owner,"[data-action=" ++ st.dialog ++ "]");
        dom.focus_set(if (trigger == null) node(owner,"stage") else trigger,false)
    }
    if (published.insert_form != null and published.insert_form != st.insert_form) dom.request_frame(owner,"insert_dialog_ready")
    return published
}
pub fn open_insert(st,kind,variant=null,editing_object=false) any^ {
    let spec = insert_data.spec(kind)
    let obj = m.chosen(st)
    let existing = insert_model.settings(obj.source)
    let matched = existing.kind == kind
    let selection = (kind in ["link","action"]) and obj.tag == 'text' and existing == null
    let values = {*:spec.defaults,*:if (editing_object and matched) existing.params else {},
        *:if (kind == "shape" and variant != null) {kind:variant} else {},
        *:if (selection) {label:m.text(obj.source)} else {},
        *:if (editing_object and matched and kind == "picture") {path:obj.source.src,fit:string(obj.source.fit)} else {}}
    let dated = if (kind == "date" and values.text == "") {*:values,text:string(date())} else values;
    if (spec == null) raise error("Slides: choose a supported insert object")
    else {*:st,insert_form:{kind:kind,values:dated,target:if ((editing_object and matched) or selection) st.selected else ""},menu:"",error:""}
}
pub pn read_insert(owner,st) map^ {
    let values = st.insert_form.values
    let fields = insert_data.spec(st.insert_form.kind).fields;
    {*:values,*:map([for (entry in fields) {
        let control = node(owner,"insert-field-" ++ entry.id)
        let value = dom.get_state(control,if (entry.kind == "checkbox") "checked" else "value");
        (entry.id,if (entry.kind == "number") float(value)^ else if (entry.kind == "checkbox") value == true or value == "true" else value)
    }])}
}
pub pn capture_slide(st,index) string^ {
    let prefix = "temp/slides/capture-" ++ string(sys.proc.self.pid#) ++ "-" ++ string(st.serial)
    io.mkdir("temp") ^ {null}
    io.mkdir("temp/slides") ^ {null}
    let page = slide.snapshot(<presentation *:map(st.doc),base_uri:st.base,*content(st.doc)>,{slide:index,cue:'final'})^
    output(format(page,'html'),prefix ++ ".html",{atomic:true})^
    let rendered = io.exec(sys.proc.self.argv#[0],["--no-log","render",prefix ++ ".html","-o",prefix ++ ".png",
        "--viewport-width",c.fmt(st.plan.width),"--viewport-height",c.fmt(st.plan.height)],{timeout_ms:30000})^;
    if (rendered.exit_code != 0) raise error("Slides: could not render the slide picture")
    return media.data_uri(input(prefix ++ ".png",'binary')^,"image/png")^
}
pn resolve_insert(st,kind,values) map^ {
    var result = values
    if (kind in ["picture","screenshot","video","audio"]) {
        if (kind == "screenshot" and values.mode == "slide") { result = {*:result,preview:capture_slide(st,st.index)^} }
        else if (trim(values.path) != "") { result = {*:result,path:url_resolve(st.base,values.path)} }
        if (kind == "video" and values.poster != "") { result = {*:result,poster:url_resolve(st.base,values.poster)} }
    }
    if (kind == "object" and trim(values.path) != "") {
        result = {*:result,source:input(url_resolve(st.base,values.path),'text')^}
    }
    if (kind == "zoom") {
        let found = [for (scene in st.plan.slides where scene.id == values.target) scene][0]
        if (found == null) raise error("Slides: choose a destination slide")
        result = {*:result,preview:capture_slide(st,found.index)^}
    }
    return result
}
pub pn focus_insert(owner) {
    dom.focus_set(dom.query_selector(node(owner,"insert-dialog"),"input,select,textarea"),false)
}
pub pn finish_inline(owner,st,accept=true) {
    if (st.inline == null) { return st }
    let control = node(owner,if (st.inline.kind=="rich") "canvas-rich-text" else "canvas-text")
    // removing or parking a focused surface must not commit its draft a second time.
    if (control!=null) dom.set_attribute(control,"data-inline-finishing","true")
    if (st.inline.ready!=null and st.inline.ready>0) dom.cancel_frame(owner,st.inline.ready)
    var next = {*:st,inline:null}
    if (st.inline.kind=="rich") {
        let host = node(owner,"rich-text-host")
        if (accept) {
            dom.dispatch(host,{type:"slide_inline_read",bubbles:false})
            let failure = c.value(dom.get_attribute(host,"data-inline-error"),"");
            if (failure!="") { return {*:st,error:failure,status:failure} }
            if (dom.get_attribute(host,"data-inline-changed")=="true")
                m.set_text(next,dom.get_attribute(host,"data-inline-mark"),true,insert_model.inline_source) ^ { next = {*:next,error:^.message} } ~ { next = ~ }
        }
        // keep the retained editor owner alive when the procedural slide canvas is replaced.
        dom.dispatch(host,{type:"slide_inline_end",bubbles:false})
        dom.append_child(node(owner,"inline-parking"),host)
    }
    if (st.inline.kind!="rich") {
        if (accept) m.set_text(next,field(owner,"canvas-text")) ^ { next = {*:next,error:^.message} } ~ { next = ~ }
        if (control!=null) dom.remove(control)
    }
    // unchanged markup is cached, so closing a draft must undo its temporary visibility change.
    u.style(node(owner,"edit-" ++ st.inline.selected),"visibility",st.inline.visibility)
    return publish(owner,next)
}
pub pn begin_inline(owner,st,point=null) {
    let obj = m.chosen(st)
    if (st.inline!=null or not inline_text.can_edit(obj)) { return st }
    let place = node(owner,"edit-place-" ++ st.selected)
    let plain = obj.tag=='text' and all([for (child in content(obj.source)) child is string]);
    let visibility = dom.style_get_property(node(owner,"edit-" ++ st.selected),"visibility")
    u.style(node(owner,"edit-" ++ st.selected),"visibility","hidden")
    if (plain) {
        let fragment = dom.parse_fragment(place,format(v.inline_editor(obj,"transparent"),'xml'))
        for (control in dom.child_nodes(fragment)) dom.append_child(place,control)
        dom.focus_set(node(owner,"canvas-text"),false)
        let value = m.text(obj.source)
        let end = text_offsets.utf16_from_codepoint(value,len(value),0,0)
        dom.tc_set_selection(node(owner,"canvas-text"),end,end,"none")
        spelling.update(owner,"slides-text",value)
    } else {
        let host = node(owner,"rich-text-host")
        dom.dispatch(host,{type:"slide_inline_begin",bubbles:false,detail:{object:obj,point:point}})
        dom.append_child(place,host)
    }
    // the newly inserted control needs committed layout before the entry click can be resolved.
    return {*:st,editing:null,inline:{selected:st.selected,kind:if (plain) "plain" else "rich",point:point,visibility:visibility,
        ready:if (plain and point!=null) dom.request_frame(owner,"slide_plain_ready") else 0}}
}
pub pn plain_ready(owner,st,token) {
    if (st.inline==null or st.inline.kind!="plain" or st.inline.ready!=token) { return st }
    inline_text.place_caret(node(owner,"canvas-text"),st.inline.point,true)
    return {*:st,inline:{*:st.inline,ready:0}}
}
pub pn file_shortcut(owner,st,key,base) {
    let ready = finish_inline(owner,st)
    if (ready.inline!=null) { return ready }
    let next = dispatch(owner,ready,{s:"save",o:"open",p:"print"}[key],base)
    dom.focus_set(node(owner,"file-path"),false)
    return next
}
pub fn drag_geometry(st,x,y) {
    let d = st.drag
    let obj = d.object
    let parents = [for (target in m.scene(st).targets where target.tag == 'group' and starts_with(st.selected,target.dom_key ++ "-")) target]
    let angle = sum([for (p in parents) p.rotation]) + (if (d.resize) obj.rotation else 0.0)
    let factor = math.prod([for (p in parents) p.scale]) * (if (d.resize) obj.scale else 1.0)
    let delta = geometry.rotate_point({x:(x-d.x)/st.scale/factor,y:(y-d.y)/st.scale/factor},{x:0.0,y:0.0},-angle)
    let snap = if (st.grid) 10.0 else 1.0;
    if (d.resize) {
        let width = max(8.0,round((obj.width+delta.x)/snap)*snap)
        let height = max(8.0,round((obj.height+delta.y)/snap)*snap)
        let dw = width-obj.width
        let dh = height-obj.height
        let rotated = geometry.rotate_point({x:dw,y:dh},{x:0.0,y:0.0},obj.rotation);
        // CSS transforms use the box center; compensate so the opposite corner stays fixed.
        {width:width,height:height,x:obj.x+(rotated.x*obj.scale-dw)/2.0,y:obj.y+(rotated.y*obj.scale-dh)/2.0}
    }
    else { ({x:round((obj.x+delta.x)/snap)*snap,y:round((obj.y+delta.y)/snap)*snap}) }
}
pub pn preview_drag(owner,st,x,y) {
    let values = drag_geometry(st,x,y)
    let place = node(owner,"edit-place-" ++ st.selected)
    for (key,val in values) u.style(place,if (key == "x") "left" else if (key == "y") "top" else string(key),c.px(val))
    // native clicks can include a stationary pointermove; selection must not become an edit.
    let changed = any([for (key,val in values) val != st.drag.object[key]])
    return {*:st,drag:{*:st.drag,values:if (changed) values else null}}
}
