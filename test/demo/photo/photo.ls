// Native retained UI: the tree stays stable while procedures update its controls
// and canvas (S12.1.3, D4.5.1v4); history contains immutable recipes (S1.4).
import dom
import radiant
// keep stylesheet loading independent of unused component implementations (D8.1.1v18).
import ui_styles: lambda.ui.dtna.style
import resources: ~~.mod_resources
import files: .mod_io
import model: .mod_model
import geometry: .mod_geometry
import pipeline: .mod_pipeline
import scene: .mod_scene
import present: .mod_present
import layers: .mod_layers

let BASE = url_resolve(resources.entry_uri()^,".")
let START_PATH = sys.proc.self.env.PHOTO_SOURCE#
let INITIAL = files.open_image(if (START_PATH is string and START_PATH != "")
    url_resolve("file://" ++ sys.proc.self.cwd# ++ "/",START_PATH) else url_resolve(BASE,"assets/coast.jpg"))^
fn owner_of(evt) => dom.get_element_by_id(dom.root_node(evt.target),"photo")
fn field(owner,id) => dom.get_state(present.node(owner,id),"value")
fn replace_recipe(session_value,recipe) => {*:session_value,dirty:true,hist:if (session_value.tool == "crop")
    model.draft(session_value.hist,recipe) else model.commit_edit(session_value.hist,recipe)}
fn replace_layers(session_value,recipe) => {*:session_value,hist:model.commit_edit(session_value.hist,recipe)}
fn object_change(recipe,id,key,requested) {
    let numeric = if (contains(layers.placement_keys,key)) float(requested) ^ { null } else null;
    if (contains(layers.placement_keys,key))
        if (numeric is number) layers.change(recipe,id,key,numeric/100) else recipe
    else layers.change(recipe,id,key,requested)
}
fn output_aspect(session_value) {
    let recipe = model.current(session_value.hist)
    let box = geometry.bounds(recipe.crop,if (recipe.turns%2 == 0) session_value.source.width else session_value.source.height,
        if (recipe.turns%2 == 0) session_value.source.height else session_value.source.width);
    (box.right-box.left)/(box.bottom-box.top)
}
fn add_layer(session_value,kind,text="Your story") {
    let recipe = model.current(session_value.hist)
    let layer = layers.create(recipe,kind,text,output_aspect(session_value));
    if (len(recipe.layers) >= 24) session_value else {*:replace_layers(session_value,{*:recipe,layers:[*recipe.layers,layer]}),
        selected:layer.id,inspector_open:false}
}
pn change_source(owner,session_value,path) any^ {
    let source = files.open_image(url_resolve("file://" ++ sys.proc.self.cwd# ++ "/",path))^
    present.thumbnails(owner,source)^
    present.dialog(owner,"")
    present.message(owner,"Image opened. Your original stays untouched.")
    return {*:session_value,source:source,hist:model.history(),tool:"adjust",zoom:0,pan:{x:0,y:0},dirty:true,compare:false,drag:null,selected:0}
}
pn command(owner,session_value,action) any^ {
    let recipe = model.current(session_value.hist)
    if (action == "add-text") { return add_layer(session_value,"text",field(owner,"text-entry")) }
    if (starts_with(action,"layer-")) {
        let changed = if (action == "layer-delete") layers.remove(recipe,session_value.selected)
            else if (action == "layer-duplicate") layers.duplicate(recipe,session_value.selected)
            else layers.reorder(recipe,session_value.selected,if (action == "layer-front") 1 else -1)
        return {*:replace_layers(session_value,changed),selected:if (action == "layer-delete") 0
            else if (action == "layer-duplicate") changed.layers[last].id else session_value.selected}
    }
    if (action == "open" or action == "export" or action == "recipe") {
        present.dialog(owner,action ++ "-dialog")
        return session_value
    }
    if (action == "close-dialog") { present.dialog(owner,""); return session_value }
    if (action == "toggle-inspector") { return {*:session_value,inspector_open:not session_value.inspector_open} }
    if (action == "open-confirm") { return change_source(owner,session_value,field(owner,"open-path"))^ }
    if (starts_with(action,"sample-")) { return change_source(owner,session_value,url_resolve(BASE,"assets/" ++ slice(action,7) ++ ".jpg"))^ }
    if (action == "export-confirm") {
        // repeated activation shares the running export instead of launching another snapshot.
        if (session_value.export_job>0) { return session_value }
        let requested={*:recipe,output_width:int(field(owner,"export-width")),output_height:int(field(owner,"export-height"))}
        let request=files.prepare_export(field(owner,"export-path"),session_value.source,requested)^
        let token=radiant.image_job(owner,{action:"start",channel:"export",source:string(session_value.source.identity),
            pixels:session_value.source.pixels,options:pipeline.options(requested),destination:request.raster})^
        present.message(owner,"Exporting PNG…")
        present.dialog(owner,"")
        dom.set_attribute(owner,"data-exporting","true")
        present.attribute(owner,"export-confirm","disabled","")
        return {*:session_value,export_job:token,export_request:{*:request,before:recipe}}
    }
    if (action == "recipe-save") {
        files.write_recipe(field(owner,"recipe-path"),session_value.source,recipe)^
        present.message(owner,"Recipe saved · " ++ field(owner,"recipe-path"))
        return session_value
    }
    if (action == "recipe-load") {
        let restored = files.read_recipe(field(owner,"recipe-path"),session_value.source)^
        present.message(owner,"Recipe restored")
        present.dialog(owner,"")
        return replace_recipe(session_value,restored)
    }
    if (action == "undo" or action == "redo") {
        return {*:session_value,tool:if (session_value.tool == "crop") "adjust" else session_value.tool,dirty:true,
            hist:if (action == "undo") model.step_back(session_value.hist) else model.step_forward(session_value.hist)}
    }
    if (action == "reset") { return replace_recipe(session_value,model.recipe()) }
    if (action == "reset-adjust") {
        return replace_recipe(session_value,{*:recipe,exposure:0.0,brightness:0.0,contrast:1.0,saturation:1.0,temperature:0.0,gamma:1.0,
            black:0.0,white:1.0,shadows:0.0,highlights:0.0})
    }
    if (action == "reset-effects") { return replace_recipe(session_value,{*:recipe,vignette:0.0,softness:0.0,sharpen:0.0}) }
    if (contains(["rotate","flip-x","flip-y"],action)) { return replace_recipe(session_value,model.orient(recipe,action)) }
    if (action == "crop-apply" or action == "crop-cancel") {
        return {*:session_value,tool:"adjust",dirty:true,drag:null,inspector_open:false,
            hist:if (action == "crop-apply") model.commit_edit(session_value.hist,recipe) else model.discard(session_value.hist)}
    }
    if (action == "fit") { return {*:session_value,zoom:0,pan:{x:0,y:0}} }
    if (action == "actual") { return {*:session_value,zoom:1,pan:{x:0,y:0}} }
    if (action == "zoom-in" or action == "zoom-out") {
        return {*:session_value,zoom:geometry.clamp((if (session_value.zoom == 0) 0.5 else session_value.zoom)*
            (if (action == "zoom-in") 1.25 else 0.8),0.1,8)}
    }
    return session_value
}

view <photo_session> state session:{source:~.source,hist:model.history(),tool:"adjust",zoom:0,pan:{x:0,y:0},
    token:0,dirty:true,painted:null,painted_layers:null,painted_source:null,painted_key:null,job:0,job_key:null,
    pixel_revision:0,histogram_revision:-1,controls_dirty:true,requested_at:0.0,export_job:0,export_request:null,revision:0,compare:false,drag:null,ratio:0,gesture:null,inspector_open:false,selected:0} { ~.rendered }
on photo_activate(evt) {
    if (evt.event_phase != 2) { return 'pass' }
    present.thumbnails(evt.target,session.source)^
    session = present.paint(evt.target,session)^
    present.message(evt.target,"Ready. Open a photo or start editing.")
    return 'handled'
}
on photo_frame(evt) {
    if (evt.event_phase != 2 or evt.detail != session.token) { return 'pass' }
    var problem=null
    if (session.export_job>0) {
        let done=radiant.image_job(evt.target,{action:"poll",channel:"export",token:session.export_job})^
        if (done!=null) {
            let request=session.export_request
            if (done.success) {
                let began=clock()
                files.finish_export(request,[done.height,done.width,4]) ^ { problem=^.message }
                dom.set_attribute(evt.target,"data-export-process_us",string(done.elapsed_us))
                dom.set_attribute(evt.target,"data-export-encode_us",string(done.encode_us))
                dom.set_attribute(evt.target,"data-export-compose_us",string(int((clock()-began)*1000000)))
                if (problem is null) {
                    present.message(evt.target,"Saved PNG · " ++ string(done.width) ++ " × " ++ string(done.height) ++ " · " ++ request.target)
                    if (session.source.identity==request.source.identity and model.current(session.hist)==request.before) {
                        session=replace_recipe(session,request.recipe)
                    }
                }
            } else {
                // the native job removes its own partial files; a failed exclusive open owns none.
                problem="Photo: image processing, encoding, or destination write failed"
            }
            dom.set_attribute(evt.target,"data-exporting","false")
            present.attribute(evt.target,"export-confirm","disabled",null)
            session={*:session,export_job:0,export_request:null}
        }
    }
    present.paint(evt.target,session) ^ { problem=^.message } ~ { session=~ }
    if (not (problem is null)) {
        present.message(evt.target,problem,true)
        radiant.image_job(evt.target,{action:"cancel",channel:"preview"})
        session={*:session,job:0,token:0}
        if (session.export_job>0) { session=present.schedule(evt.target,session) }
    }
    return 'handled'
}
on click(evt) {
    let owner = owner_of(evt)
    let tool = dom.closest(evt.target,"[data-tool]")
    let preset = dom.closest(evt.target,"[data-preset]")
    let resetter = dom.closest(evt.target,"[data-reset]")
    let action = dom.closest(evt.target,"[data-action]")
    let asset = dom.closest(evt.target,"[data-add]")
    let swatch = dom.closest(evt.target,"[data-color]")
    if (owner == null) { return 'pass' }
    if (tool != null and tool != owner) {
        let selected = dom.get_attribute(tool,"data-tool")
        session = {*:session,tool:selected,inspector_open:if (selected == session.tool) not session.inspector_open else true,dirty:true,zoom:0,pan:{x:0,y:0},
            hist:if (selected == "crop") model.begin(session.hist) else if (session.tool == "crop") model.discard(session.hist) else session.hist}
        dom.set_scroll_state(present.node(owner,"inspector"),0.0,0.0)
    } else if (asset != null) {
        session = add_layer(session,dom.get_attribute(asset,"data-add"))
        if (len(model.current(session.hist).layers) >= 24) present.message(owner,"Up to 24 layers per image.")
    } else if (swatch != null) {
        session = replace_layers(session,layers.change(model.current(session.hist),session.selected,"color",dom.get_attribute(swatch,"data-color")))
    } else if (preset != null and preset != owner) {
        session = replace_recipe(session,{*:model.current(session.hist),preset:dom.get_attribute(preset,"data-preset")})
    } else if (resetter != null) {
        let key = dom.get_attribute(resetter,"data-reset")
        session = replace_recipe(session,model.change(model.current(session.hist),key,model.recipe()[key]))
    } else if (action != null) {
        var problem = null
        command(owner,session,dom.get_attribute(action,"data-action")) ^ { problem = ^.message } ~ { session = ~ }
        if (not (problem is null)) present.message(owner,problem,true)
    } else { return 'pass' }
    session = present.schedule(owner,session)
    return 'handled'
}
on input(evt) {
    let key = dom.get_attribute(evt.target,"data-adjust")
    let object_key = dom.get_attribute(evt.target,"data-object")
    if (object_key != null) {
        // native text buffers must finish a numeric/color draft before it becomes an edit.
        if (object_key == "color" or contains(layers.placement_keys,object_key)) { return 'pass' }
        let requested = dom.get_state(evt.target,"value")
        let recipe = model.current(session.hist)
        let changed = object_change(recipe,session.selected,object_key,requested)
        session = present.schedule(owner_of(evt),{*:session,hist:model.draft(session.hist,changed)})
        return 'handled'
    }
    if (key == null) { return 'pass' }
    let recipe = model.change(model.current(session.hist),key,float(dom.get_state(evt.target,"value")))
    session = present.schedule(owner_of(evt),{*:session,dirty:true,hist:model.draft(session.hist,recipe)})
    return 'handled'
}
on change(evt) {
    let owner = owner_of(evt)
    let key = dom.get_attribute(evt.target,"data-adjust")
    let edge = dom.get_attribute(evt.target,"data-crop-edge")
    let id = dom.get_attribute(evt.target,"id")
    let recipe = model.current(session.hist)
    let object_key = dom.get_attribute(evt.target,"data-object")
    if (id == "layer-list") {
        session = {*:session,selected:int(field(owner,"layer-list"))}
    } else if (object_key != null) {
        let requested = dom.get_state(evt.target,"value")
        let changed = object_change(recipe,session.selected,object_key,requested)
        session = replace_layers(session,changed)
    } else if (key != null) {
        let changed = model.change(recipe,key,float(dom.get_state(evt.target,"value")))
        session = if (session.tool == "crop") {*:session,dirty:true,hist:model.draft(session.hist,changed)}
            else {*:session,dirty:true,hist:model.commit_edit(session.hist,changed)}
    } else if (id == "crop-ratio") {
        let requested = field(owner,"crop-ratio")
        let dimensions = pipeline.dimensions(session.source.proxy,recipe)
        let ratio = if (requested == "original") dimensions[1]/dimensions[0] else float(requested)
        session = {*:session,dirty:true,ratio:ratio,hist:model.draft(session.hist,{*:recipe,crop:if (ratio == 0) geometry.full() else geometry.aspect(dimensions[1],dimensions[0],ratio)})}
    } else if (edge != null) {
        let rect = {*:recipe.crop,[edge]:float(dom.get_state(evt.target,"value"))/100}
        if (not geometry.valid(rect)) {
            present.message(owner,"Crop edges must form a nonempty rectangle inside the image",true)
            return 'handled'
        }
        session = {*:session,dirty:true,hist:model.draft(session.hist,{*:recipe,crop:rect})}
    } else if (id == "export-width" or id == "export-height") {
        if (dom.get_state(present.node(owner,"aspect-lock"),"checked") == true) {
            let dims = geometry.bounds(recipe.crop,if (recipe.turns%2 == 0) session.source.width else session.source.height,
                if (recipe.turns%2 == 0) session.source.height else session.source.width)
            let ratio = (dims.right-dims.left)/(dims.bottom-dims.top)
            let requested = int(dom.get_state(evt.target,"value"))
            present.value(owner,if (id == "export-width") "export-height" else "export-width",
                if (requested == 0) 0 else max(1,int(round(if (id == "export-width") requested/ratio else requested*ratio))))
        }
        return 'handled'
    } else { return 'pass' }
    session = present.schedule(owner,session)
    return 'handled'
}
on pointerdown(evt) {
    let owner = owner_of(evt)
    let action = dom.closest(evt.target,"[data-action]")
    let handle = dom.closest(evt.target,"[data-handle]")
    let layer_handle = dom.closest(evt.target,"[data-layer-handle]")
    if (action != null and dom.get_attribute(action,"data-action") == "compare") {
        session = present.schedule(owner,{*:session,compare:true,dirty:true})
        dom.set_pointer_capture(evt.target,evt.pointerId)
        return 'prevent-default'
    }
    if (dom.get_attribute(evt.target,"data-adjust") != null or dom.get_attribute(evt.target,"data-object") != null) {
        session = {*:session,gesture:model.current(session.hist)}
        return 'pass'
    }
    if (evt.button != 0 or dom.closest(evt.target,"#stage") == null) { return 'pass' }
    let box = dom.bounding_box(present.node(owner,"image-plane"))
    let recipe = model.current(session.hist)
    let selected = if (layer_handle != null) session.selected else if (layers.object_tool(session.tool))
        layers.hit(recipe,(evt.clientX-box.x)/box.width,(evt.clientY-box.y)/box.height,box.width/box.height) else null
    if (selected != null and selected != 0) {
        session = {*:session,selected:selected,drag:{x:evt.clientX,y:evt.clientY,width:box.width,height:box.height,
            handle:if (layer_handle == null) "move" else dom.get_attribute(layer_handle,"data-layer-handle"),
            layer:layers.selected(recipe,selected),recipe:recipe}}
        dom.set_pointer_capture(evt.target,evt.pointerId)
        return 'prevent-default'
    }
    if (layers.object_tool(session.tool)) {
        session = present.schedule(owner,{*:session,selected:0})
        return 'pass'
    }
    session = {*:session,drag:{x:evt.clientX,y:evt.clientY,pan:session.pan,rect:model.current(session.hist).crop,
        width:box.width,height:box.height,handle:if (session.tool == "crop" and handle != null) dom.get_attribute(handle,"data-handle") else "pan"}}
    dom.set_pointer_capture(evt.target,evt.pointerId)
    return 'prevent-default'
}
on pointermove(evt) {
    if (session.drag == null) { return 'pass' }
    let drag = session.drag
    if (drag.layer != null) {
        let changed = layers.drag(drag.layer,drag.handle,(evt.clientX-drag.x)/drag.width,(evt.clientY-drag.y)/drag.height,drag.width/drag.height)
        session = {*:session,hist:model.draft(session.hist,layers.update(model.current(session.hist),drag.layer.id,changed))}
    } else if (drag.handle == "pan") {
        session = {*:session,pan:{x:drag.pan.x+evt.clientX-drag.x,y:drag.pan.y+evt.clientY-drag.y}}
    } else {
        let dims = pipeline.dimensions(session.source.proxy,model.current(session.hist))
        let rect = geometry.drag(drag.rect,drag.handle,(evt.clientX-drag.x)/drag.width,(evt.clientY-drag.y)/drag.height,session.ratio,dims[1],dims[0])
        session = {*:session,hist:model.draft(session.hist,{*:model.current(session.hist),crop:rect})}
    }
    session = present.schedule(owner_of(evt),session)
    return 'prevent-default'
}
on pointerup(evt) {
    if (session.drag.layer != null) { session = replace_layers(session,model.current(session.hist)) }
    session = present.schedule(owner_of(evt),{*:session,drag:null,gesture:null,compare:false,dirty:session.dirty or session.compare})
    return 'pass'
}
on pointercancel(evt) {
    let restored = if (session.drag.layer != null) model.draft(session.hist,session.drag.recipe)
        else if (session.drag != null and session.drag.handle != "pan")
        model.draft(session.hist,{*:model.current(session.hist),crop:session.drag.rect}) else if (session.gesture != null)
        model.draft(session.hist,session.gesture) else session.hist
    session = present.schedule(owner_of(evt),{*:session,hist:restored,drag:null,gesture:null,compare:false,dirty:true})
    return 'pass'
}
on wheel(evt) {
    if (dom.closest(evt.target,"#stage") == null) { return 'pass' }
    session = present.schedule(owner_of(evt),{*:session,zoom:geometry.clamp((if (session.zoom == 0) 0.5 else session.zoom)*
        (if (evt.deltaY < 0) 1.1 else 1/1.1),0.1,8)})
    return 'prevent-default'
}
on keydown(evt) {
    let owner = owner_of(evt)
    let control = dom.closest(evt.target,"input,textarea,select")
    let editing = control != null
    if (evt.key == "Escape") {
        // the select behavior dismisses its popup before editor-level Escape actions.
        if (editing and dom.matches(control,"select") and dom.dropdown_open(control)) { return 'pass' }
        present.dialog(owner,"")
        session = present.schedule(owner,{*:session,hist:model.discard(session.hist),tool:if (session.tool == "crop") "adjust" else session.tool,
            drag:null,compare:false,dirty:true})
        return 'prevent-default'
    }
    if ((evt.ctrlKey or evt.metaKey) and lower(evt.key) == "z") {
        if (editing) { return 'pass' }
        session = present.schedule(owner,command(owner,session,if (evt.shiftKey) "redo" else "undo")^)
        return 'prevent-default'
    }
    if (not editing and layers.object_tool(session.tool) and layers.selected(model.current(session.hist),session.selected) != null) {
        if (evt.key == "Delete" or evt.key == "Backspace") {
            session = present.schedule(owner,command(owner,session,"layer-delete")^)
            return 'prevent-default'
        }
        if (contains(["ArrowLeft","ArrowRight","ArrowUp","ArrowDown"],evt.key)) {
            let layer = layers.selected(model.current(session.hist),session.selected)
            let amount = if (evt.shiftKey) 0.02 else 0.002
            let moved = layers.drag(layer,"move",if (evt.key == "ArrowLeft") -amount else if (evt.key == "ArrowRight") amount else 0,
                if (evt.key == "ArrowUp") -amount else if (evt.key == "ArrowDown") amount else 0)
            session = present.schedule(owner,replace_layers(session,layers.update(model.current(session.hist),layer.id,moved)))
            return 'prevent-default'
        }
    }
    if (dom.closest(evt.target,"#compare") != null and (evt.key == " " or evt.key == "Enter")) {
        session = present.schedule(owner,{*:session,compare:true,dirty:true})
        return 'prevent-default'
    }
    return 'pass'
}
on keyup(evt) {
    if (session.compare) { session = present.schedule(owner_of(evt),{*:session,compare:false,dirty:true}) }
    return 'pass'
}
on blur(evt) {
    if (session.compare and dom.closest(evt.relatedTarget,"#photo") == null) {
        session = present.schedule(owner_of(evt),{*:session,compare:false,drag:null,dirty:true})
    }
    return 'pass'
}
on closerequest(evt) {
    if (session.token > 0) dom.cancel_frame(owner_of(evt),session.token)
    radiant.image_job(owner_of(evt),{action:"cancel",channel:"preview"})
    radiant.image_job(owner_of(evt),{action:"cancel",channel:"export"})
    session = {*:session,token:0,job:0,export_job:0,drag:null,compare:false}
    return 'pass'
}
view <photo_document> { ~.rendered }
on load(evt) {
    let owner = dom.get_element_by_id(evt.target,"photo")
    if (owner != null) dom.request_frame(owner,"photo_activate")
    return 'handled'
}
<html lang:"en",<head <meta charset:"UTF-8"> <title "Photo Studio — Lambda / Radiant">
    <style ui_styles.css> <link rel:"stylesheet",href:url_resolve(BASE,"photo.css")>>
    apply(<photo_document rendered:<body apply(<photo_session source:INITIAL,rendered:scene.tree()>)>>)
>
