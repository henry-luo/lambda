// S12.1.3 / D4.5.2: one retained shell owns native controls and copied canvas pixels.
import dom
import resources: ~~.mod_resources
import img: lambda.image
import editor: lambda.editor.raster
import model: lambda.editor.raster.model
import interaction: lambda.editor.raster.interaction
import surface: lambda.editor.raster.view
import files: .mod_files
import scene: .mod_scene
import present: .mod_present
import tools: lambda.editor.raster.tools
import retouch: lambda.editor.raster.retouch
import effects: lambda.editor.raster.effects
import colors: lambda.slide.color
import styles: lambda.editor.raster.styles
import channels: lambda.editor.raster.channels
import workspaces: lambda.editor.raster.workspace
import batch: lambda.editor.raster.batch
import exports: lambda.editor.raster.export
import crop: lambda.editor.raster.crop
import tone: lambda.editor.raster.tone
import guides: lambda.editor.raster.guides

let BASE = url_resolve(resources.entry_uri()^,".")
let SOURCE = sys.proc.self.env.PHOTOS_SOURCE#
let INITIAL = editor.open_session(editor.new_document()^ )^
fn owner_of(evt) => dom.get_element_by_id(dom.root_node(evt.target),"photos")
fn field(owner,id) => dom.get_state(present.node(owner,id),"value")
// clearing a recovered error must also retire the same text from the status bar.
fn clear_error(st) => {*:st,error:"",status:if (st.error != "" and st.status == st.error) "Ready" else st.status}
fn base_session(st,name="Untitled") => {*:st,name:name,path:"",dialog:"",pending:"",error:"",status:"Ready · Place images to build a composition",
    effect_index:0,style_index:0,effect_draft:{},curve_draft:{},curve_channel:"rgb",curve_point:1,curve_drag:null,
    painted_doc:null,token:0,scale:1.0,size_draft:{width:st.doc.width,height:st.doc.height}}
pn publish(owner,st,workspace=null) {
    var next = st
    present.publish(owner,st) ^ { dom.set_attribute(owner,"data-error",^.message) } ~ { next = ~ }
    if (workspace != null) {
        let previous_tab = dom.get_attribute(owner,"data-tab")
        let rows = workspaces.rows(workspace,next)^
        let markup = join([for (tab in rows) format(<button type:"button",role:"tab",'data-tab':tab.id,
            'aria-selected':string(tab.active),class:if (tab.active) "active" else "",tab.name ++ (if (tab.dirty) " ●" else "")>,'xml')],"")
        let target = present.node(owner,"tabs")
        if (dom.get_attribute(target,"data-items") != markup) {
            dom.set_inner_html(target,markup)
            dom.set_attribute(target,"data-items",markup)
        }
        dom.set_attribute(owner,"data-tabs",string(len(rows)))
        dom.set_attribute(owner,"data-tab",model.value(workspace.active,""))
        dom.set_close_guard(owner,workspaces.any_dirty(workspace,interaction.cancel(next))^)
        if (next.dialog == "discard" and previous_tab != workspace.active) dom.focus_set(present.node(owner,"discard-cancel"),false)
    }
    return next
}
pn show_file(owner,st,kind) {
    let title = if (kind in ["open","open-tab"]) "Open image or Photos project" else if (kind == "place") "Place image as layer"
        else if (kind == "action-save") "Save action" else if (kind == "action-open") "Load action"
        else if (kind == "save") "Save Photos project" else if (kind == "psd") "Export layered PSD" else "Export image"
    dom.set_text_content(present.node(owner,"file-title"),title)
    dom.set_text_content(present.node(owner,"file-help"),if (kind in ["action-save","action-open"]) "Actions replay recorded edits on the currently selected layer."
        else if (kind == "save") "Use .photos to preserve all editable settings, or .psd for the supported PSD profile."
        else if (kind == "export") "Exports a resized copy. JPEG requires an opaque background. Your project stays editable."
        else if (kind == "psd") "Use .photos to keep transforms, blend modes and editable effects. Flatten the image for the supported PSD raster profile."
        else "Enter a local image, PSD or .photos project path.")
    dom.set_state(present.node(owner,"file-path"),"value",if (kind == "action-save") "temp/photos/action.laction" else if (kind == "save")
        (if (ends_with(lower(st.path),".photos")) st.path else "temp/photos/composition.photos")
        else if (kind == "psd") "temp/photos/composition.psd"
        else if (kind == "export") "temp/photos/composition.png" else "")
    if (kind == "export") {
        for (id,value in {'export-format':"png",'export-aspect':"locked",'export-width':st.doc.width,
            'export-height':st.doc.height,'export-quality':90,'export-alpha':"preserve",'export-background':"#ffffff"})
            present.set_value(present.node(owner,string(id)),string(value))
    }
    let next = publish(owner,{*:st,dialog:kind,error:""})
    dom.focus_set(present.node(owner,"file-path"),false)
    return next
}
pn replace_document(st,action,path="") any^ {
    let next = if (action == "new") editor.open_session(editor.new_document()^)^ else files.open_document(path)^
    return {*:base_session(next,if (action == "new") "Untitled" else split(path,"/")[last]),
        actions:st.actions,path:if (ends_with(lower(path),".psd") or ends_with(lower(path),".photos")) path else ""}
}
pn embed_font(owner,st,group=null) any^ {
    let bytes = input(field(owner,"art-font-path"),'binary')^;
    return editor.exec(st,{id:"font.embed",font:field(owner,"art-font"),bytes:bytes,
        family:split(field(owner,"art-font-path"),"/")[last],group:group})^
}
pn command(owner,st,action) any^ {
    if (st.recording != null and action in ["new","open","new-tab","open-tab","tab-close","batch-run","place","save","export","psd","action-save","action-open"])
        raise error("Photos: stop action recording before file operations")
    if (action == "crop-cancel") { return {*:st,crop:null} }
    if (action == "crop-apply") { return crop.apply_draft(st)^ }
    if (action == "guide-add") {
        let position = float(field(owner,"guide-position"))
        if (not model.finite(position) or position != floor(position)) raise error("Photos: guide position must be a whole pixel")
        return editor.exec(st,{id:"guide.add",axis:field(owner,"guide-axis"),position:int(position)})^
    }
    if (action == "guide-show") { return editor.exec(st,{id:"guide.configure",visible:not st.guides_visible})^ }
    if (action == "guide-snap") { return editor.exec(st,{id:"guide.configure",snap:not st.snap_enabled})^ }
    if (action == "straighten-tool") { return editor.exec(st,{id:"tool.configure",tool:"straighten"})^ }
    if (action in ["curve-reset","curve-remove"]) {
        let curves = if (action == "curve-reset") tone.with_points(st.curve_draft,st.curve_channel,tone.identity)^
            else tone.remove(st.curve_draft,st.curve_channel,st.curve_point)^;
        if (action == "curve-reset") present.set_value(present.node(owner,"curve-points"),format(tone.identity,'mark'))
        return {*:st,curve_draft:curves,curve_point:1}
    }
    if (action in ["levels-add","levels-apply","curves-add","curves-apply"]) {
        let adding = ends_with(action,"add")
        let current = effects.items(model.active(st)^.node)[st.effect_index]
        if (not adding and current == null) raise error("Photos: select an effect to update")
        let initial = if (adding) ({}) else current.options
        let options = if (starts_with(action,"levels-")) reduce([initial,*[for (control in tone.levels)
            {[control.key]:float(field(owner,"levels-" ++ control.key))/control.scale}]],(result,entry) => {*:result,*:entry})
            else {*:initial,curves:read_curves(owner,st)^}
        let changed = editor.exec(st,{id:if (adding) "effect.add" else "effect.update",options:options,
            *:if (adding) ({}) else ({index:st.effect_index}),
            name:if (adding) (if (starts_with(action,"levels-")) "Levels" else "Curves") else current.name})^;
        return {*:changed,effect_index:if (adding) len(effects.items(model.active(changed)^.node))-1 else st.effect_index}
    }
    if (starts_with(action,"align-") or starts_with(action,"distribute-")) {
        return editor.exec(st,{id:if (starts_with(action,"align-")) "layer.align" else "layer.distribute",
            *:if (starts_with(action,"align-")) ({mode:slice(action,6,len(action))}) else ({axis:slice(action,11,len(action))}),
            relative:field(owner,"align-relative")})^
    }
    if (action == "action-start") { return editor.exec(st,{id:"action.start",name:field(owner,"action-name")})^ }
    if (action == "batch-run") {
        let jobs = parse(field(owner,"batch-jobs"),'mark')^
        let report = batch.run(st.actions[last],jobs,{continue_on_error:field(owner,"batch-policy") != "stop"})^
        let message = string(report.succeeded) ++ " succeeded · " ++ string(report.failed) ++ " failed" ++
            join([for (result in report.results where not result.ok) "\n" ++ result.source ++ ": " ++ result.error],"")
        dom.set_text_content(present.node(owner,"batch-status"),message)
        return {*:st,status:message,batch_report:report}
    }
    if (action == "new-tab") { return {*:replace_document(st,"new")^,tab_request:"add"} }
    if (action == "open-tab") { return show_file(owner,st,"open-tab") }
    if (action == "tab-close") {
        if (editor.dirty(st)) { return {*:st,dialog:"discard",pending:"tab-close"} }
        return {*:st,tab_request:"close-tab"}
    }
    if (starts_with(action,"channel-")) {
        let selected = field(owner,"saved-channel")
        let component = field(owner,"channel-component")
        let label = field(owner,"channel-name");
        return editor.exec(st,if (action == "channel-preview") {id:"channel.view",channel:selected}
            else if (action == "channel-save") {id:"channel.save",name:label}
            else if (action == "channel-capture") {id:"channel.capture",channel:component,name:label}
            else if (action == "channel-replace") {id:"channel.replace",channel:selected,component:component}
            else if (action == "channel-rename") {id:"channel.rename",channel:selected,name:label}
            else {id:replace(action,"-","."),channel:selected})^
    }
    if (action == "font-embed") { return embed_font(owner,st)^ }
    if (action in ["text-add","text-edit","vector-add","vector-edit"]) {
        var next = st
        let text = starts_with(action,"text-")
        let adding = ends_with(action,"-add")
        let capture = text and adding and not (field(owner,"art-font") in model.identifiers(st.doc))
        let group = if (capture) symbol("artwork-" ++ model.fresh_id(st.doc)) else null
        // automatic font capture and insertion undo as the one operation initiated by Add Text.
        if (capture) { next = embed_font(owner,st,group)^ }
        let options = {width:int(field(owner,"art-width")),height:int(field(owner,"art-height")),
            fill:colors.parse(field(owner,"art-fill"),"artwork.fill")^,stroke:colors.parse(field(owner,"art-stroke"),"artwork.stroke")^,
            stroke_width:float(field(owner,"art-stroke-width")),
            *:if (text) ({font:field(owner,"art-font"),text:field(owner,"art-text"),size:float(field(owner,"art-size")),
                line_height:float(field(owner,"art-leading")),align:field(owner,"art-align")})
            else ({shape:field(owner,"art-shape"),radius:float(field(owner,"art-radius")),
                *:if (field(owner,"art-shape") == "path") ({path:parse(field(owner,"art-path"),'mark')^}) else ({})})};
        return editor.exec(next,{id:(if (text) "text." else "vector.") ++ (if (adding) "insert" else "update"),options:options,group:group})^
    }
    if (action in ["style-add","style-edit"]) {
        let kind = field(owner,"style-kind")
        let options = {kind:kind,color:colors.parse(field(owner,"style-color"),"style.color")^,opacity:float(field(owner,"style-opacity")),
            radius:if (kind == "overlay") 0 else int(field(owner,"style-radius")),
            dx:if (kind != "shadow") 0 else int(field(owner,"style-dx")),dy:if (kind != "shadow") 0 else int(field(owner,"style-dy"))}
        let request = if (action == "style-add") ({id:"style.add",options:options}) else ({id:"style.update",index:st.style_index,options:options})
        let changed = editor.exec(st,request)^;
        return {*:changed,style_index:if (action == "style-add") len(styles.items(model.active(changed)^.node))-1 else st.style_index}
    }
    if (action in ["style.toggle","style.remove"]) { return editor.exec(st,{id:action,index:st.style_index})^ }
    if (action == "action-replay") { return editor.exec(st,{id:"action.replay",action:st.actions[last]})^ }
    if (action == "transform-apply") { return editor.exec(st,{id:"layer.transform",angle:float(field(owner,"transform-angle")),
        scale_x:float(field(owner,"transform-x")),scale_y:float(field(owner,"transform-y"))})^ }
    if (action == "transform-reset") { return editor.exec(st,{id:"layer.transform",matrix:[1,0,0,1,0,0]})^ }
    if (action in ["flip-x","flip-y"]) { return editor.exec(st,{id:"layer.transform",scale_x:if (action == "flip-x") -1 else 1,
        scale_y:if (action == "flip-y") -1 else 1})^ }
    if (action == "effect-add") {
        var options = {}
        for (control in effects.controls) { options = {*:options,[control.key]:float(field(owner,"effect-" ++ control.key))} }
        let changed = editor.exec(st,{id:"effect.add",options:options,name:"Adjustment"})^;
        return {*:changed,effect_index:len(effects.items(model.active(changed)^.node))-1}
    }
    if (action in ["effect.toggle","effect.remove"]) { return editor.exec(st,{id:action,index:st.effect_index})^ }
    if (action in ["effect-up","effect-down"]) {
        let target = st.effect_index+(if (action == "effect-up") -1 else 1)
        let changed = editor.exec(st,{id:"effect.reorder",index:st.effect_index,to:target})^;
        return {*:changed,effect_index:target}
    }
    if (action == "selection-feather") { return editor.exec(st,{id:"selection.feather",radius:float(field(owner,"feather-radius"))})^ }
    if (action == "swap-colors") { return editor.exec(st,{id:"tool.configure",foreground:st.background,background:st.foreground})^ }
    if (action == "file-cancel" or action == "discard-cancel") {
        dom.focus_set(present.node(owner,if (st.dialog == "discard") "stage" else st.dialog),false)
        return {*:st,dialog:"",pending:"",error:""}
    }
    if (action == "discard-save") { return show_file(owner,st,"save") }
    if (action == "discard-confirm") {
        if (st.pending in ["close","tab-close"]) { return {*:st,dialog:"",pending:"",tab_request:if (st.pending == "close") "close-window" else "close-tab"} }
        if (st.pending == "new") { return replace_document(st,"new")^ }
        return show_file(owner,{*:st,dialog:"",pending:""},"open")
    }
    if (action == "new" or action == "open") {
        if (editor.dirty(st)) { return {*:st,dialog:"discard",pending:action} }
        if (action == "new") { return replace_document(st,"new")^ }
        return show_file(owner,st,"open")
    }
    if (action in ["place","save","export","psd","action-save","action-open"]) { return show_file(owner,st,action) }
    if (action == "file-confirm") {
        let path = field(owner,"file-path")
        if (st.dialog == "action-open") { return {*:editor.exec(st,{id:"action.load",action:files.read_action(path)^})^,dialog:"",error:"",status:"Action loaded"} }
        if (st.dialog == "action-save") {
            files.write_action(st.actions[last],path)^
            return {*:st,dialog:"",error:"",status:"Action saved"}
        }
        if (st.dialog == "open") { return replace_document(st,"open",path)^ }
        if (st.dialog == "open-tab") { return {*:replace_document(st,"open",path)^,tab_request:"add"} }
        if (st.dialog == "place") { return {*:files.place(st,path)^,dialog:"",error:"",status:"Image placed"} }
        let format = if (st.dialog == "psd") "psd" else if (st.dialog == "export") field(owner,"export-format")
            else if (ends_with(lower(path),".psd")) "psd" else "photos"
        let options = if (st.dialog == "export") ({width:int(field(owner,"export-width")),height:int(field(owner,"export-height")),
            alpha:field(owner,"export-alpha"),
            *:if (field(owner,"export-alpha") == "flatten") ({background:field(owner,"export-background")}) else ({}),
            *:if (format == "jpeg") ({quality:int(field(owner,"export-quality"))}) else ({})}) else ({})
        let saved = files.write_document(st,path,format,options)^
        let next = if (st.dialog == "save") editor.mark_saved(st,saved.snapshot) else st
        let result = {*:next,dialog:"",path:if (st.dialog == "save") path else st.path,error:"",status:"Saved · " ++ path,pending:""}
        if (st.pending in ["close","tab-close"]) { return {*:result,tab_request:if (st.pending == "close") "close-window" else "close-tab"} }
        if (st.pending == "new") { return replace_document(result,"new")^ }
        if (st.pending == "open") { return show_file(owner,result,"open") }
        return result
    }
    if (action in ["fit","actual","zoom-in","zoom-out"]) {
        return {*:st,zoom:if (action == "fit") 0.0 else if (action == "actual") 1.0
            else max(0.05,min(8.0,(if (st.zoom == 0) st.scale else st.zoom)*(if (action == "zoom-in") 1.25 else 0.8))),
            pan:if (action in ["fit","actual"]) {x:0.0,y:0.0} else st.pan}
    }
    if (action == "front" or action == "back") {
        let row = model.active(st)^
        let siblings = [for (candidate in editor.layers(st.doc) where candidate.parent == row.parent) candidate]
        let current = [for (i,candidate in siblings where candidate.id == row.id) i][0]
        let index = max(0,min(len(siblings)-1,current+(if (action == "front") -1 else 1)));
        return editor.exec(st,{id:"layer.reorder",index:index})^
    }
    if (action in ["exposure-up","exposure-down","mono","sharpen"]) {
        let options = if (action == "mono") {saturation:0.0} else if (action == "sharpen") {sharpen:0.5}
            else {exposure:if (action == "exposure-up") 0.25 else -0.25};
        return editor.exec(st,{id:"effect.add",options:options,name:if (action == "mono") "Monochrome" else if (action == "sharpen") "Sharpen" else "Exposure"})^
    }
    if (action == "fill-white") {
        return editor.exec(st,{id:"pixels.fill",color:if (st.target == "mask") [1.0] else [1.0,1.0,1.0,1.0]})^
    }
    if (action in ["resize-image","resize-canvas"]) {
        return editor.exec(st,{id:if (action == "resize-image") "document.resize_image" else "document.resize_canvas",
            width:int(field(owner,"image-width")),height:int(field(owner,"image-height"))})^
    }
    return editor.exec(st,{id:action})^
}
pn pointer_input(owner,evt) {
    let handle = dom.closest(evt.target,"[data-raster-handle]")
    let guide = dom.closest(evt.target,"[data-raster-guide]")
    return {pointer:evt.pointerId,screen:{x:evt.clientX,y:evt.clientY},handle:dom.get_attribute(handle,"data-raster-handle"),
        guide:dom.get_attribute(guide,"data-raster-guide"),
        point:surface.point(owner,evt.clientX,evt.clientY),shift:evt.shiftKey,alt:evt.altKey}
}
pn curve_point(owner,evt) {
    let box = dom.bounding_box(present.node(owner,"curve-chart"))
    return {x:max(0,min(1,(evt.clientX-box.x)/box.width)),y:max(0,min(1,1-(evt.clientY-box.y)/box.height))}
}
pn curve_sample(owner,st,evt) any^ {
    return {*:st,curve_draft:tone.move(st.curve_draft,st.curve_channel,st.curve_drag.index,curve_point(owner,evt))^}
}
pn read_curves(owner,st) any^ {
    return tone.with_points(st.curve_draft,st.curve_channel,parse(field(owner,"curve-points"),'mark')^)^
}
pn begin_curve(owner,st,evt) any^ {
    let curves = read_curves(owner,st)^
    let selected = dom.closest(evt.target,"[data-curve-point]")
    let chosen = if (selected == null) tone.insert(curves,st.curve_channel,curve_point(owner,evt))^
        else {curves:curves,index:int(dom.get_attribute(selected,"data-curve-point"))}
    dom.focus_set(present.node(owner,"curve-editor"),false)
    // the graph is republished during a drag; its stable parent owns pointer capture.
    dom.set_pointer_capture(present.node(owner,"curve-editor"),evt.pointerId)
    return {*:st,curve_draft:chosen.curves,curve_point:chosen.index,
        curve_drag:{pointer:evt.pointerId,index:chosen.index,before:curves}}
}
pn cancel_preview(owner,st) {
    if (st.gesture.pointer != null) dom.release_pointer_capture(present.node(owner,"stage"),st.gesture.pointer)
    if (st.token > 0) dom.cancel_frame(owner,st.token)
    if (st.curve_drag != null) dom.release_pointer_capture(present.node(owner,"curve-editor"),st.curve_drag.pointer)
    return {*:interaction.cancel(st),token:0,curve_drag:null,
        curve_draft:if (st.curve_drag == null) st.curve_draft else st.curve_drag.before}
}
pn publish_preview(owner,st,workspace) {
    var next = st
    // pixel drafts must not invalidate the inspector subtree before the next input sample.
    surface.publish(owner,st) ^ { next = publish(owner,{*:cancel_preview(owner,st),error:^.message,status:^.message},workspace) } ~ { next = ~ }
    return next
}
fn tab_session(session,actions) => {*:session,actions:actions,painted_doc:null,painted_selection:null,
    artwork_node:null,style_node:null,token:0,tab_request:"",dialog:"",pending:""}
fn tab_transition(workspace,before,after) any^ {
    if (after.tab_request == "add") {
        let current = tab_session(after,before.actions)
        let changed = workspaces.add(workspaces.capture(workspace,tab_session(before,before.actions))^,current)^;
        {workspace:changed,session:current,close:false}
    } else if (after.tab_request in ["close-tab","close-window"]) {
        let current = {*:after,tab_request:"",dialog:"",pending:""}
        let changed = workspaces.close(workspaces.capture(workspace,current)^,workspace.active,true)^
        let dirty = [for (tab in changed.tabs where editor.dirty(tab.session)) tab]
        if (after.tab_request == "close-window" and len(dirty)==0) ({workspace:changed,session:current,close:true})
        else {
            let ready = if (len(changed.tabs)>0) changed else workspaces.add(changed,base_session(editor.open_session(editor.new_document()^)^))^
            let target = if (after.tab_request == "close-window") dirty[0] else workspaces.lookup(ready,ready.active)
            let selected = tab_session(target.session,before.actions);
            {workspace:{*:ready,active:target.id},session:if (after.tab_request == "close-window")
                {*:selected,dialog:"discard",pending:"close"} else selected,close:false}
        }
    } else ({workspace:workspace,session:after,close:false})
}

view <photos_session> state session:base_session(~.initial,"Coast" ),
    documents:(workspaces.add(workspaces.create(),base_session(~.initial,"Coast"))^) { ~.rendered }
on photos_activate(evt) {
    if (evt.event_phase != 2) { return 'pass' }
    let path = if (SOURCE is string and SOURCE != "") url_resolve("file://" ++ sys.proc.self.cwd# ++ "/",SOURCE)
        else url_resolve(BASE,"../photo/assets/coast.jpg")
    replace_document(session,"open",path) ^ { session = {*:session,error:^.message,status:^.message} } ~ { session = ~ }
    session = publish(evt.target,session,documents)
    return 'handled'
}
on photos_frame(evt) {
    if (evt.event_phase != 2 or evt.detail != session.token) { return 'pass' }
    var next = {*:session,token:0}
    interaction.render(next) ^ { next = {*:cancel_preview(evt.target,next),error:^.message,status:^.message} } ~ { next = ~ }
    if (next.gesture == null) {
        session = publish(evt.target,next,documents)
        return 'handled'
    }
    session = publish_preview(evt.target,next,documents)
    return 'handled'
}
on click(evt) {
    let owner = owner_of(evt)
    if (session.dialog != "" and dom.closest(evt.target,if (session.dialog == "discard") "#discard-dialog" else "#file-dialog") == null) {
        return 'prevent-default'
    }
    let button = dom.closest(evt.target,"[data-action]")
    let layer = dom.closest(evt.target,"[data-layer],[data-mask],[data-visibility]")
    let tool = dom.closest(evt.target,"[data-tool]")
    let tab = dom.closest(evt.target,"[data-tab]")
    var next = clear_error(cancel_preview(owner,session))
    let starting = next
    if (tab != null and tab != owner) {
        workspaces.activate(documents,dom.get_attribute(tab,"data-tab"),next) ^ { next = {*:next,error:^.message,status:^.message} }
            ~ { documents = ~.workspace; next = tab_session(~.session,starting.actions) }
    }
    else if (tool != null and tool != owner) {
        editor.exec(next,{id:"tool.configure",tool:dom.get_attribute(tool,"data-tool")}) ^ { next = {*:next,error:^.message,status:^.message} } ~ { next = ~ }
    }
    else if (dom.closest(evt.target,"[data-select-effect]") != null) {
        next = {*:next,effect_index:int(dom.get_attribute(dom.closest(evt.target,"[data-select-effect]"),"data-select-effect"))}
    }
    else if (dom.closest(evt.target,"[data-select-style]") != null) {
        next = {*:next,style_index:int(dom.get_attribute(dom.closest(evt.target,"[data-select-style]"),"data-select-style"))}
    }
    else if (layer != null) {
        let mask = dom.get_attribute(layer,"data-mask")
        let visible = dom.get_attribute(layer,"data-visibility")
        let id = if (mask != null) mask else if (visible != null) visible else dom.get_attribute(layer,"data-layer")
        let ids = if (evt.shiftKey and mask == null and visible == null)
            (if (id in session.selected) [for (chosen in session.selected where chosen != id) chosen] else [*session.selected,id]) else [id]
        editor.exec(next,{id:"layer.select",ids:ids,target:if (mask != null) "mask" else "pixels"}) ^ { next = {*:next,error:^.message,status:^.message} } ~ { next = ~ }
        if (visible != null) {
            editor.exec(next,{id:"layer.visibility"}) ^ { next = {*:next,error:^.message,status:^.message} } ~ { next = ~ }
        }
    } else if (button != null) {
        var problem = null
        // S7.6.7v4: procedure handlers capture successful values in their statement arm.
        let action = dom.get_attribute(button,"data-action")
        if (action == "guide-remove") {
            editor.exec(next,{id:"guide.remove",guide:dom.get_attribute(button,"data-guide")}) ^ { problem = ^.message } ~ { next = ~ }
        }
        if (action != "guide-remove") { command(owner,next,action) ^ { problem = ^.message } ~ { next = ~ } }
        if (problem != null) { next = {*:next,error:problem,status:problem} }
    } else { return 'pass' }
    var close = false
    tab_transition(documents,starting,next) ^ { next = {*:starting,error:^.message,status:^.message} }
        ~ { documents = ~.workspace; next = ~.session; close = ~.close }
    session = publish(owner,next,documents)
    if (close) { dom.set_close_guard(owner,false); dom.request_window_close(owner) }
    return 'handled'
}
on change(evt) {
    let owner = owner_of(evt)
    let id = dom.get_attribute(evt.target,"id")
    if (id in ["image-width","image-height"]) {
        let key = if (id == "image-width") "width" else "height"
        session = {*:session,size_draft:{*:session.size_draft,[key]:int(field(owner,id))}}
        return 'handled'
    }
    if (starts_with(id,"export-")) {
        if (id == "export-format") {
            let format = field(owner,id)
            present.set_value(present.node(owner,"file-path"),exports.destination(field(owner,"file-path"),format)^)
            present.set_value(present.node(owner,"export-alpha"),if (format == "jpeg") "flatten" else "preserve")
        }
        if (id in ["export-width","export-height"] and field(owner,"export-aspect") == "locked") {
            let width = id == "export-width"
            let value = float(field(owner,id))
            if (model.finite(value) and value>0) present.set_value(present.node(owner,if (width) "export-height" else "export-width"),
                string(exports.proportional_extent(session.doc,value,width)))
        }
        session = publish(owner,session,documents)
        return 'handled'
    }
    var next = clear_error(session)
    if (id == "straighten-axis") {
        editor.exec(next,{id:"tool.configure",straighten_axis:field(owner,id)}) ^ { next = {*:next,error:^.message,status:^.message} } ~ { next = ~ }
        session = publish(owner,next,documents)
        return 'handled'
    }
    if (id in ["curve-channel","curve-points"]) {
        // switching channels must retain text drafts even before a native blur/change arrives.
        read_curves(owner,next) ^ { next = {*:next,error:^.message,status:^.message} }
            ~ { next = {*:next,curve_draft:~,curve_channel:if (id == "curve-channel") field(owner,id) else next.curve_channel,curve_point:1} }
        session = publish(owner,next,documents)
        return 'handled'
    }
    if (id == "channel-view") {
        editor.exec(next,{id:"channel.view",channel:field(owner,id)}) ^ { next = {*:next,error:^.message,status:^.message} } ~ { next = ~ }
        session = publish(owner,next,documents)
        return 'handled'
    }
    if (id in ["retouch-strength","retouch-radius","retouch-aligned"]) {
        let config = if (id == "retouch-strength") {strength:float(field(owner,id))}
            else if (id == "retouch-radius") {radius:int(field(owner,id))} else {aligned:field(owner,id) == "aligned"}
        editor.exec(next,{id:"retouch.configure",*:config}) ^ { next = {*:next,error:^.message,status:^.message} } ~ { next = ~ }
        session = publish(owner,next,documents)
        return 'handled'
    }
    if (starts_with(id,"effect-")) {
        let key = slice(id,7,len(id))
        let current = effects.items(model.active(session)^.node)[session.effect_index]
        if (current == null) { session = {*:session,effect_draft:{*:session.effect_draft,[key]:float(field(owner,id))}}; return 'handled' }
        editor.exec(next,{id:"effect.update",index:session.effect_index,options:{*:current.options,[key]:float(field(owner,id))}}) ^ { next = {*:next,error:^.message,status:^.message} } ~ { next = ~ }
        session = publish(owner,next,documents)
        return 'handled'
    }
    let action = if (starts_with(id,"brush-")) {id:"tool.configure",brush:{[slice(id,6,len(id))]:float(field(owner,id))}}
        else if (id in ["foreground","background","tolerance","selection-mode","gradient-kind"])
            {id:"tool.configure",[if (id == "selection-mode") "selection_mode" else if (id == "gradient-kind") "gradient_kind" else id]:
                if (id == "tolerance") float(field(owner,id)) else field(owner,id)}
        else if (id == "layer-name") {id:"layer.rename",name:field(owner,id)}
        else if (id == "blend") {id:"layer.blend",blend:field(owner,id)}
        else if (id == "opacity") {id:"layer.opacity",opacity:float(field(owner,id))}
        else if (id in ["position-x","position-y"]) (
            let active = model.active(session)^,
            let box = model.bounds(active.node),
            {id:"layer.move",dx:if (id == "position-x") int(field(owner,id))-int(box.x) else 0,
                dy:if (id == "position-y") int(field(owner,id))-int(box.y) else 0}) else null
    if (action == null) { return 'pass' }
    editor.exec(next,action) ^ { next = {*:next,error:^.message,status:^.message} } ~ { next = ~ }
    session = publish(owner,next,documents)
    return 'handled'
}
on pointerdown(evt) {
    let owner = owner_of(evt)
    if (evt.button == 0 and session.dialog == "" and dom.closest(evt.target,"#curve-chart") != null) {
        var next = clear_error(session)
        begin_curve(owner,next,evt) ^ { next = {*:next,error:^.message,status:^.message} } ~ { next = ~ }
        session = publish(owner,next,documents)
        return 'prevent-default'
    }
    if (evt.button != 0 or session.dialog != "" or dom.closest(evt.target,"#stage") == null or dom.closest(evt.target,"#crop-controls") != null) { return 'pass' }
    dom.focus_set(present.node(owner,"stage"),false)
    var next = clear_error(session)
    interaction.begin(next,pointer_input(owner,evt)) ^ { next = {*:next,error:^.message,status:^.message} } ~ { next = ~ }
    if (next.gesture != null) dom.set_pointer_capture(present.node(owner,"stage"),evt.pointerId)
    if (next.gesture.mode in ["brush","erase",*retouch.kinds]) { session = publish_preview(owner,next,documents) }
    else { session = publish(owner,next,documents) }
    return 'prevent-default'
}
on mousedown(evt) {
    if (dom.closest(evt.target,"#stage") != null) {
        dom.focus_set(present.node(owner_of(evt),"stage"),false)
        return 'prevent-default'
    }
    return 'pass'
}
on pointermove(evt) {
    if (session.curve_drag != null and evt.pointerId == session.curve_drag.pointer) {
        let owner = owner_of(evt)
        session = curve_sample(owner,session,evt)^
        if (session.token == 0) { session = {*:session,token:dom.request_frame(owner,"photos_frame")} }
        return 'prevent-default'
    }
    if (session.gesture == null or evt.pointerId != session.gesture.pointer) { return 'pass' }
    let owner = owner_of(evt)
    session = interaction.sample(session,pointer_input(owner,evt))
    if (session.token == 0) { session = {*:session,token:dom.request_frame(owner,"photos_frame")} }
    return 'prevent-default'
}
on pointerup(evt) {
    if (session.curve_drag != null and evt.pointerId == session.curve_drag.pointer) {
        let owner = owner_of(evt)
        if (session.token>0) dom.cancel_frame(owner,session.token)
        let next = curve_sample(owner,session,evt)^
        dom.release_pointer_capture(present.node(owner,"curve-editor"),evt.pointerId)
        session = publish(owner,{*:next,curve_drag:null,token:0},documents)
        return 'prevent-default'
    }
    if (session.gesture == null or evt.pointerId != session.gesture.pointer) { return 'pass' }
    let owner = owner_of(evt)
    if (session.token > 0) dom.cancel_frame(owner,session.token)
    var next = session
    interaction.finish(interaction.preview(session,pointer_input(owner,evt))^) ^ { next = {*:interaction.cancel(session),error:^.message,status:^.message} } ~ { next = ~ }
    dom.release_pointer_capture(present.node(owner,"stage"),evt.pointerId)
    session = publish(owner,{*:next,token:0},documents)
    return 'prevent-default'
}
on pointercancel(evt) {
    if (session.curve_drag != null and evt.pointerId == session.curve_drag.pointer) {
        let owner = owner_of(evt)
        session = publish(owner,cancel_preview(owner,session),documents)
        return 'prevent-default'
    }
    if (session.gesture == null or evt.pointerId != session.gesture.pointer) { return 'pass' }
    let owner = owner_of(evt)
    session = publish(owner,cancel_preview(owner,session),documents)
    return 'prevent-default'
}
on keydown(evt) {
    let owner = owner_of(evt)
    let typing = dom.closest(evt.target,"input,textarea,select") != null
    let modifier = evt.ctrlKey or evt.metaKey
    var action = ""
    if (evt.key == "Escape") {
        let trigger = if (session.dialog in ["open","place","save","export"]) session.dialog else "stage"
        session = publish(owner,{*:cancel_preview(owner,session),dialog:"",pending:"",crop:null},documents)
        dom.focus_set(present.node(owner,trigger),false)
        return 'prevent-default'
    }
    if (session.dialog != "" and evt.key == "Tab") {
        let controls = if (session.dialog == "discard") ["discard-cancel","discard-save","discard-confirm"]
            else if (session.dialog == "export") ["file-path","export-format","export-aspect","export-width","export-height",
                *if (field(owner,"export-format") == "jpeg") ["export-quality"] else [],"export-alpha",
                *if (field(owner,"export-alpha") == "flatten") ["export-background"] else [],"file-cancel","file-confirm"]
            else ["file-path","file-cancel","file-confirm"]
        let current = model.value([for (i,id in controls where id == dom.get_attribute(evt.target,"id")) i][0],0)
        dom.focus_set(present.node(owner,controls[(current+(if (evt.shiftKey) len(controls)-1 else 1))%len(controls)]),false)
        return 'prevent-default'
    }
    if (session.dialog != "") { return 'pass' }
    if (typing) { return 'pass' }
    if (evt.key == "Enter" and session.tool == "crop") { action = "crop-apply" }
    else if (modifier and lower(evt.key) == "z") { action = if (evt.shiftKey) "history.redo" else "history.undo" }
    else if (modifier and lower(evt.key) == "s") { action = "save" }
    else if (modifier and lower(evt.key) == "a") { action = "selection.all" }
    else if (modifier and lower(evt.key) == "d") { action = "selection.clear" }
    else if (modifier and evt.shiftKey and lower(evt.key) == "i") { action = "selection.invert" }
    else if (modifier and lower(evt.key) == "o") { action = "open" }
    else if (modifier and lower(evt.key) == "n") { action = "new" }
    else if (modifier and lower(evt.key) == "c") { action = if (evt.shiftKey) "clipboard.copy_merged" else "clipboard.copy" }
    else if (modifier and lower(evt.key) == "x") { action = "clipboard.cut" }
    else if (modifier and lower(evt.key) == "v") { action = "clipboard.paste" }
    else if (not modifier and lower(evt.key) == "x") { action = "swap-colors" }
    else if (not modifier and evt.key in ["[","]"]) {
        editor.exec(session,{id:"tool.configure",brush:{size:max(0.5,min(4096,session.brush.size*(if (evt.key == "[") 0.8 else 1.25)))}}) ^ {
            session = {*:session,error:^.message,status:^.message}
        } ~ { session = ~ }
        session = publish(owner,session,documents)
        return 'prevent-default'
    }
    else if (evt.key == "Delete" or evt.key == "Backspace") {
        action = if (session.selection != null or session.tool in ["brush","erase","bucket","gradient"]) "pixels.clear" else "layer.delete"
    }
    else if (not modifier and lower(evt.key) in [for (tool in tools.catalog) tool.key]) {
        let tool = [for (tool in tools.catalog where tool.key == lower(evt.key)) tool.id][0]
        editor.exec(cancel_preview(owner,session),{id:"tool.configure",tool:tool}) ^ { session = {*:session,error:^.message,status:^.message} } ~ { session = ~ }
        session = publish(owner,session,documents)
        return 'prevent-default'
    } else if (evt.key in ["ArrowLeft","ArrowRight","ArrowUp","ArrowDown"]) {
        let amount = if (evt.shiftKey) 10 else 1
        editor.exec(session,{id:"layer.move",dx:if (evt.key == "ArrowLeft") -amount else if (evt.key == "ArrowRight") amount else 0,
            dy:if (evt.key == "ArrowUp") -amount else if (evt.key == "ArrowDown") amount else 0}) ^ { session = {*:session,error:^.message,status:^.message} } ~ { session = ~ }
        session = publish(owner,session,documents)
        return 'prevent-default'
    }
    if (action == "") { return 'pass' }
    session = cancel_preview(owner,session)
    command(owner,session,action) ^ { session = {*:session,error:^.message,status:^.message} } ~ { session = ~ }
    session = publish(owner,session,documents)
    return 'prevent-default'
}
on resize(evt) {
    if (session.token == 0) { session = {*:session,token:dom.request_frame(owner_of(evt),"photos_frame")} }
    return 'pass'
}
on closerequest(evt) {
    let owner = owner_of(evt)
    // a preview is disposable; the close decision uses the last committed snapshot.
    session = cancel_preview(owner,session)
    if (session.recording != null) {
        session = publish(owner,{*:session,error:"Stop action recording before closing documents",status:"Stop action recording before closing documents"},documents)
        return 'prevent-default'
    }
    documents = workspaces.capture(documents,session)^
    let dirty = [for (tab in documents.tabs where editor.dirty(tab.session)) tab]
    if (len(dirty)>0) {
        let target = if (editor.dirty(session)) workspaces.lookup(documents,documents.active) else dirty[0]
        documents = {*:documents,active:target.id}
        session = publish(owner,{*:tab_session(target.session,session.actions),dialog:"discard",pending:"close"},documents)
        return 'prevent-default'
    }
    dom.set_close_guard(owner,false)
    dom.request_window_close(owner)
    return 'pass'
}
view <photos_document> { ~.rendered }
on load(evt) {
    let owner = dom.get_element_by_id(evt.target,"photos")
    if (owner != null) dom.request_frame(owner,"photos_activate")
    return 'handled'
}
<html lang:"en",<head <meta charset:"UTF-8"> <title "Photos — Lambda / Radiant">
    <link rel:"stylesheet",href:url_resolve(BASE,"photos.css")>>
    apply(<photos_document rendered:<body apply(<photos_session initial:INITIAL,rendered:scene.tree()>)>>)
>
