// D4.5.2 / S12.1.3: native pixels are copied into a retained document-owned canvas.
import dom
import img: lambda.image
import model: .model
import viewport: lambda.editor.draw.viewport
import channels: .channels
import presentation: .render

pub fn node(owner,selector) => dom.query_selector(owner,selector)
pub pn point(owner,x,y) {
    let surface = dom.bounding_box(node(owner,".raster-surface"))
    let root = dom.get_attribute(node(owner,".raster-stage"),"data-scale");
    return viewport.point(surface,float(root),x,y)
}
pub pn publish(owner,st) any^ {
    let stage = node(owner,".raster-stage")
    let surface = node(owner,".raster-surface")
    let viewport_box = dom.bounding_box(stage)
    let size = {width:st.doc.width,height:st.doc.height}
    let scale = if (st.zoom == 0) viewport.fit(size,viewport_box,40.0) else st.zoom
    let geometry = {width:size.width*scale,height:size.height*scale,
        left:(viewport_box.width-size.width*scale)/2+st.pan.x,
        top:(viewport_box.height-size.height*scale)/2+st.pan.y}
    // unchanged geometry must not invalidate layout before the next pointer sample.
    if (st.painted_doc == null or geometry != st.painted_geometry or scale != st.scale) {
        for (property,requested in geometry) dom.style_set_property(surface,string(property),string(requested) ++ "px")
        dom.set_attribute(stage,"data-scale",string(scale))
    }
    let frame = presentation.frame(st)^
    let rendered = frame.raster
    if (frame.changed or st.painted_channel != st.channel_view)
        dom.set_canvas_pixels(node(owner,".raster-canvas"),channels.preview(st,rendered)^.pixels)^
    let selection = node(owner,".raster-selection")
    if (st.painted_doc == null or (st.selection == null) != (st.painted_selection == null))
        dom.style_set_property(selection,"display",if (st.selection == null) "none" else "block")
    if (st.selection != null and st.painted_selection != st.selection)
        dom.set_canvas_pixels(selection,img.coverage_overlay(st.selection)^.pixels)^
    let overlay = node(owner,".raster-target")
    let active = if (len(st.selected)==1) model.lookup(st.doc,st.selected[0]) else null
    let box = if (active == null) null else model.bounds(active.node)
    if (st.painted_doc == null or box != st.painted_bounds or scale != st.scale) {
        dom.style_set_property(overlay,"display",if (active == null) "none" else "block")
        if (box != null) for (key,requested in {left:box.x,top:box.y,width:box.width,height:box.height})
            dom.style_set_property(overlay,string(key),string(requested*scale) ++ "px")
    }
    return {*:st,painted_doc:st.doc,painted_raster:rendered,painted_channel:st.channel_view,painted_selection:st.selection,
        painted_geometry:geometry,painted_bounds:box,scale:scale}
}
