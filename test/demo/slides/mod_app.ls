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
pub pn selection(owner,st) {
    for (old in dom.query_selector_all(node(owner,"slide-render"),".editor-selected")) {
        dom.set_attribute(old,"class","slide-object")
        for (handle in dom.query_selector_all(old,".editor-handle")) dom.remove(handle)
    }
    let place = node(owner,"edit-place-" ++ st.selected)
    if (place != null) {
        dom.set_attribute(place,"class","slide-object editor-selected")
        let fragment = dom.parse_fragment(place,"<div class=\"editor-handle\" data-resize=\"true\"></div>")
        for (handle in dom.child_nodes(fragment)) dom.append_child(place,handle)
    }
}
pub pn publish(owner,st) {
    let obj = m.chosen(st)
    let editing = st.editing.target
    u.markup(node(owner,"ribbon"),format(v.ribbon(st.tab),'html'))
    let display = if (st.display_doc == st.doc and st.display_base == st.base) st else
        {*:st,display_doc:st.doc,display_base:st.base,display_plan:slide.compile(resources.slide_sources(st.doc,st.base))^}
    let visual_state = {*:st,plan:display.display_plan}
    u.markup(node(owner,"thumbnails"),format(v.thumbnails(visual_state)^,'html'))
    u.markup(node(owner,"slide-render"),format([v.canvas(display.display_plan,st.index,"edit")^],'html'))
    u.markup(node(owner,"object-list"),join([for (option in v.object_options(st)) format(option,'xml')],""))
    selection(owner,st)
    u.attr(node(owner,"paper"),"class",if (st.grid) "grid" else "")
    for (key,val in {'data-index':st.index,'data-slides':len(st.plan.slides),'data-selected':st.selected,
        'data-dirty':st.doc != st.saved,'data-error':st.error,'data-dialog':st.dialog,'data-mode':if (st.show) "present" else "edit",'data-undo':len(st.undo),
        'data-x':obj.x,'data-y':obj.y,'data-width':obj.width,'data-height':obj.height,'data-editing':editing}) u.attr(owner,string(key),val)
    for (key,text in {'save-state':if (st.doc == st.saved) "Saved" else "Unsaved changes",
        'slide-count':string(len(st.plan.slides)) ++ " slides",'slide-heading':c.value(m.scene(st).source.title,"Slide " ++ string(st.index+1)),
        position:"Slide " ++ string(st.index+1) ++ " of " ++ string(len(st.plan.slides)),'canvas-size':c.fmt(st.plan.width) ++ " × " ++ c.fmt(st.plan.height),status:if (st.error != "") st.error else st.status,
        'object-kind':if (obj == null) "Slide" else string(obj.tag),'text-label':if (obj != null and not m.plain(obj.source)) "Content (Mark)" else "Text",
        'dialog-title':if (st.dialog == "open") "Open presentation" else if (st.dialog == "export") "Export handout" else "Save presentation",
        'dialog-copy':if (st.dialog == "open") "Open a .slides file. Your current deck remains available through Undo."
            else if (st.dialog == "export") "Export final slides and speaker notes to an HTML handout." else "Save an editable .slides presentation.",
        'dialog-error':st.error}) u.text(owner,string(key),text)
    u.text(owner,"file-confirm",if (st.dialog == "open") "Open" else if (st.dialog == "export") "Export" else "Save")
    for (id,shown in {'object-properties':obj != null,'slide-properties':obj == null,
        'text-properties':obj.tag == 'text' or obj.tag == 'content','font-properties':obj.tag == 'text',
        'shape-properties':obj.tag == 'shape','image-properties':obj.tag == 'image'}) u.show(owner,string(id),shown)
    u.show(owner,"file-dialog",st.dialog != "","flex")
    u.show(owner,"show-layer",st.show)
    for (button in dom.query_selector_all(owner,"[data-tab]")) u.attr(button,"aria-selected",dom.get_attribute(button,"data-tab") == st.tab)
    for (id,disabled in {undo:len(st.undo)==0,redo:len(st.redo)==0,copy:obj==null,paste:st.clipboard==null,duplicate:obj==null,
        delete:obj==null,'apply-text':obj==null or not contains(['text','content'],obj.tag),front:obj==null,back:obj==null}) {
        let button = node(owner,string(id))
        if (disabled) dom.set_attribute(button,"disabled","")
        else if (dom.has_attribute(button,"disabled")) dom.remove_attribute(button,"disabled")
    }
    for (id,val in {'deck-title':st.plan.title,'speaker-notes':m.notes(st),'prop-slide-title':m.scene(st).source.title,
        'prop-slide-background':html.layer_background(st.plan,m.scene(st)),layout:c.value(m.scene(st).source.layout,'blank'),
        transition:m.scene(st).transition,'prop-slide-transition_duration':m.scene(st).transition_duration,
        'object-list':st.selected}) value(owner,string(id),val,editing)
    if (obj != null) {
        for (key in ["x","y","width","height","rotation","opacity","font_size"]) value(owner,"prop-" ++ key,obj[key],editing)
        for (key in ["color","fill","stroke","stroke_width","radius","src","fit","style"])
            value(owner,"prop-" ++ key,if (key == "color" or key == "fill") obj.paint else c.value(obj.source[key],if (key == "fit") "contain" else ""),editing)
        value(owner,"prop-text",if (m.plain(obj.source)) m.text(obj.source) else join([for (child in content(obj.source)) format(child,'mark')],"\n"),editing)
    }
    return fit(owner,display)
}
pub pn change(owner,st,key,input) any^ {
    if (key == "deck-title") { return m.commit_doc(st,<presentation *:map(st.doc),title:input,*content(st.doc)>)^ }
    if (key == "notes") { return m.set_notes(st,input)^ }
    if (starts_with(key,"slide-")) {
        let field = slice(key,6)
        let value = if (contains(["transition","layout"],field)) symbol(input)
            else if (field == "transition_duration") float(input)^ else input;
        return m.slide_properties(st,{[field]:value})^
    }
    if (key == "text") { return m.set_text(st,input,not m.plain(m.chosen(st).source))^ }
    let value = if (contains(["x","y","width","height","rotation","opacity","font_size","stroke_width","radius"],key)) float(input)^
        else if (key == "fit") symbol(input) else input;
    return m.properties(st,{[key]:value})^
}
pub pn command(owner,st,action,base) any^ {
    let obj = m.chosen(st)
    if (action == "undo" or action == "redo") { return m.history(st,action == "redo") }
    if (action == "new") { return {*:m.commit_doc(st,m.starter(),0,"")^,base:base,path:"",status:"New presentation. Undo restores your previous deck."} }
    if (action == "add-slide" or action == "duplicate-slide") { return m.add_slide(st,action == "duplicate-slide")^ }
    if (action == "delete-slide") { return m.delete_slide(st)^ }
    if (action == "slide-up" or action == "slide-down") { return m.move_slide(st,if (action == "slide-up") -1 else 1)^ }
    if (starts_with(action,"insert-")) { return {*:m.add_object(st,slice(action,7))^,status:"Object inserted. Edit its content and appearance in Format."} }
    if (action == "copy") { return {*:st,clipboard:obj.source,status:"Object copied."} }
    if (action == "paste" or action == "duplicate") { return m.paste(st,if (action == "duplicate") obj.source else null)^ }
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
    if (contains(["bold","italic","align-left","align-center","align-right"],action) and obj.tag == 'text') {
        let style = c.value(obj.source.style,"")
        let property = if (action == "bold") "font-weight" else if (action == "italic") "font-style" else "text-align"
        let val = if (action == "bold") (if (contains(style,"font-weight:700")) "400" else "700")
            else if (action == "italic") (if (contains(style,"font-style:italic")) "normal" else "italic") else slice(action,6)
        let clean = join([for (decl in split(style,";") where not starts_with(trim(decl),property ++ ":") and trim(decl) != "") decl],";");
        return m.properties(st,{style:clean ++ ";" ++ property ++ ":" ++ val ++ ";"})^
    }
    if (starts_with(action,"animate-") and obj != null) { return m.animate(st,slice(action,8),field(owner,"animation-start"))^ }
    if (action == "clear-animations") { return m.clear_animations(st)^ }
    if (action == "present" or action == "present-current") {
        let screen = dom.viewport_size(owner)
        dom.dispatch(node(owner,"show-host"),{type:"editor_present",bubbles:false,detail:{deck:st.doc,base:st.base,
            width:screen.width,height:max(160.0,screen.height-150.0),index:if (action == "present-current") st.index else 0}})
        return {*:st,show:true}
    }
    if (action == "end-show") {
        dom.dispatch(node(owner,"show-host"),{type:"editor_end",bubbles:false})
        dom.focus_set(node(owner,"stage"),false)
        return {*:st,show:false}
    }
    if (contains(["open","save","export"],action)) {
        value(owner,"file-path",if (action == "export") "temp/slides/handout.html" else if (st.path != "" and action == "open") st.path else "temp/slides/presentation.slides")
        return {*:st,dialog:action,error:""}
    }
    if (action == "file-cancel") { return {*:st,dialog:"",error:""} }
    if (action == "file-confirm") {
        let path = trim(field(owner,"file-path"))
        let ext = if (st.dialog == "export") ".html" else ".slides"
        if (not ends_with(lower(path),ext)) raise error("Slides: choose a " ++ ext ++ " file path")
        let uri = url_resolve("file://" ++ sys.proc.self.cwd# ++ "/",path)^;
        if (st.dialog == "open") { return m.open_deck(st,input(path,'slides')^,url_resolve(uri,".")^,path)^ }
        io.mkdir("temp") ^ { null }
        io.mkdir("temp/slides") ^ { null }
        // freeze relative assets to their original base when Save As changes the deck directory.
        let deck = <presentation *:map(st.doc),base_uri:st.base,*content(st.doc)>;
        if (st.dialog == "export") {
            output(format(slide.handout(deck)^,'html'),path,{atomic:true})^
        } else { output(deck,path,{format:'mark',atomic:true})^ }
        return {*:st,saved:if (st.dialog == "save") st.doc else st.saved,path:if (st.dialog == "save") path else st.path,
            dialog:"",error:"",status:"Written to " ++ path}
    }
    return st
}

pub pn dispatch(owner,st,action,base) {
    var result = {*:st,editing:null}
    command(owner,result,action,base) ^ { result = {*:result,error:^.message,status:^.message} } ~ { result = ~ }
    return publish(owner,result)
}
pub pn finish_inline(owner,st,accept=true) {
    if (st.inline == null) { return st }
    let value = field(owner,"canvas-text")
    var next = {*:st,inline:null}
    if (accept) m.set_text(next,value) ^ { next = {*:next,error:^.message} } ~ { next = ~ }
    let control = node(owner,"canvas-text")
    if (control != null) dom.remove(control)
    return publish(owner,next)
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
