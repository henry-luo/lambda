import e: lambda.doc.ebook
let book = e.parse(input("test/input/ebook/minimal.epub","zip")^)^
let chapter = e.chapter(book,0)^
let query = "世界 😀"
let results = e.search(book,query)^
let found = results[0].locator
let passages = e.search(book,"passage")^
let changed = e.parse(input("test/input/ebook/epub2.epub","zip")^)^;
[
    len(results) == 1,
    starts_with(found.quote,query),
    e.locate(book,found)^ == found,
    found.source_text_position == index_of(chapter.text,query),
    len(passages) == 2,
    len(e.search(book,"passage",{limit:1})^) == 1,
    (e.locate(changed,found) ^ { ^.code }) == 7809,
    (e.locate(book,{*:found,rendition:99}) ^ { ^.code }) == 7809,
    (e.locator(book,chapter,-1) ^ { ^.code }) == 7809,
    (e.locator(book,chapter,null) ^ { ^.code }) == 7809,
    (e.locate(book,{*:found,quote:"wrong passage"}) ^ { ^.code }) == 7809,
    e.search(book,"")^ == []
]
