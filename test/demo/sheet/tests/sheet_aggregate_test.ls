// AGGREGATE honors options before evaluating referenced cells.
import m: ~~.mod_model
import f: ~~.mod_formula
let sheet={*:m.empty_sheet(),hidden_rows:[1],cells:[
 {row:0,col:0,raw:"1"},{row:1,col:0,raw:"2"},{row:2,col:0,raw:"=1/0"},
 {row:3,col:0,raw:"4"},{row:4,col:0,raw:"=SUBTOTAL(9,A1:A2)"},
 {row:5,col:0,raw:"=AGGREGATE(9,6,A1:A4)"}]}
let result=(expr) any^ => f.display({*:sheet,cells:[*sheet.cells,{row:8,col:1,raw:expr}]},8,1);
[
 result("=AGGREGATE(9,0,A1:A6)")=="#DIV/0!",
 result("=AGGREGATE(9,2,A1:A6)")==7,
 result("=AGGREGATE(9,3,A1:A6)")==5,
 result("=AGGREGATE(9,6,A1:A6)")==17,
 result("=AGGREGATE(1,3,A1:A6)")==2.5,
 result("=AGGREGATE(14,3,A1:A6,1)")==4,
 result("=AGGREGATE(15,3,A1:A6,2)")==4,
 result("=AGGREGATE(16,3,A1:A6,0.5)")==2.5,
 result("=AGGREGATE(20,3,A1:A6)")=="#VALUE!",
 result("=AGGREGATE(9,8,A1:A6)")=="#VALUE!",
 result("=AGGREGATE(14,3,A1:A6)")=="#VALUE!",
 result("=AGGREGATE(14,3,SEQUENCE(5),2)")==4,
 result("=AGGREGATE(9,0,A1:A2*2)")==6,
 result("=AGGREGATE(9,6,1/0)")==0,
 result("=AGGREGATE(9,6,SEQUENCE(3)/(SEQUENCE(3)-1))")==3.5,
 result("=AGGREGATE(9,0,SEQUENCE(3)/(SEQUENCE(3)-1))")=="#DIV/0!",
 result("=AGGREGATE(1,0,A1:A2)")==1.5,
 result("=AGGREGATE(2,0,A1:A2)")==2,
 result("=AGGREGATE(3,0,A1:A2)")==2,
 result("=AGGREGATE(4,0,A1:A2)")==2,
 result("=AGGREGATE(5,0,A1:A2)")==1,
 result("=AGGREGATE(6,0,A1:A2)")==2,
 result("=ROUND(AGGREGATE(7,0,A1:A2),6)")==0.707107,
 result("=AGGREGATE(8,0,A1:A2)")==0.5,
 result("=AGGREGATE(9,0,A1:A2)")==3,
 result("=AGGREGATE(10,0,A1:A2)")==0.5,
 result("=AGGREGATE(11,0,A1:A2)")==0.25,
 result("=AGGREGATE(12,0,A1:A2)")==1.5,
 result("=AGGREGATE(13,0,A1:A2)")=="#N/A",
 result("=AGGREGATE(14,0,A1:A2,1)")==2,
 result("=AGGREGATE(15,0,A1:A2,1)")==1,
 result("=AGGREGATE(16,0,A1:A2,0.5)")==1.5,
 result("=AGGREGATE(17,0,A1:A2,2)")==1.5,
 result("=AGGREGATE(18,0,A1:A2,0.5)")==1.5,
 result("=AGGREGATE(19,0,A1:A2,2)")==1.5
]
