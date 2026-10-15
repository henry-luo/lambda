import d: lambda.doc.docx
let root = "test/input/docx/reference/"
let paragraph = d.convert(root ++ "single-paragraph.docx")^
let table = d.convert(root ++ "tables.docx")^
let numbered = d.convert(root ++ "simple-list.docx")^
let picture = d.convert(root ++ "tiny-picture.docx")^
let strike = d.convert(root ++ "strikethrough.docx")^
let underline = d.convert(root ++ "underline.docx")^
let bom = d.convert(root ++ "utf8-bom.docx")^
let strict = d.convert(root ++ "strict-format.docx")^
let base_picture = d.convert(root ++ "tiny-picture-target-base-relative.docx")^;
[
    paragraph.text == "Walking on imported air\n\n",
    table.text == "Above\n\nTop left\tTop right\nBottom left\tBottom right\nBelow\n\n",
    numbered.text == "Apple\n\nBanana\n\n",
    contains(numbered.html,"<ul><li><p>Apple</p></li>"),
    contains(picture.html,"data:image/png;base64,iVBORw0KGgo"),
    contains(picture.html,"width=\"10\" height=\"10\""),
    contains(strike.html,"<s>Today's Special: Salmon</s>"),
    contains(underline.html,"<u>Sunset</u>"),
    bom.text == "This XML has a byte order mark.\n\n",
    strict.text == "Test\n\n",
    all([for (r in [paragraph,table,numbered,picture,strike,underline,bom,strict,base_picture]) len(r.diagnostics) == 0]),
    base_picture.html == picture.html
]
