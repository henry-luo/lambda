import d: lambda.doc.docbook
let source = input("test/input/docbook/article.xml",{type:"xml",flavor:"document"})^
let result = d.convert(source,{parameters:{["auto-toc"]:false,["section-numbers"]:false,["use-docbook-css"]:false}})^
let html = d.to_html(source,{parameters:{["auto-toc"]:false,["section-numbers"]:false,["use-docbook-css"]:false}})^
let moved = d.convert(source,{parameters:{["resource-base-uri"]:"assets/"}})^;
[
    len(result.assets) == 0,
    not contains(html,"list-of-titles"),
    not contains(html,"class=\"label\""),
    not contains(html,"docbook.css"),
    moved.assets[0].path == "assets/css/docbook.css",
    len(result.diagnostics) == 0
]
