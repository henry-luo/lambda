// CSL tree evaluation and locale fallback are data-driven (D7.2.3, S1.8).
import cp: lambda.doc.latex.citeproc.citeproc
import data: lambda.doc.latex.citeproc.data
import loc: lambda.doc.latex.citeproc.locale
let locales = loc.compile({'en-US':input("lmd/package/doc/latex/citeproc/locales/locales-en-US.xml", "text")^,
 'de-DE':input("lmd/package/doc/latex/citeproc/locales/locales-de-DE.xml", "text")^,
 'fr-FR':input("lmd/package/doc/latex/citeproc/locales/locales-fr-FR.xml", "text")^})^
let cases = input("test/lambda/latex/fixtures/citeproc/core.json", "json")^;
[for (fixture in cases) {
 let compiled = cp.compile(fixture.style,locales)^
 let refs = data.json(fixture.references)^
 let result = cp.process(compiled,refs.references,[{id:"c1",items:[{id:"item"}]}])^;
 [fixture.id, result.citations[0].text == fixture.expected]
}]
