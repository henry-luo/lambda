// Conditional formulas reuse spreadsheet calls and translate relative references (S12.3.4v2).
import x: lambda.doc.xlsx
import files: ~~.mod_io
import m: ~~.mod_model
import f: ~~.mod_formula
import cf: ~~.mod_conditional
let src="test/input/xlsx/conditional-formulas.xlsx"
let pkg=x.convert(src)^
let book=files.read(src)^
let st=m.initial(book)^
let sheet=m.indexed(st)
let restored=files.restore(parse(format(book,'json')^,'json')^)^
let style=(s,r,c) any^ => cf.style(s,r,c,f.display(s,r,c)^,{})^
let edited=m.set_value(m.select(st,2,1),"25")^
let changed=m.indexed(edited)
let anchor={row:1,col:2}
let dependent=m.set_value(m.select(st,1,1),"=B3*3")^
let dependent_sheet=m.indexed(dependent)
let broken={*:sheet,conditional_rules:[{ranges:[{top:1,left:2,bottom:1,right:2}],operator:"expression",
    values:[],formulas:["1/0"],anchor:anchor,priority:1,stop:false,style:{fill:"#ffff00"}}]};
[
 len(pkg.sheets[0].conditional_rules)==2 and len(pkg.diagnostics)==0,
 pkg.sheets[0].conditional_rules[0].operator=="expression",
 pkg.sheets[0].conditional_rules[1].formulas==["$E1"],
 style(sheet,1,2)^.fill=="#ffff00",
 style(sheet,2,2)^.fill==null,
 style(sheet,3,2)^.fill=="#ffff00",
 style(sheet,1,3)^.italic==true,
 style(sheet,2,3)^.italic==null,
 style(sheet,3,3)^.italic==true,
 style(changed,2,2)^.fill=="#ffff00",
 restored.sheets[0].conditional_rules==book.sheets[0].conditional_rules,
 f.conditional(sheet,"$B2>$E$1",anchor,1,2)==true,
 f.conditional(sheet,"$B2>$E$1",anchor,2,2)==false,
 f.conditional(sheet,"$B2>$E$1",anchor,3,2)==true,
 f.conditional(sheet,"B$2",anchor,3,2)==15,
 f.conditional(sheet,"$B2",anchor,3,3)==20,
 f.conditional(sheet,"$B$2",anchor,3,3)==15,
 f.conditional(sheet,"SUM($B2:B2)",anchor,2,2)==5,
 f.conditional(sheet,"B2",anchor,2,3)==1,
 f.conditional(sheet,"$E1",{row:1,col:3},2,3)==20,
 f.conditional(sheet,"$E1",{row:1,col:3},3,3)==3,
 f.conditional(sheet,"Notes!$A$1",anchor,3,3)=="Second worksheet",
 f.conditional(dependent_sheet,"$B2>$E$1",anchor,1,2)==true,
 f.conditional(dependent_sheet,"$B2>$E$1",anchor,2,2)==false,
 len(cf.style(broken,1,2,1,{})^)==0,
 (f.conditional(sheet,"A1",anchor,0,0) ^ { ^ }) is error,
 (f.conditional(sheet,"1/0",anchor,1,2) ^ { ^ }) is error,
 (f.conditional(sheet,"UNKNOWN(1)",anchor,1,2) ^ { ^ }) is error
]
