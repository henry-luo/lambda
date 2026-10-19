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
import effects: lambda.editor.raster.effects

let BASE = url_resolve(resources.entry_uri()^,".")
let SOURCE = sys.proc.self.env.PHOTOS_SOURCE#
let INITIAL = editor.open_session(editor.new_document()^ )^
fn owner_of(evt) => dom.get_element_by_id(dom.root_node(evt.target),"photos")
fn field(owner,id) => dom.get_state(present.node(owner,id),"value")
fn base_session(st,name="Untitled") => {*:st,name:name,path:"",dialog:"",pending:"",error:"",status:"Ready · Place images to build a composition",
    effect_index:0,effect_draft:{},painted_doc:null,token:0,scale:1.0,size_draft:{width:st.doc.width,height:st.doc.height}}
pn publish(owner,st) {
    var next = st
    present.publish(owner,st) ^ { dom.set_attribute(owner,"data-error",^.message) } ~ { next = ~ }
    return next
}
pn show_file(owner,st,kind) {
    let title = if (kind == "open") "Open image or Photos project" else if (kind == "place") "Place image as layer"
        else if (kind == "action-save") "Save action" else if (kind == "action-open") "Load action"
        else if (kind == "save") "Save Photos project" else if (kind == "psd") "Export layered PSD" else "Export PNG"
    dom.set_text_content(present.node(owner,"file-title"),title)
    dom.set_text_content(present.node(owner,"file-help"),if (kind in ["action-save","action-open"]) "Actions replay recorded edits on the currently selected layer."
        else if (kind == "save") "Use .photos to preserve all editable settings, or .psd for the supported PSD profile."
        else if (kind == "export") "Exports the current composition with transparency."
        else if (kind == "psd") "Retained transforms, blend modes and enabled effects require a project, or explicit rasterization before PSD export."
        else "Enter a local image, PSD or .photos project path.")
    dom.set_state(present.node(owner,"file-path"),"value",if (kind == "action-save") "temp/photos/action.laction" else if (kind == "save")
        (if (ends_with(lower(st.path),".photos")) st.path else "temp/photos/composition.photos")
        else if (kind == "psd") "temp/photos/composition.psd"
        else if (kind == "export") "temp/photos/composition.png" else "")
    let next = publish(owner,{*:st,dialog:kind,error:""})
    dom.focus_set(present.node(owner,"file-path"),false)
    return next
}
pn replace_document(st,action,path="") any^ {
    let next = if (action == "new") editor.open_session(editor.new_document()^)^ else files.open_document(path)^
    return {*:base_session(next,if (action == "new") "Untitled" else split(path,"/")[last]),
        actions:st.actions,path:if (ends_with(lower(path),".psd") or ends_with(lower(path),".photos")) path else ""}
}
pn command(owner,st,action) any^ {
    if (st.recording != null and action in ["new","open","place","save","export","psd","action-save","action-open"])
        raise error("Photos: stop action recording before file operations")
    if (action == "action-start") { return editor.exec(st,{id:"action.start",name:field(owner,"action-name")})^ }
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
        if (st.pending == "close") { dom.set_close_guard(owner,false); dom.request_window_close(owner); return st }
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
        if (st.dialog == "place") { return {*:files.place(st,path)^,dialog:"",error:"",status:"Image placed"} }
        let format = if (st.dialog == "psd") "psd" else if (st.dialog == "export") "png"
            else if (ends_with(lower(path),".psd")) "psd" else "photos"
        let saved = files.write_document(st,path,format)^
        let next = if (st.dialog == "save") editor.mark_saved(st,saved.snapshot) else st
        let result = {*:next,dialog:"",path:if (st.dialog == "save") path else st.path,error:"",status:"Saved · " ++ path,pending:""}
        if (st.pending == "close") { dom.set_close_guard(owner,false); dom.request_window_close(owner); return result }
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
    return {pointer:evt.pointerId,screen:{x:evt.clientX,y:evt.clientY},
        point:surface.point(owner,evt.clientX,evt.clientY),shift:evt.shiftKey,alt:evt.altKey}
}
pn cancel_preview(owner,st) {
    if (st.gesture.pointer != null) dom.release_pointer_capture(present.node(owner,"stage"),st.gesture.pointer)
    if (st.token > 0) dom.cancel_frame(owner,st.token)
    return {*:interaction.cancel(st),token:0}
}

view <photos_session> state session:base_session(~.initial,"Coast" ) { ~.rendered }
on photos_activate(evt) {
    if (evt.event_phase != 2) { return 'pass' }
    let path = if (SOURCE is string and SOURCE != "") url_resolve("file://" ++ sys.proc.self.cwd# ++ "/",SOURCE)
        else url_resolve(BASE,"../photo/assets/coast.jpg")
    replace_document(session,"open",path) ^ { session = {*:session,error:^.message,status:^.message} } ~ { session = ~ }
    session = publish(evt.target,session)
    return 'handled'
}
on photos_frame(evt) {
    if (evt.event_phase != 2 or evt.detail != session.token) { return 'pass' }
    var next = {*:session,token:0}
    interaction.render(next) ^ { next = {*:cancel_preview(evt.target,next),error:^.message,status:^.message} } ~ { next = ~ }
    session = publish(evt.target,next)
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
    var next = {*:cancel_preview(owner,session),error:""}
    if (tool != null and tool != owner) {
        editor.exec(next,{id:"tool.configure",tool:dom.get_attribute(tool,"data-tool")}) ^ { next = {*:next,error:^.message,status:^.message} } ~ { next = ~ }
    }
    else if (dom.closest(evt.target,"[data-select-effect]") != null) {
        next = {*:next,effect_index:int(dom.get_attribute(dom.closest(evt.target,"[data-select-effect]"),"data-select-effect"))}
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
        command(owner,next,dom.get_attribute(button,"data-action")) ^ { problem = ^.message } ~ { next = ~ }
        if (problem != null) { next = {*:next,error:problem,status:problem} }
    } else { return 'pass' }
    session = publish(owner,next)
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
    var next = {*:session,error:""}
    if (id in ["retouch-strength","retouch-radius","retouch-aligned"]) {
        let config = if (id == "retouch-strength") {strength:float(field(owner,id))}
            else if (id == "retouch-radius") {radius:int(field(owner,id))} else {aligned:field(owner,id) == "aligned"}
        editor.exec(next,{id:"retouch.configure",*:config}) ^ { next = {*:next,error:^.message,status:^.message} } ~ { next = ~ }
        session = publish(owner,next)
        return 'handled'
    }
    if (starts_with(id,"effect-")) {
        let key = slice(id,7,len(id))
        let current = effects.items(model.active(session)^.node)[session.effect_index]
        if (current == null) { session = {*:session,effect_draft:{*:session.effect_draft,[key]:float(field(owner,id))}}; return 'handled' }
        editor.exec(next,{id:"effect.update",index:session.effect_index,options:{*:current.options,[key]:float(field(owner,id))}}) ^ { next = {*:next,error:^.message,status:^.message} } ~ { next = ~ }
        session = publish(owner,next)
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
    session = publish(owner,next)
    return 'handled'
}
on pointerdown(evt) {
    let owner = owner_of(evt)
    if (evt.button != 0 or session.dialog != "" or dom.closest(evt.target,"#stage") == null) { return 'pass' }
    dom.focus_set(present.node(owner,"stage"),false)
    var next = session
    interaction.begin(session,pointer_input(owner,evt)) ^ { next = {*:next,error:^.message,status:^.message} } ~ { next = ~ }
    if (next.gesture != null) dom.set_pointer_capture(present.node(owner,"stage"),evt.pointerId)
    session = publish(owner,next)
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
    if (session.gesture == null or evt.pointerId != session.gesture.pointer) { return 'pass' }
    let owner = owner_of(evt)
    session = interaction.sample(session,pointer_input(owner,evt))
    if (session.token == 0) { session = {*:session,token:dom.request_frame(owner,"photos_frame")} }
    return 'prevent-default'
}
on pointerup(evt) {
    if (session.gesture == null or evt.pointerId != session.gesture.pointer) { return 'pass' }
    let owner = owner_of(evt)
    var next = session
    interaction.finish(interaction.preview(session,pointer_input(owner,evt))^) ^ { next = {*:interaction.cancel(session),error:^.message,status:^.message} } ~ { next = ~ }
    dom.release_pointer_capture(present.node(owner,"stage"),evt.pointerId)
    session = publish(owner,next)
    return 'prevent-default'
}
on pointercancel(evt) {
    if (session.gesture == null or evt.pointerId != session.gesture.pointer) { return 'pass' }
    let owner = owner_of(evt)
    session = publish(owner,cancel_preview(owner,session))
    return 'prevent-default'
}
on keydown(evt) {
    let owner = owner_of(evt)
    let typing = dom.closest(evt.target,"input,textarea,select") != null
    let modifier = evt.ctrlKey or evt.metaKey
    var action = ""
    if (evt.key == "Escape") {
        let trigger = if (session.dialog in ["open","place","save","export"]) session.dialog else "stage"
        session = publish(owner,{*:cancel_preview(owner,session),dialog:"",pending:""})
        dom.focus_set(present.node(owner,trigger),false)
        return 'prevent-default'
    }
    if (session.dialog != "" and evt.key == "Tab") {
        let controls = if (session.dialog == "discard") ["discard-cancel","discard-save","discard-confirm"] else ["file-path","file-cancel","file-confirm"]
        let current = model.value([for (i,id in controls where id == dom.get_attribute(evt.target,"id")) i][0],0)
        dom.focus_set(present.node(owner,controls[(current+(if (evt.shiftKey) 2 else 1))%3]),false)
        return 'prevent-default'
    }
    if (session.dialog != "") { return 'pass' }
    if (typing) { return 'pass' }
    if (modifier and lower(evt.key) == "z") { action = if (evt.shiftKey) "history.redo" else "history.undo" }
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
        session = publish(owner,session)
        return 'prevent-default'
    }
    else if (evt.key == "Delete" or evt.key == "Backspace") {
        action = if (session.selection != null or session.tool in ["brush","erase","bucket","gradient"]) "pixels.clear" else "layer.delete"
    }
    else if (not modifier and lower(evt.key) in [for (tool in tools.catalog) tool.key]) {
        let tool = [for (tool in tools.catalog where tool.key == lower(evt.key)) tool.id][0]
        editor.exec(cancel_preview(owner,session),{id:"tool.configure",tool:tool}) ^ { session = {*:session,error:^.message,status:^.message} } ~ { session = ~ }
        session = publish(owner,session)
        return 'prevent-default'
    } else if (evt.key in ["ArrowLeft","ArrowRight","ArrowUp","ArrowDown"]) {
        let amount = if (evt.shiftKey) 10 else 1
        editor.exec(session,{id:"layer.move",dx:if (evt.key == "ArrowLeft") -amount else if (evt.key == "ArrowRight") amount else 0,
            dy:if (evt.key == "ArrowUp") -amount else if (evt.key == "ArrowDown") amount else 0}) ^ { session = {*:session,error:^.message,status:^.message} } ~ { session = ~ }
        session = publish(owner,session)
        return 'prevent-default'
    }
    if (action == "") { return 'pass' }
    session = cancel_preview(owner,session)
    command(owner,session,action) ^ { session = {*:session,error:^.message,status:^.message} } ~ { session = ~ }
    session = publish(owner,session)
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
    if (editor.dirty(session)) {
        session = publish(owner,{*:session,dialog:"discard",pending:"close"})
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
