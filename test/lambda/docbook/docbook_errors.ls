import d: lambda.doc.docbook
let xml = "<article xmlns='http://docbook.org/ns/docbook'><title>T</title><para>P</para></article>"
fn parsed(text) element^ => parse(text,{type:"xml",flavor:"document"})^
let source = parsed(xml)^
let unsupported = parsed("<article xmlns='http://docbook.org/ns/docbook'><para>Before <custom>kept</custom> after.</para><para><xref linkend='absent'/></para></article>")^
let result = d.convert(unsupported)^;
[
    (d.convert(xml) ^ { ^.code }) == 7700,
    (d.convert(parsed("<article xmlns='urn:wrong'><para>P</para></article>")^) ^ { ^.code }) == 7700,
    (d.convert(parsed("<article xmlns='http://docbook.org/ns/docbook'><para xml:id='x'>A</para><para xml:id='x'>B</para></article>")^) ^ { ^.code }) == 7703,
    (d.convert(source,{unknown:true}) ^ { ^.code }) == 7701,
    (d.convert(source,{parameters:{["section-numbers"]:"true"}}) ^ { ^.code }) == 7701,
    (d.convert(source,{parameters:{["allow-eval"]:true}}) ^ { ^.code }) == 7701,
    (d.convert(source,{parameters:{["resource-base-uri"]:"../"}}) ^ { ^.code }) == 7701,
    [for (issue in result.diagnostics) issue.code] == ["unsupported-element","unresolved-reference"],
    contains(d.to_html(unsupported)^,"kept"),
    (d.convert(unsupported,{unsupported:"error"}) ^ { ^.code }) == 7704,
    (d.convert(parsed("<article xmlns='http://docbook.org/ns/docbook'><informaltable><tgroup cols='2'><tbody><row><entry namest='absent'>X</entry></row></tbody></tgroup></informaltable></article>")^) ^ { ^.code }) == 7705,
    (d.convert(parsed("<article xmlns='http://docbook.org/ns/docbook'><informaltable><tgroup cols='2'><tbody><row><entry morerows='2'>X</entry></row></tbody></tgroup></informaltable></article>")^) ^ { ^.code }) == 7705
]
