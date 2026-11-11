import profile: lambda.doc.latex.tex_engine.profile
let kernel = input(sys.lambda.home# ++ "/package/doc/latex/tex_engine/kernel.tex", "text")^;
let baseline = parse("", {type:"tex"})^.messages;
fn report(name, source) any^ {
 let r = profile.expand_with_kernel(source, kernel, {resources:{local:{},bundled:{}}}, 100000)^;
 let n = parse(source, {type:"tex"})^;
 let m = slice(n.messages, len(baseline), len(n.messages))^;
 {name:name, issue:r.issue, script:r.messages, native:m, matches:r.messages == m, diagnostics:r.diagnostics}
}
report("single", "\\message{hello}")
report("successive", "\\message{A}\\message{B}")
report("write", "\\message{A}\\immediate\\write16{B}\\message{C}")
report("empty", "\\message{}\\message{A}\\message{}")
report("write empty", "\\message{A}\\immediate\\write16{}\\message{B}")
report("newline", "\\newlinechar=64\\message{A@@B@}\\message{C}")
report("changed newline", "\\message{A@B}\\newlinechar=64\\message{C@D}")
report("error", "\\message{A}\\errmessage{bad}\\message{B}")
report("show", "\\message{A}\\showtokens{B}\\message{C}")
report("expand", "\\newcounter{n}\\setcounter{n}{3}\\def\\word{Hi}\\message{\\word: \\arabic{n}}")
report("write only empty", "\\immediate\\write16{}")
report("write newline", "\\newlinechar=64\\immediate\\write16{A@B}")
report("leading newline", "\\newlinechar=64\\message{@A}")
report("newline as separator", "\\message{A}\\newlinechar=32\\message{B}")
report("show newline", "\\newlinechar=64\\message{A}\\showtokens{B@C}")
report("error newline", "\\newlinechar=64\\errmessage{A@B}")
report("unicode", "\\newlinechar=64\\message{λ@漢}")
