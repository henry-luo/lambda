// S12.1.3 / D4.5.1v4: keep the native shell and controls; update retained regions in handlers.
import dom
import resources: ~~.mod_resources
import model: .mod_model
import files: .mod_io
import ui: .mod_ui
import thumbs: .mod_thumbnails
import updates: .mod_dom

let BASE = url_resolve(resources.entry_uri()^,".")
let CWD = sys.proc.self.cwd#
// read the system path before joining sidebar destinations or expanding ~ (S2.4.3v3).
let HOME = sys.home#
let START = model.absolute(slice(url_resolve(BASE,"files/"),7),CWD)
let PLACES = [{id:"demo",name:"Demo Files",icon:"folder",path:START},
    {id:"home",name:"Home",icon:"home",path:HOME},
    *[for (id,caption in {desktop:"Desktop",documents:"Documents",downloads:"Downloads",pictures:"Pictures"})
        {id:string(id),name:caption,icon:string(id),path:model.absolute(caption,HOME)}],
    {id:"project",name:"Project",icon:"folder",path:CWD},
    {id:"computer",name:"Computer",icon:"computer",path:"/"}]
let INITIAL = model.listing(model.initial(START),START,files.read_folder(START)^)
fn owner_of(evt) => dom.get_element_by_id(dom.root_node(evt.target),"finder")
fn node(owner,id) => dom.get_element_by_id(owner,id)
pn text(owner,id,value) {
    let target = node(owner,id)
    if (dom.text_content(target) != value) dom.set_text_content(target,value)
}
pn attr(target,key,value) {
    updates.attr(target,key,value)
}
pn show(owner,id,visible,display="block") {
    let target = node(owner,id)
    let requested = if (visible) display else "none"
    if (dom.style_get_property(target,"display") != requested) dom.style_set_property(target,"display",requested)
}
pn region(owner,id,markup) {
    let target = node(owner,id)
    // avoid retiring the clicked row between the first and second click of a double-click.
    if (dom.get_attribute(target,"data-markup") != markup) {
        thumbs.release(target)
        dom.set_inner_html(target,markup)
        dom.set_attribute(target,"data-markup",markup)
    }
}
pn disabled(owner,id,value) {
    let target = node(owner,id)
    let previous = dom.get_attribute(target,"disabled")
    if (value and previous == null) dom.set_attribute(target,"disabled","")
    if (not value and previous != null) dom.remove_attribute(target,"disabled")
}
pn publish(owner,st) {
    var next = st
    if (dom.get_attribute(owner,"data-path") != st.path) { next = {*:next,thumbnails:thumbs.cancel_jobs(owner,next.thumbnails)} }
    if (st.preview_data == null and st.focus != "" and (st.preview or st.look)) {
        let data = files.preview(model.focused(st)) ^ { {kind:"error",message:^.message} }
        next = {*:next,preview_data:data}
    }
    for (key,value in {path:next.path,mode:next.mode,selected:len(next.selected),hidden:next.hidden,dialog:next.dialog,look:next.look}) attr(owner,"data-" ++ string(key),value)
    text(owner,"folder-title",if (next.path == START) "Demo Files" else model.basename(next.path))
    text(owner,"status",model.status(next))
    region(owner,"breadcrumbs",format(ui.breadcrumb(next),'html'))
    let listing = {entries:next.entries,path:next.path,mode:next.mode,query:next.query,sort:next.sort,descending:next.descending,hidden:next.hidden}
    // selection and dialogs reuse the published listing instead of rebuilding every SVG and row.
    let replaced = next.published == null or next.published != listing
    if (replaced) region(owner,"files",format(ui.files(next),'html'))
    else for (row in dom.query_selector_all(node(owner,"files"),"[data-path]")) {
        let path = dom.get_attribute(row,"data-path")
        attr(row,"aria-selected",contains(next.selected,path))
        attr(row,"data-focused",path == next.focus)
    }
    let focused = dom.query_selector(node(owner,"files"),"[data-focused='true']")
    if (focused != null) attr(node(owner,"files"),"aria-activedescendant",dom.get_attribute(focused,"id"))
    else if (dom.get_attribute(node(owner,"files"),"aria-activedescendant") != null) dom.remove_attribute(node(owner,"files"),"aria-activedescendant")
    for (place in dom.query_selector_all(owner,".place")) attr(place,"aria-current",if (dom.get_attribute(place,"data-location") == next.path) "page" else "false")
    for (id,pressed in {icons:next.mode == "icons",list:next.mode == "list",preview:next.preview,hidden:next.hidden}) attr(node(owner,string(id)),"aria-pressed",pressed)
    disabled(owner,"back",next.cursor == 0)
    disabled(owner,"forward",next.cursor >= len(next.history)-1)
    disabled(owner,"direction",len(next.entries)==0)
    text(owner,"direction",if (next.descending) "↓" else "↑")
    text(owner,"hidden",if (next.hidden) "Hide hidden files" else "Show hidden files")
    show(owner,"list-heading",next.mode == "list","grid")
    show(owner,"preview-pane",next.preview)
    show(owner,"error",next.error != "" and not next.dialog,"flex")
    text(owner,"error-text",next.error)
    text(owner,"go-error",next.error)
    show(owner,"go-dialog",next.dialog,"flex")
    show(owner,"quick-look",next.look,"flex")
    region(owner,"preview-body",format(ui.preview(next),'html'))
    if (next.look) {
        text(owner,"look-title",model.focused(next).name)
        region(owner,"look-body",format(ui.preview(next,true),'html'))
    }
    if (dom.get_state(node(owner,"search"),"value") != next.query) dom.set_state(node(owner,"search"),"value",next.query)
    return {*:next,published:listing,thumbnails:thumbs.schedule(owner,next.thumbnails)}
}
fn navigate(st,path,cursor=-1) map^ {
    if (not (path is string) or trim(path)=="") raise error("Enter a folder path.")
    else {
        let target = model.absolute(path,st.path,HOME)
        let entries = files.read_folder(target)^;
        model.listing(st,target,entries,cursor)
    }
}
fn open_selected(st) map^ {
    let entry = model.focused(st);
    if (entry == null) st else if (entry.dir) navigate(st,entry.path)^ else {*:st,look:true,preview_data:null}
}
pn command(owner,st,action) map^ {
    if (action == "back" and st.cursor>0) { return navigate(st,st.history[st.cursor-1],st.cursor-1)^ }
    if (action == "forward" and st.cursor<len(st.history)-1) { return navigate(st,st.history[st.cursor+1],st.cursor+1)^ }
    if (action == "parent") { return navigate(st,model.parent(st.path))^ }
    if (action == "refresh") {
        let entries = files.read_folder(st.path)^
        let thumbnails = thumbs.cancel_jobs(owner,st.thumbnails,true)
        thumbs.release(owner)
        return model.reconcile({*:st,entries:entries,thumbnails:thumbnails,error:""})
    }
    if (action == "icons" or action == "list") { return {*:st,mode:action} }
    if (action == "preview") { return {*:st,preview:not st.preview} }
    if (action == "direction") { return {*:st,descending:not st.descending} }
    if (action == "hidden") { return model.reconcile({*:st,hidden:not st.hidden}) }
    if (action == "open") { return open_selected(st)^ }
    if (action == "look") { return if (st.focus == "") st else {*:st,look:not st.look} }
    if (action == "close-look") { return {*:st,look:false} }
    if (action == "go") {
        dom.set_state(node(owner,"folder-path"),"value",st.path)
        return {*:st,dialog:true,error:""}
    }
    if (action == "go-confirm") { return navigate(st,dom.get_state(node(owner,"folder-path"),"value"))^ }
    if (action == "cancel") { return {*:st,dialog:false,error:""} }
    if (action == "dismiss") { return {*:st,error:""} }
    if (action == "all") { return {*:st,selected:[for (e in model.visible(st)) e.path],focus:model.visible(st)[0].path or "",preview_data:null} }
    return st
}
pn dispatch(owner,st,action) {
    // S7.6.7v4: procedure handlers are statements; capture each selected arm explicitly.
    var result = st
    command(owner,st,action) ^ { result = {*:st,error:^.message} } ~ { result = ~ }
    let next = publish(owner,result)
    if (next.dialog and not st.dialog) dom.focus_set(node(owner,"folder-path"),false)
    else if (next.look and not st.look) dom.focus_set(node(owner,"look-close"),false)
    else if (st.dialog and not next.dialog) dom.focus_set(node(owner,if (action == "cancel") "go" else "files"),false)
    else if (st.look and not next.look) dom.focus_set(node(owner,"files"),false)
    return next
}

view <finder_session> state session:INITIAL { ~.rendered }
on finder_activate(evt) {
    if (evt.event_phase != 2) { return 'pass' }
    session = publish(evt.target,session)
    return 'handled'
}
on finder_thumbnails(evt) {
    if (evt.event_phase != 2) { return 'pass' }
    if (session.thumbnails.frame>0 and evt.detail != session.thumbnails.frame) dom.cancel_frame(evt.target,session.thumbnails.frame)
    session = {*:session,thumbnails:thumbs.tick(evt.target,session.thumbnails)}
    return 'handled'
}
on finder_viewport(evt) {
    if (evt.event_phase != 2) { return 'pass' }
    if (session.thumbnails.frame>0) dom.cancel_frame(evt.target,session.thumbnails.frame)
    session = {*:session,thumbnails:thumbs.tick(evt.target,session.thumbnails,true)}
    return 'handled'
}
on resize(evt) {
    let owner = owner_of(evt)
    let thumbnails = thumbs.cancel_jobs(owner,session.thumbnails)
    thumbs.release(owner)
    session = publish(owner,{*:session,thumbnails:thumbnails})
    return 'pass'
}
on closerequest(evt) {
    session = {*:session,thumbnails:thumbs.cancel_jobs(owner_of(evt),session.thumbnails)}
    return 'pass'
}
on click(evt) {
    let owner = owner_of(evt)
    let button = dom.closest(evt.target,"button")
    if (button == null) { return 'pass' }
    if (session.dialog and dom.closest(button,"#go-dialog") == null) { return 'prevent-default' }
    if (session.look and dom.closest(button,"#quick-look") == null) { return 'prevent-default' }
    let path = dom.get_attribute(button,"data-path")
    let location = dom.get_attribute(button,"data-location")
    let action = dom.get_attribute(button,"data-action")
    if (path != null) {
        session = publish(owner,model.choose(session,path,evt.metaKey or evt.ctrlKey,evt.shiftKey))
        dom.focus_set(node(owner,"files"),false)
    } else if (location != null) {
        session = publish(owner,navigate(session,location) ^ { {*:session,error:^.message} })
        dom.focus_set(node(owner,"files"),false)
    } else if (action != null) { session = dispatch(owner,session,action) }
    else { return 'pass' }
    return 'handled'
}
on dblclick(evt) {
    if (session.dialog or session.look or dom.closest(evt.target,"[data-path]") == null) { return 'pass' }
    session = dispatch(owner_of(evt),session,"open")
    return 'prevent-default'
}
on input(evt) {
    if (dom.get_attribute(evt.target,"id") != "search" or session.dialog or session.look) { return 'pass' }
    session = publish(owner_of(evt),model.reconcile({*:session,query:dom.get_state(evt.target,"value")}))
    return 'handled'
}
on change(evt) {
    if (dom.get_attribute(evt.target,"id") != "sort" or session.dialog or session.look) { return 'pass' }
    session = publish(owner_of(evt),{*:session,sort:dom.get_state(evt.target,"value")})
    return 'handled'
}
on keydown(evt) {
    let owner = owner_of(evt)
    let key = evt.key
    let modifier = evt.metaKey or evt.ctrlKey
    if (session.dialog or session.look) {
        if (key == "Escape" or (session.look and key == " ")) {
            session = dispatch(owner,session,if (session.dialog) "cancel" else "close-look")
            return 'prevent-default'
        }
        if (session.dialog and key == "Enter") { session = dispatch(owner,session,"go-confirm"); return 'prevent-default' }
        if (key == "Tab") {
            let controls = dom.query_selector_all(node(owner,if (session.dialog) "go-dialog" else "quick-look"),"button,input")
            let at = model.position([for (c in controls) c],evt.target)
            let next = if (evt.shiftKey) (at+len(controls)-1)%len(controls) else (at+1)%len(controls)
            dom.focus_set(controls[next],false)
            return 'prevent-default'
        }
        return 'pass'
    }
    if (modifier and lower(key)=="f") { dom.focus_set(node(owner,"search"),false); return 'prevent-default' }
    if (modifier and evt.shiftKey and lower(key)=="g") { session = dispatch(owner,session,"go"); return 'prevent-default' }
    if (dom.closest(evt.target,"input,textarea,select") != null) { return 'pass' }
    let action = if (modifier and key=="1") "icons" else if (modifier and key=="2") "list"
        else if (modifier and lower(key)=="a") "all" else if (modifier and lower(key)=="r") "refresh"
        else if (modifier and evt.shiftKey and (key=="." or key==">")) "hidden"
        else if ((evt.altKey and key=="ArrowLeft") or (modifier and key=="[")) "back"
        else if ((evt.altKey and key=="ArrowRight") or (modifier and key=="]")) "forward"
        else if ((modifier or evt.altKey) and key=="ArrowUp") "parent"
        else if (key=="Enter" or (modifier and key=="ArrowDown")) "open"
        else if (key==" ") "look" else ""
    if (action != "") { session = dispatch(owner,session,action); return 'prevent-default' }
    if (contains(["ArrowUp","ArrowDown","ArrowLeft","ArrowRight","Home","End"],key) and dom.closest(evt.target,"#files") != null) {
        let rows = dom.query_selector_all(node(owner,"files"),".file")
        let top = dom.bounding_box(rows[0]).y
        // derive the grid stride from rendered rows, including responsive layouts.
        let columns = if (session.mode == "list") 1 else max(1,len([for (row in rows where abs(dom.bounding_box(row).y-top)<1) row]))
        let delta = if (key=="Home") -len(session.entries) else if (key=="End") len(session.entries)
            else if (key=="ArrowUp") -columns else if (key=="ArrowDown") columns else if (key=="ArrowLeft") -1 else 1
        session = publish(owner,model.move(session,delta,evt.shiftKey))
        let focused = dom.query_selector(node(owner,"files"),"[data-focused='true']")
        if (focused != null) dom.scroll_into_view(focused)
        return 'prevent-default'
    }
    if (key=="Escape") {
        session = publish(owner,{*:session,selected:[],focus:"",anchor:"",preview_data:null,error:""})
        return 'prevent-default'
    }
    return 'pass'
}
view <finder_document> { ~.rendered }
on load(evt) {
    let owner = dom.get_element_by_id(evt.target,"finder")
    if (owner != null) dom.request_frame(owner,"finder_activate")
    return 'handled'
}
<html lang:"en",<head <meta charset:"UTF-8"><meta name:"viewport",content:"width=device-width, initial-scale=1">
    <title "Finder — Lambda"><link rel:"stylesheet",href:url_resolve(BASE,"finder.css")>>
    apply(<finder_document rendered:<body apply(<finder_session rendered:ui.tree(INITIAL,PLACES)>)>>)
>
