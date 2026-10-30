// Process isolation keeps lookup, normalization and resource loading off the UI loop (S13.1.3v2).
import spelling: lambda.doc.spelling

pn main() {
    let request = input(sys.proc.self.env.LAMBDA_SPELLING_REQUEST#,'mark')^
    let checker = spelling.with_words(spelling.load()^,["Lambda","Radiant"])
    // requests contain changed paragraphs only, with offsets local to each paragraph.
    let units = [for (unit in request.units) {*:unit,report:spelling.check(checker,unit.text)^}];
    print(format({units:units,checked:sum([for (unit in units) unit.report.checked])},'json') ++ "\n")
}
