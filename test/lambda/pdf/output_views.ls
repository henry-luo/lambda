import pdf: lambda.pdf.pdf

pn main() {
    let written = pdf.export_source("test/html/pdf_lambda_views.html", "temp/pdf_lambda_views.pdf", {width: 200, height: 120})^;
    let parsed = input("temp/pdf_lambda_views.pdf", 'pdf')^;
    return [written > 400, len(parsed.pages) == 1]
}
