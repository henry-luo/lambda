import compare: ._tex_definition_compare
import profile: lambda.doc.latex.tex_engine.profile
import digest: lambda.doc.latex.tex_engine.digest
import reconstruct: lambda.doc.latex.tex_engine.reconstruct
let cases=[
 "\\def\\q{A}\\count0=4 {X}Z", "\\def\\q{A}{X}{Y}Z",
 "\\def\\q{A}{X}[Y](1,2)Z", "\\def\\q{A}{\\textbf{X}}Z",
 "\\newenvironment{q}{A}{B}\\count0=4{\\textbf{X}}Z",
 "\\def\\q{A}% comment\n{X}Z", "\\def\\q{A} % comment\n{X}Z",
 "\\def\\q{A}{X} % comment\n[Y]{Z}Q", "\\def\\q{A}%a\r\n%b\r\n{X}Z",
 "\\def\\q{A} % comment\n(non-coordinate)Z", "\\def\\q{A} {X}Z",
 "\\def\\group{{X}}\\def\\q{A}\\group Z", "\\def\\q#1{#1}\\count0=4{X}Z",
 "\\newcommand{\\q}[1]{#1}\\count0=4{X}Z", "\\def\\m{\\def~{X}{Y}}\\m~Z",
 "\\def\\q{A}\\count0=4{é}Z"
];
let results=[for(body in cases,mode in [false,true]) compare.check(body,mode)^];
{cases:len(results),supported:all([for(r in results) r.issue==null]),matches:all([for(r in results) r.matches==true])};
let root="test/lambda/latex/fixtures/local_packages/";let path=root ++ "script-definition-join-main.tex";
let child="script-definition-join-child.tex";let kernel=input(sys.lambda.home ++ "/package/doc/latex/tex_engine/kernel.tex","text")^;
fn foreign(mode) any^ {
 let r=profile.expand_with_kernel(input(path,"text")^,kernel,{source_name:path,constructor_argument_mode:mode,echo_definitions:true,adapters:[],resources:{local:{[child]:{data:input(root ++ child,"text")^}},bundled:{}}},100000)^;
 let d=digest.digest(r)^; let n=input(path,"latex")^; let t=input(path,"tex")^;
 {issue:d.issue,tree:d.ast==n,text:reconstruct.reconstruct(r)^.text==t.text,diagnostics:r.diagnostic_records==t.diagnostics}
}
let foreign_results=[for(mode in [false,true]) foreign(mode)^];
{cases:len(foreign_results),supported:all([for(r in foreign_results) r.issue==null]),tree:all([for(r in foreign_results) r.tree]),text:all([for(r in foreign_results) r.text]),diagnostics:all([for(r in foreign_results) r.diagnostics])}
