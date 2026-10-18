import pdf: lambda.doc.pdf.output
import resolve: lambda.doc.pdf.resolve

pn main() {
    let written = output(pdf.render("test/html/pdf_text_export.html", {width: 400, height: 520})^,
        "temp/pdf_text_continuous.pdf", {atomic: true})^;
    let paged = output(pdf.render("test/html/pdf_text_export.html", {paged: true})^,
        "temp/pdf_text_paged.pdf", {atomic: true})^;
    let parsed = input("temp/pdf_text_paged.pdf", 'pdf')^;
    let page_fonts = [for (page in parsed.pages) resolve.page_resources(parsed, page).Font];
    let fonts = [for (key, ref in page_fonts[0]) resolve.deref(parsed, ref)];
    let descriptions = [for (font in fonts where font.Subtype == "Type0") resolve.deref(parsed, font.DescendantFonts[0])];
    return [written > 1000, paged > 1000, len(parsed.pages) == 2,
        len(page_fonts[0]) >= 1, len(page_fonts[1]) >= 1,
        all([for (font in fonts) font.ToUnicode != null and
            ((font.Subtype == "Type0" and font.Encoding == "Identity-H") or font.Subtype == "Type3")]),
        all([for (font in descriptions) font.FontDescriptor != null and font.W != null])]
}
