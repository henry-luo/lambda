import d: lambda.doc.docx
let cases = [
    {file:"no-main",code:7602},{file:"escape",code:7605},{file:"encoded-escape",code:7605},
    {file:"macro",code:7602},{file:"malformed",code:7604},{file:"unbound-prefix",code:7604},
    {file:"dtd",code:7604},{file:"nul",code:7604},{file:"style-cycle",code:7607},
    {file:"missing-main-part",code:7603},{file:"duplicate-rel",code:7602},
    {file:"duplicate-types",code:7602},{file:"corrupt-member",code:407}]
let r = d.convert("test/input/docx/unsupported.docx")^
let strict = d.convert("test/input/docx/unsupported.docx",{unsupported:"error"}) ^ { ^.code }
let remote = d.convert("https://example.invalid/file.docx") ^ { ^.code };
let table = d.convert("test/input/docx/unsupported-table-child.docx")^;
[
    all([for (item in cases) (d.convert("test/input/docx/" ++ item.file ++ ".docx") ^ { ^.code }) == item.code]),
    [for (issue in r.diagnostics) issue.code] == ["unsupported-drawing","unsafe-or-missing-link","missing-annotation"],
    all([for (issue in r.diagnostics) issue.part == "word/document.xml" and issue.severity == "warning"]),
    not contains(r.html,"javascript:"),
    r.text == "Unsafe\n\n",
    strict == 7606,
    remote == 7600,
    table.diagnostics[0].code == "unsupported-element"
]
