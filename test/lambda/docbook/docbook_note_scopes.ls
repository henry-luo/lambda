import d: lambda.doc.docbook
let source = input("test/input/docbook/book_notes.xml",{type:"xml",flavor:"document"})^
let single = d.to_html(source)^
let chunked = d.convert(source,{output:"chunked"})^
let second = format(chunked.pages[2].document,"html")^
let unnumbered = d.convert(source,{output:"chunked",parameters:{["chunk-renumber-footnotes"]:false}})^
let original = format(unnumbered.pages[2].document,"html")^;
[
    contains(single,"href=\"#note-two-fnote\">2</a>"),
    contains(second,"href=\"#note-two-fnote\">1</a>"),
    contains(original,"href=\"#note-two-fnote\">2</a>"),
    contains(second,"2<span class=\"sep\">.</span>1"),
    not contains(second,"First note."),
    contains(second,"Second note."),
    len(chunked.diagnostics) == 0
]
