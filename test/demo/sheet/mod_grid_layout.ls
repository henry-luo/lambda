// Frozen prefixes and the scrolling window are separate bounded grid segments.
import m: .mod_model
import grid: lambda.ui.core.virtual_grid

fn indices(sheet,direction,start,end) any^ => if (start>=end) [] else
    [for (i in start to (end-1) where not m.hidden(sheet,direction,i)^) i]
pub fn segments(sheet,direction,axis,window,freeze) any^ {
    let boundary=max(0,min(axis.count,freeze))
    let first=max(boundary,window.first)
    let end=max(boundary,window.last);
    {fixed:indices(sheet,direction,0,boundary)^,scroll:indices(sheet,direction,first,end)^,
        fixed_first:0,fixed_end:boundary,scroll_first:first,scroll_end:end,
        gap:grid.offset(axis,first)^-grid.offset(axis,boundary)^,
        after:grid.extent(axis)^-grid.offset(axis,end)^}
}
fn live(sheet,direction,start,end) any^ => indices(sheet,direction,start,end)^
// A merge is clipped separately to the fixed and scrolling segments.
pub fn fragment(sheet,row,col,row_start,row_end,col_start,col_end) any^ {
    let rect=m.merge_at(sheet,row,col)^;
    if (rect==null) {row:row,col:col,rows:1,cols:1,render:true,show:true} else {
        let rows=live(sheet,"row",max(rect.top,row_start),min(rect.bottom+1,row_end))^
        let cols=live(sheet,"column",max(rect.left,col_start),min(rect.right+1,col_end))^;
        // A hidden anchor paints in the first visible part of its merged range.
        let first_row=if (m.hidden(sheet,"row",rect.top)^) live(sheet,"row",rect.top,rect.bottom+1)^[0] else rect.top
        let first_col=if (m.hidden(sheet,"column",rect.left)^) live(sheet,"column",rect.left,rect.right+1)^[0] else rect.left;
        {row:rect.top,col:rect.left,rows:len(rows),cols:len(cols),show:row==first_row and col==first_col,
            render:len(rows)>0 and len(cols)>0 and row==rows[0] and col==cols[0]}
    }
}
pub fn sticky(row_fixed,col_fixed,row,col,axes) any^ =>
    if (not row_fixed and not col_fixed) "" else
    "position:sticky;" ++ (if (row_fixed) "top:" ++ string(26+grid.offset(axes.rows,row)^) ++ "px;" else "") ++
    (if (col_fixed) "left:" ++ string(44+grid.offset(axes.columns,col)^) ++ "px;" else "") ++
    "z-index:" ++ (if (row_fixed and col_fixed) "6" else "3") ++ ";background-color:#fff;"
