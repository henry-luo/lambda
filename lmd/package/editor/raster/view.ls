// D4.5.2 / S12.1.3: native pixels are copied into a retained document-owned canvas.
import dom
import img: lambda.image
import model: .model
import viewport: lambda.editor.draw.viewport
import channels: .channels
import presentation: .render
import transforms: .transform
import guides: .guides
import navigation: .navigation

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
pub pn navigator_input(owner,st,x,y) {
    let bounds = dom.bounding_box(node(owner,".raster-navigator"))
    let box = navigation.thumbnail(st.doc,bounds);
    return {viewport:dom.bounding_box(node(owner,".raster-stage")),
        navigator:navigation.navigator_point(st.doc,box,{x:x-bounds.x,y:y-bounds.y})}
}
pn publish_navigation(owner,st,rendered,bounds,camera_box) any^ {
    let grid = node(owner,".raster-grid")
    let lines = navigation.grid_lines(st,bounds)
    let grid_rect = navigation.visible(st,bounds)
    let grid_scene = if (len(lines)==0) null else {lines:lines,rect:grid_rect,scale:camera_box.scale}
    if (grid != null and (st.painted_doc == null or grid_scene != st.painted_grid)) {
        // clip the SVG itself, so deep zoom never allocates a document-sized overlay bitmap.
        if (len(lines)>0) place_box(grid,grid_rect,camera_box.scale)
        dom.set_inner_html(grid,if (len(lines)==0) "" else format(<svg width:grid_rect.width*camera_box.scale,height:grid_rect.height*camera_box.scale,
            viewBox:join([for (value in [grid_rect.x,grid_rect.y,grid_rect.width,grid_rect.height]) string(value)]," "), *[
            for (major in [false,true]) <path fill:"none",stroke:if (major) "#b4bdce" else "#788293",
                opacity:if (major) 0.65 else 0.35,'stroke-width':1/camera_box.scale,
                d:join([for (line in lines where line.major == major)
                    if (line.axis == "x") "M " ++ string(line.value) ++ " " ++ string(grid_rect.y) ++ " V " ++ string(grid_rect.y+grid_rect.height)
                    else "M " ++ string(grid_rect.x) ++ " " ++ string(line.value) ++ " H " ++ string(grid_rect.x+grid_rect.width)]," ")>]>,'xml'))
    }
    let ruler_scene = if (not st.rulers_visible) ({visible:false}) else
        ({visible:true,x:camera_box.x,y:camera_box.y,scale:camera_box.scale,width:bounds.width,height:bounds.height})
    if (st.painted_doc == null or ruler_scene != st.painted_rulers) {
        for (axis in ["x","y"]) {
            let target = node(owner,".raster-ruler-" ++ axis)
            if (target != null) {
                dom.style_set_property(target,"display",if (st.rulers_visible) "block" else "none")
                if (st.rulers_visible) {
                    let horizontal = axis == "x"
                    let length = if (horizontal) bounds.width else bounds.height
                    let ticks = navigation.ticks(camera_box[axis],length,camera_box.scale)
                    dom.set_inner_html(target,format(<svg width:if (horizontal) length else 22,height:if (horizontal) 22 else length,
                        *[for (tick in ticks where tick.position>22) *[
                            <path stroke:"#9299a5",d:if (horizontal) "M " ++ string(tick.position) ++ " 17 v 5"
                                else "M 17 " ++ string(tick.position) ++ " h 5">,
                            <text fill:"#c5cbd5",'font-size':9,x:if (horizontal) tick.position+3 else 12,
                                y:if (horizontal) 12 else tick.position-3,
                                *:if (horizontal) ({}) else ({transform:"rotate(-90 12 " ++ string(tick.position-3) ++ ")"}),string(tick.value)>]]>,'xml'))
                }
            }
        }
        let stage = node(owner,".raster-stage")
        dom.set_attribute(stage,"data-rulers",string(st.rulers_visible))
    }
    let navigator = node(owner,".raster-navigator")
    let navigation_cache = {painted_grid:grid_scene,painted_rulers:ruler_scene,navigator_raster:st.navigator_raster,
        navigator_size:st.navigator_size,painted_navigator:st.painted_navigator}
    let defer_thumbnail = st.gesture != null and not (st.gesture.mode in ["pan","navigator"])
    if (navigator != null and defer_thumbnail and st.painted_doc != null) {
        return navigation_cache
    }
    if (navigator != null) {
        let nav_bounds = dom.bounding_box(navigator)
        let thumb = navigation.thumbnail(st.doc,nav_bounds)
        let size = {width:thumb.width,height:thumb.height}
        // live strokes retain the last committed thumbnail; release refreshes it once.
        let refresh = st.painted_doc == null or size != st.navigator_size or st.navigator_raster != rendered
        if (refresh) {
            dom.set_canvas_pixels(node(owner,".raster-navigator canvas"),img.resize(rendered,size.width,size.height)^.pixels)^
        }
        let rect = navigation.visible(st,bounds)
        let scene = {thumb:thumb,rect:rect,width:st.doc.width,height:st.doc.height}
        if (st.painted_doc == null or scene != st.painted_navigator) {
            place_box(node(owner,".raster-navigator canvas"),thumb,1)
            let target = node(owner,".raster-navigator-viewport")
            place_box(target,{x:thumb.x+rect.x*thumb.width/st.doc.width,y:thumb.y+rect.y*thumb.height/st.doc.height,
                width:rect.width*thumb.width/st.doc.width,height:rect.height*thumb.height/st.doc.height},1)
            dom.style_set_property(target,"display",if (rect.width>0 and rect.height>0) "block" else "none")
        }
        return {*:navigation_cache,navigator_raster:if (refresh) rendered else st.navigator_raster,navigator_size:size,painted_navigator:scene}
    }
    return navigation_cache
}
pub pn publish(owner,st) any^ {
    let stage = node(owner,".raster-stage")
    let surface = node(owner,".raster-surface")
    let viewport_box = dom.bounding_box(stage)
    let size = {width:st.doc.width,height:st.doc.height}
    let camera = navigation.camera(st,viewport_box)
    let scale = camera.scale
    let geometry = {width:camera.width,height:camera.height,left:camera.x,top:camera.y}
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
    let guide_scene = {items:guides.items(st.doc),visible:st.guides_visible,scale:scale,width:size.width,height:size.height,interactive:st.tool == "move" and not st.guides_locked}
    if (guide_layer != null and (st.painted_doc == null or guide_scene != st.painted_guides)) {
        dom.set_inner_html(guide_layer,join([for (g in guide_scene.items) format(
            <div class:"raster-guide guide-" ++ g.axis,'data-raster-guide':g.id,role:"separator",
                'aria-label':(if (g.axis == "x") "Vertical" else "Horizontal") ++ " guide at " ++ string(g.position),
                style:if (g.axis == "x") "left:" ++ string(g.position*scale) ++ "px;height:" ++ string(size.height*scale) ++ "px"
                    else "top:" ++ string(g.position*scale) ++ "px;width:" ++ string(size.width*scale) ++ "px",<span>>,'xml')],""))
        dom.style_set_property(guide_layer,"display",if (st.guides_visible) "block" else "none")
        dom.style_set_property(guide_layer,"pointer-events",if (guide_scene.interactive) "auto" else "none")
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
    let navigation_state = publish_navigation(owner,st,rendered,viewport_box,camera)^
    return {*:st,*:navigation_state,painted_doc:st.doc,painted_raster:rendered,painted_channel:st.channel_view,painted_selection:st.selection,
        painted_geometry:geometry,painted_bounds:box,painted_crop:crop_box,painted_handles:handles_visible,
        painted_guides:guide_scene,painted_reference:reference_scene,scale:scale}
}
