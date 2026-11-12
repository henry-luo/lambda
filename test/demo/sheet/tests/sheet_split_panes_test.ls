// Split geometry uses OOXML twips and survives Sheet JSON (D11.1.2).
import x: lambda.doc.xlsx
import files: ~~.mod_io
import m: ~~.mod_model
let src="test/input/xlsx/split-panes.xlsx"
let parsed=x.convert(src)^
let book=files.read(src)^
let sheet=book.sheets[0]
let st=m.initial(book)^
let restored=files.restore(parse(format(book,'json')^,'json')^)^
let columns=files.read("test/input/xlsx/split-columns.xlsx")^
let rows=files.read("test/input/xlsx/split-rows.xlsx")^
let cs=columns.sheets[0].split
let rs=rows.sheets[0].split
let bad=(replacement) any^ => files.restore({*:book,sheets:[{*:sheet,split:replacement}]}) ^ { ^ };
[
 parsed.sheets[0].split=={x:6000.0,y:4500.0,top_left:{row:6,col:4},active:"bottomRight",selection:{row:7,col:5}},
 sheet.split==parsed.sheets[0].split,
 restored.sheets[0].split==sheet.split,
 not any([for (issue in parsed.diagnostics) issue.code=="split-panes"]),
 m.split_names(sheet.split)==["topLeft","topRight","bottomLeft","bottomRight"],
 st.pane_active=="bottomRight" and st.row==7 and st.col==5,
 st.split_ready==false,
 cs.x==6000 and cs.y==0 and cs.top_left=={row:6,col:4},
 rs.x==0 and rs.y==4500 and rs.top_left=={row:6,col:4},
 m.split_names(cs)==["topLeft","topRight"],
 m.split_names(rs)==["topLeft","bottomLeft"],
 m.initial(columns)^.pane_active=="topRight",
 m.initial(rows)^.pane_active=="bottomLeft",
 m.active_pane({*:cs,active:"bottomRight"})=="topRight",
 m.active_pane({*:rs,active:"bottomRight"})=="bottomLeft",
 (bad({*:sheet.split,x:-1}) is error),
 (bad({*:sheet.split,x:0,y:0}) is error),
 (bad({*:sheet.split,active:"unknown"}) is error),
 (bad({*:sheet.split,top_left:{row:1000000,col:0}}) is error),
 (files.restore({*:book,sheets:[{*:sheet,frozen:{rows:1,cols:1}}]}) ^ { ^ }) is error
]
