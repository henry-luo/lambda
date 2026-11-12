import pptx: lambda.doc.pptx
import slide: lambda.slide
pn main() {
    let converted = pptx.convert(sys.proc.self.env.LAMBDA_PPTX_SURVEY_PATH#) ^ { ^ }
    if (converted is error) {
        print(format({error:converted.message},'json')^)
    } else {
        let plan = slide.compile(converted.document) ^ { ^ }
        if (plan is error) print(format({error:plan.message},'json')^)
        else print(format({slides:len(plan.slides),text:converted.text,diagnostics:converted.diagnostics},'json')^)
    }
}
