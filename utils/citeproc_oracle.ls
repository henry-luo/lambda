// Tooling entry point: capture inputs once, then evaluate each independent batch.
import cp: lambda.latex.citeproc.citeproc
import data: lambda.latex.citeproc.data
import locale: lambda.latex.citeproc.locale
import c: lambda.latex.citeproc.common

fn evaluate(fixture, locales) map^ {
    let compiled = cp.compile(fixture.style, locales)^
    let refs = data.json(fixture.references)^
    let result = cp.process(compiled, refs.references, fixture.requests,
        {nocite: c.get(fixture, "nocite", [])})^;
    {id: fixture.id, citations: [for (item in result.citations) item.text],
        bibliography: [for (item in result.bibliography) item.text],
        diagnostics: result.diagnostics}
}

pn main() {
    let fixtures = input(sys.proc.self.env.CSL_ORACLE_INPUT#, "json")^
    let locales = locale.compile({'en-US': input(sys.lambda.home# ++ "/package/latex/citeproc/locales/locales-en-US.xml", "text")^,
        'de-DE': input(sys.lambda.home# ++ "/package/latex/citeproc/locales/locales-de-DE.xml", "text")^,
        'fr-FR': input(sys.lambda.home# ++ "/package/latex/citeproc/locales/locales-fr-FR.xml", "text")^})^
    let results = [for (fixture in fixtures) evaluate(fixture, locales) ^ {
        {id: fixture.id, citations: [], bibliography: [], diagnostics: [c.error_issue(^)]}
    }]
    output(results, sys.proc.self.env.CSL_ORACLE_OUTPUT#, "json")^
}
