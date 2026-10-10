import dom
import model: .mod_model
import geometry: .mod_geometry
import pipeline: .mod_pipeline
import presets: .mod_presets

pub fn node(owner, id) => dom.get_element_by_id(owner,id)
pub pn text(owner,id,value) {
    let target = node(owner,id)
    if (dom.text_content(target) != string(value)) dom.set_text_content(target,string(value))
}
pub pn attribute(owner,id,key,requested) {
    let target = node(owner,id)
    if (dom.get_attribute(target,key) != requested) {
        if (requested == null) dom.remove_attribute(target,key) else dom.set_attribute(target,key,requested)
    }
}
pub pn visible(owner,id,shown) {
    let target = node(owner,id)
    let requested = if (shown) "block" else "none"
    if (dom.style_get_property(target,"display") != requested) dom.style_set_property(target,"display",requested)
}
pub pn value(owner,id,requested) {
    let control = node(owner,id)
    if (dom.get_state(control,"value") != string(requested)) dom.set_state(control,"value",string(requested))
}
pub pn message(owner,content,failed=false) {
    text(owner,"status",content)
    dom.set_attribute(owner,"data-error",if (failed) content else "")
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
    for (tool in ["adjust","filters","crop"]) {
        visible(owner,"panel-" ++ tool,tool == session_value.tool)
        attribute(owner,"tool-" ++ tool,"class",if (tool == session_value.tool) "tool selected" else "tool")
        attribute(owner,"tool-" ++ tool,"aria-pressed",string(tool == session_value.tool))
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
    dom.set_attribute(owner,"data-history",string(session_value.hist.cursor))
    dom.set_attribute(owner,"data-preset",recipe.preset)
    dom.set_attribute(owner,"data-tool",session_value.tool)
    dom.set_attribute(owner,"data-inspector",string(session_value.inspector_open))
    attribute(owner,"inspector-toggle","aria-expanded",string(session_value.inspector_open))
    visible(owner,"compare-label",session_value.compare)
}
pub pn frame_geometry(owner,session_value,dimensions) {
    let stage = dom.bounding_box(node(owner,"stage"))
    let factor = if (session_value.zoom == 0) geometry.fit(dimensions[1],dimensions[0],max(1,stage.width-80),max(1,stage.height-80))
        else session_value.zoom*max(session_value.source.width,session_value.source.height)/max(shape(session_value.source.proxy)[0],shape(session_value.source.proxy)[1])
    let width = dimensions[1]*factor
    let height = dimensions[0]*factor
    let image = node(owner,"image-plane")
    dom.style_set_property(image,"width",string(width) ++ "px")
    dom.style_set_property(image,"height",string(height) ++ "px")
    dom.style_set_property(image,"left",string((stage.width-width)/2+session_value.pan.x) ++ "px")
    dom.style_set_property(image,"top",string((stage.height-height)/2+session_value.pan.y) ++ "px")
    text(owner,"zoom-label",if (session_value.zoom == 0) "Fit" else string(round(session_value.zoom*100)) ++ "%")
    let rect = model.current(session_value.hist).crop
    let overlay = node(owner,"crop-frame")
    visible(owner,"crop-frame",session_value.tool == "crop")
    for (property,requested in {left:rect.left*100,top:rect.top*100,width:(rect.right-rect.left)*100,height:(rect.bottom-rect.top)*100})
        dom.style_set_property(overlay,string(property),string(requested) ++ "%")
    for (edge in ["left","top","right","bottom"]) value(owner,"crop-" ++ edge,round(rect[edge]*1000)/10)
}
pub pn paint(owner,session_value) any^ {
    synchronize(owner,session_value)
    let pixels = if (session_value.dirty or session_value.painted == null)
        pipeline.evaluate(session_value.source.proxy,{*:model.current(session_value.hist),output_width:0,output_height:0},session_value.compare,session_value.tool == "crop")
        else session_value.painted
    if (session_value.dirty or session_value.painted == null) dom.set_canvas_pixels(node(owner,"preview"),pixels)^
    frame_geometry(owner,session_value,shape(pixels))
    let revision = session_value.revision+1
    dom.set_attribute(owner,"data-revision",string(revision))
    return {*:session_value,dirty:false,painted:pixels,token:0,revision:revision}
}
pub pn schedule(owner,session_value) {
    return if (session_value.token > 0) session_value else {*:session_value,token:dom.request_frame(owner,"photo_frame")}
}
