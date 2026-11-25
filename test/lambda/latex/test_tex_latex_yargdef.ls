// S1.8, D10.2.2v2: package definitions preserve their argument signatures.
let cases = [
    "\\makeatletter\\let\\l@ngrel@x\\long\\@yargdef\\foo\\@ne{1}{<#1>}\\foo{A}\\makeatother",
    "\\makeatletter\\@yargdef\\foo\\tw@{2}{#1/#2}\\foo[O]{R}\\makeatother",
    "\\makeatletter\\@yargdef\\foo\\@ne{0}{Z}\\foo\\makeatother",
    "\\makeatletter\\@yargdef\\foo\\@ne{12}{Z}\\foo\\makeatother",
    "\\makeatletter\\let\\l@ngrel@x\\protected\\@yargdef\\foo\\@ne{1}{<#1>}\\edef\\a{\\foo{A}}\\a\\makeatother",
    "\\makeatletter\\@yargdef\\foo\\@ne{1}{#2}\\foo{A}\\makeatother"
];
fn run(source) any^ {
    let result = parse(source,"tex")^;
    {text:result.text, diagnostics:[for (diagnostic in result.diagnostics) diagnostic.code]}
}
[for (source in cases) run(source)^]
