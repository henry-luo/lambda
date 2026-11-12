// Array function results retain their shape inside callable formula plans.
import m: ~~.mod_model
import f: ~~.mod_formula
let sheet={*:m.empty_sheet(),cells:[{row:0,col:0,raw:"2"},{row:1,col:0,raw:"1"},{row:2,col:0,raw:"2"},
 {row:0,col:1,raw:"a"},{row:1,col:1,raw:"b"},{row:2,col:1,raw:"c"}]}
let result=(expr) any^ => f.raw_value({*:sheet,cells:[*sheet.cells,{row:8,col:4,raw:expr}]},8,4);
[
 result("=SEQUENCE(2,3)").values==[1,2,3,4,5,6],
 result("=SEQUENCE(2,3,10,2)").values==[10,12,14,16,18,20],
 result("=TRANSPOSE(A1:B3)").values==[2,1,2,"a","b","c"],
 result("=SORT(A1:B3,1,1)").values==[1,"b",2,"a",2,"c"],
 result("=UNIQUE(A1:A3)").values==[2,1],
 result("=UNIQUE(A1:A3,,TRUE)").values==[1],
 result("=FILTER(A1:B3,A1:A3=2)").values==[2,"a",2,"c"],
 result("=TEXTSPLIT(\"a,b;c,d\",\",\",\";\")").values==["a","b","c","d"],
 result("=SUM(SEQUENCE(3))")==6,
 result("=TEXTSPLIT(\"aXbXc\",\"x\",,,1)").values==["a","b","c"],
 result("=TEXTSPLIT(\"a,,b\",\",\",,TRUE)").values==["a","b"],
 result("=INDEX(SEQUENCE(3),2)")==2,
 result("=XLOOKUP(2,SEQUENCE(3),A1:A3)")==1,
 (result("=SUM(TEXTSPLIT(\"a,b;c\",\",\",\";\"))") ^ {^.message})=="#N/A",
 result("=LAMBDA(x,x*2)(SEQUENCE(3))").values==[2,4,6],
 result("=LET(x,SEQUENCE(3),x+1)").values==[2,3,4],
 result("=UNIQUE(TEXTSPLIT(\"One,one,TWO\",,\",\"))").values==["One","TWO"],
 result("=_xlfn._xlws.FILTER(A1:A3,A1:A3=2)").values==[2,2],
 result("=_xlfn.LET(_xlpm.x,SEQUENCE(2),_xlpm.x+1)").values==[2,3],
 result("=SEQUENCE(2)+TRANSPOSE(SEQUENCE(2))").values==[2,3,3,4],
 result("=FILTER(A1:A3,A1:A3>9,\"none\")")=="none",
 result("=SORT(A1:B3,1,-1)").values==[2,"a",2,"c",1,"b"],
 result("=TRANSPOSE(SEQUENCE(1,3))").rows==3
]
