// Internal process wire format contains Mark data, never Lambda source (S1.8).
import pptx: lambda.doc.pptx
pn main() {
    let reply = pptx.convert(sys.proc.self.env.LAMBDA_SLIDES_OPEN_PATH#) ^ { {error:^.message} }
        ~ { {source:format(~.document,'mark')^,diagnostics:~.diagnostics} }
    print(format(reply,'json')^)
}
