import f: .mod_formula
import m: .mod_model
import xlsx: .mod_xlsx

fn valid_style(style) any^ => style is map and all([for (key,value in style,let property=string(key))
    if (property in ["bold","italic","underline","wrap","border"]) value is bool
    else if (property=="size") value is int and value>=1 and value<=72
    else if (property=="format") value in ["general","number","currency","percent","date","date1904","time","datetime","datetime1904"]
    else if (property=="align") value in ["left","center","right"]
    else if (property=="font") value in ["Arial","Helvetica Neue","Georgia","Courier New"]
    else if (property=="fill" or property=="color") value is string and (value=="transparent" or
        (len(value)==7 and starts_with(value,"#") and all([for (ch in ((split(lower(slice(value,1)),"")) ^ { [] })) contains("0123456789abcdef",ch)])))
    else false])
fn valid_cell(c,sheet) any^ => c is map and c.row is int and c.col is int and c.row>=0 and c.col>=0 and
    c.row<sheet.rows and c.col<sheet.cols and c.raw is string and len(c.raw)<=32767 and valid_style(c.style)
fn valid_dimensions(sheet,axis) {
    let sizes=sheet[m.dimension_property(axis)]
    if (sizes==null) true else sizes is map and all([for (key,value in sizes) {
        let index=int(string(key)) ^ {null};
        index!=null and string(index)==string(key) and m.valid_dimension(sheet,axis,index,value)
    }])
}
fn valid_rect(rect,sheet) => rect is map and rect.top is int and rect.left is int and rect.bottom is int and rect.right is int and
    rect.top>=0 and rect.left>=0 and rect.bottom>=rect.top and rect.right>=rect.left and
    rect.bottom<sheet.rows and rect.right<sheet.cols
fn valid_condition_values(rule) any^ => if (rule.operator in ["equal","notEqual","greaterThan","greaterThanOrEqual",
    "lessThan","lessThanOrEqual","between","notBetween"])
        len(rule.values)==(if (rule.operator in ["between","notBetween"]) 2 else 1) and all([for (value in rule.values) value is number])
    else if (rule.operator in ["containsText","notContainsText","beginsWith","endsWith"]) len(rule.values)==1 and rule.values[0] is string
    else if (rule.operator in ["containsBlanks","notContainsBlanks"]) len(rule.values)==0 else false
fn valid_layout(sheet) any^ =>
    (sheet.merges==null or (sheet.merges is array and all([for (rect in sheet.merges) valid_rect(rect,sheet)]))) and
    (sheet.hidden_rows==null or (sheet.hidden_rows is array and all([for (r in sheet.hidden_rows) r is int and r>=0 and r<sheet.rows]))) and
    (sheet.hidden_columns==null or (sheet.hidden_columns is array and all([for (span in sheet.hidden_columns)
        span.min is int and span.max is int and span.min>=0 and span.max>=span.min and span.max<sheet.cols]))) and
    (sheet.conditional_rules==null or (sheet.conditional_rules is array and all([for (rule in sheet.conditional_rules)
        rule is map and rule.ranges is array and len(rule.ranges)>0 and all([for (rect in rule.ranges) valid_rect(rect,sheet)]) and
        rule.values is array and valid_condition_values(rule)^ and rule.priority is int and rule.stop is bool and valid_style(rule.style)]))) and
    (sheet.frozen==null or (sheet.frozen is map and sheet.frozen.rows is int and sheet.frozen.cols is int and
        sheet.frozen.rows>=0 and sheet.frozen.rows<=sheet.rows and sheet.frozen.cols>=0 and sheet.frozen.cols<=sheet.cols))
fn valid_sheet(s) any^ => s is map and s.title is string and s.rows is int and s.cols is int and
    s.rows>=1 and s.rows<=m.MAX_ROWS and s.cols>=1 and s.cols<=m.MAX_COLUMNS and
    (s.date1904==null or s.date1904 is bool) and s.cells is array and
    all([for (c in s.cells) valid_cell(c,s)]) and
    // Cell addresses are map keys, so duplicate detection does not compare every pair.
    len(map([for (c in s.cells) *[f.address(c.row,c.col),true]]))==len(s.cells) and
    valid_dimensions(s,"column") and valid_dimensions(s,"row") and valid_layout(s)
pub fn restore(book) map^ {
    if (book is map and book.format=="lambda-sheet" and book.version==1 and book.title is string and
        book.sheets is array and len(book.sheets)>0 and len(book.sheets)<=50 and all([for (s in book.sheets) valid_sheet(s)])) book
    else raise error("Sheet: invalid workbook; expected lambda-sheet version 1")
}
pub fn is_xlsx(path) any^ => ends_with(lower(path),".xlsx")
pub fn read(path) map^ => if (is_xlsx(path)^) restore(xlsx.convert(path)^.book)^ else restore(input(path,'json')^)^
pub fn import_xlsx(path) map^ => xlsx.convert(path)^
fn quoted(value) => "\"" ++ replace(if (value is number) (format(value,'json') ^ {string(value)}) else string(value),"\"","\"\"") ++ "\""
pub fn csv(sheet) string^ {
    let used=[for (c in sheet.cells where c.raw!="") c]
    let rows=if (len(used)==0) 0 else max([for (c in used) c.row])
    let cols=if (len(used)==0) 0 else max([for (c in used) c.col]);
    // CSV currently materializes the used rectangle before writing it.
    if ((rows+1)*(cols+1)>m.MAX_MATERIALIZED_CELLS) raise error("Sheet: used range is too large to export as CSV")
    else join([for (r in 0 to rows) join([for (c in 0 to cols) quoted(f.display(sheet,r,c))],",")],"\r\n") ++ "\r\n"
}
pub pn write(book,path) any^ { return output(format(book,'json')^,path,{format:'text',atomic:true})^ }
pub pn export_csv(sheet,path) any^ { return output(csv(sheet)^,path,{format:'text',atomic:true})^ }
