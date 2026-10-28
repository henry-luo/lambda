import pptx: lambda.doc.pptx
import slide: lambda.slide
let result = pptx.convert("test/input/pptx/formatting.pptx")^
let plan = slide.compile(result.document)^;
[
    contains(result.text,"Introduction\n3. First\n1. Nested\n4. Second\nInterruption\n(7) Restart"),
    contains(result.html,"href=\"https://example.com/page\""),
    not contains(result.html,"javascript:"),
    contains(result.text,"Unsafe label"),
    [for (issue in result.diagnostics) issue.code]==["unsupported-link"],
    plan.slides[0].objects[1].width==2.0 and plan.slides[0].objects[1].x==699.0,
    contains(result.html,"stroke=\"#E04E39\"") and contains(result.html,"<line"),
    pptx.convert("test/input/pptx/features.pptx")^.metadata=={title:"Feature showcase",creator:"Lambda tests"}
]
