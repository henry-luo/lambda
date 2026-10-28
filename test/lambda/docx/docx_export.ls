import docx: lambda.doc.docx
import c: lambda.doc.docx.common
import xml: lambda.doc.xml
fn entry(tree,path) => if (len(path)==0) tree else entry([for (n in content(tree) where n.name==path[0]) n][0],drop(path,1))
// Observe serialized package parts, including the XML attribute types a real DOCX reader sees.
fn part(tree,path) => c.xml_node(xml.root(parse(format(content(entry(tree,split(path,"/")))[0],'xml')^,'xml')^))
let report = docx.export_document(input("test/input/docx/export/features.html",'html')^,{unsupported:"error"})^
let document = part(report.archive,"word/document.xml")
let numbering = part(report.archive,"word/numbering.xml")
let styles = part(report.archive,"word/styles.xml")
let rels = part(report.archive,"word/_rels/document.xml.rels")
let props = part(report.archive,"docProps/core.xml")
let paragraphs = c.children(c.child(document,"body"),"p")
let tables = c.descendants(document,"tbl",c.W)
let drawings = c.descendants(document,"inline",c.WP)
let ids = [for (n in c.descendants(document,"docPr",c.WP)) c.attr(n,"id","")]
let restart = docx.export_document(<article <ol <li "Same">><ol <li "Same">>>)^;
let widths = part(docx.export_document(<table style:"width:400px",<tr <td style:"width:100px","Narrow"><td style:"width:300px","Wide">>>)^.archive,"word/document.xml")
let caption = part(docx.export_document(<table <caption "A useful caption"><tr <td "Cell">>>)^.archive,"word/document.xml")
let indented = part(docx.export_document(<ol <li indent:2,"Nested level">>)^.archive,"word/document.xml");
[
    len(report.diagnostics)==0 and name(report.archive)=='fs',
    document.tag=="document" and document.uri==c.W,
    contains(c.text(document),"DOCX export 世界") and contains(c.text(document),"Styled & <XML>"),
    len(c.descendants(document,"b",c.W))>0 and len(c.descendants(document,"i",c.W))>0,
    c.attr(c.descendants(document,"u",c.W)[0],"val")=="single",
    [for (n in c.descendants(document,"vertAlign",c.W)) c.attr(n,"val")]==["subscript","superscript"],
    contains([for (n in c.descendants(document,"color",c.W)) c.attr(n,"val")],"2468ac"),
    contains([for (n in c.descendants(document,"sz",c.W)) c.attr(n,"val")],"36"),
    contains([for (n in c.descendants(document,"jc",c.W)) c.attr(n,"val")],"right"),
    len(c.descendants(numbering,"abstractNum",c.W))==3,
    c.attr(c.descendants(numbering,"start",c.W)[0],"val")=="3",
    len(c.descendants(part(restart.archive,"word/numbering.xml"),"num",c.W))==2,
    len(tables)==2 and len(c.descendants(tables[0],"gridCol",c.W))==3,
    c.attr(c.descendants(tables[0],"gridSpan",c.W)[0],"val")=="2",
    [for (n in c.descendants(tables[0],"vMerge",c.W)) c.attr(n,"val")]==["restart","continue"],
    len(c.children(c.children(tables[1],"tr")[1],"tc"))==4,
    len(drawings)==2 and len(unique(ids))==2,
    len(content(entry(report.archive,["word","media"])))==1,
    [for (n in c.descendants(document,"extent",c.WP)) [c.attr(n,"cx",""),c.attr(n,"cy","")]]==[["914400","457200"],["457200","228600"]],
    any([for (n in c.children(rels,null,c.P)) c.attr(n,"Target","")=="https://example.com/report?a=1&b=2" and c.attr(n,"TargetMode","")=="External"]),
    c.attr(c.descendants(document,"hyperlink",c.W)[1],"anchor")==c.attr(c.descendants(document,"bookmarkStart",c.W)[0],"name"),
    len(c.descendants(document,"tab",c.W))==1 and len(c.descendants(document,"br",c.W))==1,
    len(c.descendants(document,"pageBreakBefore",c.W))==1,
    c.attr(c.descendants(document,"pgSz",c.W)[0],"w")=="11906",
    c.attr(c.descendants(document,"pgMar",c.W)[0],"left")=="1440",
    c.text(c.child(props,"title","http://purl.org/dc/elements/1.1/"))=="Export & 世界",
    len(c.children(styles,"style"))==7,
    len(c.children(tables[0],"tr"))==2 and len(c.descendants(tables[0],"tblHeader",c.W))==1,
    [for (n in c.descendants(widths,"gridCol",c.W)) c.attr(n,"w")]==["1500","4500"],
    c.attr(c.descendants(indented,"ilvl",c.W)[0],"val")=="2",
    len(docx.export_document(<p <img src:"blue.png",alt:"Local">>,{base:"test/input/docx/export",unsupported:"error"})^.diagnostics)==0,
    c.text(c.child(c.child(caption,"body"),"p"))=="A useful caption" and len(c.descendants(caption,"tbl",c.W))==1
]
