import dom
import radiant
import model: .mod_model
import geometry: .mod_geometry
import pipeline: .mod_pipeline
import presets: .mod_presets
import layers: .mod_layers

pub fn node(owner, id) => dom.get_element_by_id(owner,id)
pub pn text(owner,id,value) {
    let target = node(owner,id)
    if (dom.text_content(target) != string(value)) dom.set_text_content(target,string(value))
}
pub pn node_attribute(target,key,requested) {
    if (dom.get_attribute(target,key) != requested) {
        if (requested == null) dom.remove_attribute(target,key) else dom.set_attribute(target,key,requested)
    }
}
pub pn attribute(owner,id,key,requested) { node_attribute(node(owner,id),key,requested) }
pn style_property(target,key,requested) {
    // stable geometry must not enqueue another style mutation on every frame.
    if (dom.style_get_property(target,key) != requested) dom.style_set_property(target,key,requested)
}
pub pn visible(owner,id,shown) {
    let target = node(owner,id)
    let requested = if (shown) "block" else "none"
    style_property(target,"display",requested)
}
pub pn value(owner,id,requested) {
    let control = node(owner,id)
    if (dom.get_state(control,"value") != string(requested)) {
        if (dom.matches(control,"select")) {
            let options = dom.query_selector_all(control,"option")
            let index = [for (i,option in options where dom.get_attribute(option,"value") == string(requested)) i][0]
            if (index != null) dom.set_selected_index(control,index)
        } else {
            // hidden native controls acquire their live buffers on first layout; sync when shown.
            let box = dom.bounding_box(control)
            if (box.width > 0 and box.height > 0) dom.set_state(control,"value",string(requested))
        }
    }
}
pub pn message(owner,content,failed=false) {
    text(owner,"status",content)
    node_attribute(owner,"data-error",if (failed) content else "")
}
pub pn dialog(owner,id) {
    for (candidate in ["open-dialog","export-dialog","recipe-dialog"]) visible(owner,candidate,candidate == id)
    if (id != "") dom.focus_set(node(owner,if (id == "open-dialog") "open-path" else if (id == "export-dialog") "export-path" else "recipe-path"),false)
}
pub pn thumbnails(owner,source) any^ {
    let small = pipeline.proxy(source.proxy,128)
    for (preset in presets.catalog) {
        dom.set_canvas_pixels(node(owner,"thumb-" ++ preset.id),pipeline.evaluate(small,{*:model.recipe(),preset:preset.id}))^
    }
}
pub pn synchronize(owner,session_value) {
    let recipe = model.current(session_value.hist)
    for (key,interval in model.limits) {
        value(owner,"control-" ++ key,recipe[key])
        text(owner,"value-" ++ key,round(recipe[key]*100)/100)
    }
    for (tool in layers.tools) {
        visible(owner,"panel-" ++ tool.id,tool.id == session_value.tool)
        attribute(owner,"tool-" ++ tool.id,"class",if (tool.id == session_value.tool) "tool selected" else "tool")
        attribute(owner,"tool-" ++ tool.id,"aria-pressed",string(tool.id == session_value.tool))
    }
    for (preset in presets.catalog) {
        attribute(owner,"preset-" ++ preset.id,"class",if (preset.id == recipe.preset) "preset selected" else "preset")
        attribute(owner,"preset-" ++ preset.id,"aria-pressed",string(preset.id == recipe.preset))
    }
    for (entry in [{id:"undo",disabled:session_value.hist.cursor == 0},{id:"redo",disabled:session_value.hist.cursor == len(session_value.hist.entries)-1}]) {
        attribute(owner,entry.id,"disabled",if (entry.disabled) "" else null)
    }
    text(owner,"history-info",string(session_value.hist.cursor) ++ " / " ++ string(len(session_value.hist.entries)-1) ++ " edits")
    text(owner,"source-name",split(session_value.source.path,"/")[last])
    text(owner,"source-size",string(session_value.source.width) ++ " × " ++ string(session_value.source.height))
    node_attribute(owner,"data-history",string(session_value.hist.cursor))
    node_attribute(owner,"data-preset",recipe.preset)
    node_attribute(owner,"data-tool",session_value.tool)
    node_attribute(owner,"data-inspector",string(session_value.inspector_open))
    attribute(owner,"inspector-toggle","aria-expanded",string(session_value.inspector_open))
    visible(owner,"compare-label",session_value.compare)
    node_attribute(owner,"data-layers",string(len(recipe.layers)))
    let selected = layers.selected(recipe,session_value.selected)
    node_attribute(owner,"data-selected",if (selected == null) "0" else string(selected.id))
    visible(owner,"panel-layers",layers.object_tool(session_value.tool))
    visible(owner,"layer-empty",len(recipe.layers) == 0)
    visible(owner,"object-properties",selected != null)
    let options = format([<option value:"0","Choose a layer">,*[for (layer in recipe.layers)
        <option value:string(layer.id),(if (layer.kind == "text") layer.text else layer.kind) ++ " · " ++ string(layer.id)>]],'html')
    let selector = node(owner,"layer-list")
    if (dom.get_attribute(selector,"data-options") != options) {
        dom.set_inner_html(selector,options)
        dom.set_attribute(selector,"data-options",options)
    }
    value(owner,"layer-list",if (selected == null) 0 else selected.id)
    if (selected != null) {
        for (row in ["text","font","align"]) visible(owner,"object-" ++ row ++ "-row",selected.kind == "text")
        for (key in ["text","font","align","color","opacity","rotation","x","y","width","height"])
            value(owner,"object-" ++ key,if (contains(layers.placement_keys,key)) round(selected[key]*1000)/10 else selected[key])
    }
}
pub pn frame_geometry(owner,session_value,dimensions) {
    let stage = dom.bounding_box(node(owner,"stage"))
    let factor = if (session_value.zoom == 0) geometry.fit(dimensions[1],dimensions[0],max(1,stage.width-80),max(1,stage.height-80))
        else session_value.zoom*max(session_value.source.width,session_value.source.height)/max(shape(session_value.source.proxy)[0],shape(session_value.source.proxy)[1])
    let width = dimensions[1]*factor
    let height = dimensions[0]*factor
    let image = node(owner,"image-plane")
    style_property(image,"width",string(width) ++ "px")
    style_property(image,"height",string(height) ++ "px")
    style_property(image,"left",string((stage.width-width)/2+session_value.pan.x) ++ "px")
    style_property(image,"top",string((stage.height-height)/2+session_value.pan.y) ++ "px")
    text(owner,"zoom-label",if (session_value.zoom == 0) "Fit" else string(round(session_value.zoom*100)) ++ "%")
    let rect = model.current(session_value.hist).crop
    let overlay = node(owner,"crop-frame")
    visible(owner,"crop-frame",session_value.tool == "crop")
    for (property,requested in {left:rect.left*100,top:rect.top*100,width:(rect.right-rect.left)*100,height:(rect.bottom-rect.top)*100})
        style_property(overlay,string(property),string(requested) ++ "%")
    for (edge in ["left","top","right","bottom"]) value(owner,"crop-" ++ edge,round(rect[edge]*1000)/10)
    let selected = layers.selected(model.current(session_value.hist),session_value.selected)
    visible(owner,"artwork",not session_value.compare and session_value.tool != "crop")
    visible(owner,"selection-frame",selected != null and layers.object_tool(session_value.tool) and not session_value.compare)
    if (selected != null) {
        let frame = node(owner,"selection-frame")
        for (property,requested in {left:selected.x*100,top:selected.y*100,width:selected.width*100,height:selected.height*100})
            style_property(frame,string(property),string(requested) ++ "%")
        style_property(frame,"transform","rotate(" ++ string(selected.rotation) ++ "deg)")
        text(owner,"selection-label",if (selected.kind == "text") "Text" else selected.kind)
    }
}
pub pn paint(owner,session_value) any^ {
    let controls_dirty=session_value.controls_dirty or session_value.dirty or session_value.painted==null
    if (controls_dirty) { synchronize(owner,session_value) }
    let recipe=model.current(session_value.hist)
    let source_key=string(session_value.source.identity)
    let key={*:pipeline.options(recipe,session_value.compare,session_value.tool=="crop"),
        layers:[],output_width:0,output_height:0}
    var updated={*:session_value,token:0,dirty:false,controls_dirty:false}
    var pixels=session_value.painted
    var changed=false
    if (pixels==null or session_value.painted_source!=source_key) {
        radiant.image_job(owner,{action:"cancel",channel:"preview"})^
        pixels=image_process(session_value.source.proxy,key)^
        updated={*:updated,job:0,painted_key:key,painted_source:source_key}
        changed=true
    } else if (key!=session_value.painted_key) {
        if (session_value.job==0 or key!=session_value.job_key) {
            let token=radiant.image_job(owner,{action:"start",channel:"preview",source:source_key,
                pixels:session_value.source.proxy,options:key})^
            updated={*:updated,job:token,job_key:key}
        } else {
            let done=radiant.image_job(owner,{action:"poll",channel:"preview",token:session_value.job})^
            if (done!=null) {
                pixels=done.pixels^
                updated={*:updated,job:0,painted_key:key}
                changed=true
                for (metric in ["elapsed_us","scratch_bytes","cache_bytes","geometry_hits","color_hits","blur_hits"])
                    dom.set_attribute(owner,"data-image-" ++ metric,string(done[metric]))
            }
        }
    } else if (session_value.job>0) {
        radiant.image_job(owner,{action:"cancel",channel:"preview"})^
        updated={*:updated,job:0}
    }
    if (changed) {
        let began=clock()
        dom.set_canvas_pixels(node(owner,"preview"),pixels)^
        dom.set_attribute(owner,"data-image-upload_us",string(int((clock()-began)*1000000)))
        if (session_value.requested_at>0)
            dom.set_attribute(owner,"data-image-preview_upload_ms",string((clock()-session_value.requested_at)*1000))
    }
    let dimensions=shape(pixels)
    if (recipe.layers!=session_value.painted_layers or dimensions!=shape(session_value.painted)) {
        dom.set_inner_html(node(owner,"artwork"),format(layers.svg(recipe,dimensions[1],dimensions[0]),'xml'))
    }
    let pixel_revision=session_value.pixel_revision+(if (changed) 1 else 0)
    if (session_value.tool=="adjust" and dom.bounding_box(node(owner,"histogram")).width>0 and
        session_value.histogram_revision!=pixel_revision) {
        let began=clock()
        let counts=histogram(as_float(grayscale(pixels)),32)
        let peak=max(1,max(counts))
        dom.set_inner_html(node(owner,"histogram"),format(<svg viewBox:"0 0 224 56",width:224,height:56, *[
            for (i,count in counts) <rect x:(i*7),y:(56-count/peak*52),width:5,height:(count/peak*52),fill:"#80d6b3">]>,'xml'))
        updated={*:updated,histogram_revision:pixel_revision}
        dom.set_attribute(owner,"data-image-histogram_us",string(int((clock()-began)*1000000)))
    }
    if (controls_dirty or changed) { frame_geometry(owner,updated,dimensions) }
    let revision=session_value.revision+(if (controls_dirty or changed) 1 else 0)
    node_attribute(owner,"data-revision",string(revision))
    node_attribute(owner,"data-processing",string(updated.job>0))
    updated={*:updated,painted:pixels,painted_layers:recipe.layers,pixel_revision:pixel_revision,revision:revision}
    return if (updated.job>0 or updated.export_job>0) schedule(owner,updated,true) else updated
}
pub pn schedule(owner,session_value,poll=false) {
    // polling keeps the retained UI untouched until a result or another input arrives.
    let updated=if (poll) session_value else {*:session_value,controls_dirty:true,
        requested_at:if (session_value.dirty) clock() else session_value.requested_at}
    return if (updated.token>0) updated else {*:updated,token:dom.request_frame(owner,"photo_frame")}
}
