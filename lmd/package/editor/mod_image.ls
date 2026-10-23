// Image selection and sizing are model transactions, so one resize is one undo (D7.2.5).
import .mod_doc
import .mod_step
import .mod_source_pos
import .mod_transaction
import .mod_editor
import commands: .mod_commands
import adapter: .mod_dom_adapter
import dom

fn image_node(n) => is_node(n) and contains(['img','image'],n.tag)
pub fn images(doc) => [for (entry in descendants(doc) where image_node(entry.node)) entry]
pub fn selected(editor) {
    let path = editor.selection.path
    let n = if (editor.selection.kind=='node') node_at(editor.doc,path) else null
    if (image_node(n)) {path:path,node:n} else null
}
pub pn image_element(editor,surface,path=null) {
    let target = if (path==null) selected(editor).path else path
    let entries = [for (entry in images(editor.doc) where entry.node.tag=='img') entry]
    let index = [for (i,entry in entries where entry.path==target) i][0]
    return if (surface==null or index==null) null else dom.query_selector_all(surface,"img")[index]
}
// The projection preserves image order; a press selects its model atom before native caret placement.
pub pn hit(editor,node,surface) {
    let target = dom.closest(node,"img")
    if (target==null or surface==null or not dom.contains(surface,target)) { return null }
    let nodes = dom.query_selector_all(surface,"img")
    let index = [for (i,n in nodes where n==target) i][0]
    return [for (entry in images(editor.doc) where entry.node.tag=='img') entry.path][index]
}
pub fn select_image(editor,path) => if (image_node(node_at(editor.doc,path)))
    edit_set_selection(editor,node_selection(path)) else editor
pub fn resize_transaction(tx,path,width,height) =>
    tx_step(tx_step(tx,step_set_attr(path,'width',width)),step_set_attr(path,'height',height))
pub fn resize_image(editor,path,width,height) map^ {
    let image = node_at(editor.doc,path)
    let natural = width==null and height==null
    if (not image_node(image)) raise error("Select an image first.")
    else if (not natural and not (width is number and height is number and width>=16 and width<=4096 and height>=1 and height<=16384))
        raise error("Image width must be between 16 and 4096 pixels.")
    else edit_apply(editor,resize_transaction(tx_set_selection(tx_begin(editor.doc,editor.selection),node_selection(path)),path,width,height))
}

pub fn move_transaction(editor,path,target) => if (not image_node(node_at(editor.doc,path))) null
    else commands.cmd_move_inline_node(editor,path,target)
pub fn move_image(editor,path,target) {
    let tx = move_transaction(editor,path,target)
    if (tx==null) editor else edit_apply(editor,tx)
}

// the model validates the move; a separate DOM range measures its preview caret.
pub pn drop_target(editor,path,evt,surface) {
    let box = dom.bounding_box(surface)
    if (evt.clientX<box.x or evt.clientX>=box.x+box.width or evt.clientY<box.y or evt.clientY>=box.y+box.height) { return null }
    // capture can continue outside the visible editor; hidden document text is not a drop target.
    let hit = dom.element_from_point(dom.owner_document(surface),evt.clientX,evt.clientY)
    if (hit==null or not dom.contains(surface,hit)) { return null }
    let target = adapter.normalize_source_pos(editor.doc,evt.source_pos)
    if (move_transaction(editor,path,target)==null) { return null }
    let boundary = dom.boundary_from_point(surface,int(evt.clientX),int(evt.clientY),null)
    if (boundary==null or not dom.contains(surface,boundary.node)) { return null }
    let caret_range = dom.document_create_range(dom.root_node(surface))
    dom.range_set_start(caret_range,boundary.node,boundary.offset)
    dom.range_collapse(caret_range,true)
    let caret = dom.range_get_bounding_client_rect(caret_range)
    return if (caret.height>0) {target:target,caret:caret} else null
}
