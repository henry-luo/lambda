// Keep editable LaTeX beside a self-contained typeset SVG (D7.2.5).
import rendering: lambda.editor.math.render
import objects: .mod_objects
import lambda.editor.core.doc
import lambda.editor.core.step
import lambda.editor.core.transaction
import image: lambda.editor.image
import css: lambda.editor.core.inline_format
import layout: .mod_object_layout
import catalog: lambda.editor.math.catalog
import field: .mod_math_field

pub let examples = catalog.examples
pub let defaults = {source:"a^2+b^2=c^2",size:22,color:"#24334b",display:true,justify:"center",linear:false,number:""}
pub fn settings(n) any^ => objects.settings(n,"data-word-equation",defaults)
pub fn render(p) map^ => rendering.render(p,650,1000,"Word")^
pub let field_options = rendering.field_options
// caret slots in the stored image's pixels; a numbered equation centres its body vertically.
pub fn caret_geometry(p,root) map^ {
    let measured = render(p)^
    let slots = field.geometry(root,field_options(p))^;
    {width:measured.width,height:measured.height,slots:[for (g in slots)
        {*:g,top:g.top+measured.body_top,bottom:g.bottom+measured.body_top}]}
}
pub fn transaction(editor,params,selected=null) map^ {
    let p = {*:defaults,*:params}
    let measured = render(p)^
    let tx = objects.transaction(editor,"data-word-equation",p,measured.element,measured.width,measured.height,p.source,selected)^
    let path = tx.sel_after.path
    let n = node_at(tx.doc_after,path)
    let flow = {*:layout.settings(n),wrap:if (p.display) "topBottom" else "inline"}
    // display affects document flow as well as math style; screen and exports share this source CSS.
    let style = css.css_set_many(layout.style(n,flow),{
        ["margin-left"]:if (p.display and p.justify!="left") "auto" else "0",
        ["margin-right"]:if (p.display and p.justify!="right") "auto" else "0"});
    tx_step(tx_step(tx,step_set_attr(path,'style',style)),step_set_attr(path,layout.key,format(flow,'json')^))
}
pub fn format_transaction(editor,params) map^ {
    let selected = image.selected(editor)
    let p = settings(selected.node)
    if (p==null) raise error("Word: select an equation first")
    else {
        let tx = transaction(editor,{*:p,*:params},selected)^
        // Apply/Close can reassert the same graphic; an unchanged equation is not an undo step.
        if (tx.doc_after==editor.doc) tx_set_meta(tx,"addToHistory",false) else tx
    }
}
