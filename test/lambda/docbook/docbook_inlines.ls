import d: lambda.doc.docbook
let source = input("test/input/docbook/inlines.xml",{type:"xml",flavor:"document"})^
let result = d.convert(source)^
let html = d.to_html(source)^;
[
    len(result.diagnostics) == 0,
    contains(html,"<code class=\"email\">lambda@example.org</code>"),
    contains(html,"<q>quoted</q>"),
    contains(html,"class=\"directory filename\""),
    contains(html,"<em class=\"replaceable\">file</em>"),
    contains(html,"<em class=\"wordasword\">alpha</em>"),
    contains(html,"<sup>2</sup>"),
    contains(html,"class=\"productname trade\""),
    contains(html,"<strong class=\"bold notice\">Marked</strong>")
]
