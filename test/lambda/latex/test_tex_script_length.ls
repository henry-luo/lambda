import profile: lambda.doc.latex.tex_engine.profile
import reconstruct: lambda.doc.latex.tex_engine.reconstruct
import digest: lambda.doc.latex.tex_engine.digest
let kernel=input(sys.lambda.home ++ "/package/doc/latex/tex_engine/kernel.tex","text")^;
let cases=["\\newlength{\\x}\\setlength{\\x}{2pt}\\the\\x", "\\newlength{abc}X", "\\newlength{}X", "\\newlength 4X", "\\newlength{\\x \\y}\\the\\x", "\\newlength{{\\x}}X", "\\newlength{\\relax}X", "\\newlength{\\x}\\newlength{\\x}X", "\\newlength{\\x}\\setlength{\\x}{4pt}\\addtolength{\\x}{1pt}\\the\\x", "\\setlength{\\textwidth}{12pt}\\the\\textwidth", "\\addtolength{\\textwidth}{3pt}\\the\\textwidth", "\\setlength{\\parindent}{3pt}\\the\\parindent", "\\setlength{\\parskip}{2pt plus 1fil}\\the\\parskip", "\\setlength{\\unknown}{2pt}X", "\\addtolength{\\unknown}{2pt}X", "\\newlength{\\x}{\\setlength{\\x}{2pt}\\the\\x}\\the\\x", "\\newlength{\\x}{\\globaldefs=1 \\setlength{\\x}{2pt}}\\the\\x", "\\newlength{\\x}\\afterassignment Q\\setlength{\\x}{2pt}\\the\\x", "\\newlength{\\x}\\setlength{\\x}{4}\\the\\x", "\\newlength{\\x}\\setlength{\\x}{16384pt}\\the\\x", "\\newlength{\\x}\\setlength{\\x}{\\dimexpr 1pt/0\\relax}\\the\\x", "\\newlength{\\x}\\let\\y=\\x\\setlength{\\y}{2pt}\\the\\x", "\\newskip\\a\\newlength{\\x}\\the\\count12", "\\def\\m{\\newlength{\\x}}\\m\\setlength{\\x}{2pt}\\the\\x", "\\def\\m{\\setlength{\\textwidth}{2pt}}\\m\\the\\textwidth", "\\newlength{\\x}\\begin{document}A\\setlength{\\x}{2pt}B\\the\\x\\end{document}", "\\newlength{\\x}\\begin{document}\\textbf{A\\setlength{\\x}{2pt}B}\\the\\x\\end{document}", "\\def\\f{\\setlength{\\parindent}{\\dimexpr1pt+2pt\\relax}}\\f", "\\def\\f{\\addtolength{\\parskip}{\\dimexpr1pt+2pt\\relax}}\\f", "\\def\\f{\\setlength{\\escapechar}{-1}\\setlength{\\parindent}{\\dimexpr1pt+2pt\\relax}}\\f", "\\catcode`\\~=13\\newlength{~}\\def\\f{\\setlength{~}{2pt}}\\f\\the~"];
fn check(source,mode) any^ {
 let result=profile.expand_with_kernel(source,kernel,{echo_definitions:true,constructor_argument_mode:mode,adapters:[],resources:{local:{},bundled:{}}},100000)^;
 let native=parse(source,{type:"tex"})^;
 {issue:result.issue,text:reconstruct.reconstruct(result)^.text==native.text,
  diagnostics:result.diagnostic_records==native.diagnostics,messages:result.messages==native.messages}
}
let results=[for(source in cases,mode in [false,true]) check(source,mode)^];
{cases:len(results),supported:all([for(r in results) r.issue==null]),text:all([for(r in results) r.text]),
 diagnostics:all([for(r in results) r.diagnostics]),messages:all([for(r in results) r.messages])};
let source="\\newlength{\\x}\\begin{document}A\\end{document}";
let trees=[for(mode in [false,true]) digest.digest(profile.expand_with_kernel(source,kernel,
 {echo_definitions:true,constructor_argument_mode:mode,adapters:[],resources:{local:{},bundled:{}}},100000)^)^];
{ast_cases:len(trees),supported:all([for(t in trees) t.issue==null]),matches:all([for(t in trees) t.ast==parse(source,{type:"latex"})^])};

let root="test/lambda/latex/fixtures/local_packages/";
let name="script-length-main.tex";
let child=input(root ++ "script-length-child.tex","text")^;
let main=input(root ++ name,"text")^;
let native=input(root ++ name,"tex")^;
let foreign=[for(mode in [false,true]) profile.expand_with_kernel(main,kernel,
 {echo_definitions:true,constructor_argument_mode:mode,adapters:[],source_name:root ++ name,
 resources:{local:{'script-length-child.tex':{data:child}},bundled:{}}},100000)^];
{foreign_cases:len(foreign),supported:all([for(r in foreign) r.issue==null]),
 text:all([for(r in foreign) reconstruct.reconstruct(r)^.text==native.text]),
 diagnostics:all([for(r in foreign) r.diagnostic_records==native.diagnostics]),
 messages:all([for(r in foreign) r.messages==native.messages])}
