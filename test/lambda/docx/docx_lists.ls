import d: lambda.doc.docx
let r = d.convert("test/input/docx/lists.docx")^;
[
    r.html == "<article class=\"docx-document\"><ol start=\"3\"><li><p>First</p><ol><li><p>Nested</p></li></ol></li><li><p>Next</p></li></ol><p>Interruption</p><ol start=\"5\"><li><p>Continued</p></li></ol><ul><li><p>Bullet</p></li></ul></article>",
    r.text == "First\n\nNested\n\nNext\n\nInterruption\n\nContinued\n\nBullet\n\n",
    len(r.diagnostics) == 0,
    content(r.document)[0].start == "3"
]
