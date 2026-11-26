import profile: lambda.doc.latex.tex_engine.profile
import digest: lambda.doc.latex.tex_engine.digest
import reconstruct: lambda.doc.latex.tex_engine.reconstruct

// JSON normalizes element representation before comparing the captured tree.
pub fn tree_matches(actual, reference, source) bool^ =>
    source == reference.source and parse(format(actual,"json")^,{type:"json"})^ == reference.latex

// Compare both reconstructed text and the complete captured native tree.
pub fn compare(expanded, reference, source) any^ {
    let result=digest.digest(expanded)^;
    let rebuilt=reconstruct.reconstruct(expanded)^;
    {issue:result.issue,unsupported:result.unsupported,
        text_match:source==reference.source and rebuilt.text==reference.tex.text,
        ast_match:if(result.ast==null) null else tree_matches(result.ast,reference,source)^}
}

pub fn run(source, kernel, options, reference) any^ =>
    compare(profile.expand_with_kernel(source,kernel,options,100000)^,reference,source)
