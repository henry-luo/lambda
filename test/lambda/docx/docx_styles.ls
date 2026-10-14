import d: lambda.doc.docx
let r = d.convert("test/input/docx/styles.docx")^
let mapped = d.convert("test/input/docx/styles.docx",{style_map:{Custom:{tag:"blockquote",class:"custom & quote\""}}})^
let missing = d.convert("test/input/docx/missing-style.docx")^;
[
    r.html == "<article class=\"docx-document\"><h1><strong><em>Inherited heading</em></strong></h1><p><strong><u><s>Marks</s></u></strong><em><sup>2</sup></em><em>\t</em><br><em>tail</em></p><h1><em>Toggled bold</em></h1></article>",
    r.text == "Inherited heading\n\nMarks2\t\ntail\n\nToggled bold\n\n",
    len(r.diagnostics) == 0,
    contains(mapped.html,"<blockquote class=\"custom &amp; quote&quot;\"><strong><em>Inherited heading"),
    missing.diagnostics[0].code == "missing-style",
    missing.text == "Body\n\n",
    d.to_html("test/input/docx/paragraph-mark.docx")^ == "<article class=\"docx-document\"><p><em>Visible text</em></p></article>"
]
