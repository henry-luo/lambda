import d: lambda.doc.docbook
let source = input("test/input/docbook/book.xml",{type:"xml",flavor:"document"})^
let result = d.convert(source)^
let html = d.to_html(source)^;
[
    len(result.diagnostics) == 0,
    len(result.toc) == 7,
    [for (entry in result.toc) entry.number] == [null,"1","1","2","2.1","2","A"],
    contains(html,"Chapter <span class=\"label\">1</span>"),
    contains(html,"2<span class=\"sep\">.</span>1"),
    contains(html,"Appendix <span class=\"label\">A</span>"),
    contains(html,"class=\"subtitle\">Native publishing"),
    contains(html,"Chapter <span class=\"label\">2</span>, Next steps")
]
