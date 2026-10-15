// fixed page controls retain source metadata; the common Radiant producers own layout and paint.
import resolve: .resolve

fn finite(v) => (v is int or v is float) and not (v is nan) and not (v is inf) and v != -inf

fn page_ordinal(raw) {
    let text = trim(string(raw))
    if (len(text) == 0 or len([for (i in 0 to len(text) - 1 where not contains("0123456789", text[i])) i]) > 0) null
    else int(text)
}

fn page_range(text, total) map^ {
    let ends = split(trim(text), "-")
    let first = page_ordinal(ends[0])
    let final_page = if (len(ends) == 2) page_ordinal(ends[1]) else first
    if (len(ends) > 2 or first == null or final_page == null or first < 1 or final_page < first or final_page > total)
        raise error("pdf paged intake: invalid or out-of-range source page selection")
    else {first: first, last: final_page}
}

pub fn selected_pages(pdf, opts) array^ {
    let total = resolve.page_count(pdf)
    let budget = if (opts and opts.max_pages != null) page_ordinal(opts.max_pages) else 10000
    let selection = if (opts and opts.import_pages != null) trim(string(opts.import_pages)) else "all"
    if (pdf.page_labels_error != null) raise error("pdf paged intake: " ++ string(pdf.page_labels_error))
    else if (total == 0) raise error("pdf paged intake: document has no pages")
    else if (budget == null or budget < 1) raise error("pdf paged intake: invalid page budget")
    else if (selection == "all") {
        if (total > budget) raise error("pdf paged intake: complete sequence exceeds its page budget")
        else [for (i in 0 to total - 1) i]
    } else {
        let ranges = [for (part in split(selection, ",")) page_range(part, total)^]
        let pages = [for (i in 0 to total - 1 where
            len([for (span in ranges where i + 1 >= span.first and i + 1 <= span.last) span]) > 0) i]
        if (len(pages) == 0) raise error("pdf paged intake: source selection is empty")
        else if (len(pages) > budget) raise error("pdf paged intake: selected sequence exceeds its page budget")
        else pages
    }
}

fn box(pdf, raw, required) any^ {
    let value = resolve.deref(pdf, raw)
    if (value == null and not required) null
    else if (not (value is array) or len(value) != 4 or len([for (v in value where not finite(v)) v]) > 0)
        raise error("pdf paged intake: page box requires four finite coordinates")
    else if (value[2] <= value[0] or value[3] <= value[1])
        raise error("pdf paged intake: reversed or empty page boxes are unsupported")
    else value
}

pub fn source_geometry(pdf, page) map^ {
    let media = box(pdf, page.media_box, true)^
    let crop = box(pdf, page.crop_box, false)^
    let bleed = box(pdf, page.dict.BleedBox, false)^
    let trim_box = box(pdf, page.dict.TrimBox, false)^
    let art = box(pdf, page.dict.ArtBox, false)^
    let rotation = if (page.rotate == null) 0 else resolve.deref(pdf, page.rotate)
    let user_unit = resolve.deref(pdf, page.dict.UserUnit)
    if (not finite(rotation) or rotation != floor(rotation) or rotation % 90 != 0 or
        rotation < -2147483648 or rotation > 2147483647)
        raise error("pdf paged intake: rotation requires a signed multiple of 90 degrees")
    else if (user_unit != null and user_unit != 1)
        raise error("pdf paged intake: non-default UserUnit is unsupported")
    else if (crop != null and (min(media[2], crop[2]) <= max(media[0], crop[0]) or
        min(media[3], crop[3]) <= max(media[1], crop[1])))
        raise error("pdf paged intake: crop box does not intersect media box")
    else {media: media, crop: crop, bleed: bleed, trim_box: trim_box, art: art, rotation: rotation}
}

fn box_text(box) => join([for (coordinate in box) string(float(coordinate))], " ")

fn svg_resources(node) bool^ {
    if (not (node is element)) true
    else if ((node.href != null and starts_with(string(node.href), "img:")) or node['data-pdf-form'] != null)
        raise error("pdf paged intake: unresolved image or form resource")
    else all([for (child in content(node)) svg_resources(child)^])
}

pub fn page(parts, geometry, ordinal, label) element^ {
    let checked = svg_resources(parts.svg)^
    let svg = format(parts.svg, 'xml')^
    // binary JSON formatting uses the shared base64 encoder, including arbitrary embedded SVG bytes.
    let encoded = format(binary(svg), 'json')^
    let uri = "data:image/svg+xml;base64," ++ slice(encoded, 1, len(encoded) - 1)
    let attributes = {
        'source-media-box': box_text(geometry.media),
        'source-rotation': string(int(geometry.rotation)), 'source-page': string(ordinal),
        *: if (label != null) {'source-label': string(label)} else {},
        *: if (geometry.crop != null) {'source-crop-box': box_text(geometry.crop)} else {},
        *: if (geometry.bleed != null) {'source-bleed-box': box_text(geometry.bleed)} else {},
        *: if (geometry.trim_box != null) {'source-trim-box': box_text(geometry.trim_box)} else {},
        *: if (geometry.art != null) {'source-art-box': box_text(geometry.art)} else {}
    };
    <'r:fixed-page' *: attributes,
        width: string(parts.rect.w) ++ "pt", height: string(parts.rect.h) ++ "pt",
        style: "width:100%;height:100%;margin:0;padding:0;font-size:0;line-height:0",
        <img src: uri, style: "display:block;width:100%;height:100%;margin:0;padding:0">
    >
}
