// D10.2.2v2: these outputs preserve native package and constructor cases.
import facade: lambda.doc.latex.tex_engine.facade
fn output_has(source, expected) any^ {
    let result=facade.tex(source)^;
    result.issue == null and len(result.root.diagnostics) == 0 and contains(result.root.text, expected)^
}
fn insert_boundary(source, expected) any^ {
    let result=facade.tex(source)^;
    result.issue == null and len(result.root.diagnostics) == 0 and
        not contains(result.root.text, "X")^ and not contains(result.root.text, "\\def")^ and
        contains(result.root.text, expected)^
}
let cases=[
    {source:"\\def\\a{A}\\edef\\b{\\expandafter\\unexpanded{\\a}}\\def\\a{B}\\b", expected:"B"},
    {source:"\\begin{document}\\newcounter{n}\\setcounter{n}{\\numexpr1+2}\\arabic{n}\\end{document}", expected:"3\\end{document}"},
    {source:"\\begin{document}\\newcounter{n}\\setcounter{n}{\\numexpr\\numexpr1+2\\relax+3}\\arabic{n}\\end{document}", expected:"6\\end{document}"},
    {source:"\\begin{document}\\newcounter{n}\\setcounter{n}{4}\\arabic{n}\\end{document}", expected:"4\\end{document}"},
    {source:"\\newenvironment{foo}{B}{E}\\begin{document}\\textbf{A\\begin{foo}X\\end{foo}Z}\\end{document}", expected:"ABXEZ"},
    {source:"\\newenvironment{foo}{B}{E}\\begin{document}\\textbf{\\begin{foo}X\\end{foo}\\begin{foo}Y\\end{foo}}\\end{document}", expected:"BXEBYE"}
];
let inserts=[
    {source:"\\begin{document}A \\insert0{\\vadjust{\\unskip}X}B\\end{document}", expected:"AB"},
    {source:"\\begin{document}A \\insert0{\\vadjust{\\unskip}\\global\\def\\saved{Y}X}\\saved B\\end{document}", expected:"AYB"}
];
[*[for (example in cases) output_has(example.source,example.expected)^],
    *[for (example in inserts) insert_boundary(example.source,example.expected)^]]
