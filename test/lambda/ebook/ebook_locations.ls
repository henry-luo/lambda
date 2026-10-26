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
    e.search(book,"")^ == [],
    // options: exact case by default, case folding, whole words, one chapter
    len(e.search(book,"PASSAGE")^) == 0,
    len(e.search(book,"PASSAGE",{match_case:false})^) == 2,
    len(e.search(book,"pass")^) == 2,
    len(e.search(book,"pass",{whole_word:true})^) == 0,
    [for (r in e.search(book,"passage",{chapter:1})^) r.chapter] == [1],
    (e.search(book,"passage",{chapter:9}) ^ { ^.code }) == 7801,
    // the excerpt splits around the match
    (let r = e.search(book,"remember")^[0], r.before ++ r.match ++ r.after == "First chapterCafé 世界 😀. A passage to remember.Continue reading"
        and r.match == "remember")
]
