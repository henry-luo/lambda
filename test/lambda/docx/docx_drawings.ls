import d: lambda.doc.docx
let r = d.convert("test/input/docx/drawing-layers.docx",{presentation:"source",unsupported:"error"})^
let paragraphs = content(r.document)
let back = content(paragraphs[0])
let front = content(paragraphs[1])[0]
let unsupported = d.convert("test/input/docx/drawing-unsupported.docx")^
let omitted = d.convert("test/input/docx/drawing-layers.docx",{images:"omit"})^;
[
    len(r.diagnostics) == 0 and not contains(r.text,"Fallback"),
    contains(r.document.style,"position:relative;z-index:0;"),
    contains(paragraphs[0].style,"position:relative;") and name(back[2]) == 'br',
    contains(back[0].style,"left:0px;top:100px;width:200px;height:100px;z-index:-1;"),
    contains(back[1].style,"left:-24px;top:-12px;") and contains(back[1].style,"z-index:-2;"),
    content(back[0])[0].class == "docx-shape" and contains(content(back[0])[0].style,"background-color:#123456;"),
    content(back[0])[0]["aria-hidden"] == "true",
    contains(front.style,"left:48px;top:24px;") and contains(front.style,"z-index:1;margin-left:-20px;"),
    contains(content(front)[0].style,"background-color:#00AA11;"),
    content(back[1])[0].class == "docx-image-crop",
    contains(content(content(back[1])[0])[0].style,"width:200%;height:125%;left:-50%;top:-12.5%;"),
    contains(unsupported.text,"Fallback") and not contains(unsupported.html,"docx-shape"),
    [for (issue in unsupported.diagnostics) issue.code] == ["unsupported-alternate-content",
        "unsupported-shape","unsupported-shape","unsupported-image-crop","unsupported-floating-image"],
    (d.convert("test/input/docx/drawing-unsupported.docx",{unsupported:"error"}) ^ { ^.code }) == 7606,
    [for (issue in omitted.diagnostics) issue.code] == ["omitted-image"] and not contains(omitted.html,"<img")
]
