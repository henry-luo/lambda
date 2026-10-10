// The opt-in CSL path uses the existing LaTeX scopes, footnotes and HTML serializer.
import latex: lambda.latex.latex
import html: lambda.latex.to_html
let source = input("test/lambda/latex/fixtures/citeproc/document.tex", "text")^
let ast = latex.parse_source(source, "test/lambda/latex/fixtures/citeproc/document.tex")
let styles = ["ieee", "apa", "chicago-author-date", "chicago-fullnote-bibliography"];
[for (profile in styles) {
 let result = latex.render_result(ast,{citeproc:{style:profile}})
 let text = html.to_html(result.elements);
 [profile, result.diagnostics == [],
  contains(text,"href=\"#bib-doe\""), contains(text,"href=\"#bib-s1-p2-roe\""),
  contains(text,"id=\"fn-2\""), contains(text,"An Uncited Resource"),
  result.metadata.bibliography.processor == "lambda-script-citeproc",
  all([for(asset in result.assets) asset.available]),
  if (profile == "chicago-fullnote-bibliography") contains(text,"id=\"fn-3\"")
    else not contains(text,"id=\"fn-3\"")]
}];
let bad = latex.render_result(ast,{citeproc:{style_xml:"<style/>"}});
[unique([for(issue in bad.diagnostics) issue.code]), contains(html.to_html(bad.elements),"Citation could not be processed")]
