import d: lambda.doc.docx
let r = d.convert("test/input/docx/japanese-numbering.docx",{unsupported:"error"})^
let large = d.convert("test/input/docx/japanese-numbering-range.docx")^
let partial = d.convert("test/input/docx/unsupported-numbering.docx")^;
[
    len(r.diagnostics)==0,
    r.html=="<article class=\"docx-document\"><ol start=\"3\" style=\"list-style-type:japanese-informal;\"><li><p>First</p><ol style=\"list-style-type:japanese-informal;\"><li><p>Nested</p></li></ol></li><li><p>Next</p></li></ol><p>Interruption</p><ol start=\"5\" style=\"list-style-type:japanese-informal;\"><li><p>Continued</p></li></ol><ul><li><p>Bullet</p></li></ul></article>",
    content(r.document)[0].start=="3" and content(r.document)[2].start=="5",
    len(large.diagnostics)==1 and large.diagnostics[0].code=="unsupported-numbering-range",
    d.convert("test/input/docx/japanese-numbering-range.docx",{unsupported:"error"}) ^ { true } ~ { false },
    contains(partial.html,"Unsupported numbering sample") and len(partial.diagnostics)==1,
    partial.diagnostics[0].severity=="warning" and contains(partial.diagnostics[0].message,"ordinalText"),
    d.convert("test/input/docx/unsupported-numbering.docx",{unsupported:"error"}) ^ { true } ~ { false }
]
