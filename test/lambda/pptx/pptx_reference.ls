import pptx: lambda.doc.pptx
import slide: lambda.slide
let references = input("test/input/pptx/reference/oracle.json",'json')^
fn compact(value) => join([for (ch in split(value,"")) if (trim(ch)=="") "" else ch],"")
let results = [for (reference in references) {
    let imported = pptx.convert("test/input/pptx/reference/" ++ reference.file)^
    let plan = slide.compile(imported.document)^;
    {reference:reference,result:imported,plan:plan}
}];
[
    len(results)==6,
    all([for (row in results) len(row.plan.slides)==row.reference.slides]),
    all([for (row in results) abs(row.plan.width-row.reference.width)<0.001 and abs(row.plan.height-row.reference.height)<0.001]),
    all([for (row in results) compact(row.result.text)==compact(row.reference.text)]),
    len(results[0].result.diagnostics)==0,
    not contains(results[0].result.text,"•"),
    [for (issue in results[1].result.diagnostics) issue.code]==["unsupported-shape"],
    contains(results[2].result.html,"font-style:italic") and contains(results[2].result.html,"font-style:normal"),
    contains(results[2].result.html,"text-decoration:none") and contains(results[2].result.html,"text-decoration-style:wavy"),
    contains(results[2].result.html,"lang=\"fr-FR\""),
    len(results[3].plan.slides[0].notes)>0,
    len([for (scene in results[4].plan.slides,obj in scene.objects where obj.tag=='image') obj])==2,
    contains(results[5].result.text,"a\tb\tc\nd\te\tf\ng\th\ti"),
    all([for (row in results,issue in row.result.diagnostics) issue.severity=="warning"])
]
