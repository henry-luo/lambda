import profile: lambda.doc.latex.tex_engine.profile
import recovery: ._tex_script_recovery
import digest: lambda.doc.latex.tex_engine.digest
import rebuild: lambda.doc.latex.tex_engine.reconstruct
let kernel=input(sys.lambda.home ++ "/package/doc/latex/tex_engine/kernel.tex","text")^;
let options={echo_definitions:true,adapters:[],resources:{local:{},bundled:{}}};
let cases=["\\the\\wd0/\\the\\ht0/\\the\\dp0",
 "\\wd0=1pt\\the\\wd0",
 "\\setbox0=\\hbox{Hi}\\the\\wd0/\\the\\ht0/\\the\\dp0",
 "\\setbox0=\\hbox{Hi}\\wd0=2pt\\ht0=3pt\\dp0=-4pt\\the\\wd0/\\the\\ht0/\\the\\dp0",
 "\\setbox0=\\hbox{Hi}\\setbox1=\\copy0\\wd1=2pt\\the\\wd0/\\the\\wd1",
 "\\setbox0=\\hbox{Hi}\\setbox1=\\box0\\wd1=2pt\\the\\wd0/\\the\\wd1",
 "\\setbox0=\\hbox{Hi}{\\wd0=2pt}\\the\\wd0",
 "\\setbox0=\\hbox{Hi}\\setbox1=\\copy0{\\setbox0=\\hbox{Other}\\wd1=3pt}\\the\\wd0/\\the\\wd1",
 "\\setbox0=\\hbox{Hi}{\\setbox0=\\hbox{Other}\\wd0=3pt}\\the\\wd0",
 "\\setbox0=\\hbox{Hi}{\\global\\wd0=2pt}\\the\\wd0",
 "\\setbox0=\\hbox{Hi}\\wd0=2pt\\dimen0=2\\wd0\\the\\dimen0",
 "\\setbox0=\\hbox{Hi}\\wd0=2pt\\count0=\\wd0\\the\\count0",
 "\\setbox0=\\hbox{Hi}\\wd0=2pt\\ifdim\\wd0=2pt Y\\else N\\fi",
 "\\setbox0=\\hbox{Hi}\\wd0=2pt\\skip0=\\wd0 plus 1pt\\the\\skip0",
 "\\setbox0=\\hbox{Hi}\\wd0=2pt\\the\\dimexpr\\wd0+1pt\\relax",
 "\\let\\w=\\wd\\setbox0=\\hbox{Hi}\\w0=2pt\\the\\w0",
 "\\setbox0=\\hbox{Hi}\\afterassignment A\\wd0=2pt\\the\\wd0",
 "\\setbox0=\\hbox{Hi}\\wd0=1pt\\wd0=2pt\\the\\wd0",
 "\\setbox0=\\hbox{Hi}\\wd0=16384pt\\the\\wd0",
 "\\setbox0=\\hbox{Hi}\\wd0=bad\\the\\wd0",
"\\setbox0=\\vbox{X}\\ht0=3pt\\dp0=4pt\\the\\ht0/\\the\\dp0",
 "\\def\\i{7}\\setbox\\i=\\hbox{A}\\wd\\i=2pt\\the\\wd7",
 "\\count0=7\\setbox7=\\hbox{A}\\wd\\count0=2pt\\the\\wd7",
 "\\setbox0=\\hbox{A}\\setbox1=\\hbox{B}\\wd0=3pt\\wd1=\\wd0\\the\\wd1",
 "\\setbox0=\\hbox{A}\\wd0=1pt\\dimen0=\\wd0\\wd0=3pt\\the\\dimen0",
 "\\setbox0=\\hbox{A}\\wd0=-2pt\\the\\dimexpr-\\wd0\\relax",
 "\\setbox0=\\hbox{A}\\wd0=-2pt\\the\\wd0/\\number\\wd0",
 "\\setbox0=\\hbox{A}\\setbox1=\\copy0{\\setbox0=\\hbox{B}\\wd1=3pt\\wd0=4pt}\\the\\wd0/\\the\\wd1",
 "\\setbox0=\\hbox{A}{\\setbox1=\\copy0\\wd1=3pt}\\the\\wd0/\\the\\wd1",
 "\\globaldefs=-1\\setbox0=\\hbox{A}{\\global\\wd0=4pt}\\the\\wd0",
 "\\setbox0=\\hbox{A}\\wd0=1pt\\setbox0=\\hbox{B}\\the\\wd0",
 "\\setbox0=\\hbox{A}\\wd0=1pt\\setbox1=\\copy0\\setbox0=\\hbox{B}\\the\\wd0/\\the\\wd1",
 "\\wd0=\\numexpr2+1\\relax pt X\\the\\wd0",
 "\\setbox0=\\hbox{A}\\wd0=\\numexpr2+1\\relax pt X\\the\\wd0",
 "\\setbox0=\\hbox{A}\\wd0=1pt\\ifnum\\wd0=65536 Y\\else N\\fi",
 "\\setbox0=\\hbox{A}\\wd0=1pt\\ifdim\\wd0<2pt Y\\else N\\fi",
 "\\setbox0=\\hbox{A}\\afterassignment\\relax\\wd0=2pt\\the\\wd0",
 "\\setbox0=\\hbox{A}\\wd0=2pt\\showthe\\wd0\\the\\wd0",
 "\\setbox0=\\hbox{A}\\ht0=16384pt\\dp0=\\ht0\\the\\dp0",
 "\\setbox0=\\vbox{A}\\setbox1=\\copy0\\dp1=-3pt\\the\\dp0/\\the\\dp1"];
let results=[for(source in cases) recovery.check(source)^];
{cases:len(results),supported:all([for(r in results) r.issue==null]),text:all([for(r in results) r.text]),
 diagnostics:all([for(r in results) r.diagnostics]),messages:all([for(r in results) r.messages])};
fn check_ast(body,mode) any^ {
 let source="\\begin{document}" ++ body ++ "\\end{document}";
 let r=profile.expand_with_kernel(source,kernel,{*:options,constructor_argument_mode:mode},100000)^;
 let d=digest.digest(r)^; let n=parse(source,{type:"latex"})^;
 {issue:d.issue,match:d.ast==n}
}
let bodies=["\\setbox0=\\hbox{Hi}\\wd0=2pt\\the\\wd0", "\\the\\wd0",
 "A\\setbox0=\\hbox{Hi}\\the\\wd0 B", "\\textbf{\\the\\wd0}"];
let trees=[for(body in bodies, mode in [false,true]) check_ast(body,mode)^];
{ast_cases:len(trees),supported:all([for(t in trees) t.issue==null]),matches:all([for(t in trees) t.match])};

let root="test/lambda/latex/fixtures/local_packages/";
let name="script-box-metrics-main.tex";
let child=input(root ++ "script-box-metrics-child.tex","text")^;
let source=input(root ++ name,"text")^;
let foreign=profile.expand_with_kernel(source,kernel,{*:options,source_name:root ++ name,
    resources:{local:{'script-box-metrics-child.tex':{data:child}},bundled:{}}},100000)^;
let native=input(root ++ name,"tex")^;
{issue:foreign.issue,text:rebuild.reconstruct(foreign)^.text==native.text,
 diagnostics:foreign.diagnostic_records==native.diagnostics,messages:foreign.messages==native.messages}
