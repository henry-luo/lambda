// selection chrome reads native range geometry; formatting stays in model commands (D7.2.5).
import dom
import lambda.editor.mod_source_pos

pub fn available(editor) => editor.selection.kind=='text' and
    editor.selection.anchor!=editor.selection.head and selection_to_string(editor.doc,editor.selection)!=""
pub fn visible(st) => not st.read_only and st.dialog==null and st.context==null and
    not st.mini_hidden and available(st.editor)

// use visible selection fragments so a scrolled multiline range never anchors in the ribbon.
pub fn placement(rects,viewport,size,gap=8) {
    let visible = [for (r in rects where r.width>0 and r.height>0 and r.right>viewport.left and
        r.left<viewport.right and r.bottom>viewport.top and r.top<viewport.bottom) r]
    let first = visible[0]
    let tail = visible[last]
    let above = first.top-size.height-gap
    let below = tail.bottom+gap;
    if (first==null or viewport.right-viewport.left<size.width or viewport.bottom-viewport.top<size.height) null
    else {left:max(viewport.left,min(first.left,viewport.right-size.width)),
        top:max(viewport.top,min(if (above>=viewport.top) above else below,viewport.bottom-size.height))}
}
pub fn measure(node) {
    // a formatting redraw can retire the frame's target; measure the owning document's live toolbar.
    let root = dom.document_element(node)
    let toolbar = dom.get_element_by_id(root,"selection-toolbar")
    if (toolbar==null) null else {
        let selection = dom.document_selection(node)
        let bounds = dom.selection_boundaries(selection)
        let host = dom.get_element_by_id(root,"edit-surface")
        let stage = dom.bounding_box(dom.get_element_by_id(root,"stage"))
        let window = dom.bounding_box(dom.get_element_by_id(root,"word"))
        let viewport = {left:max(8,stage.x+8),top:max(8,stage.y+8),
            right:min(window.width-8,stage.x+stage.width-8),bottom:min(window.height-8,stage.y+stage.height-8)}
        let owned = dom.selection_get_range_count(selection)>0 and not dom.selection_get_is_collapsed(selection) and
            dom.contains(host,bounds.anchor_node) and dom.contains(host,bounds.focus_node)
        let rects = if (owned) dom.range_get_client_rects(dom.selection_get_range_at(selection,0)) else []
        let point = placement(rects,viewport,dom.bounding_box(toolbar));
        {left:point.left or 0,top:point.top or 0,max_width:max(0,viewport.right-viewport.left),visible:point!=null}
    }
}
pn request(node,event) {
    let root = dom.document_element(node)
    let owner = dom.get_element_by_id(root,"word")
    if (owner!=null) dom.request_frame(owner,event)
}
pub pn refresh(node) { request(node,"word_toolbar_ready") }
fn frame_style(box) => "left:" ++ string(box.left or 0) ++ "px;top:" ++ string(box.top or 0) ++ "px;visibility:" ++
    (if (box.visible) "visible" else "hidden") ++
    (if (box.max_width==null) "" else ";max-width:" ++ string(box.max_width) ++ "px")
// viewport movement updates presentation geometry without a document-state redraw.
pub pn position(node) {
    let toolbar = dom.get_element_by_id(dom.document_element(node),"selection-toolbar")
    if (toolbar!=null) {
        let style = frame_style(measure(node))
        // placement is presentation state; it must not recreate the gesture's template inputs.
        if (dom.get_attribute(toolbar,"style")!=style) dom.set_attribute(toolbar,"style",style)
    }
}
pub fn frame(rendered) =>
    <div id:"selection-toolbar",class:"selection-toolbar",role:"toolbar",["aria-label"]:"Selection formatting",
        style:frame_style(null),*rendered>
