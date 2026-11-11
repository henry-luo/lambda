import profile: lambda.doc.latex.tex_engine.profile
let kernel = input(sys.lambda.home# ++ "/package/doc/latex/tex_engine/kernel.tex", "text")^;
let baseline = parse("", {type:"tex"})^.messages;
fn report(label, source) any^ {
 let r=profile.expand_with_kernel(source,kernel,{resources:{local:{},bundled:{}}},100000)^;
 let n=parse(source,{type:"tex"})^;
 let m=slice(n.messages,len(baseline),len(n.messages))^;
 {case:label,issue:r.issue,unsupported:r.unsupported,script:r.messages,native:m,matches:r.messages == m}
}
report("open, close", "\\immediate\\openout0=unused.txt \\immediate\\write0{hidden}\\immediate\\closeout0\\immediate\\write0{visible}")
report("unopened", "\\write1{visible}")
report("independent", "\\openout2=unused.txt \\write1{one}\\write2{hidden}\\write3{three}")
report("close by wrapped number", "\\openout1=unused.txt \\closeout17\\write1{visible}")
report("open by wrapped number", "\\openout17=unused.txt \\write1{hidden}\\write17{visible}")
report("negative stream", "\\openout-1=unused.txt \\write15{hidden}\\write-1{visible}")
report("global scope", "{\\openout3=unused.txt }\\write3{hidden}{\\closeout3}\\write3{visible}")
report("buffer retained", "\\message{A}\\openout0=unused.txt \\write0{hidden}\\message{B}")
report("expanded contents", "\\openout0=unused.txt \\write0{\\message{nested}}\\message{A}")
report("aliased commands", "\\let\\op=\\openout\\let\\cl=\\closeout\\op4=unused.txt \\write4{hidden}\\cl4\\write4{visible}")
report("expanded filename", "\\def\\fname{unused.txt}\\openout0=\\fname \\write0{hidden}\\closeout0\\write0{visible}")
report("quoted filename", "\\openout0=\"unused name.txt\" \\write0{hidden}\\closeout0\\write0{visible}")
