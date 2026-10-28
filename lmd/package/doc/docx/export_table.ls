// HTML spans become an explicit Word grid; continuation cells never duplicate visible text.
import c: .export_common
import style: .export_style
import errors: .common
fn rows(node,path,header=false) => [for (i,child in c.kids(node) where c.tag(child) in ["tr","thead","tbody","tfoot"])
    if (c.tag(child)=="tr") {node:child,path:[*path,i],header:header or
        (len(c.kids(child))>0 and all([for (cell in c.kids(child)) c.tag(cell)=="th"]))}
    else *rows(child,[*path,i],c.tag(child)=="thead")]
fn free_column(column,width,carry) {
    let conflicts = [for (cell in carry where column<cell.column+cell.width and column+width>cell.column) cell.column+cell.width];
    if (len(conflicts)==0) column else free_column(max(conflicts),width,carry)
}
fn place(cells,carry,index=0,column=0,result=[]) {
    if (index>=len(cells)) result else {
        let cell = cells[index]
        let width = c.ordinal(cell.node.colspan)
        let span = c.ordinal(cell.node.rowspan)
        let start = free_column(column,width,carry);
        place(cells,carry,index+1,start+width,[*result,{*:cell,column:start,width:width,
            remaining:span-1,merge:if (span>1) "restart" else null}])
    }
}
fn grid_rows(source,index=0,carry=[],out=[]) {
    if (index>=len(source)) out else {
        let row = source[index]
        let own = place([for (i,cell in c.kids(row.node) where c.tag(cell) in ["td","th"]) {node:cell,path:[*row.path,i]}],carry)
        let continued = [for (cell in carry) {*:cell,merge:"continue",remaining:cell.remaining-1}]
        let cells = sort([*own,*continued],{by:(cell)=>cell.column})
        let next = [for (cell in cells where cell.remaining>0) cell];
        grid_rows(source,index+1,next,[*out,{source:row,cells:cells}])
    }
}
fn column_widths(node,grid,columns,total) {
    let declared = [for (group in c.kids(node) where c.tag(group)=="colgroup") for (col in c.kids(group) where c.tag(col)=="col")
        for (i in 1 to min(63,c.ordinal(col.span))) style.length(style.css_get(col.style,"width") or col.width,total/15.0)*15.0]
    // Single-cell constraints take precedence; spanning cells supply equal shares where no narrower constraint exists.
    let widths = [for (column in 0 to columns-1) {
        let cells = sort([for (row in grid,cell in row.cells,
            let width=style.length(style.css_get(cell.node.style,"width") or cell.node.width,total/15.0)
            where cell.node!=null and cell.merge!="continue" and column>=cell.column and column<cell.column+cell.width and
                width!=null and width>0) {width:width*15.0/cell.width,span:cell.width}],{by:(cell)=>cell.span});
        declared[column] or cells[0].width
    }]
    let specified = sum([for (width in widths where width is number and width>0) width])
    let missing = len([for (width in widths where width==null or width<=0) width])
    let filled = [for (width in widths) if (width is number and width>0) width else max(1.0,(total-specified)/max(1,missing))]
    let factor = total/sum(filled);
    [for (width in filled) width*factor]
}
pub fn render(node,path,ctx,blocks) map^ {
    let grid = grid_rows(rows(node,path))
    let columns = max([1,*[for (row in grid,cell in row.cells) cell.column+cell.width]])
    let css = (ctx.options.styles.table or "") ++ ";" ++ (node.style or "")
    let percent = style.css_get(css,"width") or node.width
    let width = if (ends_with(percent or "","%")) null else style.length(percent)
    let total = if (width!=null and width>0) width*15.0 else ctx.page_width
    let widths = if (columns>63) [] else column_widths(node,grid,columns,total)
    let captions = c.merge([for (i,caption in c.kids(node) where c.tag(caption)=="caption")
        blocks([<p *:c.attrs(caption),*c.kids(caption)>],[*path,i],{*:ctx,numbering:null})^])
    let omitted = [for (child in c.kids(node) where not (c.tag(child) in ["tr","thead","tbody","tfoot","colgroup","caption"])
        and not (child is string and trim(child)=="")) child]
    let dense = if (columns>63) [] else [for (row in grid) {*:row,cells:[for (column in 0 to columns-1)
        for (cell in (let own=[for (cell in row.cells where cell.column==column) cell],
            if (len(own)>0) own else if (any([for (cell in row.cells) column>=cell.column and column<cell.column+cell.width])) []
            else [{node:null,path:[],column:column,width:1,merge:null}])) cell]}]
    let converted = [for (row in dense) [for (cell in row.cells) {
        let defaults = ctx.options.styles[if (c.tag(cell.node)=="th") "th" else "td"] or ""
        let props = style.inherit(cell.node,ctx.props,defaults)
        let body = if (cell.node==null or cell.merge=="continue") c.result() else blocks(c.kids(cell.node),cell.path,{*:ctx,props:props,numbering:null,cell:true})
        let cell_css = defaults ++ ";" ++ (cell.node.style or "")
        let padding = style.sides(cell_css,"padding",props.size or 16.0)
        let background = style.color(style.css_get(cell_css,"background-color") or style.css_get(cell_css,"background"))
        let vertical = style.css_get(cell_css,"vertical-align")
        let inner = body.nodes
        let properties = <'w:tcPr' <'w:tcW' 'w:w':int(round(sum(slice(widths,cell.column,cell.column+cell.width)))),'w:type':"dxa">;
            if (cell.width>1) <'w:gridSpan' 'w:val':cell.width>;
            if (cell.merge!=null) <'w:vMerge' 'w:val':cell.merge>;
            if (background!=null) <'w:shd' 'w:val':"clear",'w:fill':background>;
            if (vertical in ["top","middle","bottom"]) <'w:vAlign' 'w:val':if (vertical=="middle") "center" else vertical>;
            <'w:tcBorders' *[for (side in ["top","left","bottom","right"]) style.border(cell_css,side)]>;
            <'w:tcMar' *[for (side in ["top","left","bottom","right"] where padding[side]!=null)
                style.edge({'w:w':int(round(max(0.0,padding[side])*15.0)),'w:type':"dxa"},side)]>>;
        {node:<'w:tc' <'w:tcPr' *c.ordered(properties,["w:tcW","w:gridSpan","w:vMerge","w:tcBorders","w:shd","w:tcMar","w:vAlign"])>;
            *inner; if (len(inner)==0 or string(name(inner[last]))!="w:p") <'w:p'>>,
         diagnostics:body.diagnostics ++ style.diagnostics(cell.node,cell_css)}
    }]];
    if (columns>63) raise errors.fail("invalid-source","DOCX tables are limited to 63 grid columns")
    else if (len(grid)==0) c.result([],[c.issue("table","Empty table was omitted")])
    else c.result([*captions.nodes,<'w:tbl' <'w:tblPr' <'w:tblW' 'w:type':if (ends_with(percent or "","%")) "pct" else "dxa",
            'w:w':if (ends_with(percent or "","%")) int(round(style.length(percent,100.0)*50.0)) else int(round(total))>;
            <'w:tblLayout' 'w:type':"fixed">>;
        <'w:tblGrid' *[for (width in widths) <'w:gridCol' 'w:w':int(round(width))>]>;
        *[for (i,row in converted) <'w:tr' if (grid[i].source.header) <'w:trPr' <'w:tblHeader'>>; *[for (cell in row) cell.node]>]>],
        [*captions.diagnostics,*[for (row in converted,cell in row) *cell.diagnostics],*style.diagnostics(node,css),
            *if (len(omitted)>0) [c.issue("table","Content outside table rows or captions was omitted")] else []])
}
