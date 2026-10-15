import d: lambda.doc.docx
let r = d.convert("test/input/docx/cached-fields.docx",{unsupported:"error"})^;
[
    r.text == "Summary\t3\n\nSaved result\n\nSummary body\n\n",
    contains(r.html,"<a href=\"#entry\">Summary</a>\t<strong>3</strong>"),
    contains(r.html,"<p>Saved result</p>"),
    contains(r.html,"<span id=\"entry\"></span>Summary body"),
    not contains(r.html,"PAGEREF") and not contains(r.html,"INCLUDETEXT"),
    not contains(r.html,"example.invalid"),
    len(r.diagnostics) == 0
]
