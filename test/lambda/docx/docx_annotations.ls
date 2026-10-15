import d: lambda.doc.docx
let r = d.convert("test/input/docx/annotations.docx",{id_prefix:"n-"})^
let nested = d.convert("test/input/docx/nested-note.docx")^
let unsafe = d.convert("test/input/docx/unsafe-note-link.docx")^
let invalid = ["duplicate-note","missing-note-id","invalid-notes-root","external-notes"];
[
    r.text == "Body[1][1][1][1]\n\n[2]\n[1] Foot \n\nnote\n\n[2] Second\n\n[1] End\n\n[1] Comment\n\n",
    contains(r.html,"href=\"https://example.com/main\">Body</a>"),
    contains(r.html,"href=\"https://example.com/note\">note</a>"),
    len(split(r.html,"href=\"#n-footnote-9\"")) == 3,
    len(split(r.html,"id=\"n-footnote-9\"")) == 2,
    contains(r.html,"href=\"#n-footnote-2\">[2]</a>"),
    contains(r.html,"id=\"n-endnote-3\""),
    contains(r.html,"id=\"n-comment-2\""),
    len(r.diagnostics) == 0,
    all([for (file in invalid)
        (d.convert("test/input/docx/" ++ file ++ ".docx") ^ { ^.code }) == 7602]),
    [for (issue in nested.diagnostics) issue.code] == ["unsupported-nested-reference"],
    nested.diagnostics[0].part == "annotations/footnotes.xml",
    [for (issue in unsafe.diagnostics) issue.code] == ["unsafe-or-missing-link"],
    unsafe.diagnostics[0].part == "annotations/footnotes.xml",
    contains(unsafe.html,"<p>note</p>") and not contains(unsafe.html,"javascript:"),
    (d.convert("test/input/docx/nested-note.docx",{unsupported:"error"}) ^ { ^.code }) == 7606
]
