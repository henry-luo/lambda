// Process isolation keeps lookup, normalization and resource loading off the UI loop (S13.1.3v2).
import spelling: lambda.doc.spelling
import document: lambda.doc.spelling.document

pn main() {
    let request = input(sys.proc.self.env.LAMBDA_SPELLING_REQUEST#,'mark')^
    let checker = spelling.with_words(spelling.load()^,["Lambda","Radiant"])
    let report = document.check(checker,request.document)^;
    print(format(report,'json') ++ "\n")
}
