// S12.1.3 / D10.2.1: range gestures own model selection; the host supplies pointer transport.
import dom
import m: .mod_model
import capture: lambda.ui.core.splitter
import window: .mod_window
import gesture: lambda.ui.core.virtual_grid_gesture

fn port(owner) => dom.get_element_by_id(dom.root_node(owner),"grid-scroll")
pub fn cell(target) {
    let hit=dom.closest(target,".sheet-grid td[data-row][data-col]")
    if (hit==null) null else {row:int(dom.get_attribute(hit,"data-row")),col:int(dom.get_attribute(hit,"data-col"))}
}
pub pn begin(owner,st,evt) {
    let hit=cell(evt.target)
    let handle=dom.closest(evt.target,"[data-selection-handle]")!=null
    let bounds=m.bounds(st)
    // the corner handle resizes the existing range from its top-left cell.
    let base=if (handle) {*:st,anchor:{row:bounds.top,col:bounds.left}} else st
    let next=m.select(base,hit.row,hit.col,handle or evt.shiftKey==true)
    let gesture=capture.capture(dom.closest(evt.target,".grid-scroll"),evt,dom.closest(evt.target,".sheet-grid"))
    return {*:next,selection_click:true,selection_drag:{*:gesture,
        previous:{row:st.row,col:st.col,anchor:st.anchor}}}
}
pub pn move(owner,st,x,y,final=false,edge_tick=false) {
    let root=dom.get_element_by_id(dom.root_node(owner),"sheet")
    let axes=if (edge_tick) window.axes(st)^ else null
    let update=gesture.move(root,port(owner),st.selection_drag,x,y,final,edge_tick,axes,
        44,26,".sheet-grid td[data-row][data-col]","sheet_drag_scroll")^
    let next={*:st,selection_drag:update.gesture}
    if ((update.hit==null or (update.hit.row==st.row and update.hit.col==st.col))^) { return next }
    return m.select(next,update.hit.row,update.hit.col,true)
}
pub pn finish(owner,st,cancel=false) {
    let gesture=st.selection_drag
    if (gesture==null) { return st }
    capture.release(port(owner),gesture)
    let next={*:st,selection_drag:null}
    // Restore the selection through the model so its cached statistics agree.
    if (cancel) {
        let anchor=gesture.previous.anchor
        return m.select(m.select(next,anchor.row,anchor.col),gesture.previous.row,gesture.previous.col,true)
    }
    return next
}
