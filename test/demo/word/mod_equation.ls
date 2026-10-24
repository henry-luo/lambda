// Keep editable LaTeX beside a self-contained typeset SVG (D7.2.5).
import mathdoc: lambda.doc.math.math
import objects: .mod_objects
import colors: lambda.ui.core.component
import lambda.editor.mod_doc
import lambda.editor.mod_step
import lambda.editor.mod_transaction
import image: lambda.editor.mod_image
import css: lambda.editor.mod_inline_format
import layout: .mod_object_layout
import catalog: .mod_equation_catalog

pub let examples = catalog.examples
pub let defaults = {source:"a^2+b^2=c^2",size:22,color:"#24334b",display:true,justify:"center",linear:false,number:""}
pub fn settings(n) => objects.settings(n,"data-word-equation",defaults)
pub fn render(p) map^ {
    if (not (p.source is string) or trim(p.source)=="" or len(p.source)>4000 or
        not (p.size is number) or p.size<10 or p.size>72 or not colors.hex_color(p.color) or not (p.display is bool) or
        not contains(["left","center","right"],p.justify) or not (p.linear is bool) or
        not (p.number is string) or len(p.number)>32 or contains(p.number,"\n"))
        raise error("Word: enter a LaTeX equation (up to 4000 characters), size 10–72 px, and a #RRGGBB color")
    else {
        let ast = parse(p.source,{type:"math",flavor:"latex"})^
        let unknown = mathdoc.unmapped_commands(ast)
        if (len(unknown)>0) raise error("Word: unsupported equation command: " ++ join(unknown,", ")) else {
            let options = {display:p.display==true,font_size:p.size,color:p.color}
            let measured = mathdoc.render_box(if (p.linear) <math <text_group p.source>> else ast,options)^
            let label = if (p.number=="") null else mathdoc.render_box(<math <text_group p.number>>,options)^
            let body_width = measured.width*p.size
            let body_height = (measured.height+measured.depth)*p.size
            let label_width = if (label==null) 0 else label.width*p.size
            let label_height = if (label==null) 0 else (label.height+label.depth)*p.size
            let width = max(1,ceil(body_width+(if (label==null) 0 else p.size+label_width)))
            let height = max(1,ceil(max(body_height,label_height)))
            if (width>650 or height>1000) raise error("Word: equation is too large; reduce its size or source")
            else {width:width,height:height,element:if (label==null) measured.element else
                <svg xmlns:"http://www.w3.org/2000/svg",width:width,height:height,viewBox:"0 0 " ++ string(width) ++ " " ++ string(height),
                    <g transform:"translate(0 " ++ string((height-body_height)/2) ++ ")",measured.element>
                    <g transform:"translate(" ++ string(body_width+p.size) ++ " " ++ string((height-label_height)/2) ++ ")",label.element>>}
        }
    }
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
