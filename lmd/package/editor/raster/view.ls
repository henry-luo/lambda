// D4.5.2 / S12.1.3: native pixels are copied into a retained document-owned canvas.
import dom
import img: lambda.image
import model: .model
import viewport: lambda.editor.draw.viewport
import channels: .channels
import presentation: .render
import transforms: .transform
import guides: .guides

pub fn node(owner,selector) => dom.query_selector(owner,selector)
pn place_box(target,box,scale) {
    for (key,value in {left:box.x,top:box.y,width:box.width,height:box.height})
        dom.style_set_property(target,string(key),string(value*scale) ++ "px")
}
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
    let box = model.selection_bounds(st)
    if (st.painted_doc == null or box != st.painted_bounds or scale != st.scale) {
        dom.style_set_property(overlay,"display",if (box == null) "none" else "block")
        if (box != null) place_box(overlay,box,scale)
    }
    let controls = node(owner,".raster-transform")
    let handles_visible = st.tool == "transform" and transforms.available(st) and box.width>0 and box.height>0
    if (controls != null and (st.painted_doc == null or box != st.painted_bounds or scale != st.scale or handles_visible != st.painted_handles)) {
        dom.style_set_property(controls,"display",if (handles_visible) "block" else "none")
        if (handles_visible) place_box(controls,box,scale)
    }
    let guide_layer = node(owner,".raster-guides")
    let guide_scene = {items:guides.items(st.doc),visible:st.guides_visible,scale:scale,width:size.width,height:size.height,interactive:st.tool == "move"}
    if (guide_layer != null and (st.painted_doc == null or guide_scene != st.painted_guides)) {
        dom.set_inner_html(guide_layer,join([for (g in guide_scene.items) format(
            <div class:"raster-guide guide-" ++ g.axis,'data-raster-guide':g.id,role:"separator",
                'aria-label':(if (g.axis == "x") "Vertical" else "Horizontal") ++ " guide at " ++ string(g.position),
                style:if (g.axis == "x") "left:" ++ string(g.position*scale) ++ "px;height:" ++ string(size.height*scale) ++ "px"
                    else "top:" ++ string(g.position*scale) ++ "px;width:" ++ string(size.width*scale) ++ "px",<span>>,'xml')],""))
        dom.style_set_property(guide_layer,"display",if (st.guides_visible) "block" else "none")
        dom.style_set_property(guide_layer,"pointer-events",if (st.tool == "move") "auto" else "none")
    }
    let reference = node(owner,".raster-reference")
    let line = if (st.gesture.mode == "straighten") {start:st.gesture.start,end:st.gesture.end} else null
    let snap = st.gesture.snapped
    let reference_scene = {line:line,snap:snap,scale:scale,width:size.width,height:size.height}
    if (reference != null and (st.painted_doc == null or reference_scene != st.painted_reference))
        dom.set_inner_html(reference,format(<svg width:size.width*scale,height:size.height*scale,*[
            if (line != null) <path d:"M " ++ string(line.start.x*scale) ++ " " ++ string(line.start.y*scale) ++ " L " ++
                string(line.end.x*scale) ++ " " ++ string(line.end.y*scale),stroke:"#ffdb7b",'stroke-width':2,fill:"none">,
            *[for (axis in ["x","y"] where snap[axis] != null) <path stroke:"#ff7dd6",'stroke-width':1,fill:"none",
                d:if (axis == "x") "M " ++ string(snap.x*scale) ++ " 0 V " ++ string(size.height*scale)
                    else "M 0 " ++ string(snap.y*scale) ++ " H " ++ string(size.width*scale)>]]>,'xml'))
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
                place_box(panel,rect,scale)
            }
        }
        if (crop_box != null) place_box(crop,crop_box,scale)
    }
    return {*:st,painted_doc:st.doc,painted_raster:rendered,painted_channel:st.channel_view,painted_selection:st.selection,
        painted_geometry:geometry,painted_bounds:box,painted_crop:crop_box,painted_handles:handles_visible,
        painted_guides:guide_scene,painted_reference:reference_scene,scale:scale}
}
