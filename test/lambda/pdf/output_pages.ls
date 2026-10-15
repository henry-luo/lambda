import pdf: lambda.pdf.output
import radiant
import schema: lambda.pdf.view_schema

fn inspect(event, node, ctx) string^ {
    if (event == "begin_page") {
        if (node.page_number != ctx.page_number or node.node.rect.width != ctx.page_width or
                node.node.rect.height != ctx.page_height)
            raise error("page schema differs from selected native geometry")
        else "P"
    }
    else if (event == "begin_fragment") {
        if (node.ref.tree_id != ctx.fragment.ref.tree_id or node.ref.generation != ctx.fragment.ref.generation or
                node.ref.node_id != ctx.fragment.ref.node_id or node.state.occurrence_count < 1)
            raise error("fragment schema differs from selected occurrence")
        else "F"
    }
    else ""
}

pn main() {
    let all_bytes = pdf.export("test/html/paged_view.html", "temp/pdf_lambda_pages.pdf",
        {paged: true, width: 200, height: 120})^;
    let all_pages = input("temp/pdf_lambda_pages.pdf", 'pdf')^;
    let selected_bytes = pdf.export("test/html/paged_view.html", "temp/pdf_lambda_selected.pdf",
        {paged: true, pages: "1,3", width: 200, height: 120})^;
    let selected = input("temp/pdf_lambda_selected.pdf", 'pdf')^;
    let walked = radiant.pdf_walk("test/html/paged_view.html", {paged: true, pages: "2"}, inspect, schema.schemas())^;
    let saved = output("preserve page destination", "temp/pdf_lambda_page_error.pdf")^;
    var rejected = false;
    pdf.export("test/html/paged_view.html", "temp/pdf_lambda_page_error.pdf", {paged: true, pages: "9"}) ^ {rejected = true};
    var unsupported = false;
    pdf.export("test/html/pdf_lambda_unsupported.html", "temp/pdf_lambda_page_error.pdf", {paged: true}) ^ {unsupported = true};
    return [all_bytes > 600, len(all_pages.pages) == 3, all_pages.pages[0].media_box == [0, 0, 150, 90],
        selected_bytes > 400 and len(selected.pages) == 2, len(walked.pages) == 1 and walked.pages[0].number == 2,
        len(walked.pages[0].content) > 1, rejected and saved > 0, unsupported,
        input("temp/pdf_lambda_page_error.pdf", 'text')^ == "preserve page destination"]
}
