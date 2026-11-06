import wiki: lambda.doc.wiki
let document = <doc <p "hello">>
let unsupported = <doc <p mystery:"value", "hello">>
let report = wiki.format_report(unsupported,{flavor:"mediawiki"})^;
[wiki.format(document,{flavor:"mediawiki",loss:"error"})^ == "hello\n\n",
 (wiki.format(document,{bogus:true}) ^ { "rejected" }) == "rejected",
 len(report.diagnostics) == 1,
 report.diagnostics[0].code == "unsupported-attribute"]
