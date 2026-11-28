import profile: lambda.doc.latex.tex_engine.profile
import recovery: ._tex_script_recovery
import digest: lambda.doc.latex.tex_engine.digest
import rebuild: lambda.doc.latex.tex_engine.reconstruct
import snapshot: ._tex_native_snapshot
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
let references=input("test/lambda/latex/fixtures/tex_box_dimensions_native.json","json")^;
// retain a fresh first expansion, then reuse the compatible immutable kernel.
let cached=profile.preload_kernel(kernel,options,100000)^;
let results=[for(i,source in cases) recovery.check_expansion(source,options,references.cases[i],if(i==0) null else cached)^];
{cases:len(results),supported:all([for(r in results) r.issue==null]),text:all([for(r in results) r.text_match]),
 diagnostics:all([for(r in results) r.diagnostics_match]),messages:all([for(r in results) r.messages_match])};
fn check_ast(body,mode,reference,cached) any^ {
 let source="\\begin{document}" ++ body ++ "\\end{document}";
 let r=profile.expand_with_kernel(source,kernel,{*:options,constructor_argument_mode:mode},100000,cached)^;
 let d=digest.digest(r)^;
 {issue:d.issue,match:snapshot.tree_matches(d.ast,reference,source)^}
}
let bodies=["\\setbox0=\\hbox{Hi}\\wd0=2pt\\the\\wd0", "\\the\\wd0",
 "A\\setbox0=\\hbox{Hi}\\the\\wd0 B", "\\textbf{\\the\\wd0}"];
let constructor_cached=profile.preload_kernel(kernel,{*:options,constructor_argument_mode:true},100000)^;
let trees=[for(i,body in bodies, mode in [false,true]) check_ast(body,mode,references.trees[i],
    if(i==0) null else if(mode) constructor_cached else cached)^];
{ast_cases:len(trees),supported:all([for(t in trees) t.issue==null]),matches:all([for(t in trees) t.match])};

let root="test/lambda/latex/fixtures/local_packages/";
let name="script-box-metrics-main.tex";
let child=input(root ++ "script-box-metrics-child.tex","text")^;
let source=input(root ++ name,"text")^;
let foreign=profile.expand_with_kernel(source,kernel,{*:options,source_name:root ++ name,
    resources:{local:{'script-box-metrics-child.tex':{data:child}},bundled:{}}},100000)^;
let native=references.local.tex;
let local_bound=source==references.local.source and child==references.local.child;
{issue:foreign.issue,text:local_bound and rebuild.reconstruct(foreign)^.text==native.text,
 diagnostics:foreign.diagnostic_records==native.diagnostics,messages:foreign.messages==native.messages}
