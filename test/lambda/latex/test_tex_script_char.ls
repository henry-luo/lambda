import profile: lambda.doc.latex.tex_engine.profile
let native_refs=input("test/lambda/latex/fixtures/tex_char_native.json","json")^;
let kernel=input(sys.lambda.home ++ "/package/doc/latex/tex_engine/kernel.tex","text")^;
fn report(label,source) any^ {
 let r=profile.expand_with_kernel(source,kernel,{resources:{local:{},bundled:{}}},100000)^;
 let refs=[for(item in native_refs.cases where item.label==label)item];
 let ref=if(len(refs)==1) refs[0] else raise error("native character reference missing");
 let n=if(source==ref.source) ref.tex else raise error("native character source mismatch");
 {case:label,issue:r.issue,unsupported:r.unsupported,tokens:[for(t in r.output) {kind:t.kind,code:if(t.kind == "char") ord(t.value) else null,cat:t.cat}],diagnostics:r.diagnostics,native_diagnostics:[for(d in n.diagnostics) d.code],native_bytes:[for(b in binary(n.text)^) b]}
}
report("ascii", "\\char65\\char36")
report("octal and hex", "\\char'101\\char\"42")
report("character constant", "\\char`Z")
report("unicode", "\\char955\\char128578")
report("zero", "\\char0")
report("negative", "\\char-1")
report("too large", "\\char1114112")
report("surrogate", "\\char55296")
report("following", "\\char65!B")
report("count register", "\\count0=67\\char\\count0")
report("alias", "\\let\\c=\\char\\c68")
report("special characters", "\\char123\\char125\\char37\\char92")
report("inside macro", "\\def\\emit{\\char65}\\emit")
