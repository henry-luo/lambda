import e: lambda.doc.ebook
import c: lambda.doc.ebook.common
fn load_error(path) => e.parse(input(path,"zip")^) ^ { ^.code }
let unsafe = e.chapter(e.parse(input("test/input/ebook/unsafe.epub","zip")^)^,0)^;
[
    load_error("test/input/ebook/missing-spine.epub") == 7802,
    load_error("test/input/ebook/escape.epub") == 7805,
    load_error("test/input/ebook/protected.epub") == 7807,
    (e.parse(null) ^ { ^.code }) == 7800,
    (e.parse(null,{rendition:-1}) ^ { ^.code }) == 7801,
    (e.chapter(e.parse(input("test/input/ebook/css-cycle.epub","zip")^)^,0) ^ { ^.code }) == 7808,
    not contains(unsafe.html,"<script"),
    not contains(unsafe.html,"<iframe"),
    not contains(unsafe.html,"onclick"),
    not contains(unsafe.html,"https://example.invalid"),
    contains(unsafe.html,"Safe text"),
    len(unsafe.diagnostics) == 3,
    not contains(e.chapter(e.parse(input("test/input/ebook/raw-style.epub","zip")^)^,0)^.html,"</style><script>"),
    c.target("Books/text/one.xhtml","#caf%C3%A9")^.fragment == "café",
    c.target("Books/text/one.xhtml","#%2Fpart")^.fragment == "/part",
    (e.parse(null,{unknown:true}) ^ { ^.code }) == 7801,
    (e.chapter(null,0,{unknown:true}) ^ { ^.code }) == 7801,
    (e.search(null,"text",{unknown:true}) ^ { ^.code }) == 7801,
    (e.reader(null,{unknown:true}) ^ { ^.code }) == 7801,
    c.target("",c.url({href:"Books/text/a%2Fb.xhtml",fragment:"part%2F"}))^ == {href:"Books/text/a%2Fb.xhtml",fragment:"part%2F"},
    contains(e.chapter(e.parse(input("test/input/ebook/encoded-links.epub","zip")^)^,0)^.html,"#part%252F")
]
