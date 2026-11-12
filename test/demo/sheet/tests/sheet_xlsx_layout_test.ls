// XLSX layout and differential formatting survive import and editable JSON (D11.1.2).
import x: lambda.doc.xlsx
import files: ~~.mod_io
import m: ~~.mod_model
import f: ~~.mod_formula
import c: ~~.mod_conditional
import win: ~~.mod_window
import layout: ~~.mod_grid_layout
import grid: lambda.ui.core.virtual_grid
let parsed=x.convert("test/input/xlsx/layout.xlsx")^
let imported=files.import_xlsx("test/input/xlsx/layout.xlsx")^
let clipped=files.import_xlsx("test/input/xlsx/layout-clipped.xlsx")^
let book=files.restore(imported.book)^
let sheet=book.sheets[0]
let restored=files.restore(parse(format(book,'json')^,'json')^)^
let st=m.initial(book)^
let c2=m.select(st,1,2)
let a2=m.select(st,2,0)
let down=m.move(a2,1,0)^
let right=m.move(c2,0,1)^
let row4=m.select(st,3,1)
let nextrow=m.move(row4,1,0)^
let a=win.axes(st)^
let columns=layout.segments(sheet,"column",a.columns,{first:0,last:7},1)^
let rows=layout.segments(sheet,"row",a.rows,{first:0,last:8},1)^
let frag1=layout.fragment(sheet,0,0,0,1,0,1)^
let frag2=layout.fragment(sheet,0,1,0,1,1,7)^
let frag3=layout.fragment(sheet,0,2,0,1,1,7)^
let hidden_anchor={*:sheet,hidden_columns:[{min:0,max:0}]}
let visible_fragment=layout.fragment(hidden_anchor,0,1,0,1,0,7)^
let high=c.style(sheet,1,1,15,{color:"#000000"})^
let low=c.style(sheet,2,1,5,{})^
let unchanged=c.style(sheet,3,1,-2,{})^
let bad=(replacement) any^ => files.restore({*:book,sheets:[{*:sheet,*:replacement}]}) ^ { ^ };
[
 parsed.sheets[0].merges==[{top:0,left:0,bottom:0,right:2},{top:1,left:0,bottom:2,right:0}],
 sheet.merges==parsed.sheets[0].merges,
 sheet.hidden_rows==[4] and sheet.hidden_columns==[{min:3,max:3}],
 sheet.frozen=={rows:1,cols:1},
 len(sheet.conditional_rules)==4 and len(parsed.diagnostics)==1 and parsed.diagnostics[0].code=="conditional-formatting",
 restored.sheets[0].merges==sheet.merges and restored.sheets[0].frozen==sheet.frozen,
 restored.sheets[0].conditional_rules==sheet.conditional_rules,
 restored.sheets[0].hidden_rows==sheet.hidden_rows and restored.sheets[0].hidden_columns==sheet.hidden_columns,
 high.color=="#ff0000" and high.fill=="#ffff00" and high.bold==true and high.italic==null,
 low.italic==true and low.fill==null,
 c.style(sheet,1,2,"Warning",{})^.italic==true,
 c.style(sheet,2,2,"",{})^.fill=="#ffff00",
 len(c.style(sheet,3,2,"other",{})^)==0,
 len(unchanged)==0,
 m.select(st,2,0).row==1 and m.select(st,2,0).col==0,
 down.row==3 and down.col==0,
 right.row==1 and right.col==4,
 m.select(c2,1,3).col==4,
 m.select(row4,4,1).row==5,
 nextrow.row==5 and nextrow.col==1,
 grid.offset(a.columns,4)^==grid.offset(a.columns,3)^,
 grid.offset(a.rows,5)^==grid.offset(a.rows,4)^,
 columns.fixed==[0] and not contains(columns.scroll,3),
 rows.fixed==[0] and not contains(rows.scroll,4),
 frag1.render and frag1.cols==1 and frag1.row==0 and frag1.col==0,
 frag2.render and frag2.cols==2 and frag2.row==0 and frag2.col==0,
 not frag3.render,
 visible_fragment.render and visible_fragment.show and visible_fragment.col==0,
 not frag2.show,
 (bad({merges:[{top:0,left:0,bottom:200000,right:1}]}) is error),
 (bad({hidden_rows:[-1]}) is error),
 (bad({hidden_columns:[{min:3,max:10000}]}) is error),
 (bad({frozen:{rows:999999,cols:0}}) is error),
 (bad({conditional_rules:[{ranges:[],operator:"equal",values:[1],priority:1,stop:false,style:{}}]}) is error),
 (bad({conditional_rules:[{ranges:[{top:0,left:0,bottom:0,right:0}],operator:"bad",values:[1],priority:1,stop:false,style:{}}]}) is error),
 f.display(sheet,1,1)==15,
 f.display(sheet,4,1)==99,
 clipped.book.sheets[0].rows==m.MAX_ROWS and clipped.book.sheets[0].conditional_rules[0].ranges[0].bottom==m.MAX_ROWS-1,
 any([for (issue in clipped.diagnostics) issue.code=="conditional-range-clipped"]),
 files.restore(clipped.book) is map
]
