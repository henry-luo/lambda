// Image selection and sizing are model transactions, so one resize is one undo (D7.2.5).
import .mod_doc
import .mod_step
import .mod_source_pos
import .mod_transaction
import .mod_editor

fn image_node(n) => is_node(n) and contains(['img','image'],n.tag)
pub fn images(doc) => [for (entry in descendants(doc) where image_node(entry.node)) entry]
pub fn selected(editor) {
    let path = editor.selection.path
    let n = if (editor.selection.kind=='node') node_at(editor.doc,path) else null
    if (image_node(n)) {path:path,node:n} else null
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
