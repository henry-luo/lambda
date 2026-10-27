import docx: lambda.doc.docx
let losses = docx.export_document(input("test/input/docx/export/losses.html",'html')^)^
let bad_xml = parse("\"\\u0001\"",'json')^;
[
    all([for (opts in [false,[],{other:true},{unsupported:"ignore"},{styles:[]},{page:"bad"},{page:{width:-1}},{page:{left:1000}},{page:{typo:20}}])
        (docx.export_document(<p "Document">,opts) ^ { ^.code })==7601]),
    (docx.export_document(42) ^ { ^.code })==7600,
    (docx.export_document(<p bad_xml>) ^ { ^.code })==7600,
    (docx.export_document(input("test/input/docx/export/losses.html",'html')^,{unsupported:"error"}) ^ { ^.code })==7606,
    all([for (code in ["export-style","export-link","export-image","export-element"]) code in [for (issue in losses.diagnostics) issue.code]]),
    all([for (issue in losses.diagnostics) issue.severity=="warning" and issue.message is string]),
    not contains(format(losses.archive,'mark')^,"javascript:alert"),
    contains(format(losses.archive,'mark')^,"Remote image"),
    (docx.export_document(input("test/input/docx/export/styled.html",'html')^,{unsupported:"error"}) ^ { ^.code })==7606,
    len(docx.export_document(<article>)^.diagnostics)==0
]
