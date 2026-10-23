// Generated objects share image ownership, selection, and history (S1.4, D7.2.5).
import lambda.editor.mod_doc
import lambda.editor.mod_step
import lambda.editor.mod_transaction
import commands: lambda.editor.mod_commands
import image: lambda.editor.mod_image
import media: lambda.doc.media
import inline_format: lambda.editor.mod_inline_format

pub fn settings(n,key,defaults=null) {
    let stored = parse(attrs_get(n.attrs,symbol(key)) or "null",'json') ^ { null }
    if (not (stored is map)) defaults else if (defaults==null) stored else {*:defaults,*:stored}
}
pub fn source(graphic) string^ => media.data_uri(binary(format(graphic,'xml')^),"image/svg+xml")^
pub fn rotate_transaction(tx,path,angle) {
    let css = attrs_get(node_at(tx.doc_after,path).attrs,'style')
    let style = inline_format.css_set(css,"transform",if (angle==0) null else "rotate(" ++ string(angle) ++ "deg)")
    tx_step(tx_step(tx,step_set_attr(path,'style',style)),step_set_attr(path,'data-word-rotation',angle))
}
pub fn transaction(editor,key,p,graphic,width,height,label,selected=null) map^ {
    let src = source(graphic)^
    let tx = if (selected==null) commands.cmd_insert_image(editor,src,label) else tx_begin(editor.doc,editor.selection)
    if (tx==null) raise error("Word: an object cannot be inserted at this selection") else {
        let path = if (selected==null) tx.sel_after.path else selected.path
        let updated = tx_step(tx_step(tx_step(image.resize_transaction(tx,path,width,height),step_set_attr(path,'src',src)),
            step_set_attr(path,'alt',label)),step_set_attr(path,symbol(key),format(p,'json')^))
        if (p.rotation==null) updated else rotate_transaction(updated,path,p.rotation)
    }
}
