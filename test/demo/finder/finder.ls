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
fn modal_id(st) => if (st.operation != null) "operation-dialog" else if (st.dialog) "go-dialog" else if (st.look) "quick-look" else ""
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
fn listing_key(st) => {revision:st.revision,path:st.path,mode:st.mode,query:st.query,sort:st.sort,descending:st.descending,hidden:st.hidden,window:model.window(st)}
pn resize_preview(owner,st,width) {
    let available = dom.bounding_box(node(owner,"content")).width
    let size = model.preview_size(width,available)
    let limits = model.preview_limits(available)
    let requested = string(size) ++ "px"
    let changed = dom.style_get_property(node(owner,"preview-pane"),"width") != requested
    if (changed)
        dom.style_set_property(node(owner,"preview-pane"),"width",requested)
    for (key,value in {valuemin:limits.min,valuemax:limits.max,valuenow:round(size)})
        attr(node(owner,"preview-divider"),"aria-" ++ string(key),value)
    // panel changes also rewrap icon columns; measure the resulting layout on the next frame.
    return {*:st,preview_width:size,measure_frame:if (changed and st.measure_frame==0) dom.request_frame(owner,"finder_measure") else st.measure_frame}
}
pn finish_preview_resize(owner,st,cancel=false) {
    if (st.preview_drag == null) { return st }
    let gesture = st.preview_drag
    var next = {*:st,preview_drag:null}
    if (cancel) { next = resize_preview(owner,next,gesture.width) }
    attr(owner,"data-preview-resizing",false)
    let divider = node(owner,"preview-divider")
    if (dom.has_pointer_capture(divider,gesture.pointer)) dom.release_pointer_capture(divider,gesture.pointer)
    return {*:next,thumbnails:thumbs.schedule(owner,next.thumbnails)}
}
pn publish_columns(owner,st,listing) {
    let previous = st.published
    if (previous == null or previous.mode != "columns") region(owner,"files",format(ui.files(st),'html'))
    else {
        let container = dom.query_selector(node(owner,"files"),".columns")
        // retain ancestor rows so opening a child preserves scroll positions and double-click targets.
        for (i,target in dom.query_selector_all(container,".file-column")) {
            if (i>=len(st.columns) or dom.get_attribute(target,"data-folder") != st.columns[i].path) {
                thumbs.release(target)
                dom.remove_child(container,target)
            }
        }
        for (i,c in st.columns) {
            if (node(owner,"column-" ++ string(i)) == null) {
                let fragment = dom.parse_fragment(container,format(ui.column(st,i),'html'))
                dom.append_child(container,fragment)
            } else if (previous.columns[i] != listing.columns[i]) {
                region(owner,"column-files-" ++ string(i),format(ui.column_files(st,i),'html'))
            }
        }
    }
}
pn publish(owner,st) {
    var next = model.prepare(st)
    if (dom.get_attribute(owner,"data-path") != st.path) { next = {*:next,thumbnails:thumbs.cancel_jobs(owner,next.thumbnails)} }
    if (st.preview_data == null and model.preview_entry(st) != null and (st.preview or st.look)) {
        let data = files.preview(model.preview_entry(st)) ^ { {kind:"error",message:^.message} }
        next = {*:next,preview_data:data}
    }
    for (key,value in {path:next.path,mode:next.mode,selected:len(next.selected),hidden:next.hidden,dialog:next.dialog,look:next.look,
        menu:next.menu != null,operation:next.operation.action or "",dragging:next.drag != null,readonly:model.read_only(next)}) attr(owner,"data-" ++ string(key),value)
    text(owner,"folder-title",if (next.path == START) "Demo Files" else model.basename(next.path))
    text(owner,"status",model.status(next))
    region(owner,"breadcrumbs",format(ui.breadcrumb(next),'html'))
    let listing = {*:listing_key(next),columns:if (next.mode == "columns") [for (i,c in next.columns) listing_key(model.column(next,i))] else null}
    // selection and dialogs reuse the published listing instead of rebuilding every SVG and row.
    let replaced = next.published == null or next.published != listing
    if (next.mode == "columns") publish_columns(owner,next,listing)
    else if (replaced) region(owner,"files",format(ui.files(next),'html'))
    if (next.mode == "columns" or not replaced) for (row in dom.query_selector_all(node(owner,"files"),"[data-path]")) {
        let path = dom.get_attribute(row,"data-path")
        let index = if (next.mode == "columns") int(dom.get_attribute(row,"data-column")) else -1
        let column = if (index>=0) model.column(next,index) else next
        attr(row,"aria-selected",contains(column.selected,path))
        attr(row,"data-focused",path == column.focus)
        if (index>=0) attr(row,"data-trail",index<len(next.columns)-1 and contains(column.selected,path))
    }
    if (next.mode == "columns" and (next.published == null or next.published.mode != "columns" or next.published.path != next.path))
        dom.scroll_into_view_options(node(owner,"column-" ++ string(len(next.columns)-1)),{block:"nearest",inline:"end"})
    let focused = dom.query_selector(node(owner,"files"),"[data-focused='true']")
    if (next.reveal and focused != null) dom.scroll_into_view_options(focused,{block:"nearest",inline:"nearest"})
    if (focused != null) attr(node(owner,"files"),"aria-activedescendant",dom.get_attribute(focused,"id"))
    else if (dom.get_attribute(node(owner,"files"),"aria-activedescendant") != null) dom.remove_attribute(node(owner,"files"),"aria-activedescendant")
    for (place in dom.query_selector_all(owner,".place")) attr(place,"aria-current",if (dom.get_attribute(place,"data-location") == next.path) "page" else "false")
    for (id,pressed in {icons:next.mode == "icons",list:next.mode == "list",columns:next.mode == "columns",preview:next.preview,hidden:next.hidden}) attr(node(owner,string(id)),"aria-pressed",pressed)
    disabled(owner,"back",next.cursor == 0)
    disabled(owner,"forward",next.cursor >= len(next.history)-1)
    disabled(owner,"direction",len(next.entries)==0)
    disabled(owner,"new-folder",not model.writable(next,"new-folder"))
    text(owner,"direction",if (next.descending) "↓" else "↑")
    text(owner,"hidden",if (next.hidden) "Hide hidden files" else "Show hidden files")
    show(owner,"list-heading",next.mode == "list","grid")
    show(owner,"preview-pane",next.preview)
    show(owner,"preview-divider",next.preview)
    if (next.preview_width != null) { next = resize_preview(owner,next,next.preview_width) }
    show(owner,"error",next.error != "" and not next.dialog and next.operation == null,"flex")
    text(owner,"error-text",next.error)
    text(owner,"go-error",next.error)
    show(owner,"go-dialog",next.dialog,"flex")
    show(owner,"quick-look",next.look,"flex")
    show(owner,"context-menu",next.menu != null)
    attr(node(owner,"actions"),"aria-expanded",next.menu != null)
    if (next.menu != null) {
        for (action in ["open","look"]) disabled(owner,"menu-" ++ action,len(next.selected)==0)
        for (action in ["rename","move","delete","new-file","new-folder"]) {
            let selected = if (action == "rename") len(next.selected)==1 else if (contains(["move","delete"],action)) len(next.selected)>0 else true
            disabled(owner,"menu-" ++ action,not selected or not model.writable(next,action))
        }
        dom.style_set_property(node(owner,"context-menu"),"left",string(next.menu.x) ++ "px")
        dom.style_set_property(node(owner,"context-menu"),"top",string(next.menu.y) ++ "px")
    }
    show(owner,"operation-dialog",next.operation != null,"flex")
    show(owner,"move-picker",next.operation.action == "move")
    if (next.operation != null) {
        let action = next.operation.action
        let titles = {'new-file':"New File",'new-folder':"New Folder",rename:"Rename",move:"Move to Folder",delete:"Delete Permanently"}
        text(owner,"operation-title",titles[action])
        text(owner,"operation-description",if (action == "delete") "Permanently delete " ++ string(len(next.operation.paths)) ++
            " selected item(s), including any folder contents? This cannot be undone."
            else if (action == "rename") "Rename “" ++ model.basename(next.operation.paths[0]) ++ "”."
            else if (action == "move") "Choose a destination folder for the selected items."
            else "Create an empty " ++ (if (action == "new-folder") "folder" else "file") ++ " in “" ++ model.basename(next.operation.folder) ++ "”.")
        text(owner,"operation-label",if (action == "move") "Destination folder" else "Name")
        text(owner,"operation-error",next.error)
        text(owner,"operation-confirm",if (action == "delete") "Delete Permanently" else if (action == "rename") "Rename" else if (action == "move") "Move" else "Create")
        attr(node(owner,"operation-confirm"),"class",if (action == "delete") "danger" else "primary")
        show(owner,"operation-field",action != "delete")
        if (action == "move") {
            let picker = next.operation.picker
            region(owner,"destination-breadcrumbs",if (picker == null) "" else format(ui.breadcrumb(picker,true),'html'))
            region(owner,"destination-folders",format(ui.destination_folders(picker),'html'))
            disabled(owner,"destination-parent",picker == null or picker.path == "/")
        }
    }
    // retaining the same frame keeps its browsing context and scroll position during panel resizing.
    region(owner,"preview-body",if (next.preview) format(ui.preview(next),'html') else "")
    if (next.look) {
        text(owner,"look-title",model.focused(next).name)
        region(owner,"look-body",format(ui.preview(next,true),'html'))
    } else region(owner,"look-body","")
    if (dom.get_state(node(owner,"search"),"value") != next.query) dom.set_state(node(owner,"search"),"value",next.query)
    return {*:next,published:listing,reveal:false,thumbnails:thumbs.schedule(owner,next.thumbnails),
        measure_frame:if (replaced and next.measure_frame==0) dom.request_frame(owner,"finder_measure") else next.measure_frame}
}
fn navigate(st,path,cursor=-1) map^ {
    if (not (path is string) or trim(path)=="") raise error("Enter a folder path.")
    else {
        let target = model.absolute(path,st.path,HOME)
        files.navigate(st,target,cursor)^
    }
}
fn open_selected(st) map^ {
    let entry = model.focused(st);
    if (entry == null) st else if (model.browsable(entry)) navigate(st,entry.path)^ else {*:st,look:true,preview_data:null}
}
pn begin_operation(owner,st,action) map^ {
    if (action == "rename" and len(st.selected)!=1) raise error("Select one item to rename.")
    if (contains(["move","delete"],action) and len(st.selected)==0) raise error("Select an item first.")
    if (not model.writable(st,action)) raise error("Archive contents are read-only.")
    let value = if (action == "rename") model.basename(st.selected[0]) else if (action == "move") model.parent(st.path)
        else if (action == "new-folder") "New Folder" else "Untitled.txt"
    dom.set_state(node(owner,"operation-value"),"value",value)
    var next = {*:st,operation:{action:action,folder:st.path,paths:st.selected,picker:null},menu:null,error:""}
    if (action == "move") {
        browse_destination(owner,next,value) ^ { next = {*:next,error:^.message} } ~ { next = ~ }
    }
    return next
}
pn browse_destination(owner,st,value) map^ {
    let path = model.absolute(value,st.operation.folder,HOME)
    let picker = files.read_destination(path,st.operation.paths)^
    // read first so an inaccessible folder keeps the previous browser and typed path available.
    dom.set_state(node(owner,"operation-value"),"value",path)
    return {*:st,operation:{*:st.operation,picker:picker},error:""}
}
pn perform_files(owner,st,request) map^ {
    let result = files.perform(request)^
    let thumbnails = thumbs.cancel_jobs(owner,st.thumbnails,true)
    thumbs.release(owner)
    let relocated = model.relocated(st,result.changes)
    let selecting = if (len(result.created)>0) result.created[0] else if (request.action == "rename" and len(result.changes)>0) result.changes[0].to else ""
    var next = files.reload({*:relocated,thumbnails:thumbnails,operation:null,menu:null,drag:null,look:false,
        query:if (selecting != "") "" else relocated.query,hidden:relocated.hidden or (selecting != "" and starts_with(model.basename(selecting),"."))})
    if (selecting != "") { next = model.choose(next,selecting) }
    return {*:next,error:result.error or next.error}
}
fn menu_position(owner,x,y) {
    let box = dom.bounding_box(owner);
    {x:max(6,min(x,box.width-215)),y:max(6,min(y,box.height-290))}
}
pn open_menu(owner,st,target,x,y) {
    let column = dom.closest(target,".file-column")
    let active = if (column != null) model.activate_column(st,int(dom.get_attribute(column,"data-column"))) else st
    let row = dom.closest(target,".file")
    let path = dom.get_attribute(row,"data-path")
    let selected = if (path != null) (if (contains(active.selected,path)) active else model.choose(active,path))
        else {*:active,selected:[],focus:"",anchor:"",preview_data:null}
    let next = publish(owner,{*:selected,menu:menu_position(owner,x,y),error:""})
    dom.focus_set(dom.query_selector(node(owner,"context-menu"),"button:not([disabled])"),false)
    return next
}
pn command(owner,st,action,destination=null) map^ {
    if (contains(["new-file","new-folder","rename","move","delete"],action)) { return begin_operation(owner,st,action)^ }
    if (action == "operation-cancel") { return {*:st,operation:null,error:""} }
    if (starts_with(action,"destination-") and st.operation.action == "move") {
        let value = if (action == "destination-folder") destination else if (action == "destination-parent") model.parent(st.operation.picker.path)
            else if (action == "destination-home") HOME else if (action == "destination-current") st.operation.folder
            else dom.get_state(node(owner,"operation-value"),"value")
        return browse_destination(owner,st,value)^
    }
    if (action == "operation-confirm" and st.operation != null) {
        let operation = st.operation
        let value = dom.get_state(node(owner,"operation-value"),"value")
        return perform_files(owner,st,{*:operation,value:if (operation.action == "move") model.absolute(value,operation.folder,HOME) else value})^
    }
    if (action == "actions") {
        let box = dom.bounding_box(node(owner,"actions"))
        return {*:st,menu:if (st.menu != null) null else menu_position(owner,box.x,box.y+box.height),error:""}
    }
    if (action == "menu-cancel") { return {*:st,menu:null} }
    if (action == "back" and st.cursor>0) { return navigate(st,st.history[st.cursor-1],st.cursor-1)^ }
    if (action == "forward" and st.cursor<len(st.history)-1) { return navigate(st,st.history[st.cursor+1],st.cursor+1)^ }
    if (action == "parent") {
        return if (st.mode == "columns" and len(st.columns)>1) model.previous_column(st) else navigate(st,model.parent(st.path))^
    }
    if (action == "refresh") {
        let thumbnails = thumbs.cancel_jobs(owner,st.thumbnails,true)
        thumbs.release(owner)
        return files.reload({*:st,thumbnails:thumbnails,error:"",menu:null})
    }
    if (action == "icons" or action == "list" or action == "columns") { return {*:st,mode:action,reveal:true} }
    if (action == "column-left") { return model.previous_column(st) }
    if (action == "column-right") {
        let entry = model.focused(st)
        return if (model.browsable(entry)) navigate(st,entry.path)^ else st
    }
    if (action == "preview") { return {*:st,preview:not st.preview,
        measure_frame:if (st.measure_frame==0) dom.request_frame(owner,"finder_measure") else st.measure_frame} }
    if (action == "direction") { return {*:st,descending:not st.descending,reveal:true} }
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
pn dispatch(owner,st,action,destination=null) {
    // S7.6.7v4: procedure handlers are statements; capture each selected arm explicitly.
    let current = if (action == "actions") st else {*:st,menu:null}
    var result = current
    command(owner,current,action,destination) ^ { result = {*:current,error:^.message} } ~ { result = ~ }
    let next = publish(owner,result)
    if (next.operation != null and st.operation == null) dom.focus_set(node(owner,if (next.operation.action == "delete") "operation-cancel" else "operation-value"),false)
    else if (st.operation != null and next.operation == null) dom.focus_set(node(owner,"files"),false)
    else if (next.menu != null and st.menu == null) dom.focus_set(dom.query_selector(node(owner,"context-menu"),"button:not([disabled])"),false)
    else if (action == "menu-cancel") dom.focus_set(node(owner,"files"),false)
    else if (next.dialog and not st.dialog) dom.focus_set(node(owner,"folder-path"),false)
    else if (next.look and not st.look) dom.focus_set(node(owner,"look-close"),false)
    else if (st.dialog and not next.dialog) dom.focus_set(node(owner,if (action == "cancel") "go" else "files"),false)
    else if (st.look and not next.look) dom.focus_set(node(owner,"files"),false)
    else if (contains(["icons","list","columns"],action)) dom.focus_set(node(owner,"files"),false)
    return next
}

view <finder_session> state session:INITIAL { ~.rendered }
on finder_activate(evt) {
    if (evt.event_phase != 2) { return 'pass' }
    session = publish(evt.target,{*:session,viewports:evt.detail.viewports})
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
    if (session.measure_frame>0) dom.cancel_frame(evt.target,session.measure_frame)
    let geometry_changed = any([for (v in evt.detail.viewports) (
        let old = [for (p in session.viewports or [] where p.path == v.path and p.mode == v.mode) p][0],
        old == null or old.columns != v.columns or old.row_height != v.row_height or old.height != v.height)])
    let measured = {*:session,viewports:evt.detail.viewports,measure_frame:0,reveal:geometry_changed and session.focus != ""}
    let before = [for (i,c in session.columns) model.window(model.column(session,i))]
    let after = [for (i,c in measured.columns) model.window(model.column(measured,i))]
    session = if (before != after) publish(evt.target,measured) else measured
    if (session.preview_width != null) { session = resize_preview(evt.target,session,session.preview_width) }
    if (session.thumbnails.frame>0) dom.cancel_frame(evt.target,session.thumbnails.frame)
    session = {*:session,thumbnails:thumbs.tick(evt.target,session.thumbnails,true)}
    return 'handled'
}
on finder_resize(evt) {
    if (evt.event_phase != 2) { return 'pass' }
    let owner = owner_of(evt)
    session = finish_preview_resize(owner,session)
    let thumbnails = thumbs.cancel_jobs(owner,session.thumbnails)
    thumbs.release(owner)
    session = publish(owner,{*:session,thumbnails:thumbnails,menu:null})
    // responsive sidebar sizing settles after the resize event; clamp against that frame's geometry.
    dom.request_frame(owner,"finder_measure")
    return 'pass'
}
on closerequest(evt) {
    session = {*:session,thumbnails:thumbs.cancel_jobs(owner_of(evt),session.thumbnails)}
    return 'pass'
}
on finder_drag_start(evt) {
    if (evt.event_phase != 2 or modal_id(session) != "") { return 'pass' }
    let index = evt.detail.column
    let active = if (session.mode == "columns" and index>=0) model.column(session,index) else session
    let path = evt.detail.path
    if (not contains([for (e in active.entries) e.path],path)) { return 'pass' }
    if (model.read_only(active)) { return 'pass' }
    // keep the native source row alive throughout the drag; selection context is captured without navigating.
    session = {*:session,menu:null,drag:{folder:active.path,paths:if (contains(active.selected,path)) active.selected else [path]}}
    attr(evt.target,"data-dragging",true)
    attr(evt.target,"data-menu",false)
    attr(node(evt.target,"actions"),"aria-expanded",false)
    show(evt.target,"context-menu",false)
    return 'handled'
}
on finder_drop(evt) {
    if (evt.event_phase != 2 or session.drag == null or modal_id(session) != "") { return 'pass' }
    let owner = evt.target
    let request = {action:"move",folder:session.drag.folder,paths:session.drag.paths,value:model.absolute(evt.detail.folder,session.path,HOME)}
    var next = session
    perform_files(owner,session,request) ^ { next = {*:session,drag:null,error:^.message} } ~ { next = ~ }
    session = publish(owner,next)
    dom.focus_set(node(owner,"files"),false)
    return 'handled'
}
on finder_drag_end(evt) {
    if (evt.event_phase != 2) { return 'pass' }
    session = {*:session,drag:null}
    attr(evt.target,"data-dragging",false)
    return 'handled'
}
on contextmenu(evt) {
    if (modal_id(session) != "" or dom.closest(evt.target,"#files") == null) { return 'pass' }
    session = open_menu(owner_of(evt),session,evt.target,evt.clientX,evt.clientY)
    return 'prevent-default'
}
on click(evt) {
    if (evt.button != null and evt.button != 0) { return 'pass' }
    let owner = owner_of(evt)
    let button = dom.closest(evt.target,"button")
    if (button == null) {
        if (session.menu != null) { session = dispatch(owner,session,"menu-cancel"); return 'handled' }
        return 'pass'
    }
    if (session.operation != null and dom.closest(button,"#operation-dialog") == null) { return 'prevent-default' }
    if (session.dialog and dom.closest(button,"#go-dialog") == null) { return 'prevent-default' }
    if (session.look and dom.closest(button,"#quick-look") == null) { return 'prevent-default' }
    let path = dom.get_attribute(button,"data-path")
    let location = dom.get_attribute(button,"data-location")
    let action = dom.get_attribute(button,"data-action")
    let destination = dom.get_attribute(button,"data-destination")
    if (session.operation.action == "move" and destination != null) {
        session = dispatch(owner,session,"destination-folder",destination)
        dom.focus_set(node(owner,"destination-folders"),false)
        return 'handled'
    }
    if (session.menu != null and dom.closest(button,"#context-menu") == null and action != "actions") { session = {*:session,menu:null} }
    if (path != null) {
        if (session.mode == "columns" and path == session.path and not (evt.metaKey or evt.ctrlKey or evt.shiftKey)) {
            dom.focus_set(node(owner,"files"),false)
            return 'handled'
        }
        let index = dom.get_attribute(button,"data-column")
        let active = if (session.mode == "columns" and index != null) model.activate_column(session,int(index)) else session
        var next = model.choose(active,path,evt.metaKey or evt.ctrlKey,evt.shiftKey)
        let entry = model.focused(next)
        if (session.mode == "columns" and model.browsable(entry) and not (evt.metaKey or evt.ctrlKey or evt.shiftKey)) {
            // activating an ancestor is selection context; record only the final folder in navigation history.
            next = {*:next,history:session.history,cursor:session.cursor}
            next = navigate(next,path) ^ { {*:next,error:^.message} }
        }
        session = publish(owner,next)
        dom.focus_set(node(owner,"files"),false)
    } else if (location != null) {
        session = publish(owner,navigate(session,location) ^ { {*:session,error:^.message} })
        dom.focus_set(node(owner,"files"),false)
    } else if (action != null) { session = dispatch(owner,session,action) }
    else { return 'pass' }
    return 'handled'
}
on pointerdown(evt) {
    let divider = dom.closest(evt.target,"#preview-divider")
    if (divider == null or evt.button != 0 or modal_id(session) != "") { return 'pass' }
    let owner = owner_of(evt)
    session = {*:session,preview_drag:{x:evt.clientX,width:dom.bounding_box(node(owner,"preview-pane")).width,pointer:evt.pointerId}}
    attr(owner,"data-preview-resizing",true)
    dom.focus_set(divider,false)
    dom.set_pointer_capture(divider,evt.pointerId)
    return 'prevent-default'
}
on pointermove(evt) {
    if (session.preview_drag == null) { return 'pass' }
    let owner = owner_of(evt)
    let gesture = session.preview_drag
    session = resize_preview(owner,session,gesture.width+gesture.x-evt.clientX)
    session = {*:session,thumbnails:thumbs.schedule(owner,session.thumbnails)}
    return 'prevent-default'
}
on pointerup(evt) {
    if (session.preview_drag == null) { return 'pass' }
    session = finish_preview_resize(owner_of(evt),session)
    return 'prevent-default'
}
on pointercancel(evt) {
    session = finish_preview_resize(owner_of(evt),session,true)
    return 'pass'
}
on lostpointercapture(evt) {
    session = finish_preview_resize(owner_of(evt),session)
    return 'pass'
}
on dblclick(evt) {
    if (modal_id(session) != "" or dom.closest(evt.target,"[data-path]") == null) { return 'pass' }
    // a folder's first click already opens its child column; the second must not open a different row.
    if (session.mode == "columns" and dom.get_attribute(dom.closest(evt.target,"[data-path]"),"data-path") != session.focus) { return 'prevent-default' }
    session = dispatch(owner_of(evt),session,"open")
    return 'prevent-default'
}
on input(evt) {
    if (dom.get_attribute(evt.target,"id") != "search" or modal_id(session) != "") { return 'pass' }
    session = publish(owner_of(evt),model.reconcile({*:session,query:dom.get_state(evt.target,"value"),reveal:true}))
    return 'handled'
}
on change(evt) {
    if (dom.get_attribute(evt.target,"id") != "sort" or modal_id(session) != "") { return 'pass' }
    session = publish(owner_of(evt),{*:session,sort:dom.get_state(evt.target,"value"),reveal:true})
    return 'handled'
}
on keydown(evt) {
    let owner = owner_of(evt)
    let key = evt.key
    let modifier = evt.metaKey or evt.ctrlKey
    let modal = modal_id(session)
    if (session.preview_drag != null and key == "Escape") {
        session = finish_preview_resize(owner,session,true)
        return 'prevent-default'
    }
    if (modal == "" and dom.closest(evt.target,"#preview-divider") != null and contains(["ArrowLeft","ArrowRight","Home","End"],key)) {
        let limits = model.preview_limits(dom.bounding_box(node(owner,"content")).width)
        let width = dom.bounding_box(node(owner,"preview-pane")).width
        session = resize_preview(owner,session,if (key == "Home") limits.min else if (key == "End") limits.max
            else width+(if (key == "ArrowLeft") 20 else -20))
        session = {*:session,thumbnails:thumbs.schedule(owner,session.thumbnails)}
        return 'prevent-default'
    }
    if (modal != "") {
        if (key == "Escape" or (session.look and key == " ")) {
            session = dispatch(owner,session,if (session.operation != null) "operation-cancel" else if (session.dialog) "cancel" else "close-look")
            return 'prevent-default'
        }
        if ((session.dialog or session.operation != null) and key == "Enter" and
            not contains(["cancel","operation-cancel"],dom.get_attribute(evt.target,"data-action"))) {
            // folder-browser buttons activate navigation through their ordinary click, never a move.
            if (session.operation.action == "move" and dom.closest(evt.target,"#move-picker") != null) { return 'pass' }
            session = dispatch(owner,session,if (session.operation != null) "operation-confirm" else "go-confirm")
            return 'prevent-default'
        }
        if (key == "Tab") {
            // a delete confirmation has no text control, even if its previously laid-out input still has cached geometry.
            let selector = if (session.operation.action == "delete") "#operation-cancel,#operation-confirm"
                else if (session.operation.action == "move") "button:not([disabled]),input"
                else if (session.operation != null) "#operation-value,#operation-cancel,#operation-confirm" else "button,input"
            let controls = dom.query_selector_all(node(owner,modal),selector)
            let at = model.position([for (c in controls) c],evt.target)
            let next = if (evt.shiftKey) (at+len(controls)-1)%len(controls) else (at+1)%len(controls)
            dom.focus_set(controls[next],false)
            return 'prevent-default'
        }
        return 'pass'
    }
    if (session.menu != null) {
        if (key == "Escape" or key == "Tab") { session = dispatch(owner,session,"menu-cancel"); return 'prevent-default' }
        if (contains(["ArrowUp","ArrowDown","Home","End"],key)) {
            let controls = dom.query_selector_all(node(owner,"context-menu"),"button:not([disabled])")
            let at = model.position([for (c in controls) c],evt.target)
            let next = if (key == "Home") 0 else if (key == "End") len(controls)-1
                else (at+len(controls)+(if (key == "ArrowUp") -1 else 1))%len(controls)
            dom.focus_set(controls[next],false)
            return 'prevent-default'
        }
    }
    if (modifier and lower(key)=="f") { dom.focus_set(node(owner,"search"),false); return 'prevent-default' }
    if (modifier and evt.shiftKey and lower(key)=="g") { session = dispatch(owner,session,"go"); return 'prevent-default' }
    if (dom.closest(evt.target,"input,textarea,select") != null) { return 'pass' }
    if (modifier and evt.shiftKey and lower(key) == "m") {
        let target = dom.query_selector(node(owner,"files"),"[data-focused='true']") or node(owner,"files")
        let box = dom.bounding_box(target)
        session = open_menu(owner,session,target,box.x,box.y+box.height)
        return 'prevent-default'
    }
    let action = if (modifier and key=="1") "icons" else if (modifier and key=="2") "list" else if (modifier and key=="3") "columns"
        else if (modifier and lower(key) == "e") "rename" else if (key == "Delete" or (modifier and key == "Backspace")) "delete"
        else if (modifier and evt.shiftKey and lower(key) == "v") "move"
        else if (modifier and evt.shiftKey and lower(key)=="n") "new-folder" else if (modifier and evt.altKey and lower(key)=="n") "new-file"
        else if (modifier and lower(key)=="a") "all" else if (modifier and lower(key)=="r") "refresh"
        else if (modifier and evt.shiftKey and (key=="." or key==">")) "hidden"
        else if ((evt.altKey and key=="ArrowLeft") or (modifier and key=="[")) "back"
        else if ((evt.altKey and key=="ArrowRight") or (modifier and key=="]")) "forward"
        else if ((modifier or evt.altKey) and key=="ArrowUp") "parent"
        else if (key=="Enter" or (modifier and key=="ArrowDown")) "open"
        else if (session.mode == "columns" and contains(["ArrowLeft","ArrowRight"],key) and not (modifier or evt.altKey or evt.shiftKey) and dom.closest(evt.target,"#files") != null)
            (if (key=="ArrowLeft") "column-left" else if (key=="ArrowRight") "column-right" else "")
        else if (key==" ") "look" else ""
    if (action != "") { session = dispatch(owner,session,action); return 'prevent-default' }
    if (contains(["ArrowUp","ArrowDown","ArrowLeft","ArrowRight","Home","End"],key) and dom.closest(evt.target,"#files") != null) {
        let rows = dom.query_selector_all(if (session.mode == "columns") node(owner,"column-files-" ++ string(len(session.columns)-1)) else node(owner,"files"),".file")
        let top = dom.bounding_box(rows[0]).y
        // derive the grid stride from rendered rows, including responsive layouts.
        let columns = if (session.mode != "icons") 1 else max(1,len([for (row in rows where abs(dom.bounding_box(row).y-top)<1) row]))
        let delta = if (key=="ArrowUp") -columns else if (key=="ArrowDown") columns else if (key=="ArrowLeft") -1 else 1
        let matching = model.visible(session)
        // Home/End name an endpoint even when no row currently has focus.
        let moving = if (len(matching)==0) session else if (key=="Home" or key=="End")
            model.choose(session,matching[if (key=="Home") 0 else len(matching)-1].path,false,evt.shiftKey)
            else model.move(session,delta,evt.shiftKey)
        session = publish(owner,moving)
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
<html lang:"en",<head <meta charset:"UTF-8"><meta name:"viewport",content:"width=device-width, initial-scale=1">
    <title "Finder — Lambda"><link rel:"stylesheet",href:url_resolve(BASE,"finder.css")>>
    <body apply(<finder_session rendered:ui.tree(INITIAL,PLACES)>);
        <script src:url_resolve(BASE,"finder.js")>>
>
