import d: lambda.doc.docx
let r = d.convert("test/input/docx/media.docx",{id_prefix:"doc-"})^
let omitted = d.convert("test/input/docx/media.docx",{images:"omit"})^
let external = d.convert("test/input/docx/external-image.docx")^;
[
    r.metadata.title == "Example document",
    r.metadata.creator == "Lambda",
    contains(r.html,"<span id=\"doc-mark\"></span>"),
    contains(r.html,"href=\"#doc-mark\""),
    contains(r.html,"href=\"https://example.com/?a=1&amp;b=2\""),
    contains(r.html,"src=\"data:image/png;base64,iVBORw0KGgo"),
    contains(r.html,"alt=\"Pixel &amp; alt\" width=\"96\" height=\"48\""),
    r.text == "ExampleJump\n\n",
    len(r.diagnostics) == 0,
    not contains(omitted.html,"<img"),
    omitted.diagnostics[0].code == "omitted-image",
    external.diagnostics[0].code == "external-image",
    not contains(external.html,"example.invalid"),
    content(content(r.document)[0])[3].width == "96"
]
