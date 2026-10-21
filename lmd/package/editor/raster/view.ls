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
    let crop = node(owner,".raster-crop")
    let crop_box = if (st.tool == "crop") st.crop else null
    if (crop != null and (st.painted_doc == null or crop_box != st.painted_crop or scale != st.scale)) {
        dom.style_set_property(crop,"display",if (crop_box == null) "none" else "block")
        let shade = node(owner,".raster-crop-shade")
        if (shade != null) {
            dom.style_set_property(shade,"display",if (crop_box == null) "none" else "block")
            // finite surrounding rectangles avoid allocating an oversized shadow surface.
            if (crop_box != null) for (rect in [
                {edge:"top",x:0,y:0,width:size.width,height:crop_box.y},
                {edge:"bottom",x:0,y:crop_box.y+crop_box.height,width:size.width,height:size.height-crop_box.y-crop_box.height},
                {edge:"left",x:0,y:crop_box.y,width:crop_box.x,height:crop_box.height},
                {edge:"right",x:crop_box.x+crop_box.width,y:crop_box.y,width:size.width-crop_box.x-crop_box.width,height:crop_box.height}]) {
                let panel = node(owner,"[data-crop-shade='" ++ rect.edge ++ "']")
                for (key,value in {left:rect.x,top:rect.y,width:rect.width,height:rect.height})
                    dom.style_set_property(panel,string(key),string(value*scale) ++ "px")
            }
        }
        if (crop_box != null) for (key,value in {left:crop_box.x,top:crop_box.y,width:crop_box.width,height:crop_box.height})
            dom.style_set_property(crop,string(key),string(value*scale) ++ "px")
    }
    return {*:st,painted_doc:st.doc,painted_raster:rendered,painted_channel:st.channel_view,painted_selection:st.selection,
        painted_geometry:geometry,painted_bounds:box,painted_crop:crop_box,scale:scale}
}
