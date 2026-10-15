import pdf: lambda.doc.pdf.output
import radiant
import schema: lambda.doc.pdf.view_schema

fn discard(event, node, ctx) => ""
fn rejected(source) => pdf.render(source, {}) ^ {true}

pn main() {
    let saved = output("keep existing PDF", "temp/pdf_lambda_phase6_error.pdf", {atomic: true})^;
    var missing = false;
    let attempted = pdf.render(<html <body <div id:"outline", style:"display:none", ["data-pdf-outline-title"]:"unresolved", "outline">>>, {}) ^ {missing = true};
    if (not missing) output(attempted, "temp/pdf_lambda_phase6_error.pdf", {atomic: true})^;
    let no_id = rejected(<html <body <div ["data-pdf-outline-title"]:"missing id", "x">>>);
    let bad_level = rejected(<html <body <div id:"chapter", ["data-pdf-outline-title"]:"chapter", ["data-pdf-outline-level"]:"33", "x">>>);
    let forged = radiant.pdf_walk_view(<PdfSource>, {}, discard, schema.schemas()) ^ {true};
    return [saved > 0, missing, no_id == true, bad_level == true, forged == true,
        input("temp/pdf_lambda_phase6_error.pdf", 'text')^ == "keep existing PDF"]
}
