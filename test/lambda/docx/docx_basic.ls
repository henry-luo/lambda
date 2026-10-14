import d: lambda.doc.docx
fn captured(value:element) => value.entry_path
let r = d.convert("test/input/docx/minimal.docx")^
let zip = input("test/input/docx/minimal.docx","zip")^
let spaced = d.convert("test/input/docx/relocated.docx")^;
[
    name(r.document) == 'article',
    r.html == "<article class=\"docx-document\"><p>Hello, DOCX!</p></article>",
    r.text == "Hello, DOCX!\n\n",
    len(r.diagnostics) == 0,
    d.convert(zip)^.html == r.html,
    d.to_html(zip)^ == r.html,
    d.extract_text(zip)^ == r.text,
    d.to_elements(zip)^ == r.document,
    spaced.text == "  Café 世界 & <XML>  \n\n",
    spaced.html == "<article class=\"docx-document\"><p>  Café 世界 &amp; &lt;XML&gt;  </p></article>",
    d.extract_text("test/input/docx/alternate-prefix.docx")^ == "Prefix independent\n\n",
    d.extract_text("test/input/docx/strict.docx")^ == "Strict namespace\n\n",
    d.convert("test/input/zip/office.docx")^.text == "Hello\n\n",
    zip is element,
    zip is (element | null),
    [zip] is element[],
    captured(zip) == "",
    name(zip) == 'fs'
]
