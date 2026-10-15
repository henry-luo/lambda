import pdf: lambda.pdf.pdf

pn main() {
    let written = output(pdf.render_source("test/html/pdf_lambda_views.html", {width: 200, height: 120})^, "temp/pdf_lambda_views.pdf", {atomic: true})^;
    let parsed = input("temp/pdf_lambda_views.pdf", 'pdf')^;
    return [written > 400, len(parsed.pages) == 1]
}
