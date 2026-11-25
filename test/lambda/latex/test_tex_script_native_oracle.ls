import facade: lambda.doc.latex.tex_engine.facade

// This fixture was captured from the native provider before its retirement.
let cases = input("test/lambda/latex/fixtures/tex_native_oracle.json", "json")^;
let checked = [for (item in cases) {
    let result = if (item.kind == "tex") facade.tex(item.source, {ini:item.ini})^
        else facade.latex(item.source, {ini:item.ini})^;
    {source:item.source, issue:result.issue,
        matches:result.issue == null and format(result.root, 'json')^ == item.expected}
}];
{cases:len(checked), failures:[for (item in checked where not item.matches) item]}
