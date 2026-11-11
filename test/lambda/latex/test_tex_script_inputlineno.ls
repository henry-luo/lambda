import profile: lambda.doc.latex.tex_engine.profile
let kernel=input(sys.lambda.home# ++ "/package/doc/latex/tex_engine/kernel.tex","text")^;
let baseline=parse("",{type:"tex"})^.messages;
fn report(label,source) any^ {
 let r=profile.expand_with_kernel(source,kernel,{resources:{local:{},bundled:{}}},100000)^;
 let n=parse(source,{type:"tex"})^;
 let messages=slice(n.messages,len(baseline),len(n.messages))^;
 {case:label,issue:r.issue,unsupported:r.unsupported,script:r.messages,native:messages,matches:r.messages == messages}
}
report("first line", "\\immediate\\write16{L:\\the\\inputlineno/\\number\\inputlineno}")
report("multiple lines", "\\immediate\\write16{A:\\the\\inputlineno}\n\\immediate\\write16{B:\\the\\inputlineno}\n%comment\n\\immediate\\write16{D:\\number\\inputlineno}")
report("CRLF", "\\immediate\\write16{A:\\the\\inputlineno}\r\n\\immediate\\write16{B:\\the\\inputlineno}")
report("line after blank", "\n\n\\immediate\\write16{L:\\number\\inputlineno}")
report("signed scan", "\\count0=-\\inputlineno\\immediate\\write16{N:\\the\\count0}")
report("macro call", "\\def\\where{\\the\\inputlineno}\n\\immediate\\write16{M:\\where}")
report("multiline expansion", "\\immediate\\write16{A\n\\the\\inputlineno\nZ}")
report("scantokens", "\\immediate\\write16{BEFORE:\\the\\inputlineno}\n\\scantokens{\\immediate\\write16{INNER:\\the\\inputlineno}}\\immediate\\write16{AFTER:\\the\\inputlineno}")
let root="test/lambda/latex/fixtures/local_packages/";
let doc=input(root ++ "script_inputline.tex","text")^;
let sty=input(root ++ "script_inputline.sty","text")^;
let x=profile.expand_with_kernel(doc,kernel,{adapters:[],resources:{local:{'script_inputline.sty':{data:sty}},bundled:{}}},100000)^;
let native=input(root ++ "script_inputline.tex","tex")^;
let nm=slice(native.messages,len(baseline),len(native.messages))^;
"local package:"; {issue:x.issue,unsupported:x.unsupported,script:x.messages,native:nm,matches:x.messages == nm}
