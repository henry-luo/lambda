import d: lambda.doc.docx
let root = "test/input/docx/reference/"
let footnotes = d.convert(root ++ "footnotes.docx",{id_prefix:"doc-"})^
let endnotes = d.convert(root ++ "endnotes.docx",{id_prefix:"doc-"})^
let linked = d.convert(root ++ "footnote-hyperlink.docx")^
let comments = d.convert(root ++ "comments.docx")^;
[
    footnotes.text == "Ouch[1].[2]\n\n[1]  A tachyon walks into a bar.\n\n[2]  Fin.\n\n",
    contains(footnotes.html,"href=\"#doc-footnote-1\">[1]</a>"),
    contains(footnotes.html,"<li id=\"doc-footnote-1\"><p> A tachyon walks into a bar.</p></li>"),
    contains(footnotes.html,"<li id=\"doc-footnote-2\"><p> Fin."),
    endnotes.text == footnotes.text,
    contains(endnotes.html,"href=\"#doc-endnote-2\">[1]</a>"),
    contains(endnotes.html,"href=\"#doc-endnote-3\">[2]</a>"),
    contains(endnotes.html,"<section class=\"docx-endnotes\"><ol><li id=\"doc-endnote-2\">"),
    linked.text == "[1]\n\n[1]  Example\n\n",
    contains(linked.html,"href=\"http://www.example.com\"><u>Example</u></a>"),
    comments.text == "Ouch[1].[2]\n\n[1] A tachyon walks into a bar.\n\n[2] Fin.\n\n",
    contains(comments.html,"<section class=\"docx-comments\"><ol><li id=\"comment-0\">"),
    contains(comments.html,"href=\"#comment-2\">[2]</a>"),
    all([for (r in [footnotes,endnotes,linked,comments]) len(r.diagnostics) == 0]),
    d.convert(root ++ "footnotes.docx",{unsupported:"error"})^.text == footnotes.text
]
