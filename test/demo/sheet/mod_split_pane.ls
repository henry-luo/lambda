// Split-divider gestures preview in the view and commit one workbook change.
import dom
import m: .mod_model
import capture: lambda.ui.core.splitter

pub fn control(target) => dom.closest(target,"[data-split-resize]")
fn current(st,axis) => (if (axis=="x") m.current(st).split.x else m.current(st).split.y)/15
fn changed(st,axis,pixels) any^ {
    let sheet=m.current(st)
    let split={*:sheet.split,*:map([axis,pixels*15])};
    m.change(st,{*:st.book,sheets:[for (i,s in st.book.sheets) if (i==st.sheet) {*:s,split:split} else s]},"Split adjusted")
}
pub pn begin(st,evt,divider) {
    let axis=dom.get_attribute(divider,"data-split-resize")
    let box=dom.bounding_box(dom.closest(divider,".split-frame"))
    let maximum=max(50,(if (axis=="x") box.width else box.height)-56)
    let gesture=capture.capture(divider,evt,divider)
    return {*:st,split_drag:{*:gesture,axis:axis,id:dom.get_attribute(divider,"id"),
        start:if (axis=="x") evt.clientX else evt.clientY,original:current(st,axis),value:current(st,axis),maximum:maximum}}
}
pub fn move(st,x,y) {
    let drag=st.split_drag
    let delta=(if (drag.axis=="x") x else y)-drag.start
    let value=if (delta==0) drag.original else max(50,min(drag.maximum,drag.original+delta));
    {*:st,split_drag:{*:drag,value:value}}
}
pub pn finish(owner,st,cancel=false) any^ {
    let drag=st.split_drag
    if (drag==null) { return st }
    let divider=dom.get_element_by_id(dom.root_node(owner),drag.id)
    if (divider!=null) capture.release(divider,drag)^
    let next={*:st,split_drag:null}
    if (cancel or drag.value==drag.original) { return next }
    return changed(next,drag.axis,drag.value)^
}
pub fn keyboard(st,axis,key,maximum,step=20) any^ {
    let amount=if (key in ["ArrowRight","ArrowDown"]) step else -step
    let value=if (key=="Home") 50 else if (key=="End") maximum else current(st,axis)+amount;
    changed(st,axis,max(50,min(maximum,value)))^
}
