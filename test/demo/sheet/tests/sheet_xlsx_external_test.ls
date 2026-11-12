// External workbooks cover real XLSX producers and formatting metadata (D11.1.2).
import x: lambda.doc.xlsx
import files: ~~.mod_io
import m: ~~.mod_model
import f: ~~.mod_formula
import v: ~~.mod_view
let base="test/input/xlsx/external/"
let names=["bloated-workbook.xlsx","contacts.xlsx","customer-lists.xlsx","employees.xlsx","event-schedule.xlsx",
    "exceljs-1904.xlsx","excelize-Book1.xlsx","excelize-MergeCell.xlsx","excelize-SharedStrings.xlsx","files-sample-1000rows.xlsx",
    "formatted-report.xlsx","hidden-frozen.xlsx","inventory.xlsx","merged-formats.xlsx","messy-data.xlsx",
    "numbers-stored-as-text.xlsx","protected-sample.xlsx","sales-orders.xlsx","sample-budget.xlsx","workbook-protected.xlsx"]
let loaded=[for (name in names) files.import_xlsx(base ++ name) ^ { ^ }]
let event=files.import_xlsx(base ++ "event-schedule.xlsx")^
let schedule=event.book.sheets[0]
let fmt=files.import_xlsx("test/input/xlsx/number-formats.xlsx")^.book.sheets[0]
let protected=x.convert(base ++ "protected-sample.xlsx")^
let workbook=x.convert(base ++ "workbook-protected.xlsx")^
let hidden=x.convert(base ++ "hidden-frozen.xlsx")^
let dated=files.import_xlsx(base ++ "exceljs-1904.xlsx")^
let invalid=x.convert("test/input/xlsx/bad-case-relationship.xlsx") ^ { ^ }
let restored=files.restore(parse(format(event.book,'json')^,'json')^)^
let get=(sheet,row,col) any^ => f.cell(sheet,row,col)^
let shown=(sheet,row,col) any^ => v.formatted(f.display(sheet,row,col)^,get(sheet,row,col).style)^;
[
 len(loaded)==20 and all([for (book in loaded) book is map]),
 invalid is error and contains(invalid.message,"Missing package part xl/SharedStrings.xml"),
 dated.book.sheets[0].date1904==true,
 shown(dated.book.sheets[0],3,1)=="1904-01-01",
 shown(schedule,1,1)=="2026-11-02",
 shown(schedule,1,2)=="09:00:00",
 shown(schedule,1,4)=="11:30:00",
 shown(restored.sheets[0],1,1)=="2026-11-02",
 shown(restored.sheets[0],1,2)=="09:00:00",
 get(fmt,0,0).style.format=="datetime",
 shown(fmt,0,0)=="2024-01-01 12:00:00",
 get(fmt,0,1).style.format==null,
 shown(fmt,0,1)=="7",
 shown(fmt,0,2)=="09:00:00",
 shown(fmt,0,3)=="2024-01-01",
 v.formatted(0.5,{format:"datetime1904"})=="1904-01-01 12:00:00",
 v.formatted(45292+86399.8/86400,{format:"datetime"})=="2024-01-02 00:00:00",
 any([for (item in protected.diagnostics) item.code=="sheet-protection"]),
 any([for (item in workbook.diagnostics) item.code=="workbook-protection"]),
 any([for (item in hidden.diagnostics) item.code=="hidden-dimensions"]),
 any([for (item in hidden.diagnostics) item.code=="frozen-panes"])
]
