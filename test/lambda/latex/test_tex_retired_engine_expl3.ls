// D10.2.2v2: preserve the native expl3 expansion and undefined-token cases.
import facade: lambda.doc.latex.tex_engine.facade
let examples = [
    "\\ExplSyntaxOn\\text_uppercase:n{aB}\\ExplSyntaxOff",
    "\\ExplSyntaxOn\\text_uppercase:n{\\ref{aB}}\\ExplSyntaxOff",
    "\\ExplSyntaxOn\\text_uppercase:n{\\pageref{aB}}\\ExplSyntaxOff",
    "\\ExplSyntaxOn\\text_uppercase:n{\\missingcommand{aB}}\\ExplSyntaxOff",
    "\\ExplSyntaxOn\\text_uppercase:n{\\relax{aB}}\\ExplSyntaxOff",
    "\\def\\eqref#1{(#1)}\\ExplSyntaxOn\\text_uppercase:n{\\eqref{aB}}\\ExplSyntaxOff",
    "\\ExplSyntaxOn\\text_uppercase:n{\\eqref{aB}}\\ExplSyntaxOff",
    "\\ExplSyntaxOn\\text_uppercase:n{\\ref{aB}/\\pageref{aB}/\\eqref{aB}}\\ExplSyntaxOff"
];
fn accepts(source) any^ {
    let result=facade.tex(source)^;
    result.issue == null and not any([for (d in result.root.diagnostics)
        d.code == "tex-missing-number" or d.code == "tex-missing-brace"])
}
let source = "\\ExplSyntaxOn X\\token_if_expandable:NTF\\missingcommand{T}{F}Y" ++
    "X\\cs_if_exist:cTF{l__text_expand_\\token_to_str:N\\missingcommand _tl}{T}{F}Y" ++
    "X\\str_if_eq:nnTF{\\missingcommand}{\\missingcommand}{T}{F}Y" ++
    "X\\str_if_eq:nnTF{\\missingcommand}{\\ref}{T}{F}Y\\ExplSyntaxOff";
let result=facade.tex(source)^;
[*[for (example in examples) accepts(example)^],
    result.issue == null and len(result.root.diagnostics) == 0 and contains(result.root.text,"XFYXFYXTYXFY")^]
