import pptx: lambda.doc.pptx
import slide: lambda.slide
let result = pptx.convert("test/input/pptx/features.pptx")^
let plan = slide.compile(result.document)^
let scene = plan.slides[0]
let title = scene.objects[1]
let group = scene.objects[5]
let omitted = pptx.convert("test/input/pptx/features.pptx",{images:"omit"})^;
[
    [for (s in plan.slides) s.id]==["slide-300","slide-256"],
    scene.source.title=="A clearer story",
    scene.source.background=="#F1F6F5",
    scene.objects[0].paint=="#008C86",
    title.x==48.0 and title.y==42.0 and title.width==864.0 and title.height==100.0,
    contains(format(title.source,'html')^,"Georgia") and contains(format(title.source,'html')^,"font-size:48px"),
    contains(result.html,"font-weight:700") and contains(result.html,"font-style:italic"),
    contains(result.text,"• Café 世界"),
    starts_with(scene.objects[3].source.src,"data:image/png;base64,"),
    contains(result.text,"Revenue") and contains(result.text,"$42m"),
    group.tag=='group' and group.x==700.0 and group.width==160.0,
    group.children[0].x==0.0 and group.children[0].y==0.0 and group.children[0].width==160.0,
    scene.notes[0][0]=="Introduce the opportunity.",
    not contains(result.text,"Introduce the opportunity."),
    len(result.diagnostics)==0,
    [for (d in omitted.diagnostics) d.code]==["omitted-image"],
    not contains(omitted.html,"data:image/png"),
    slide.compile(parse(format(result.document,'mark')^,'mark')^)^.slides[0].objects[3].source.src==scene.objects[3].source.src
]
