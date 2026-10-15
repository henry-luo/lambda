import d: lambda.doc.docx
let r = d.convert("test/input/docx/table-gaps.docx",{unsupported:"error"})^
let rows = content(content(content(r.document)[0])[0])
let cells = content(rows[1]);
[
    r.text == "A\tB\tC\tD\nOffset\n",
    len(cells) == 3,
    cells[0].class == "docx-grid-gap" and cells[0].colspan == "1",
    cells[2].class == "docx-grid-gap" and cells[2].colspan == "2",
    cells[0]["aria-hidden"] == "true" and cells[2]["aria-hidden"] == "true",
    content(content(cells[1])[0])[0] == "Offset",
    len(r.diagnostics) == 0
]
