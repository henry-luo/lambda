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
// Sheet's bounded grid clips formatting ranges without discarding the workbook.
fn clipped_rules(src) => [for (rule in src.conditional_rules,
    let ranges=[for (rect in rule.ranges where rect.top<m.MAX_ROWS and rect.left<m.MAX_COLUMNS)
        {*:rect,bottom:min(rect.bottom,m.MAX_ROWS-1),right:min(rect.right,m.MAX_COLUMNS-1)}]
    where len(ranges)>0) {*:rule,ranges:ranges}]
fn clipped(src) => any([for (rule in src.conditional_rules, rect in rule.ranges)
    rect.bottom>=m.MAX_ROWS or rect.right>=m.MAX_COLUMNS])
fn sheet(src,date1904) map^ {
    let invalid=[for (cell in src.cells where cell.row>=m.MAX_ROWS or cell.col>=m.MAX_COLUMNS or len(raw(cell)^)>32767) cell];
    if (len(invalid)>0 or any([for (rect in src.merges) rect.bottom>=m.MAX_ROWS or rect.right>=m.MAX_COLUMNS]) or
        (src.split!=null and (src.split.x<0 or src.split.y<0 or src.split.top_left.row>=m.MAX_ROWS or src.split.top_left.col>=m.MAX_COLUMNS or
            src.split.selection.row>=m.MAX_ROWS or src.split.selection.col>=m.MAX_COLUMNS)) or
        (src.frozen!=null and (src.frozen.rows<0 or src.frozen.rows>m.MAX_ROWS or src.frozen.cols<0 or src.frozen.cols>m.MAX_COLUMNS)))
        raise error("Sheet: XLSX contains a cell or layout outside the supported grid or text limit")
    else {
        let column_widths=dimension_sizes(src,"column")^
        let row_heights=dimension_sizes(src,"row")^
        let rows=max([60,*[for (cell in src.cells) cell.row+1],*[for (row in src.row_heights) min(m.MAX_ROWS,row.row+1)],
            *[for (rect in src.merges) min(m.MAX_ROWS,rect.bottom+1)],
            *[for (rule in src.conditional_rules, rect in rule.ranges) min(m.MAX_ROWS,rect.bottom+1)],*[for (row in src.hidden_rows) min(m.MAX_ROWS,row+1)],
            if (src.frozen==null) 0 else src.frozen.rows,if (src.split==null) 0 else max(src.split.top_left.row,src.split.selection.row)+1])
        let cols=max([20,*[for (cell in src.cells) cell.col+1],*[for (col in src.column_widths) min(m.MAX_COLUMNS,col.max+1)],
            *[for (rect in src.merges) min(m.MAX_COLUMNS,rect.right+1)],
            *[for (rule in src.conditional_rules, rect in rule.ranges) min(m.MAX_COLUMNS,rect.right+1)],if (src.frozen==null) 0 else src.frozen.cols,if (src.split==null) 0 else max(src.split.top_left.col,src.split.selection.col)+1])
        let cells=[for (cell in src.cells) {row:cell.row,col:cell.col,raw:raw(cell)^,style:cell.style,
            *:if (cell.formula!=null and cell.has_value) {cached:cell.value,has_cached:true} else {}}];
        {title:src.title,rows:rows,cols:cols,cells:cells,column_widths:column_widths,row_heights:row_heights,
            merges:src.merges,split:src.split,conditional_rules:clipped_rules(src),hidden_rows:[for (r in src.hidden_rows where r>=0 and r<rows) r],
            hidden_columns:[for (span in src.hidden_columns where span.max>=0 and span.min<cols)
                {min:max(0,span.min),max:min(cols-1,span.max)}],frozen:src.frozen,
            *:if (date1904) {date1904:true} else {}}
    }
}
pub fn convert(source) map^ {
    let result=xlsx.convert(source)^
    if (len(result.sheets)>50) raise error("Sheet: XLSX exceeds the 50 worksheet limit")
    else {book:{format:"lambda-sheet",version:1,title:result.title,sheets:[for (src in result.sheets) sheet(src,result.date1904)^]},
        diagnostics:[*result.diagnostics,*[for (src in result.sheets where clipped(src)^)
            {severity:"warning",code:"conditional-range-clipped",part:src.part,message:"Conditional formatting exceeds the Sheet grid"}]]}
}
