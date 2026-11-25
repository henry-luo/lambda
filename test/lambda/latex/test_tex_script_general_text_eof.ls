import facade: lambda.doc.latex.tex_engine.facade
let names=["message","errmessage","expanded","unexpanded","detokenize","scantokens"];
let tails=["", "\\relax", " "];
let checks=[for(name in names,tail in tails,ini in [false,true]) {
 source:"\\" ++ name ++ tail,ini:ini,
 actual:facade.tex("\\" ++ name ++ tail,{ini:ini,adapters:[],chaptered:[],resources:{local:{},bundled:{}}},100000)^,
 native:parse("\\" ++ name ++ tail,{type:"tex",ini:ini})^}];
{cases:len(checks),failures:[for(c in checks where c.actual.issue!=null or c.actual.root!=c.native)c]}
