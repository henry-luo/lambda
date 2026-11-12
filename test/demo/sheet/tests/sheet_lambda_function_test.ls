// Excel LAMBDA values use lexical bindings and the dynamic call path.
import m: ~~.mod_model
import f: ~~.mod_formula
let sheet=m.empty_sheet()
let book={sheets:[sheet],defined_names:[{name:"DOUBLE",formula:"_xlfn.LAMBDA(_xlpm.x,_xlpm.x*2)",scope:null},
    {name:"FACTORY",formula:"_xlfn.LAMBDA(_xlpm.n,IF(_xlpm.n<=1,1,_xlpm.n*FACTORY(_xlpm.n-1)))",scope:null}]}
let named=(expr) any^ => f.display({*:sheet,workbook:book,cells:[{row:0,col:0,raw:expr}]},0,0)
let result=(expr) any^ => f.display({*:sheet,cells:[{row:0,col:0,raw:expr}]},0,0);
[
 result("=LAMBDA(x,x+1)(5)")==6,
 result("=LAMBDA(x,y,x*y)(3,4)")==12,
 result("=LET(f,LAMBDA(x,x*2),f(7))")==14,
 result("=LET(n,3,f,LAMBDA(x,x+n),f(5))")==8,
 result("=LAMBDA(42)()")==42,
 result("=LAMBDA(x,x+1)(1,2)")=="#VALUE!",
 result("=LAMBDA(x,x)")=="#CALC!",
 result("=LAMBDA(x,x,x)(1,2)")=="#VALUE!",
 named("=DOUBLE(6)")==12,
 named("=FACTORY(5)")==120
]
