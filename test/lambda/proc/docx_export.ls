import docx: lambda.doc.docx
pn main() {
    let source = input("test/input/docx/export/features.html",'html')^
    let saved = docx.save(source,"temp/docx-export/report.docx",{unsupported:"error",title:"Report 世界",creator:"Lambda tests"})^
    let first = input("temp/docx-export/report.docx",'binary')^
    docx.save(source,"temp/docx-export/report-copy.docx",{unsupported:"error",title:"Report 世界",creator:"Lambda tests"})^
    let loaded = docx.convert("temp/docx-export/report.docx",{presentation:"source"})^
    output("previous content","temp/docx-export/protected.docx",{format:'text',atomic:true})^
    let refused:any|error = docx.save(input("test/input/docx/export/losses.html",'html')^,"temp/docx-export/protected.docx",{unsupported:"error"})
    let warned = docx.save(input("test/input/docx/export/losses.html",'html')^,"temp/docx-export/losses.docx")^;
    [
        saved.path=="temp/docx-export/report.docx" and len(saved.diagnostics)==0,
        first==input("temp/docx-export/report-copy.docx",'binary')^,
        loaded.metadata.title=="Report 世界" and loaded.metadata.creator=="Lambda tests",
        contains(loaded.text,"DOCX export 世界") and contains(loaded.text,"Same item continuation"),
        contains(loaded.html,"font-family:&quot;Georgia&quot;") and contains(loaded.html,"font-size:24px"),
        contains(loaded.html,"<table") and contains(loaded.html,"data:image/png;base64,"),
        refused is error and input("temp/docx-export/protected.docx",'text')^=="previous content",
        len(warned.diagnostics)>0 and contains(docx.extract_text("temp/docx-export/losses.docx")^,"Still usable")
    ]
}
