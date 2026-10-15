import pdf: lambda.doc.pdf.output
import util: lambda.doc.pdf.util

pn main() {
    let written = output(pdf.render("test/html/pdf_lambda_resources.html", {width: 300, height: 160})^, "temp/pdf_lambda_resources.pdf", {atomic: true})^;
    let parsed = input("temp/pdf_lambda_resources.pdf", 'pdf')^;
    let saved = output("preserve this file", "temp/pdf_lambda_preserved.pdf")^;
    var rejected = false;
    pdf.render("test/html/pdf_lambda_unsupported.html", {unknown: true}) ^ {rejected = true};
    let preserved = input("temp/pdf_lambda_preserved.pdf", 'text')^;
    return [written > 400, len(parsed.pages) == 1, len(parsed.objects) > 4,
        rejected and saved > 0, preserved == "preserve this file",
        util.pdf_num(1e-12)^ == "0.000000000001", util.pdf_num(-1e25)^ == "-10000000000000000000000000",
        (util.pdf_num(inf) ^ {true}) == true]
}
