// D4.5.2 / S12.1.3: native pixels are copied into a retained document-owned canvas.
import dom
import img: lambda.image
import model: .model
import viewport: lambda.editor.draw.viewport

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
    for (property,requested in {width:size.width*scale,height:size.height*scale,
        left:(viewport_box.width-size.width*scale)/2+st.pan.x,
        top:(viewport_box.height-size.height*scale)/2+st.pan.y})
        dom.style_set_property(surface,string(property),string(requested) ++ "px")
    dom.set_attribute(stage,"data-scale",string(scale))
    if (st.painted_doc == null or st.painted_doc != st.doc) {
        let pixels = img.render_frame(st.doc)^
        dom.set_canvas_pixels(node(owner,".raster-canvas"),pixels.pixels)^
    }
    let overlay = node(owner,".raster-target")
    let active = if (len(st.selected)==1) model.lookup(st.doc,st.selected[0]) else null
    dom.style_set_property(overlay,"display",if (active == null) "none" else "block")
    if (active != null) {
        let box = model.bounds(active.node)
        for (key,requested in {left:box.x,top:box.y,width:box.width,height:box.height})
            dom.style_set_property(overlay,string(key),string(requested*scale) ++ "px")
    }
    return {*:st,painted_doc:st.doc,scale:scale}
}
