import d: lambda.doc.docx
fn compact(text) => replace(replace(replace(replace(text,"\n",""),"\r",""),"\t","")," ","")
let cases = [
    {file:"biz-plan1",images:"png",tables:1,gaps:0,blocks:2,first:"April 4, 20XX",last:"Frankfort, KY 09876"},
    {file:"biz-plan2",images:"jpeg",tables:6,gaps:51,blocks:116,first:"Small Business Plan",last:"projections"}
];
[for (case in cases, let path = "test/input/docx/" ++ case.file,
    let r = d.convert(input(path ++ ".docx","zip")^,{presentation:"source",include_empty_paragraphs:true})^,
    let expected = input(path ++ ".body.txt","text")^)
    {file:case.file,checks:[
        name(r.document) == 'article',
        compact(r.text) == compact(expected),
        contains(r.text,case.first) and contains(r.text,case.last),
        len(split(r.html,"src=\"data:image/" ++ case.images ++ ";base64,"))-1 == 2,
        len(split(r.html,"<table"))-1 == case.tables,
        len(split(r.html,"class=\"docx-grid-gap\""))-1 == case.gaps,
        len(content(r.document)) == case.blocks,
        not contains(r.html,"instrText") and not contains(r.html,"PAGEREF"),
        all([for (issue in r.diagnostics) issue.code == "unsupported-story"]),
        all([for (issue in r.diagnostics)
            not contains(issue.message,"fldChar") and not contains(issue.message,"instrText")]),
        case.file != "biz-plan1" or len(r.diagnostics) == 0,
        case.file != "biz-plan2" or contains(r.html,"href=\"#_Toc197437037\""),
        case.file != "biz-plan2" or (len(split(r.html,"class=\"docx-anchor\""))-1 == 3 and
            len(split(r.html,"class=\"docx-shape\""))-1 == 1 and
            len(split(r.html,"class=\"docx-image-crop\""))-1 == 2 and
            contains(r.html,"left:-96px;top:671.2px;width:816px;height:288.96px;z-index:-2;") and
            contains(r.html,"background-color:#DCDDD7;") and len(r.diagnostics) == 12)
    ]}]
