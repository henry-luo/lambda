// A complete ASCII PDF authored in Lambda. This MVP measures ASCII fragments;
// general byte-offset APIs and Radiant projection remain separate work.
fn xref_entry(offset) {
    let digits = string(offset);
    slice("0000000000", 0, 10 - len(digits)) ++ digits ++ " 00000 n \n"
}

pn main() {
    let header = "%PDF-1.4\n";
    let catalog = "1 0 obj\n<< /Type /Catalog /Pages 2 0 R >>\nendobj\n";
    let pages = "2 0 obj\n<< /Type /Pages /Kids [3 0 R] /Count 1 >>\nendobj\n";
    let page = "3 0 obj\n<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 200] /Resources << >> /Contents 4 0 R >>\nendobj\n";
    let paint = "0 0 1 rg\n20 20 160 160 re f\n";
    let stream = "4 0 obj\n<< /Length " ++ string(len(paint)) ++ " >>\nstream\n";
    let endstream = "endstream\nendobj\n";
    let first = len(header);
    let second = first + len(catalog);
    let third = second + len(pages);
    let fourth = third + len(page);
    let xref = fourth + len(stream) + len(paint) + len(endstream);
    let file = <file format:'pdf',
        header catalog pages page stream paint endstream
        "xref\n0 5\n0000000000 65535 f \n"
        xref_entry(first) xref_entry(second) xref_entry(third) xref_entry(fourth)
        "trailer\n<< /Size 5 /Root 1 0 R >>\nstartxref\n"
        string(xref) "\n%%EOF\n"
    >;
    let written = output(file, "./temp/lambda_pdf_mvp.pdf")^;
    return written > 0
}
