// common tabular presentation and Open XML packaging; I/O stays at the caller.
import util: .util

fn cell_text(value) => if (value==null) "" else if (value is map or value is array) format(value,'json') else string(value)

pub fn html_table(columns, rows, options = {}) {
    <table class:"lambda-export-table",
        if (options.caption!=null) <caption options.caption>;
        <thead <tr *[for (column in columns) <th scope:"col",column.heading>]>>
        <tbody *[for (row in rows) <tr *[for (column in columns) <td cell_text(row[column.field])>]>]>
    >
}

fn csv_cell(value) => "\"" ++ replace(cell_text(value),"\"","\"\"") ++ "\""
pub fn csv(columns, rows) => join([
    join([for (column in columns) csv_cell(column.heading)],","),
    *[for (row in rows) join([for (column in columns) csv_cell(row[column.field])],",")]],"\r\n") ++ "\r\n"

pub fn column_name(index) {
    let digit=index%26;
    (if (index>=26) column_name(index div 26-1) else "") ++ chr(65+digit)
}
fn xlsx_cell(value, address) {
    // dates and wide exact numbers are strings, avoiding Excel's 15-digit limit.
    if (value is bool) <c r:address,t:"b",<v if (value) "1" else "0">>
    else if (util.finite_number(value) and not (value is decimal) and abs(value)<1000000000000000)
        <c r:address,<v string(value)>>
    else <c r:address,t:"inlineStr",<is <t 'xml:space':"preserve",cell_text(value)>>>
}
fn xml_file(filename, value) => <fs name:filename,kind:'file',format:'xml',value>

pub fn xlsx(columns, rows, options = {}) {
    let sheet=if (options.sheet_name!=null) string(options.sheet_name) else "Timeline";
    let invalid=util.first_error([
        if (len(columns)>16384 or len(rows)>1048575) error("chart export: XLSX worksheet exceeds Excel dimensions"),
        if (len(sheet)==0 or len(sheet)>31 or any([for (ch in ["[","]",":","*","?","/","\\"]) contains(sheet,ch)]) or
            starts_with(sheet,"'") or ends_with(sheet,"'")) error("chart export: invalid worksheet name")]);
    if (invalid is error) invalid else <fs kind:'dir',
        xml_file("[Content_Types].xml",<Types xmlns:"http://schemas.openxmlformats.org/package/2006/content-types",
            <Default Extension:"rels",ContentType:"application/vnd.openxmlformats-package.relationships+xml">
            <Default Extension:"xml",ContentType:"application/xml">
            <Override PartName:"/xl/workbook.xml",ContentType:"application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml">
            <Override PartName:"/xl/worksheets/sheet1.xml",ContentType:"application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml">>);
        <fs name:"_rels",kind:'dir',xml_file(".rels",<Relationships xmlns:"http://schemas.openxmlformats.org/package/2006/relationships",
            <Relationship Id:"rId1",Type:"http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument",Target:"xl/workbook.xml">>)>
        <fs name:"xl",kind:'dir',
            xml_file("workbook.xml",<workbook xmlns:"http://schemas.openxmlformats.org/spreadsheetml/2006/main",
                'xmlns:r':"http://schemas.openxmlformats.org/officeDocument/2006/relationships",
                <sheets <sheet name:sheet,sheetId:"1",'r:id':"rId1">>>);
            <fs name:"_rels",kind:'dir',xml_file("workbook.xml.rels",<Relationships xmlns:"http://schemas.openxmlformats.org/package/2006/relationships",
                <Relationship Id:"rId1",Type:"http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet",Target:"worksheets/sheet1.xml">>)>
            <fs name:"worksheets",kind:'dir',xml_file("sheet1.xml",<worksheet xmlns:"http://schemas.openxmlformats.org/spreadsheetml/2006/main",
                <sheetData
                    <row r:"1",*[for (i,column in columns) xlsx_cell(column.heading,column_name(i) ++ "1")]>;
                    *[for (index,row in rows) <row r:string(index+2),
                        *[for (i,column in columns) xlsx_cell(row[column.field],column_name(i) ++ string(index+2))]>]
                >>)>
        >
    >
}
