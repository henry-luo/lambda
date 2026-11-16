import profile: lambda.doc.latex.tex_engine.profile
import reconstruct: lambda.doc.latex.tex_engine.reconstruct
import reader: lambda.doc.latex.tex_engine.reader
let kernel=input("lmd/package/doc/latex/tex_engine/kernel.tex","text")^;
let source=join([for(i in 1 to 20000) "% ignored"], "\n") ++ "\nZ";
let token=reader.next(reader.reader(source))^;
let r=profile.expand_with_kernel(source,kernel,{echo_definitions:true,adapters:[],resources:{local:{},bundled:{}}},100000)^;
let native=parse(source,{type:"tex"})^;
{token:token.token.value,line:token.reader.line_no,issue:r.issue,text:reconstruct.reconstruct(r)^.text==native.text,
 messages:r.messages==native.messages,diagnostics:r.diagnostic_records==native.diagnostics}
