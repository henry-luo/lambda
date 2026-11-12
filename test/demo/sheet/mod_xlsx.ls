// Convert the package's semantic workbook to Sheet's bounded editable format.
import xlsx: lambda.doc.xlsx
import m: .mod_model

fn raw(cell) any^ {
    if (cell.formula!=null and cell.formula!="") "=" ++ cell.formula
    else if (cell.type=="n" and cell.raw_value!="") cell.raw_value
    else if (cell.type=="b") (if (cell.value) "TRUE" else "FALSE")
    // The apostrophe preserves text that looks numeric, like an account code.
    else if (cell.value==null or cell.value=="") "" else "'" ++ string(cell.value)
}
fn width_cells(span) any^ {
    if (span.width<=0 or span.min>=m.MAX_COLUMNS or span.max<0 or span.min>span.max) []
    else [for (index in max(0,span.min) to min(m.MAX_COLUMNS-1,span.max))
        {key:string(index),value:max(40,min(1000,round(span.width*7+5)))}]
}
fn dimension_sizes(sheet,axis) any^ {
    let entries=if (axis=="column") [for (span in sheet.column_widths,
        entry in ((width_cells(span)) ^ { [] })) entry]
        else [for (row in sheet.row_heights where row.row>=0 and row.row<m.MAX_ROWS and row.height>0)
            {key:string(row.row),value:max(27,min(500,round(row.height*4/3)))}];
    map([for (entry in entries) (entry.key,entry.value)])
}
fn sheet(src) map^ {
    let invalid=[for (cell in src.cells where cell.row>=m.MAX_ROWS or cell.col>=m.MAX_COLUMNS or len(raw(cell)^)>32767) cell];
    if (len(invalid)>0) raise error("Sheet: XLSX contains a cell outside the supported grid or text limit")
    else {
        let column_widths=dimension_sizes(src,"column")^
        let row_heights=dimension_sizes(src,"row")^
        let rows=max([60,*[for (cell in src.cells) cell.row+1],*[for (row in src.row_heights) min(m.MAX_ROWS,row.row+1)]])
        let cols=max([20,*[for (cell in src.cells) cell.col+1],*[for (col in src.column_widths) min(m.MAX_COLUMNS,col.max+1)]])
        let cells=[for (cell in src.cells) {row:cell.row,col:cell.col,raw:raw(cell)^,style:cell.style,
            *:if (cell.formula!=null and cell.has_value) {cached:cell.value,has_cached:true} else {}}];
        {title:src.title,rows:rows,cols:cols,cells:cells,column_widths:column_widths,row_heights:row_heights}
    }
}
pub fn convert(source) map^ {
    let result=xlsx.convert(source)^
    if (len(result.sheets)>50) raise error("Sheet: XLSX exceeds the 50 worksheet limit")
    else {book:{format:"lambda-sheet",version:1,title:result.title,sheets:[for (src in result.sheets) sheet(src)^]},
        diagnostics:result.diagnostics}
}
