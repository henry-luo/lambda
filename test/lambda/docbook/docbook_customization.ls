import d: lambda.doc.docbook
fn emphasized(node,context,next) element^ {
    let underlying = next(node,context)^;
    <strong class:"custom-emphasis",*content(underlying)>
}
let source = parse("<article xmlns='http://docbook.org/ns/docbook'><para>A <emphasis>B</emphasis>.</para></article>",{type:"xml",flavor:"document"})^
let html = d.to_html(source,{customization:{emphasis:emphasized}})^
let instructions = parse("<article xmlns='http://docbook.org/ns/docbook'><para>Visible</para><?eval 1+2?></article>",{type:"xml",flavor:"document"})^;
[
    contains(html,"A <strong class=\"custom-emphasis\">B</strong>."),
    (d.convert(source,{customization:{emphasis:1}}) ^ { ^.code }) == 7701,
    d.convert(instructions)^.diagnostics[0].code == "unsupported-processing-instruction",
    (d.convert(instructions,{unsupported:"error"}) ^ { ^.code }) == 7704
]
