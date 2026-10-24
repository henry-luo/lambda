import d: lambda.doc.docx
let r = d.convert("test/input/docx/drawing-edge-fallback.docx",{presentation:"source",unsupported:"error"})^
let interior = d.convert("test/input/docx/drawing-interior.docx")^
let partial = d.convert("test/input/docx/drawing-fallback-partial.docx")^
let visible = d.convert("test/input/docx/visible-stories.docx")^
let omitted = d.convert("test/input/docx/drawing-edge-fallback.docx",{images:"omit"})^
let large = d.convert("test/input/docx/saved-anchors.docx",
    {presentation:"source",include_empty_paragraphs:true})^;
[
    len(r.diagnostics) == 0 and r.text == "Wrapped text\n\n",
    len(split(r.html,"<img"))-1 == 2,
    contains(r.html,"float:left;margin-left:-4px;") and contains(r.html,"padding:6px 6px 6px 6px;"),
    not contains(r.html,"Fallback") and len(split(r.html,"class=\"docx-anchor\""))-1 == 2,
    [for (issue in interior.diagnostics) issue.code] == ["unsupported-floating-image"],
    [for (issue in partial.diagnostics) issue.code] == ["unsupported-drawing-content","unsupported-alternate-content"],
    [for (issue in visible.diagnostics) issue.code] == ["unsupported-story","unsupported-story","unsupported-story"],
    (d.convert("test/input/docx/invalid-header-root.docx") ^ { ^.code }) == 7602,
    [for (issue in omitted.diagnostics) issue.code] == ["omitted-image","omitted-image"],
    len(large.diagnostics) == 8 and all([for (issue in large.diagnostics) issue.code == "unsupported-floating-image"]),
    len(split(large.html,"<img"))-1 == 9,
    len(split(large.html,"class=\"docx-anchor\""))-1 == 1,
    contains(large.text,"Laura   Wood") and contains(large.text,"At The Park") and
        contains(large.text,"The romance novel is more than just a tale of love")
]
