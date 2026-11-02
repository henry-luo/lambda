// Internal process wire format contains Mark data, never Lambda source (S1.8).
import pptx: lambda.doc.pptx
pn main() {
    let reply = { let __error_outcome_1 = pptx.convert(sys.proc.self.env.LAMBDA_SLIDES_OPEN_PATH#) ^ { ^ }; if (__error_outcome_1 is error) { {error:__error_outcome_1.message} } else match __error_outcome_1 { case any { {source:format(~.document,'mark')^,diagnostics:~.diagnostics} } } }
    print(format(reply,'json')^)
}
