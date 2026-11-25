import facade: lambda.doc.latex.tex_engine.facade

let sources=["\\def\\x{World}Hello \\x", "\\message{hello}Text",
    "\\count0=42 \\the\\count0", "\\ifnum2>1 Yes\\else No\\fi"];
let checked=[for(source in sources) {source:source,actual:facade.tex(source,{ini:true},100000)^,
    native:parse(source,{type:"tex",ini:true})^}];
{cases:len(checked),failures:[for(item in checked where item.actual.issue!=null or item.actual.root!=item.native) item]}
