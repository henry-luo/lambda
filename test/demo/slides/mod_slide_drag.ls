// S1.4 / S12.1.3: preview a thumbnail gesture without changing the authored deck.
import dom
import m: .mod_model
import c: lambda.slide.common
import u: ~~.mod_publish
import capture: ~~.mod_splitter

fn node(owner,id) => dom.get_element_by_id(owner,id)
pub fn thumbnail(owner,index) => dom.query_selector(node(owner,"thumbnails"),"[data-slide=\"" ++ string(index) ++ "\"]")

pub pn start(owner,st,evt,index) {
    let port = node(owner,"thumbnails")
    dom.focus_set(port,false)
    dom.set_pointer_capture(port,evt.pointerId)
    return {*:st,editing:null,slide_drag:{from:index,x:evt.clientX,y:evt.clientY,
        px:evt.clientX,py:evt.clientY,pointer:evt.pointerId,active:false,to:index,token:0}}
}

pub pn preview(owner,st,x,y,tick=false) {
    let drag = st.slide_drag
    if (drag==null or (not drag.active and max(abs(x-drag.x),abs(y-drag.y))<5.0)) { return st }
    let port = node(owner,"thumbnails")
    let box = dom.bounding_box(port)
    let inside = x>=box.x and x<box.x+box.width and y>=box.y and y<box.y+box.height
    let offset = dom.scroll_state(port)
    let metrics = dom.scroll_metrics(port)
    let step = if (not inside) 0.0 else if (y<box.y+32.0) -12.0 else if (y>box.y+box.height-32.0) 12.0 else 0.0
    let top = max(0.0,min(max(0.0,metrics.scroll_height-metrics.client_height),offset.y+step))
    let scrolling = top!=offset.y
    if (tick and scrolling) dom.set_scroll_state(port,offset.x,top)
    // captured moves target the port; committed thumbnail geometry supplies the actual insertion slot.
    let boxes = [for (item in dom.query_selector_all(port,"[data-slide]")) dom.bounding_box(item)]
    let slot = len([for (bounds in boxes where y>=bounds.y+bounds.height/2.0) bounds])
    let target = if (inside) slot-(if (slot>drag.from) 1 else 0) else null
    let line = if (slot<len(boxes)) boxes[slot].y else boxes[last].y+boxes[last].height
    let panel = dom.bounding_box(node(owner,"slide-navigator"))
    u.style(node(owner,"slide-drop-marker"),"top",c.px(max(box.y,min(box.y+box.height-3.0,line))-panel.y))
    u.show(owner,"slide-drop-marker",inside)
    u.attr(thumbnail(owner,drag.from),"data-slide-dragging",true)
    u.attr(owner,"data-slide-reordering",true)
    u.attr(owner,"data-slide-drop",c.value(target,-1))
    u.text(owner,"status",if (inside) "Move slide " ++ string(drag.from+1) ++ " to position " ++ string(target+1) ++ ". Escape cancels."
        else "Release outside the Slides panel to cancel.")
    var token = drag.token
    if (not scrolling and token>0) { dom.cancel_frame(owner,token); token = 0 }
    if (scrolling and token==0) { token = dom.request_frame(owner,"slide_drag_frame") }
    return {*:st,slide_drag:{*:drag,active:true,to:target,px:x,py:y,token:token}}
}

pub pn frame(owner,st,token) {
    let drag = st.slide_drag
    if (drag==null or drag.token!=token) { return st }
    return preview(owner,{*:st,slide_drag:{*:drag,token:0}},drag.px,drag.py,true)
}

pub pn finish(owner,st,cancel=false) {
    let drag = st.slide_drag
    if (drag==null) { return st }
    if (drag.token>0) dom.cancel_frame(owner,drag.token)
    u.show(owner,"slide-drop-marker",false)
    u.attr(owner,"data-slide-reordering",false)
    u.attr(owner,"data-slide-drop",-1)
    u.attr(thumbnail(owner,drag.from),"data-slide-dragging",false)
    capture.release(node(owner,"thumbnails"),drag)
    var next = {*:st,slide_drag:null}
    if (cancel or (drag.active and drag.to==null)) { return next }
    if (not drag.active or drag.to==drag.from) { return m.select_slide(next,drag.from) }
    m.reorder_slide(next,drag.from,drag.to) ^ { next = {*:next,error:^.message} } ~ {
        next = {*:~,status:"Slide moved to position " ++ string(drag.to+1) ++ "."}
    }
    return next
}

pub pn focus(owner,index) {
    let item = thumbnail(owner,index)
    dom.focus_set(item,false)
    dom.scroll_into_view_options(item,{block:"nearest",inline:"nearest"})
}
