import pdf: lambda.pdf.pdf
import fixed: lambda.pdf.fixed

pn main() {
    let page = {label: "Front", media_box: [0, 0, 100, 60], dict: {Resources: {}}}
    let document = {pages: [for (i in 1 to 53) page], objects: []}
    let complete = pdf.pdf_to_document(document, {paged: true})^
    let selected = pdf.pdf_to_document(document, {paged: true, import_pages: "53,2-3,2"})^
    let pages = selected[1][0]
    let rotated = {pages: [{media_box: [-10, -20, 90, 80], crop_box: [0, -10, 80, 50], rotate: 450,
        dict: {TrimBox: [1, -9, 79, 49], BleedBox: [-1, -11, 81, 51], ArtBox: [2, -8, 78, 48]}}], objects: []}
    let geometry = pdf.pdf_to_fixed_pages(rotated, {})^[1][0][0]
    let markup = format(geometry, 'xml')^
    print({
        complete: len(complete[1][0]) == 53,
        source_order: len(pages) == 3 and pages[0]['source-page'] == "2" and pages[1]['source-page'] == "3" and pages[2]['source-page'] == "53",
        native: string(name(pages)) == "r:fixed-pages" and string(name(geometry)) == "r:fixed-page",
        rotation: geometry['source-rotation'] == "450" and geometry.width == "60pt" and geometry.height == "80pt",
        source_label: pages[0]['source-label'] == "Front",
        label_error: contains(pdf.pdf_to_fixed_pages({*: document, page_labels_error: "invalid labels"}, {}) ^ { ^.message }, "invalid labels"),
        boxes: geometry['source-trim-box'] != null and geometry['source-bleed-box'] != null and geometry['source-art-box'] != null,
        vector_resource: contains(markup, "data:image/svg+xml;base64,"),
        budget: contains(pdf.pdf_to_fixed_pages(document, {max_pages: 52}) ^ { ^.message }, "complete sequence exceeds"),
        selected_budget: contains(pdf.pdf_to_fixed_pages(document, {import_pages: "1-3", max_pages: 2}) ^ { ^.message }, "selected sequence exceeds"),
        range: contains(fixed.selected_pages(document, {import_pages: "54"}) ^ { ^.message }, "out-of-range"),
        empty: contains(pdf.pdf_to_fixed_pages({pages: []}, {}) ^ { ^.message }, "no pages"),
        malformed_box: contains(fixed.source_geometry({}, {*: page, media_box: [0, 0, 100]}) ^ { ^.message }, "four finite"),
        invalid_rotation: contains(fixed.source_geometry({}, {*: page, rotate: 45}) ^ { ^.message }, "multiple of 90"),
        user_unit: contains(fixed.source_geometry({}, {*: page, dict: {UserUnit: 2}}) ^ { ^.message }, "UserUnit")
    })
}
