import grid: lambda.ui.core.virtual_grid
import m: .mod_model

// Defaults and manual sizes become sparse axis overrides, in zoomed CSS pixels.
pub fn axis(st, direction) any^ {
    let sheet=m.current(st)
    let sizes=sheet[m.dimension_property(direction)] or {}
    let defaults=if (direction=="row") {'0':39} else {'0':220,'1':150}
    let drag=st.resize_drag
    let preview=if (drag!=null and drag.axis==direction) map([string(drag.index),drag.value]) else {}
    let hidden=if (direction=="row") map([for (r in (sheet.hidden_rows or [])) *[string(r),0]])
        else map([for (span in (sheet.hidden_columns or []), c in span.min to span.max) *[string(c),0]])
    let all_sizes={*:defaults,*:sizes,*:preview,*:hidden}
    let item_count=if (direction=="row") sheet.rows else sheet.cols
    let base=if (direction=="row") 27 else 124;
    grid.axis(item_count,base*st.zoom,[for (key,value in all_sizes) {index:int(string(key)),size:value*st.zoom}])
}

// Scroll updates retain these indexes until a dimension, zoom or preview changes.
pub fn key(st) {
    let sheet=m.current(st)
    let drag=st.resize_drag;
    {rows:sheet.rows,columns:sheet.cols,row_sizes:sheet.row_heights,column_sizes:sheet.column_widths,hidden_rows:sheet.hidden_rows,
        hidden_columns:sheet.hidden_columns,zoom:st.zoom,
        preview:if (drag==null) null else {axis:drag.axis,index:drag.index,value:drag.value}}
}
pub fn build(st) map^ => {rows:axis(st,"row")^,columns:axis(st,"column")^}
pub fn axes(st) map^ => if (st.geometry_key==key(st) and st.geometry!=null) st.geometry else build(st)^

pub fn visible(a, viewport, row_overscan=2, column_overscan=1) map^ =>
    {rows:grid.window(a.rows,viewport.y,max(0,viewport.height-26),row_overscan)^,
        columns:grid.window(a.columns,viewport.x,max(0,viewport.width-44),column_overscan)^}
