import d: lambda.doc.docbook
let source = input("test/input/docbook/book.xml",{type:"xml",flavor:"document"})^
let result = d.convert(source,{output:"chunked"})^
let chapter = format(result.pages[2].document,"html")^
let index = format(result.pages[0].document,"html")^;
[
    [for (page in result.pages) page.path] == ["index.html","pf01.html","ch01.html","ch01s02.html","ch02.html","appa.html"],
    contains(chapter,"href=\"ch02.html\""),
    contains(chapter,"id=\"install\""),
    not contains(chapter,"id=\"run\""),
    contains(index,"href=\"ch01.html#install\""),
    contains(index,"href=\"ch01s02.html#nested\""),
    not contains(index,"id=\"chapter-one\""),
    (d.to_html(source,{output:"chunked"}) ^ { ^.code }) == 7701,
    d.convert(source,{output:"chunked",parameters:{["use-id-as-filename"]:true}})^.pages[2].path == "chapter-one.html"
]
