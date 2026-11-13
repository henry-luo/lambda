import writers: lambda.doc.misc

let document = <doc <body
    <h1 "NAME AND \"OPTIONS\"">
    <h2 "Detailed section">
    <p "body">>>
let output = writers.man(document, {title:"example"})^
let multiline = <doc <body <h1 "first\n.SH injected">>>;
[
    contains(output, ".SH \"\\&NAME AND \\(dqOPTIONS\\(dq\"\n"),
    contains(output, ".SS \"\\&Detailed section\"\n"),
    not contains(output, ".SH\n"),
    (writers.man(multiline) ^ { ^.code }) == 7802
]
