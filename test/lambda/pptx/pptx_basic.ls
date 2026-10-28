import pptx: lambda.doc.pptx
import slide: lambda.slide
let result = pptx.convert("test/input/pptx/minimal.pptx")^
let zip = input("test/input/pptx/minimal.pptx",'zip')^
let plan = slide.compile(result.document)^;
[
    name(result.document)=='presentation',
    plan.width==960.0 and plan.height==540.0,
    len(plan.slides)==1 and plan.slides[0].id=="slide-256",
    result.text=="Hello, PPTX!\n",
    len(result.diagnostics)==0,
    contains(result.html,"Hello, PPTX!") and contains(result.html,"slide-handout"),
    pptx.convert(zip)^.document==result.document,
    pptx.to_elements(zip)^==result.document,
    pptx.to_html(zip)^==result.html,
    pptx.extract_text(zip)^==result.text,
    all([for (file in ["relocated","strict","alternate-prefix"])
        pptx.convert("test/input/pptx/" ++ file ++ ".pptx")^.document==result.document])
]
