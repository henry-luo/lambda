import d: lambda.doc.docx
let root = "test/input/docx/reference/"
let empty = d.convert(root ++ "empty.docx")^
let preserved = d.convert(root ++ "empty.docx",{include_empty_paragraphs:true})^
let embedded = d.convert(root ++ "embedded-style-map.docx")^
let external = d.convert(root ++ "external-picture.docx")^
let box = d.convert(root ++ "text-box.docx")^;
[
    empty.html == "<article class=\"docx-document\"></article>",
    empty.text == "" and len(empty.diagnostics) == 0,
    preserved.html == "<article class=\"docx-document\"><p></p></article>",
    preserved.text == "\n\n" and len(preserved.diagnostics) == 0,
    embedded.text == "Walking on imported air\n\n",
    contains(embedded.html,"<p>Walking on imported air") and not contains(embedded.html,"<h1>"),
    len(embedded.diagnostics) == 0,
    external.text == "" and not contains(external.html,"<img"),
    [for (issue in external.diagnostics) issue.code] == ["external-image"],
    (d.convert(root ++ "external-picture.docx",{unsupported:"error"}) ^ { ^.code }) == 7606,
    box.text == "Datum plane\n\n",
    contains(box.html,"<p>Datum plane") and not contains(box.html,"<p><p>"),
    [for (issue in box.diagnostics) issue.code] == ["unsupported-text-box-layout"],
    (d.convert(root ++ "text-box.docx",{unsupported:"error"}) ^ { ^.code }) == 7606,
    all([for (r in [external,box], issue in r.diagnostics)
        issue.severity == "warning" and issue.part == "word/document.xml"])
]
