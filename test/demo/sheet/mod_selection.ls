// S12.1.3 / D7.2.5: range gestures own model selection; the host supplies pointer transport.
import dom
import m: .mod_model
import capture: ~~.mod_splitter

fn port(owner) => dom.get_element_by_id(dom.root_node(owner),"grid-scroll")
pub fn cell(target) {
    let hit=dom.closest(target,"#sheet-grid td[data-row][data-col]")
    if (hit==null) null else {row:int(dom.get_attribute(hit,"data-row")),col:int(dom.get_attribute(hit,"data-col"))}
}
pub pn begin(owner,st,evt) {
    let hit=cell(evt.target)
    let handle=dom.closest(evt.target,"[data-selection-handle]")!=null
    let bounds=m.bounds(st)
    // the corner handle resizes the existing range from its top-left cell.
    let base=if (handle) {*:st,anchor:{row:bounds.top,col:bounds.left}} else st
    let next=m.select(base,hit.row,hit.col,handle or evt.shiftKey==true)
    dom.focus_set(dom.get_element_by_id(dom.root_node(owner),"sheet-grid"),false)
    dom.set_pointer_capture(port(owner),evt.pointerId)
    return {*:next,selection_click:true,selection_drag:{pointer:evt.pointerId,
        previous:{row:st.row,col:st.col,anchor:st.anchor}}}
}
pub pn move(owner,st,x,y) {
    // captured moves target the scrollport; hit-test the pointer to find the cell underneath.
    let hit=cell(dom.element_from_point(dom.owner_document(owner),x,y))
    if (hit==null or (hit.row==st.row and hit.col==st.col)) { return st }
    return m.select(st,hit.row,hit.col,true)
}
pub pn finish(owner,st,cancel=false) {
    let gesture=st.selection_drag
    if (gesture==null) { return st }
    capture.release(port(owner),gesture)
    return {*:st,selection_drag:null,row:if (cancel) gesture.previous.row else st.row,
        col:if (cancel) gesture.previous.col else st.col,anchor:if (cancel) gesture.previous.anchor else st.anchor}
}
