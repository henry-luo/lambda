import xlsx: lambda.doc.xlsx
import files: ~~.mod_io
import m: ~~.mod_model
import f: ~~.mod_formula
import v: ~~.mod_view

let source="test/input/xlsx/basic.xlsx"
let pkg=xlsx.convert(source)^
let imported=files.import_xlsx(source)^
let book=files.restore(imported.book)^
let sheet=book.sheets[0]
let st=m.initial(book)
let edited=m.set_value(m.select(st,1,1),"20")^
let undone=m.history(edited,false)^
let imported1904=files.import_xlsx("test/input/xlsx/date1904.xlsx")^
let book1904=files.restore(imported1904.book)^
let state1904=m.initial(book1904)
let added1904=m.add_sheet(state1904)^
let notes_edit=m.set_value(m.select(m.activate(st,1),0,0),"Revised")^
let back=m.activate(notes_edit,0)^;
[
    len(pkg.sheets)==2,
    pkg.sheets[0].title=="Sales" and pkg.sheets[1].title=="Notes",
    pkg.sheets[0].cells[3].value=="Shared text",
    pkg.sheets[0].cells[6].formula=="SUM(B2,2)",
    pkg.diagnostics[0].code=="formula-recalculation" and pkg.sheets[1].merges[0]=={top:0,left:0,bottom:0,right:1},
    files.read(source)^==book,
    f.display(sheet,0,2)=="00123",
    f.display(sheet,1,1)==12.5,
    f.display(sheet,1,3)==14.5,
    f.display(sheet,1,4)==42,
    f.display(sheet,1,6)==14.5,
    f.display(sheet,1,7)==2024,
    f.display(sheet,1,8)==20,
    st.cache.values["H3"]=="Second worksheet",
    back.cache.values["H3"]=="Revised",
    contains(files.csv(m.indexed(back))^,"Revised"),
    book1904.sheets[0].date1904==true,
    files.restore(parse(format(book1904,'json')^,'json')^)^.sheets[0].date1904==true,
    f.display(book1904.sheets[0],1,7)==2028,
    m.current(added1904).date1904==true,
    v.formatted(f.display(sheet,1,5),sheet.cells[8].style)=="2024-01-01",
    v.formatted(60,{format:"date"})=="1900-02-29",
    v.formatted(0,{format:"date1904"})=="1904-01-01",
    f.display(m.current(edited),1,3)==22,
    f.display(m.current(edited),1,6)==22,
    f.display(m.current(edited),1,8)==20,
    f.display(m.current(edited),1,4)=="#NAME?",
    f.display(m.current(undone),1,4)==42,
    sheet.column_widths["0"]==145 and sheet.row_heights["0"]==40,
    sheet.cells[0].style.bold and sheet.cells[0].style.color=="#ff0000" and sheet.cells[0].style.fill=="#00ff00",
    f.display(book.sheets[1],0,0)=="Second worksheet"
]
