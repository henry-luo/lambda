import d: lambda.doc.docbook
let source = input("test/input/docbook/tables.xml",{type:"xml",flavor:"document"})^
let result = d.convert(source)^
let html = d.to_html(source)^;
[
    len(result.diagnostics) == 0,
    contains(html,"<th colspan=\"3\">Heading</th>"),
    contains(html,"<td rowspan=\"2\">A</td>"),
    contains(html,"<td colspan=\"2\">D</td>"),
    contains(html,"Table <span class=\"label\">1</span>"),
    contains(html,"<thead><tr><th>Name</th><th>Value</th></tr></thead>")
]
