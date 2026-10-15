import d: lambda.doc.docbook
let source = parse("<d:article xmlns:d='http://docbook.org/ns/docbook' xml:id='a'><d:info><d:title>T</d:title></d:info><d:para xml:space='preserve'>  left <x:emphasis xmlns:x='http://docbook.org/ns/docbook'>middle</x:emphasis> right  </d:para><d:section><d:title>Same</d:title></d:section><d:section><d:title>Same</d:title></d:section></d:article>",{type:"xml",flavor:"document"})^
let first = d.convert(source)^
let second = d.convert(source)^
let html = d.to_html(source)^;
[
    first.document == second.document,
    first.toc[0].href != first.toc[1].href,
    first.toc[0].href == "#a_s1" and first.toc[1].href == "#a_s2",
    contains(html,"  left <em>middle</em> right  "),
    len(first.diagnostics) == 0,
    (d.convert(<article <para "x">>) ^ { ^.code }) == 7700
]
