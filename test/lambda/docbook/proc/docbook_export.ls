import d: lambda.doc.docbook
pn main() {
    let snapshot = d.read("test/input/docbook/article.xml")^
    let result = d.convert(snapshot)^
    let written = d.write(result,"temp/docbook-export/article")^
    let html = input(written.entry,"text")^
    let css = input("temp/docbook-export/article/css/docbook.css","text")^
    var bad = null
    d.write({*:result,assets:[{path:"../outside.css",text:"bad"}]},"temp/docbook-export/rejected") ^ { bad = ^.code }
    return [written.pages == 1,written.assets == 3,contains(html,"Lambda DocBook"),
        contains(css,"DocBook xslTNG version 2.8.5"),bad == 7707]
}
