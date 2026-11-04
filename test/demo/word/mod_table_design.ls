// Table appearance is source metadata; CSS paints the same options on screen and in exports (S1.4, D7.2.5).
import lambda.editor.core.doc
import lambda.editor.core.step
import lambda.editor.core.transaction
import format: lambda.editor.core.inline_format
import colors: lambda.ui.core.component
import tables: .mod_table
import design: lambda.editor.table.design

pub let options = [
    {id:"header",label:"Header Row",attr:'data-word-header-row'},
    {id:"total",label:"Total Row",attr:'data-word-total-row'},
    {id:"bandedRows",label:"Banded Rows",attr:'data-word-banded-rows'},
    {id:"first",label:"First Column",attr:'data-word-first-column'},
    {id:"last",label:"Last Column",attr:'data-word-last-column'},
    {id:"bandedColumns",label:"Banded Columns",attr:'data-word-banded-columns'}]
pub let attributes = ['data-word-table-style',*[for (entry in options) entry.attr]]
pub let styles = design.styles
pub let pen = {style:"solid",width:0.75,color:"#64748b"}
pub let palette = [{value:"none",label:"No Color"},{value:"#ffffff",label:"White"},{value:"#eff6ff",label:"Light blue"},
    {value:"#dbeafe",label:"Blue"},{value:"#f0fdfa",label:"Light teal"},{value:"#f5f3ff",label:"Light purple"},
    {value:"#fff7ed",label:"Light orange"},{value:"#fef9c3",label:"Light yellow"},{value:"#ffe4e6",label:"Light rose"},
    {value:"#e2e8f0",label:"Gray"},{value:"#64748b",label:"Gray ink"},{value:"#2563eb",label:"Accent blue"},{value:"#334155",label:"Slate"}]
pub fn color_options(current,transparent=true) => [*[for (entry in palette where transparent or entry.value!="none") entry],
    *if (colors.hex_color(current) and not contains([for (entry in palette) entry.value],current)) [{value:current,label:current}] else []]
pub let style = design.style
pub fn settings(n) {
    let id = attrs_get(n.attrs,'data-word-table-style') or "grid"
    let first_row = [for (entry in descendants(n) where entry.node.tag=='tr') entry.node][0]
    let defaults = {header:any([for (cell in first_row.content) cell.tag=='th']),total:false,first:false,last:false,
        bandedRows:id=="soft",bandedColumns:false};
    {style:id,*:map([for (entry in options,let raw=attrs_get(n.attrs,entry.attr)) for (part in [entry.id,
        if (raw==null) defaults[entry.id] else raw==true or raw=="true"]) part])}
}
pub fn transaction(editor,p) map^ {
    let ctx = tables.context(editor)
    let current = settings(ctx.table)
    // legacy style-only tables adopt the preset's bands once; explicit checkbox choices survive later styles.
    let preset = if (p.style!=null and attrs_get(ctx.table.attrs,'data-word-banded-rows')==null) {bandedRows:p.style=="soft"} else {}
    let changes = if (p.option!=null) {[p.option]:p.value} else {*:preset,*:p}
    let chosen = {*:current,*:changes}
    if (ctx==null) raise error("Word: place the caret inside a table first")
    else if (style(chosen.style)==null or (p.option!=null and not contains([for (entry in options) entry.id],p.option)) or
        not all([for (entry in options) chosen[entry.id] is bool]))
        raise error("Word: choose a table style and valid row/column options")
    else {
        let tx = tx_step(tx_begin(editor.doc,editor.selection),step_set_attr(ctx.table_path,'data-word-table-style',chosen.style));
        reduce([tx,*options],(t,entry)any^ =>tx_step(t,step_set_attr(ctx.table_path,entry.attr,string(chosen[entry.id]))))
    }
}
pub fn shading_value(n) any^ => format.css_get(attrs_get(n.attrs,'style'),"background-color") or
    format.css_get(attrs_get(n.attrs,'style'),"background") or "none"
pub fn shade_transaction(editor,cells,p) map^ {
    if (len(cells)==0) raise error("Word: select a table cell first")
    else if (p.color!="none" and not colors.hex_color(p.color)) raise error("Word: choose No Color or a #RRGGBB shading color")
    else reduce([tx_begin(editor.doc,editor.selection),*cells],(tx,cell)any^ =>tx_step(tx,step_set_attr(cell.path,'style',
        format.css_set(format.css_set(attrs_get(node_at(tx.doc_after,cell.path).attrs,'style'),"background",null),
            "background-color",if (p.color=="none") null else p.color))))
}
pub let cell_paint = design.cell_paint
pub fn thumbnail(p) => <svg xmlns:"http://www.w3.org/2000/svg",width:68,height:48,viewBox:"0 0 68 48",'aria-hidden':"true",
    *[for (r in 0 to 3) for (c in 0 to 3,let paint=cell_paint(p,r,c,4,4)) <g
        <rect x:2+c*16,y:2+r*11,width:16,height:11,fill:paint.fill,stroke:paint.border,'stroke-width':0.6>
        <path d:"M" ++ string(5+c*16) ++ " " ++ string(7+r*11) ++ "h9",stroke:paint.color,'stroke-width':if (paint.weight==600) 1.3 else 0.7>>]>
fn cell_selectors(root,row="",column="*") => [for (group in ["",">thead",">tbody",">tfoot"]) root ++ group ++ ">tr" ++ row ++ ">" ++ column]
fn rule(selectors,body) => join(selectors,",") ++ "{" ++ body ++ "}"
fn edge_selectors(root,ending=false) {
    let edge = if (ending) ":last-child" else ":first-child";
    [root ++ ">tr" ++ edge ++ ">*",*[for (group in ["thead","tbody","tfoot"]) root ++ ">" ++ group ++ edge ++ ">tr" ++ edge ++ ">*"]]
}
pub let css = join([for (s in styles,let root=".word-content table[data-word-table-style=" ++ s.id ++ "][data-word-header-row]")
    join([
        rule(cell_selectors(root),"background:#fff;color:#334155;font-family:inherit;font-weight:400;border:0.75pt solid " ++ s.border ++ ";" ++
            if (s.minimal) "border-left:0;border-right:0;" else ""),
        rule(cell_selectors(root ++ "[data-word-banded-rows=true]",":nth-child(even)"),"background:" ++ s.band ++ ";"),
        rule(cell_selectors(root ++ "[data-word-banded-columns=true]","","*:nth-child(even)"),"background:" ++ s.band ++ ";"),
        *[for (option in [{attr:"first-column",edge:":first-child"},{attr:"last-column",edge:":last-child"}])
            rule(cell_selectors(root ++ "[data-word-" ++ option.attr ++ "=true]","","*" ++ option.edge),"font-weight:600;background:" ++ s.band ++ ";")],
        rule(edge_selectors(root ++ "[data-word-total-row=true]",true),"font-weight:600;background:" ++ s.band ++ ";border-top:1.5pt solid " ++ s.border ++ ";"),
        rule(edge_selectors(root ++ "[data-word-header-row=true]"),"font-weight:600;background:" ++ s.head ++ ";color:" ++ s.ink ++ ";")],"\n")],"\n")
