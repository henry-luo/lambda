let source = "<x:t xmlns:x=\"urn:example\" xml:space=\"preserve\">  leading &amp; trailing  </x:t>"
let document = parse(source,{type:"xml",flavor:"document"})^
let legacy = parse(source,"xml")^
let file_document = input("test/input/docx/space.xml",{type:"xml",flavor:"document"})^;
[
    content(content(document)[0])[0] == "  leading & trailing  ",
    content(content(legacy)[0])[0] == "leading & trailing",
    type(parse("<a><b></a>",{type:"xml",flavor:"document"}) ^ { ^ }) == error,
    type(parse("<a/><b/>",{type:"xml",flavor:"document"}) ^ { ^ }) == error,
    type(parse("<x:a/>",{type:"xml",flavor:"document"}) ^ { ^ }) == error,
    type(parse("<a/>" ++ chr(0) ++ "hidden",{type:"xml",flavor:"document"}) ^ { ^ }) == error,
    content(content(file_document)[0])[0] == "  leading & trailing  "
]
