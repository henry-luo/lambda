import d: lambda.doc.docbook
let source = input("test/input/docbook/lists.xml",{type:"xml",flavor:"document"})^
let result = d.convert(source)^
let html = d.to_html(source)^;
[
    len(result.diagnostics) == 0,
    contains(html,"<li>One</li><li>Two</li>"),
    contains(html,"type=\"a\""),
    contains(html,"type=\"I\""),
    contains(html,"<span class=\"term\">First</span>"),
    contains(html,"<dd><p>Definition.</p></dd>"),
    contains(html,"Second</span>, <span class=\"term\">Alias</span>")
]
