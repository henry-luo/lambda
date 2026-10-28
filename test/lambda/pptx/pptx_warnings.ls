import pptx: lambda.doc.pptx
import display: lambda.doc.pptx.view
let result = pptx.convert("test/input/pptx/warnings.pptx")^
let groups = display.warning_groups(result.diagnostics)
let warning = display.warnings(result.diagnostics)
let collisions = display.warning_groups([{code:"a",message:"one"},{code:"b",message:"one"},{code:"a",message:"two"},{code:"a",message:"one"}]);
[
    len(result.diagnostics)==84 and len(groups)==14,
    all([for (group in groups) group.count==6]),
    len(content(result.document))==6 and contains(result.text,"Visible slide 6"),
    name(warning)=='details' and warning.open==null,
    content(content(warning)[0])[0]=="Presentation import: 84 warnings (14 types)",
    len(content(content(warning)[1]))==14,
    contains(format(warning,'html')^,"(6 occurrences)"),
    [for (group in collisions) [group.code,group.message,group.count]]==[["a","one",2],["b","one",1],["a","two",1]],
    len(display.warning_groups([]))==0 and content(content(display.warnings([{code:"a",message:"one"}]))[0])[0]=="Presentation import: 1 warning (1 type)",
    len(unique([for (issue in result.diagnostics) issue.part]))==7
]
