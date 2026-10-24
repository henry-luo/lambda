// visual coordinates include spans; source paths still own every cell (D7.2.5).
import lambda.editor.mod_doc
import lambda.editor.mod_step
import lambda.editor.mod_source_pos
import commands: lambda.editor.mod_commands

pub fn rows(doc,path) => [for (entry in descendants(node_at(doc,path),path) where entry.node.tag=='tr' and
    commands.ancestor_tag(doc,entry.path,'table')==path) {*:entry,owner:entry.path,group:parent_path(entry.path)}]
fn span(n:map,key:symbol,remaining:number) {
    let raw = attrs_get(n.attrs,key)
    let v = if (raw==null) 1 else float(raw)
    if (key=='rowspan' and v==0) remaining else if (v is number and v>=1) min(floor(v),remaining) else 1
}
pub fn at(entries,r,c) => [for (e in entries where e.row<=r and r<e.end_row and e.col<=c and c<e.end_col) e][0]
fn free_column(placed,r,c) {
    let e = at(placed,r,c)
    if (e==null) c else free_column(placed,r,e.end_col)
}
fn place(rows,r,i,c,placed) {
    if (r>=len(rows)) placed else if (i>=len(rows[r].node.content)) place(rows,r+1,0,0,placed) else {
        let n = rows[r].node.content[i]
        let col = free_column(placed,r,c)
        let remaining = len([for (rr in drop(rows,r) where rr.group==rows[r].group) rr])
        let entry = {path:[*rows[r].path,i],node:n,row:r,col:col,row_path:rows[r].path,
            end_row:r+span(n,'rowspan',remaining),end_col:col+span(n,'colspan',1000)}
        if (not contains(['td','th'],n.tag)) place(rows,r,i+1,c,placed)
        else place(rows,r,i+1,entry.end_col,[*placed,entry])
    }
}
pub fn entries(doc,path) => place(rows(doc,path),0,0,0,[])
pub fn bounds(entries) => {top:min([for (e in entries) e.row]),left:min([for (e in entries) e.col]),
    bottom:max([for (e in entries) e.end_row]),right:max([for (e in entries) e.end_col])}
pub fn intersects(e,b) => e.row<b.bottom and e.end_row>b.top and e.col<b.right and e.end_col>b.left
// a rectangular selection expands until no merged cell crosses its boundary.
pub fn rectangle(entries,b) {
    let picked = [for (e in entries where intersects(e,b)) e]
    let expanded = bounds(picked)
    if (expanded==b or len(picked)==0) picked else rectangle(entries,expanded)
}
pub fn cell_node(e:map) map^ => node_attrs(e.node.tag,
    attrs_set(attrs_set(e.node.attrs,'rowspan',if (e.end_row-e.row>1) string(e.end_row-e.row) else null),
        'colspan',if (e.end_col-e.col>1) string(e.end_col-e.col) else null),e.node.content)
fn row_node(rows:array,entries:array,i:number) map^ => node_attrs('tr',rows[i].node.attrs,
    [for (e in sort([for (entry in entries where entry.row==i) entry],(entry)=>entry.col)) cell_node(e)^])
pub fn rebuild(table:map,path:array,rows:array,entries:array) map^ => node_attrs('table',table.attrs,
    [for (i,child in table.content) *if (child.tag=='tr')
        [for (r,row in rows where row.owner==[*path,i]) row_node(rows,entries,r)^]
    else if (contains(['thead','tbody','tfoot'],child.tag))
        [for (group in [[for (r,row in rows where row.group==[*path,i]) row_node(rows,entries,r)^]] where len(group)>0)
            node_attrs(child.tag,child.attrs,group)]
    else [child]])
