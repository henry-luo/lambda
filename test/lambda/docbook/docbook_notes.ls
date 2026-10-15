import d: lambda.doc.docbook
let source = input("test/input/docbook/notes.xml",{type:"xml",flavor:"document"})^
let result = d.convert(source)^
let html = d.to_html(source)^;
[
    len(result.diagnostics) == 0,
    contains(html,"id=\"fn-fnote\""),
    contains(html,"href=\"#fn-fnote\""),
    contains(html,"Footnote body."),
    contains(html,"class=\"icon\">ⓘ"),
    contains(html,"class=\"title\">Careful"),
    contains(html,"Example <span class=\"label\">1</span>")
]
