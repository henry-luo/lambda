// S12.1.3 / D4.5.2: workers own native bytes; completed arrays are copied into retained canvases.
import dom
import radiant
import updates: ~~.mod_publish

let EMPTY = as_ubyte([[[0,0,0,0]]])
fn canvas(slot) => dom.query_selector(slot,"canvas")
fn box_key(box) => string(box.width) ++ "/" ++ string(box.height)
fn measure(owner,slot,viewport) {
    let role = dom.get_attribute(slot,"data-thumb-role")
    let box = dom.bounding_box(slot)
    let row = dom.bounding_box(dom.closest(slot,".file"))
    let column = dom.closest(slot,".column-list")
    let scrollport = if (column != null) dom.bounding_box(column) else viewport
    let visible = box.width>0 and box.height>0 and
        (role != "look" or dom.computed_style(dom.get_element_by_id(owner,"quick-look"),"display") != "none") and
        (role != "preview" or dom.computed_style(dom.get_element_by_id(owner,"preview-pane"),"display") != "none") and
        (role != "tile" or (row.y+row.height >= scrollport.y-row.height and row.y <= scrollport.y+scrollport.height+row.height and
            row.x+row.width>viewport.x and row.x<viewport.x+viewport.width));
    {slot:slot,box:box,role:role,visible:visible,status:dom.get_attribute(slot,"data-thumb-state")}
}
pn reset_slot(slot) {
    let target = canvas(slot)
    if (dom.get_attribute(target,"width") != "1" or dom.get_attribute(target,"height") != "1") dom.set_canvas_pixels(target,EMPTY)^
    updates.attr(slot,"data-thumb-state","pending")
    if (dom.get_attribute(slot,"data-thumb-box") != null) dom.remove_attribute(slot,"data-thumb-box")
}
pub pn release(region) {
    for (slot in dom.query_selector_all(region,".thumbnail-slot")) reset_slot(slot)
}
pub pn cancel_jobs(owner,st,invalidate=false) {
    if (st.frame>0) dom.cancel_frame(owner,st.frame)
    radiant.image_job(owner,{action:if (invalidate) "invalidate" else "cancel",channel:"thumbnail"})^
    // ancestor columns survive navigation; a cancelled decode must leave their slots eligible to retry.
    if (st.request != null and dom.contains(owner,st.request.slot)) reset_slot(st.request.slot)
    return {frame:0,job:0,request:null,generation:st.generation+1}
}
pub pn schedule(owner,st,measure=true) {
    return {*:st,dirty:st.dirty or measure,frame:if (st.frame>0) st.frame else dom.request_frame(owner,"finder_thumbnails")}
}
pub pn tick(owner,st,reflow=false) {
    var next = {*:st,frame:0}
    let changed = reflow or next.dirty or next.slots == null
    let viewport = if (changed) dom.bounding_box(dom.get_element_by_id(owner,"files")) else null
    // upload does not resize fixed graphic slots; poll frames reuse their geometry until the viewport changes.
    let slots = if (changed) [for (slot in dom.query_selector_all(owner,".thumbnail-slot")) measure(owner,slot,viewport)] else next.slots
    let targets = [for (item in slots where item.visible order by if (item.role == "look") 0 else if (item.role == "preview") 1 else 2) item]
    next = {*:next,dirty:false,slots:slots}
    if (changed) for (item in slots) {
        if ((not item.visible and item.status == "ready") or
            (item.status == "ready" and dom.get_attribute(item.slot,"data-thumb-box") != box_key(item.box))) reset_slot(item.slot)
    }
    if (next.job>0) {
        let request = next.request
        let current = [for (item in targets where dom.same_node(item.slot,request.slot)) item][0]
        if (current == null or request.generation != next.generation or request.box != box_key(current.box)) {
            next = cancel_jobs(owner,next)
            if (dom.contains(owner,request.slot)) reset_slot(request.slot)
        } else {
            let done = radiant.image_job(owner,{action:"poll",channel:"thumbnail",token:next.job})^
            if (done != null) {
                let slot = request.slot
                var ready = done.success
                if (ready) {
                    dom.set_canvas_pixels(canvas(slot),done.pixels) ^ { ready = false }
                    let scale = min(1.0,min(current.box.width/done.width,current.box.height/done.height))
                    dom.style_set_property(canvas(slot),"width",string(done.width*scale) ++ "px")
                    dom.style_set_property(canvas(slot),"height",string(done.height*scale) ++ "px")
                }
                updates.attr(slot,"data-thumb-state",if (ready) "ready" else "error")
                updates.attr(slot,"data-thumb-box",request.box)
                updates.attr(slot,"data-thumb-cache-hit",done.cache_hit)
                updates.attr(slot,"title",if (ready) "" else done.error.message or "Thumbnail unavailable")
                updates.attr(owner,"data-thumbnail-cache-bytes",done.cache_bytes or 0)
                next = {*:next,job:0,request:null}
            }
        }
    }
    if (next.job==0) {
        let pending = [for (item in targets where dom.get_attribute(item.slot,"data-thumb-state") == "pending") item]
        if (len(pending)>0) {
            let item = pending[0]
            let retained = sum([for (slot in dom.query_selector_all(owner,".thumbnail-slot"))
                int(dom.get_attribute(canvas(slot),"width"))*int(dom.get_attribute(canvas(slot),"height"))*4])
            let job = radiant.image_job(owner,{action:"start",channel:"thumbnail",path:dom.get_attribute(item.slot,"data-thumb-path"),
                width:item.box.width,height:item.box.height,canvas_bytes:retained})^
            updates.attr(item.slot,"data-thumb-state","loading")
            next = {*:next,job:job,request:{slot:item.slot,box:box_key(item.box),generation:next.generation}}
        }
    }
    updates.attr(owner,"data-thumbnail-loading",next.job>0)
    return if (next.job>0) schedule(owner,next,false) else next
}
