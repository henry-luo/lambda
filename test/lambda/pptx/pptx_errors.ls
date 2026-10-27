import pptx: lambda.doc.pptx
let failures = [
    {file:"no-main",code:7702},{file:"escape",code:7705},{file:"encoded-escape",code:7705},
    {file:"macro",code:7702},{file:"missing-slide",code:7703},{file:"invalid-slide",code:7702},
    {file:"malformed",code:7704},{file:"dtd",code:7704},{file:"duplicate-slide",code:7702},
    {file:"external-slide",code:7702},{file:"invalid-size",code:7702}]
let warning = pptx.convert("test/input/pptx/unsupported.pptx")^
let external = pptx.convert("test/input/pptx/external-image.pptx")^;
[
    all([for (case in failures) (pptx.convert("test/input/pptx/" ++ case.file ++ ".pptx") ^ { ^.code })==case.code]),
    contains(warning.text,"Still editable"),
    [for (issue in warning.diagnostics) issue.code]==["unsupported-shape","unsupported-graphic","unsupported-transition","unsupported-animation"],
    all([for (issue in warning.diagnostics) issue.part=="slides/first.xml" and issue.severity=="warning"]),
    (pptx.convert("test/input/pptx/unsupported.pptx",{unsupported:"error"}) ^ { ^.code })==7706,
    (pptx.convert("https://example.invalid/deck.pptx") ^ { ^.code })==7700,
    (pptx.convert("test/input/pptx/minimal.pptx",{other:true}) ^ { ^.code })==7701,
    (pptx.convert("test/input/pptx/minimal.pptx",{images:"network"}) ^ { ^.code })==7701,
    external.diagnostics[0].code=="external-image",
    not contains(external.html,"example.invalid")
]
