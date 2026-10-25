// S12.1.3: shared pointer capture and separator publishing for retained demo panels.
import dom
import updates: .mod_publish

pub pn publish(panel,divider,size,limits) {
    let requested = string(size) ++ "px"
    let changed = dom.style_get_property(panel,"width") != requested
    if (changed) dom.style_set_property(panel,"width",requested)
    for (key,value in {valuemin:limits.min,valuemax:limits.max,valuenow:round(size)})
        updates.attr(divider,"aria-" ++ string(key),value)
    return changed
}
pub pn start(panel,divider,evt) {
    let gesture = {x:evt.clientX,width:dom.bounding_box(panel).width,pointer:evt.pointerId}
    dom.focus_set(divider,false)
    dom.set_pointer_capture(divider,evt.pointerId)
    return gesture
}
pub pn release(divider,gesture) {
    if (gesture != null and dom.has_pointer_capture(divider,gesture.pointer)) dom.release_pointer_capture(divider,gesture.pointer)
}
pub fn pointer_width(gesture,x,direction=1.0) => gesture.width+direction*(x-gesture.x)
pub fn key_width(width,key,limits,direction=1.0,step=20.0) => if (key == "Home") limits.min else if (key == "End") limits.max
    else width+direction*(if (key == "ArrowRight") step else -step)
