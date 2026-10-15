import d: lambda.doc.docbook
let source = input("test/input/docbook/article.xml",{type:"xml",flavor:"document"})^
let result = d.convert(source)^
let html = d.to_html(source)^;
[
    result.metadata.title == "Lambda DocBook",
    result.compatibility.upstream_release == "2.8.5",
    result.entry == "index.html" and len(result.pages) == 1,
    len(result.diagnostics) == 0,
    result.toc[0].number == "1" and result.toc[1].href == "#second",
    contains(html,"Before <em>mixed</em> after &amp; &lt;safe&gt;."),
    contains(html,"Section <span class=\"label\">2</span>, “Second section”"),
    contains(html,"    second"),
    len(result.assets) == 3,
    starts_with(html,"<!DOCTYPE html>") and contains(html,"css/docbook.css")
]
