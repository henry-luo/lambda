import facade: lambda.doc.latex.tex_engine.facade
let sources=["\\message{hello}","\\message X", "\\message{unfinished", "\\message\\relax{A}", "\\message {a{b}c}", "A\\message{B}C", "\\errmessage{bad}", "\\errmessage X", "\\errmessage{unfinished", "\\message\\def\\x{A}", "\\message", "\\message{}X"];
let results=[for(source in sources) {source:source,actual:facade.tex(source,{adapters:[],chaptered:[],resources:{local:{},bundled:{}}},100000)^,native:parse(source,{type:"tex"})^}];
{cases:len(results),failures:[for(item in results where item.actual.issue!=null or item.actual.root!=item.native)item]}
