// Generated objects share image ownership, selection, and history (S1.4, D7.2.5).
import lambda.editor.mod_doc
import lambda.editor.mod_step
import lambda.editor.mod_transaction
import commands: lambda.editor.mod_commands
import image: lambda.editor.mod_image
import media: lambda.doc.media
import inline_format: lambda.editor.mod_inline_format
import geom: lambda.editor.draw.geometry

pub fn settings(n,key,defaults=null) {
    let stored = parse(attrs_get(n.attrs,symbol(key)) or "null",'json') ^ { null }
    // Missing or malformed metadata must not make an ordinary picture match an object kind.
    if (not (stored is map)) null else if (defaults==null) stored else {*:defaults,*:stored}
}
pub fn source(graphic) string^ => media.data_uri(binary(format(graphic,'xml')^),"image/svg+xml")^
pub fn dimensions_valid(p) => geom.finite_number(p.width) and p.width>=16 and p.width<=650 and
    geom.finite_number(p.height) and p.height>=16 and p.height<=2000 and
    geom.finite_number(p.rotation) and p.rotation>=-180 and p.rotation<=180
pub fn transform_style(n,angle=null,layout=null) {
    let rotation = if (angle==null) (float(attrs_get(n.attrs,'data-word-rotation')) ^ {0}) or 0 else angle
    let p = layout or settings(n,"data-word-layout")
    let parts = [*if (rotation==0) [] else ["rotate(" ++ string(rotation) ++ "deg)"],
        *if (p.flip_x==true or p.flip_y==true) ["scale(" ++ string(if (p.flip_x==true) -1 else 1) ++ "," ++
            string(if (p.flip_y==true) -1 else 1) ++ ")"] else []];
    if (len(parts)==0) null else join(parts," ")
}
pub fn rotate_transaction(tx,path,angle) {
    let n = node_at(tx.doc_after,path)
    let style = inline_format.css_set(attrs_get(n.attrs,'style'),"transform",transform_style(n,angle))
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
