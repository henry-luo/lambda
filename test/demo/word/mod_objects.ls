// Generated objects share image ownership, selection, and history (S1.4, D7.2.5).
import lambda.editor.mod_doc
import lambda.editor.mod_step
import lambda.editor.mod_transaction
import commands: lambda.editor.mod_commands
import image: lambda.editor.mod_image
import media: lambda.doc.media

pub fn settings(n,key,defaults=null) {
    let stored = parse(attrs_get(n.attrs,symbol(key)) or "null",'json') ^ { null }
    if (not (stored is map)) defaults else if (defaults==null) stored else {*:defaults,*:stored}
}
pub fn transaction(editor,key,p,graphic,width,height,label,selected=null) map^ {
    let src = media.data_uri(binary(format(graphic,'xml')^),"image/svg+xml")^
    let tx = if (selected==null) commands.cmd_insert_image(editor,src,label) else tx_begin(editor.doc,editor.selection)
    if (tx==null) raise error("Word: an object cannot be inserted at this selection") else {
        let path = if (selected==null) tx.sel_after.path else selected.path
        tx_step(tx_step(tx_step(image.resize_transaction(tx,path,width,height),step_set_attr(path,'src',src)),
            step_set_attr(path,'alt',label)),step_set_attr(path,symbol(key),format(p,'json')^))
    }
}
