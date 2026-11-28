import profile: lambda.doc.latex.tex_engine.profile
import reconstruct: lambda.doc.latex.tex_engine.reconstruct
import digest: lambda.doc.latex.tex_engine.digest
import native_snapshot: ._tex_native_snapshot
let native_refs=input("test/lambda/latex/fixtures/tex_read_stream_cache_native.json","json")^;
let root="test/lambda/latex/fixtures/local_packages/";
let path="test/lambda/latex/fixtures/local_packages/script_read_stream_cache.tex";
let kernel=input("lmd/package/doc/latex/tex_engine/kernel.tex","text")^;
let data="script_stream_data.dat";
let resources={local:{[data]:{data:input(root ++ data,"text")^}},bundled:{}};
[for(mode in [false,true]) {
 let r=profile.expand_with_kernel(input(path,"text")^,kernel,{source_name:path,echo_definitions:true,constructor_argument_mode:mode,adapters:[],resources:resources},100000)^;
 let native=native_refs.tex;let d=digest.digest(r)^;
 {supported:r.issue==null,text:path==native_refs.source and reconstruct.reconstruct(r)^.text==native.text,
  messages:r.messages==native.messages,diagnostics:r.diagnostic_records==native.diagnostics,
  tree:if(d.issue==null and native_snapshot.tree_matches(d.ast,native_refs,path)^) "native-match"
      else if(d.issue=="latex-digest-unsupported" and d.unsupported=="input stream command source mapping") "guarded" else "mismatch"}
}];
let bad={local:{'invalid.dat':{data:binary([255,10])^}},bundled:{}};
[for(source in ["\\openin0=invalid.dat\\relax", "\\openin0=invalid.dat\\relax\\readline0 to\\line"]) {
 let r=profile.expand_with_kernel(source,kernel,{echo_definitions:true,adapters:[],resources:bad},100000)^;
 {issue:r.issue,unsupported:r.unsupported}
}]
