import d: lambda.doc.docbook
pn main() {
    let source = d.read("test/input/docbook/book.xml")^
    let result = d.convert(source,{output:"chunked"})^
    let written = d.write(result,"temp/docbook-export/chunked")^
    let chapter = input("temp/docbook-export/chunked/ch01.html","text")^
    return [written.pages == 6,written.assets == 3,contains(chapter,"href=\"ch02.html\""),
        contains(chapter,"class=\"top\""),contains(chapter,"class=\"bottom\"")]
}
