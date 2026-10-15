// Citation history and numbering are scoped to one immutable batch (S12.1.1v2).
import cp: lambda.doc.latex.citeproc.citeproc
import data: lambda.doc.latex.citeproc.data
import locale: lambda.doc.latex.citeproc.locale
let locales = locale.compile({'en-US':input("lmd/package/doc/latex/citeproc/locales/locales-en-US.xml", "text")^})^
let cases = input("test/lambda/latex/fixtures/citeproc/batch.json", "json")^;
[for (fixture in cases) {
 let compiled = cp.compile(fixture.style,locales)^
 let refs = data.json(fixture.references)^
 let result = cp.process(compiled,refs.references,fixture.requests,{nocite:fixture.nocite})^;
 [fixture.id, [for(citation in result.citations) citation.text] == fixture.expected]
}]
