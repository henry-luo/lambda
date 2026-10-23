// layout commands edit source tables in one history entry (S12.1.3, D7.2.5).
import dom
import lambda.editor.mod_doc
import lambda.editor.mod_step
import lambda.editor.mod_source_pos
import lambda.editor.mod_transaction
import format: lambda.editor.mod_inline_format
import controller: lambda.edit.controller
import tables: .mod_table
import grid: .mod_table_grid
import page: .mod_page

pub let command_ids = ["insertRowAbove","insertRowBelow","insertColumnLeft","insertColumnRight","deleteRow","deleteColumn",
    "deleteTable","mergeCells","splitCells","splitTable","autoFit","cellSize","distributeRows","distributeColumns",
    "cellMargins","textDirection","properties","sort","repeatHeader","convertText","formula"]
pub let alignments = ["topLeft","topCenter","topRight","centerLeft","center","centerRight","bottomLeft","bottomCenter","bottomRight"]
fn css(n,key) => format.css_get(attrs_get(n.attrs,'style'),key)
pub fn points(value) {
    let raw = string(value or "")
    let suffix = [for (unit in ["pt","px","mm","cm","in"] where ends_with(raw,unit)) unit][0]
    let n = float(if (suffix==null) raw else slice(raw,0,len(raw)-len(suffix)))
    if (n is number) n*({pt:1,px:0.75,mm:72/25.4,cm:72/2.54,in:72}[suffix] or 1) else null
}
fn valid_number(n,lo,hi) => n is number and n>=lo and n<=hi
fn count(n,hi) => valid_number(n,1,hi) and floor(n)==n
fn index(values,value) => [for (i,v in values where v==value) i][0]
fn measured_sizes(values,n) => values is array and len(values)>=n and all([for (v in values) valid_number(v,0,100000)])
pub fn settings(editor) {
    let ctx = tables.context(editor)
    let first = tables.selected(editor)[0]
    let table = ctx.table
    let margins = split(css(first.node,"padding") or "6.75pt 9pt"," ")
    {height:points(css(node_at(editor.doc,first.row_path),"height")) or "",
        width:points(css(first.node,"width")) or "",columns:first.end_col-first.col,rows:first.end_row-first.row,
        table_width:if (ends_with(css(table,"width") or "100%","%")) float(replace(css(table,"width") or "100%","%","")) else 100,
        alignment:if (css(table,"margin-right")=="auto" and css(table,"margin-left")=="auto") "center"
            else if (css(table,"margin-left")=="auto") "right" else "left",
        indent:points(css(table,"margin-left")) or 0,
        top:points(css(first.node,"padding-top") or margins[0]) or 0,
        right:points(css(first.node,"padding-right") or margins[1] or margins[0]) or 0,
        bottom:points(css(first.node,"padding-bottom") or margins[2] or margins[0]) or 0,
        left:points(css(first.node,"padding-left") or margins[3] or margins[1] or margins[0]) or 0,
        direction:css(first.node,"writing-mode") or "horizontal-tb",
        repeat:attrs_get(ctx.table.attrs,'data-word-repeat-header')=="true" or ctx.table.content[0].tag=='thead'}
}
fn style(tx,path,key,value) => tx_step(tx,step_set_attr(path,'style',format.css_set(attrs_get(node_at(tx.doc_after,path).attrs,'style'),key,value)))
fn styles(tx,paths,properties) => reduce([tx,*paths],(acc,path)=>reduce([acc,*properties],(next,p)=>style(next,path,p.key,p.value)))
fn selected_paths(editor) => [for (e in tables.selected(editor)) e.path]
fn row_paths(editor) {
    let b = grid.bounds(tables.selected(editor));
    [for (r,row in grid.rows(editor.doc,tables.context(editor).table_path) where r>=b.top and r<b.bottom) row.path]
}
fn with_caret(tx,path) {
    let leaf = format.leaves(node_at(tx.doc_after,path),path)[0]
    let p = pos(leaf.path or path,0)
    tx_set_selection(tx,text_selection(p,p))
}
fn replace_table(editor,kids) {
    let ctx = tables.context(editor)
    let i = last_index(ctx.table_path)
    let parent = parent_path(ctx.table_path)
    with_caret(tx_step(tx_begin(editor.doc,editor.selection),step_replace(parent,i,i+1,kids)),[*parent,i])
}
fn grid_transaction(editor,rows,entries,r,c) map^ {
    let ctx = tables.context(editor)
    let table = grid.rebuild(ctx.table,ctx.table_path,rows,entries)^
    let tx = replace_table(editor,[table])
    let placed = grid.entries(tx.doc_after,ctx.table_path)
    with_caret(tx,(grid.at(placed,r,c) or placed[0]).path)
}
fn blank(n,tag=null) map^ => node_attrs(tag or if (n.tag=='th') 'th' else 'td',
    attrs_set(attrs_set(n.attrs,'rowspan',null),'colspan',null),[text("")])
fn is_picked(e,picked) => any([for (p in picked) p.path==e.path])

fn merge(editor) map^ {
    let picked = tables.selected(editor)
    let all_cells = tables.entries(editor)
    let b = grid.bounds(picked)
    let rows = grid.rows(editor.doc,tables.context(editor).table_path)
    if (len(picked)<2 or len(grid.rectangle(all_cells,b))!=len(picked)) raise error("Select a rectangular range of at least two cells.")
    else if (rows[b.top].group!=rows[b.bottom-1].group) raise error("Merge cells within the same header, body, or footer group.")
    else {
        let content = [for (i,e in picked) *[*if (i==0) [] else [node('br',[])], *e.node.content]]
        let first = picked[0]
        let merged = {*:first,row:b.top,col:b.left,end_row:b.bottom,end_col:b.right,
            node:node_attrs(first.node.tag,first.node.attrs,content)}
        grid_transaction(editor,rows,[for (e in all_cells) *if (e.path==first.path) [merged] else if (is_picked(e,picked)) [] else [e]],b.top,b.left)^
    }
}
// splitting adds grid lines through a cell; neighbors expand across those lines.
fn expand(entries,axis,at,amount,inclusive=false) => [for (e in entries)
    {*:e,[axis]:e[axis]+if (e[axis]>=at) amount else 0,
        ["end_" ++ axis]:e["end_" ++ axis]+if (e["end_" ++ axis]>at or
            (inclusive and e["end_" ++ axis]==at)) amount else 0}]
fn insert_rows(rows,at,amount,reference) => [*take(rows,at),
    *[for (i in 1 to amount) {*:reference,node:node('tr',[])}],*drop(rows,at)]
fn split_cells(editor,p) map^ {
    let picked = tables.selected(editor)
    let e = picked[0]
    let nr = p.rows or e.end_row-e.row
    let nc = p.columns or e.end_col-e.col
    let oldr = e.end_row-e.row
    let oldc = e.end_col-e.col
    if (len(picked)!=1 or not count(nr,32) or not count(nc,32)) raise error("Select one cell and choose 1–32 rows and columns.")
    else if ((nr<oldr and oldr%nr!=0) or (nc<oldc and oldc%nc!=0)) raise error("Choose counts that divide the merged cell's rows and columns evenly.")
    else {
        let extra_r = max(0,nr-oldr)
        let extra_c = max(0,nc-oldc)
        let rows = grid.rows(editor.doc,tables.context(editor).table_path)
        let refined = expand(expand(tables.entries(editor),"row",e.row+1,extra_r,true),"col",e.col+1,extra_c,true)
        let height = max(nr,oldr)/nr
        let width = max(nc,oldc)/nc
        let cells = [for (r in 0 to nr-1) for (c in 0 to nc-1)
            {*:e,row:e.row+r*height,col:e.col+c*width,end_row:e.row+(r+1)*height,end_col:e.col+(c+1)*width,
                node:if (r==0 and c==0) e.node else blank(e.node)^}]
        grid_transaction(editor,insert_rows(rows,e.row+1,extra_r,rows[e.row]),
            [for (entry in refined) *if (entry.path==e.path) cells else [entry]],e.row,e.col)^
    }
}
fn insert_axis(editor,axis,before) map^ {
    let all_cells = tables.entries(editor)
    let picked = tables.selected(editor)
    let b = grid.bounds(picked)
    let at = if (axis=="row") (if (before) b.top else b.bottom) else if (before) b.left else b.right
    let rows = grid.rows(editor.doc,tables.context(editor).table_path)
    let expanded = expand(all_cells,axis,at,1)
    let extent = grid.bounds(all_cells)
    let ref = if (axis=="row") (if (before) b.top else b.bottom-1) else if (before) b.left else b.right-1
    let blanks = [for (i in 0 to (if (axis=="row") extent.right else extent.bottom)-1)
        for (r in [if (axis=="row") at else i], c in [if (axis=="row") i else at]
        where grid.at(expanded,r,c)==null)
        {row:r,col:c,end_row:r+1,end_col:c+1,node:blank((grid.at(all_cells,if (axis=="row") ref else r,if (axis=="col") ref else c) or picked[0]).node,
            if (axis=="row") 'td' else null)^}]
    grid_transaction(editor,if (axis=="row") insert_rows(rows,at,1,rows[ref]) else rows,[*expanded,*blanks],
        if (axis=="row") at else b.top,if (axis=="col") at else 0)^
}
fn clipped(value,lo,hi) => value-min(max(value-lo,0),hi-lo)
fn delete_axis(editor,axis) map^ {
    let b = grid.bounds(tables.selected(editor))
    let all_cells = tables.entries(editor)
    let lo = if (axis=="row") b.top else b.left
    let hi = if (axis=="row") b.bottom else b.right
    let end = "end_" ++ axis
    let remaining = [for (e in all_cells) for (a in [clipped(e[axis],lo,hi)],z in [clipped(e[end],lo,hi)]
        where z>a) {*:e,[axis]:a,[end]:z}]
    let rows = grid.rows(editor.doc,tables.context(editor).table_path)
    if (len(remaining)==0) replace_table(editor,[node('p',[])]) else
        grid_transaction(editor,if (axis=="row") [for (i,row in rows where i<lo or i>=hi) row] else rows,remaining,
            min(b.top,grid.bounds(remaining).bottom-1),min(b.left,grid.bounds(remaining).right-1))^
}
fn split_table(editor) map^ {
    let ctx = tables.context(editor)
    let rows = grid.rows(editor.doc,ctx.table_path)
    let all_cells = tables.entries(editor)
    let cut = grid.bounds(tables.selected(editor)).top
    if (cut==0) raise error("Place the caret below the first row to split the table.")
    else if (any([for (e in all_cells) e.row<cut and e.end_row>cut])) raise error("Split merged cells that cross this row first.")
    else {
        let first = grid.rebuild(ctx.table,ctx.table_path,take(rows,cut),[for (e in all_cells where e.row<cut) e])^
        let second = grid.rebuild(ctx.table,ctx.table_path,drop(rows,cut),[for (e in all_cells where e.row>=cut) {*:e,row:e.row-cut,end_row:e.end_row-cut}])^
        let tx = replace_table(editor,[first,node('p',[]),node_attrs('table',attrs_set(second.attrs,'id',null),second.content)])
        // the separator is the editing destination after splitting a table.
        with_caret(tx,[*parent_path(ctx.table_path),last_index(ctx.table_path)+1])
    }
}
fn table_width(editor) {
    let ctx = tables.context(editor)
    let p = page.settings(editor.doc)
    let available = (page.dimension(p,"width")-p.left-p.right)*72/25.4
    let value = css(ctx.table,"width") or "100%"
    if (ends_with(value,"%")) available*(float(replace(value,"%","")) or 100)/100 else points(value) or available
}
fn size(editor,p) map^ {
    if (not contains(["height","width"],p.axis) or (p.value!=null and not valid_number(p.value,1,2000)))
        raise error("Choose a size from 1 to 2000 pt, or clear the field for automatic sizing.")
    else {
        let tx = tx_begin(editor.doc,editor.selection)
        let value = if (p.value==null) null else string(p.value) ++ "pt"
        if (p.axis=="height") styles(tx,row_paths(editor),[{key:"height",value:value}])
        else styles(tx,selected_paths(editor),[{key:"width",value:value}])
    }
}
fn distribute(editor,axis,p) map^ {
    let all_cells = tables.entries(editor)
    let b = grid.bounds(tables.selected(editor))
    let lo = if (axis=="row") b.top else b.left
    let hi = if (axis=="row") b.bottom else b.right
    let sizes = if (axis=="row") p.heights else p.widths
    let valid = measured_sizes(sizes,hi)
    if (axis=="row" and not valid) raise error("Measure the table's row heights before distributing rows.")
    else {
        let average = if (valid) sum(slice(sizes,lo,hi))/(hi-lo) else table_width(editor)/grid.bounds(all_cells).right
        let tx = tx_begin(editor.doc,editor.selection)
        if (axis=="row") styles(tx,row_paths(editor),[{key:"height",value:string(average) ++ "pt"}])
        else reduce([tx,*[for (e in all_cells where e.col<hi and e.end_col>lo) e]],(acc,e)=>
            style(acc,e.path,"width",string(average*(e.end_col-e.col)) ++ "pt"))
    }
}
fn auto_fit(editor,p) map^ {
    let ctx = tables.context(editor)
    if (not contains(["contents","window","fixed"],p.mode)) raise error("Choose an AutoFit mode.") else {
        let all_cells = tables.entries(editor)
        let cols = grid.bounds(all_cells).right
        let widths = if (measured_sizes(p.widths,cols) and len(p.widths)==cols) p.widths else [for (i in 1 to cols) table_width(editor)/cols]
        let tx = styles(tx_begin(editor.doc,editor.selection),[for (e in tables.entries(editor)) e.path],[{key:"width",value:null}])
        // freezing columns also freezes the measured table width; expanding to the window would resize them again.
        let fit = styles(tx,[ctx.table_path],[{key:"width",value:if (p.mode=="contents") "auto" else if (p.mode=="window") "100%" else string(sum(widths)) ++ "pt"},
            {key:"table-layout",value:if (p.mode=="fixed") "fixed" else "auto"}])
        if (p.mode!="fixed") fit else {
            reduce([fit,*all_cells],(acc,e)=>style(acc,e.path,"width",string(sum(slice(widths,e.col,e.end_col))) ++ "pt"))
        }
    }
}
fn properties(editor,p) map^ {
    if (not valid_number(p.table_width,1,100) or not valid_number(p.indent,0,300) or not contains(["left","center","right"],p.alignment))
        raise error("Choose a width from 1–100%, an alignment, and an indent from 0–300 pt.")
    else styles(tx_begin(editor.doc,editor.selection),[tables.context(editor).table_path],[
        {key:"width",value:string(p.table_width) ++ "%"},{key:"margin-left",value:if (p.alignment=="left") string(p.indent) ++ "pt" else "auto"},
        {key:"margin-right",value:if (p.alignment=="center") "auto" else "0pt"}])
}
fn margins(editor,p) map^ {
    if (not all([for (side in ["top","right","bottom","left"]) valid_number(p[side],0,72)]))
        raise error("Choose cell margins from 0 to 72 pt.")
    else styles(tx_begin(editor.doc,editor.selection),selected_paths(editor),
        [for (side in ["top","right","bottom","left"]) {key:"padding-" ++ side,value:string(p[side]) ++ "pt"}])
}
fn direction(editor,p) map^ {
    let current = settings(editor).direction
    let modes = ["horizontal-tb","vertical-rl","vertical-lr"]
    let mode = p.value or modes[(index(modes,current)+1)%3]
    if (not contains(modes,mode)) raise error("Choose horizontal or vertical text.")
    else styles(tx_begin(editor.doc,editor.selection),selected_paths(editor),[{key:"writing-mode",value:mode}])
}
fn convert_text(editor,p) map^ {
    if (not contains(["tabs","commas","paragraphs"],p.separator)) raise error("Choose tabs, commas, or paragraphs.") else {
        let all_cells = tables.entries(editor)
        let b = grid.bounds(all_cells)
        let separator = if (p.separator=="commas") ", " else "\t"
        let paragraphs = if (p.separator=="paragraphs") [for (e in all_cells) node('p',e.node.content)] else
            [for (r in 0 to b.bottom-1) node('p',[for (c in 0 to b.right-1) *[
                *if (c==0) [] else [text(separator)],
                *[for (e in [grid.at(all_cells,r,c)] where e.row==r and e.col==c) *e.node.content]]])]
        replace_table(editor,paragraphs)
    }
}
fn sorted_table(editor,p) map^ {
    let ctx = tables.context(editor)
    let rows = grid.rows(editor.doc,ctx.table_path)
    let all_cells = tables.entries(editor)
    if (not count(p.column,grid.bounds(all_cells).right) or not contains(["text","number"],p.type) or not (p.descending is bool))
        raise error("Choose a column, text or numeric sorting, and an order.")
    else if (any([for (e in all_cells) e.end_row-e.row>1])) raise error("Split vertically merged cells before sorting rows.")
    else {
        let keep = if (p.header) 1 else 0
        let keys = [for (i,row in rows) {index:i,group:row.group,
            sortable:i>=keep and not contains(['thead','tfoot'],node_at(editor.doc,row.group).tag),
            key:if (p.type=="number") float(trim(doc_text(grid.at(all_cells,i,p.column-1).node)))
                else lower(doc_text(grid.at(all_cells,i,p.column-1).node))}]
        if (p.type=="number" and any([for (k in keys where k.sortable) not (k.key is number)])) raise error("Every sorted cell must contain a number.") else {
            // reverse each group's stable ascending order for descending sorts.
            let ascending = [for (i,row in rows) if (not keys[i].sortable) i
                else for (candidates in [[for (k in keys where k.sortable and k.group==row.group) k]],
                    rank in [index([for (k in candidates) k.index],i)],ordered in [sort(candidates,(k)=>k.key)])
                    ordered[if (p.descending) len(ordered)-1-rank else rank].index]
            let nodes = [for (i,row in rows) {*:row,node:rows[ascending[i]].node}]
            let cells = [for (r,from in ascending) for (e in all_cells where e.row==from) {*:e,row:r,end_row:r+1}]
            grid_transaction(editor,nodes,cells,0,p.column-1)^
        }
    }
}
fn repeat_header(editor,p) map^ {
    let ctx = tables.context(editor)
    let rows = grid.rows(editor.doc,ctx.table_path)
    let b = grid.bounds(tables.selected(editor))
    let enabled = if (p.enabled is bool) p.enabled else not settings(editor).repeat
    if (enabled and b.top!=0) raise error("Select the first row, or consecutive rows starting at the top, to repeat them.")
    else if (enabled and any([for (e in tables.entries(editor)) e.row<b.bottom and e.end_row>b.bottom]))
        raise error("A merged cell crosses the last header row. Include the complete cell.")
    else {
        let header = if (enabled) [node_attrs('thead',if (ctx.table.content[0].tag=='thead') ctx.table.content[0].attrs else [],
            [for (i,row in rows where i<b.bottom) row.node])] else []
        let all_cells = tables.entries(editor)
        let cut = if (enabled) b.bottom else 0
        let body = grid.rebuild(ctx.table,ctx.table_path,drop(rows,cut),
            [for (e in all_cells where e.row>=cut) {*:e,row:e.row-cut,end_row:e.end_row-cut}])^
        replace_table(editor,[node_attrs('table',attrs_set(ctx.table.attrs,'data-word-repeat-header',string(enabled)),
            [*header,*[for (part in body.content) if (part.tag=='thead') node_attrs('tbody',part.attrs,part.content) else part]])])
    }
}

fn address(raw) {
    let chars = split(upper(trim(raw)),"")
    let alphabet = split("ABCDEFGHIJKLMNOPQRSTUVWXYZ","")
    let letters = take(chars,len([for (c in chars where contains(alphabet,c)) c]))
    let digits = drop(chars,len(letters))
    let col = reduce([0,*letters],(n,c)=>n*26+index(alphabet,c)+1)
    let row = float(join(digits,""))
    if (len(letters)==0 or len(digits)==0 or not all([for (c in digits) contains(split("0123456789",""),c)]) or
        not valid_number(row,1,1000000)) null else {row:row-1,col:col-1}
}
fn formula_cells(editor,raw) array^ {
    let all_cells = tables.entries(editor)
    let target = tables.selected(editor)[0]
    let arg = upper(trim(raw))
    if (contains(["ABOVE","BELOW","LEFT","RIGHT"],arg)) [for (e in all_cells where
        if (arg=="ABOVE") e.end_row<=target.row and e.col<=target.col and e.end_col>target.col
        else if (arg=="BELOW") e.row>=target.end_row and e.col<=target.col and e.end_col>target.col
        else if (arg=="LEFT") e.end_col<=target.col and e.row<=target.row and e.end_row>target.row
        else e.col>=target.end_col and e.row<=target.row and e.end_row>target.row) e]
    else {
        let pair = split(arg,":")
        let a = address(pair[0])
        let z = address(pair[1] or pair[0])
        let b = grid.bounds(all_cells)
        if (len(pair)>2 or a==null or z==null or a.row>=b.bottom or z.row>=b.bottom or a.col>=b.right or z.col>=b.right)
            raise error("Use ABOVE, BELOW, LEFT, RIGHT, a cell such as A1, or a range such as A1:B3.")
        else {
            let picked = [for (e in all_cells where grid.intersects(e,{top:min(a.row,z.row),bottom:max(a.row,z.row)+1,
                left:min(a.col,z.col),right:max(a.col,z.col)+1})) e]
            if (is_picked(target,picked)) raise error("The formula range includes its own cell.") else picked
        }
    }
}
fn formula(editor,p) map^ {
    let source = trim(p.expression or "")
    let expr = upper(if (starts_with(source,"=")) slice(source,1) else source)
    let parts = split(expr,"(")
    let operation = trim(parts[0])
    if (len(tables.selected(editor))!=1 or len(parts)!=2 or not ends_with(expr,")") or
        not contains(["SUM","AVERAGE","MIN","MAX","COUNT"],operation) or
        not (p.decimals=="general" or (p.decimals is number and p.decimals>=0 and p.decimals<=6 and floor(p.decimals)==p.decimals)))
        raise error("Use SUM, AVERAGE, MIN, MAX, or COUNT with one direction or cell range.")
    else {
        let cells = formula_cells(editor,slice(parts[1],0,len(parts[1])-1))^
        let values = [for (e in cells) for (value in [float(trim(doc_text(e.node)))] where value is number) value]
        if (len(values)==0 and operation!="COUNT") raise error("The formula range contains no numbers.") else {
            let value = if (operation=="COUNT") len(values) else if (operation=="SUM") sum(values)
                else if (operation=="AVERAGE") sum(values)/len(values) else if (operation=="MIN") min(values) else max(values)
            let scale = if (p.decimals=="general") 1 else [1,10,100,1000,10000,100000,1000000][p.decimals]
            let rendered = string(if (p.decimals=="general") value else round(value*scale)/scale)
            let target = tables.selected(editor)[0]
            let tx = tx_step(tx_step(tx_begin(editor.doc,editor.selection),step_set_attr(target.path,'data-word-formula',"=" ++ expr)),
                step_replace(target.path,0,len(target.node.content),[text(rendered)]))
            with_caret(tx,target.path)
        }
    }
}

pub fn transaction(editor,id,p={}) map^ {
    if (tables.context(editor)==null) raise error("Place the caret in a table first.")
    else if (id=="table.mergeCells") merge(editor)^
    else if (id=="table.splitCells") split_cells(editor,p)^
    else if (id=="table.splitTable") split_table(editor)^
    else if (id=="table.deleteTable") replace_table(editor,[node('p',[])])
    else if (contains(["table.insertRowAbove","table.insertRowBelow","table.insertColumnLeft","table.insertColumnRight"],id))
        insert_axis(editor,if (contains(id,"Row")) "row" else "col",ends_with(id,"Above") or ends_with(id,"Left"))^
    else if (contains(["table.deleteRow","table.deleteColumn"],id)) delete_axis(editor,if (id=="table.deleteRow") "row" else "col")^
    else if (id=="table.cellSize") size(editor,p)^
    else if (id=="table.autoFit") auto_fit(editor,p)^
    else if (id=="table.distributeRows" or id=="table.distributeColumns") distribute(editor,if (id=="table.distributeRows") "row" else "col",p)^
    else if (id=="table.properties") properties(editor,p)^
    else if (id=="table.cellMargins") margins(editor,p)^
    else if (id=="table.textDirection") direction(editor,p)^
    else if (id=="table.convertText") convert_text(editor,p)^
    else if (id=="table.sort") sorted_table(editor,p)^
    else if (id=="table.repeatHeader") repeat_header(editor,p)^
    else if (id=="table.formula") formula(editor,p)^
    else raise error("Unknown table layout command.")
}

fn grid_line(sizes,axis,line) {
    let coord = if (axis=="row") "y" else "x"
    let dimension = if (axis=="row") "height" else "width"
    let ends = [for (e in sizes) *[*if (e[axis]==line) [e.rect[coord]] else [],
        *if (e["end_" ++ axis]==line) [e.rect[coord]+e.rect[dimension]] else []]]
    // an internal line hidden by spans is interpolated only when no real edge exposes it.
    if (len(ends)>0) min(ends) else min([for (e in sizes where e[axis]<line and e["end_" ++ axis]>line)
        e.rect[coord]+e.rect[dimension]*(line-e[axis])/(e["end_" ++ axis]-e[axis])])
}
// geometric measurements cross into the pure command as values, never live DOM handles.
pub pn measure(editor,node,zoom) {
    let all_cells = tables.entries(editor)
    let source = tables.cells(editor.doc)
    let live = dom.query_selector_all(controller.surface(node),"td,th")
    let sizes = [for (e in all_cells) for (i in [index([for (c in source) c.path],e.path)])
        {*:e,rect:dom.bounding_box(live[i])}]
    let b = grid.bounds(all_cells)
    return {heights:[for (r in 0 to b.bottom-1) (grid_line(sizes,"row",r+1)-grid_line(sizes,"row",r))*0.75/zoom],
        widths:[for (c in 0 to b.right-1) (grid_line(sizes,"col",c+1)-grid_line(sizes,"col",c))*0.75/zoom]}
}
