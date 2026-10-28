// S12.1.3 / D7.2.5: geometry previews are transient; one completed gesture authors one change.
import dom
import m: .mod_model
import f: .mod_formula
import capture: ~~.mod_splitter

pub fn control(target) => dom.closest(target,"[data-sheet-resize]")
fn port(owner) => dom.get_element_by_id(dom.root_node(owner),"grid-scroll")
pub pn begin(owner,st,evt,divider) {
    let axis=dom.get_attribute(divider,"data-sheet-resize")
    let index=int(dom.get_attribute(divider,"data-index"))
    let box=dom.bounding_box(dom.parent_element(divider))
    let size=(if (axis=="column") box.width else box.height)/st.zoom
    let gesture=capture.capture(port(owner),evt,divider)
    return {*:st,selection_click:true,resize_drag:{*:gesture,axis:axis,index:index,
        start:if (axis=="column") evt.clientX else evt.clientY,original:size,value:size}}
}
pub fn move(st,x,y) {
    let gesture=st.resize_drag
    let limits=m.dimension_limits(gesture.axis)
    let delta=((if (gesture.axis=="column") x else y)-gesture.start)/st.zoom
    // preserve fractional layout measurements when a press/release has not moved.
    let value=if (delta==0) gesture.original else max(limits.min,min(limits.max,round((gesture.original+delta)*10)/10))
    if (value==gesture.value) st else {*:st,resize_drag:{*:gesture,value:value},
        status:(if (gesture.axis=="column") "Column " ++ f.column(gesture.index) else "Row " ++ string(gesture.index+1)) ++ ": " ++ string(value) ++ " px"}
}
pub pn finish(owner,st,cancel=false) {
    let gesture=st.resize_drag
    if (gesture==null) { return st }
    capture.release(port(owner),gesture)
    let next={*:st,resize_drag:null,status:if (cancel) "Resize cancelled" else "Ready"}
    if (cancel or gesture.value==gesture.original) { return next }
    return m.resize(next,gesture.axis,gesture.index,gesture.value) ^ {{*:next,status:string(^.message)}}
}
pub fn handles_key(divider,key) {
    let axis=dom.get_attribute(divider,"data-sheet-resize");
    key in (if (axis=="column") ["ArrowLeft","ArrowRight","Home","End"] else ["ArrowUp","ArrowDown","Home","End"])
}
pub fn keyboard(st,divider,key,step=8) map^ {
    let axis=dom.get_attribute(divider,"data-sheet-resize")
    let index=int(dom.get_attribute(divider,"data-index"))
    let direction=if (key=="ArrowDown") "ArrowRight" else if (key=="ArrowUp") "ArrowLeft" else key
    let value=capture.key_width(m.dimension(m.current(st),axis,index),direction,m.dimension_limits(axis),1,step);
    m.resize(st,axis,index,max(m.dimension_limits(axis).min,min(m.dimension_limits(axis).max,value)))^
}
