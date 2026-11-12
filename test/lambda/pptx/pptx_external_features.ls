import pptx: lambda.doc.pptx
import slide: lambda.slide

let root="test/input/pptx/external/"
let merged=pptx.convert(root ++ "mit-table-merge-encoding.pptx")^
let html=format(merged.document,'html')^
let diagram=pptx.convert(root ++ "mit-smartart-families.pptx")^
let plan=slide.compile(diagram.document)^
let saved=slide.compile(parse(format(diagram.document,'mark')^,'mark')^)^
let charts=pptx.convert(root ++ "mit-chart-series-shapes.pptx")^
let chart_plan=slide.compile(charts.document)^
fn scene_html(index) any^ => format(chart_plan.slides[index].source,'html')^
let malformed=pptx.convert("test/input/pptx/inconsistent-table-merge.pptx")^
let media=pptx.convert(root ++ "mit-av-media.pptx")^
let model3d=pptx.convert(root ++ "mit-model3d.pptx")^
let hidden=pptx.convert(root ++ "cc0-with-hidden-slide.pptx")^
let hidden_plan=slide.compile(hidden.document)^;
[
    len(merged.diagnostics)==0,
    contains(html,"colspan=\"2\" rowspan=\"2\""),
    contains(html,"colspan=\"3\" rowspan=\"3\""),
    contains(html,"colspan=\"2\"") and contains(html,"rowspan=\"2\""),
    contains(merged.text,"3,3") and contains(merged.text,"2,3"),
    len(plan.slides)==4 and all([for (scene in plan.slides) len(scene.objects)>0]),
    all([for (value in ["org-root","org-grandchild","proc-1","cycle-1","pic-1"]) contains(diagram.text,value)]),
    len([for (issue in diagram.diagnostics where issue.code=="partial-smartart") issue])==4,
    not any([for (issue in diagram.diagnostics) issue.code=="unsupported-graphic"]),
    len(saved.slides)==4 and contains(format(saved.slides[0].source,'html')^,"org-root"),
    [for (scene in hidden_plan.slides) scene.hidden]==[false,true,false],
    slide.reduce(hidden_plan,slide.initial_state(hidden_plan),{command:'next',time_ms:0.0})^.slide==2,
    len(split(scene_html(0)^,"<circle "))-1==4,
    len(split(scene_html(1)^,"<circle "))-1==4,
    contains(scene_html(3)^,"North") and contains(scene_html(3)^,"West"),
    len(split(scene_html(6)^,"<circle "))-1==4 and len(split(scene_html(7)^,"<circle "))-1==4,
    len([for (issue in charts.diagnostics where issue.code=="partial-chart") issue])==5,
    len([for (issue in charts.diagnostics where issue.code=="unsupported-chart") issue])==3,
    [for (issue in malformed.diagnostics) issue.code]==["unsupported-table-merge"],
    contains(malformed.text,"Quarter") and contains(malformed.text,"Revenue") and not contains(malformed.html,"colspan=\"2\""),
    (pptx.convert("test/input/pptx/inconsistent-table-merge.pptx",{unsupported:"error"}) ^ { ^.code })==7706,
    len([for (issue in media.diagnostics where issue.code=="unsupported-media") issue])==2,
    len([for (issue in model3d.diagnostics where issue.code=="unsupported-3d-model") issue])==1,
    slide.compile(parse(format(hidden.document,'mark')^,'mark')^) ^.slides[1].hidden == true
]
