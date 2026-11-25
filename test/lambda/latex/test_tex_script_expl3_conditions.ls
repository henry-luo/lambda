// D10.2.2v2: expl3 predicates inspect token meanings and unexpanded strings.
import facade: lambda.doc.latex.tex_engine.facade
let source = "\\ExplSyntaxOn" ++
    "\\def\\macro{a}\\protected\\def\\protectedmacro{b}" ++
    "\\token_if_expandable:NTF\\macro{T}{F}" ++
    "\\token_if_expandable:NTF\\protectedmacro{T}{F}" ++
    "\\token_if_expandable:NTF\\relax{T}{F}" ++
    "\\token_if_expandable:NTF\\missing{T}{F}" ++
    "\\token_if_expandable:NTF a{T}{F}" ++
    "\\token_if_expandable:NT\\macro{T}" ++
    "\\token_if_expandable:NF\\missing{F}" ++
    "\\int_eval:n{\\token_if_expandable_p:N\\macro}\\int_eval:n{\\token_if_expandable_p:N\\missing}" ++
    "\\str_if_eq:nnTF{\\macro}{\\macro}{T}{F}" ++
    "\\str_if_eq:nnTF{\\macro}{a}{T}{F}" ++
    "\\str_if_eq:nnT{a}{a}{T}\\str_if_eq:nnF{a}{b}{F}" ++
    "\\int_eval:n{\\str_if_eq_p:nn{a}{a}}\\int_eval:n{\\str_if_eq_p:nn{a}{b}}" ++
    "\\str_compare:nNnTF{a}<{b}{T}{F}\\str_compare:nNnF{b}<{a}{F}" ++
    "\\ExplSyntaxOff";
let result = facade.tex(source)^;
result.issue == null and len(result.root.diagnostics)^ == 0 and contains(result.root.text, "TTFFFTF10TFTF10TF")^
