import d: lambda.doc.docx
let r = d.convert("test/input/docx/tables.docx")^
let rows = content(content(content(r.document)[0])[0])
let merged = content(rows[1])[0];
[
    r.html == "<article class=\"docx-document\"><table><tbody><tr><th scope=\"col\"><p>Heading A</p></th><th scope=\"col\"><p>Heading B</p></th></tr><tr><td colspan=\"2\" rowspan=\"2\"><p>Merged</p></td></tr><tr></tr></tbody></table></article>",
    r.text == "Heading A\tHeading B\nMerged\n\n",
    len(r.diagnostics) == 0,
    merged.colspan == "2" and merged.rowspan == "2"
]
