import d: lambda.doc.docx
let saved = d.convert("test/input/docx/legacy-graphics.docx")^
let external = d.convert("test/input/docx/legacy-graphics-external.docx")^
let missing = d.convert("test/input/docx/legacy-graphics-missing.docx")^
let invalid = d.convert("test/input/docx/legacy-graphics-root.docx")^
let omitted = d.convert("test/input/docx/legacy-graphics.docx",{images:"omit"})^
let large = d.convert("test/input/docx/saved-graphics.docx",{presentation:"source",unsupported:"error"})^;
[
    saved.text == "First & label\nnext line\n<img src=x>\nSecond\nAssistant\n\n",
    contains(saved.html,"First &amp; label") and contains(saved.html,"&lt;img src=x&gt;"),
    [for (issue in saved.diagnostics) issue.code] == ["unsupported-diagram-layout"],
    contains(saved.html,"src=\"data:image/png;base64,") and contains(saved.html,"width=\"96\" height=\"48\""),
    not contains(saved.html,"Metadata") and not contains(saved.text,"duplicate") and not contains(saved.text,"metadata"),
    [for (issue in external.diagnostics) issue.code] == ["unsupported-diagram","unsupported-object-preview"],
    not contains(external.html,"example.invalid") and contains(external.text,"[Embedded object (Package)]"),
    [for (issue in missing.diagnostics) issue.code] == ["unsupported-diagram","unsupported-object-preview"],
    invalid.diagnostics[0].message == "Unsupported SmartArt data root",
    [for (issue in omitted.diagnostics) issue.code] == ["unsupported-diagram-layout","unsupported-object-preview"] and
        not contains(omitted.html,"<img"),
    (d.convert("test/input/docx/legacy-graphics.docx",{unsupported:"error"}) ^ { ^.code }) == 7606,
    len(large.diagnostics) == 0,
    contains(large.text,"Test\nT1\nT2\nT3") and contains(large.text,"Wildlife.wmv"),
    len(split(large.html,"<img"))-1 == 6 and len(split(large.html,"<svg"))-1 == 3,
    contains(large.html,"data:image/bmp;base64,") and not contains(large.html,"placeholder")
]
