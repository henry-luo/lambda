import profile: lambda.doc.latex.tex_engine.profile
let kernel = input(sys.lambda.home# ++ "/package/doc/latex/tex_engine/kernel.tex", "text")^;
fn report(source) any^ {
 let result = profile.expand_with_kernel(source, kernel, {script_expl3: true}, 100000)^;
 {text: join([for (t in result.output) if (t.kind == "control") "\\" ++ t.value else t.value], ""), issue: result.issue, unsupported: result.unsupported}
}
"basic:"; report("\\ExplSyntaxOn\\fp_eval:n{round(sqrt(2),4)}\\ExplSyntaxOff")
"expanded macro:"; report("\\ExplSyntaxOn\\cs_new:Npn\\myval:{2}\\fp_eval:n{sqrt(\\myval:)}\\ExplSyntaxOff")
"edef:"; report("\\ExplSyntaxOn\\edef\\saved{\\fp_eval:n{1/3}}\\saved\\ExplSyntaxOff")
"unsupported:"; report("\\ExplSyntaxOn\\fp_eval:n{sin(1)}\\ExplSyntaxOff")
"zero:"; report("\\ExplSyntaxOn\\fp_eval:n{1/0}\\ExplSyntaxOff")
let fixture_style = input("test/lambda/latex/fixtures/local_packages/lmdexpl.sty", "text")^;
let source = "\\usepackage{lmdexpl}\\greet/\\twice{A}/\\sumof{8}{5}/\\sorted{pear,apple,kiwi}/\\shout{Hello, world}/\\ready/\\roottwo";
let result = profile.expand_with_kernel(source, kernel, {script_expl3: true, resources: {local: {'lmdexpl.sty': {data: fixture_style}}, bundled: {}}}, 100000)^;
"full fixture:"; {text: join([for (t in result.output) t.value], ""), issue: result.issue, unsupported: result.unsupported}
"precision:"; report("\\ExplSyntaxOn\\fp_eval:n{9999999999999999+0.9}/\\fp_eval:n{0.1+0.2}/\\fp_eval:n{(1/3)*3}\\ExplSyntaxOff")
"rounding ties:"; report("\\ExplSyntaxOn\\fp_eval:n{round(2.5)}/\\fp_eval:n{round(3.5)}/\\fp_eval:n{round(-2.5)}/\\fp_eval:n{round(1250,-2)}\\ExplSyntaxOff")
"signed zero:"; report("\\ExplSyntaxOn\\fp_eval:n{-0}/\\fp_eval:n{-0*2}\\ExplSyntaxOff")
"nested expandable primitive:"; report("\\ExplSyntaxOn\\fp_eval:n{sqrt(\\int_eval:n{1+3})}\\ExplSyntaxOff")
"fractional round places unsupported:"; report("\\ExplSyntaxOn\\fp_eval:n{round(1.2345,1.5)}\\ExplSyntaxOff")
"literal precision bound:"; report("\\ExplSyntaxOn\\fp_eval:n{0.123456789012345650000000000000000001}\\ExplSyntaxOff")
"exponent bound:"; report("\\ExplSyntaxOn\\fp_eval:n{1e201}\\ExplSyntaxOff")
"domain:"; report("\\ExplSyntaxOn\\fp_eval:n{sqrt(-1)}\\ExplSyntaxOff")
"protected token unsupported:"; report("\\ExplSyntaxOn\\cs_new_protected:Npn\\locked:{1}\\fp_eval:n{\\locked:}\\ExplSyntaxOff")
