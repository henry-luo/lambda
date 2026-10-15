import e: lambda.doc.ebook
let zip = input("test/input/ebook/minimal.epub","zip")^
let book = e.parse(zip)^
let chapter = e.chapter(book,0)^
let second = e.chapter(book,"Books/text/two.xhtml")^
let ncx = e.parse(input("test/input/ebook/epub2.epub","zip")^)^.navigation.toc
let fallback = e.parse(input("test/input/ebook/fallback.epub","zip")^)^
let fixed = e.chapter(e.parse(input("test/input/ebook/fixed.epub","zip")^)^,0)^
let svg = e.chapter(e.parse(input("test/input/ebook/svg.epub","zip")^)^,0)^
let styled = e.chapter(e.parse(input("test/input/ebook/css-import.epub","zip")^)^,0)^;
[
    book.format == "epub",
    book.package == "Books/novel.opf",
    book.metadata.titles == ["A Small Book"],
    book.metadata.creators == ["Lambda"],
    len(book.reading_order) == 2,
    book.navigation.toc[0].target.href == "Books/text/one.xhtml",
    book.navigation.toc[0].children[0].label == "Next chapter",
    contains(chapter.text,"Café 世界 😀"),
    contains(chapter.html,"data:image/svg+xml;base64,"),
    contains(chapter.html,"data-ebook-href=\"Books/text/two.xhtml\""),
    len(chapter.diagnostics) == 0,
    second.text == "Next chapterThe last passage.",
    ncx[0].label == "NCX first",
    ncx[0].target.fragment == "start",
    e.chapter(fallback,0)^.text == chapter.text,
    fixed.viewport == {width:600.0,height:800.0},
    svg.layout == "fixed" and svg.viewport.width == 24.0,
    contains(styled.html,".imported"),
    not contains(styled.html,"@import"),
    not contains(styled.html,"https://example.invalid"),
    e.parse(input("test/input/ebook/minimal.epub","binary")^)^.source_identity == book.source_identity,
    len(book.source_identity) == 64,
    book.navigation.landmarks[0].label == "Start reading",
    book.navigation.page_list[0].label == "2",
    book.metadata.cover.href == "Books/images/dot.svg"
]
