// graphicx inclusion and sizing policy lives in script (D7.2.1).
import util: ~~.util
import pdf: lambda.pdf.pdf
import paths: lambda.edit.session

let KEYS = ["width", "height", "scale", "angle", "trim", "clip", "keepaspectratio", "page"]

pub fn options(el) {
    let parsed = util.parse_kv_options(util.optional_raw(el))
    if (el.starred == true) {*:parsed, clip: "true"} else parsed
}

pub fn unsupported_keys(opts) {
    [for (key, value at opts
          where not any([for (allowed in KEYS) string(key) == allowed])) string(key)]
}

pub fn source(el) => trim(util.text_of_skip_brack(el))

pub fn resolved_source(src, base_uri) {
    if (base_uri == null or base_uri == "" or starts_with(src, "/") or
        starts_with(src, "data:") or index_of(src, "://") != null) src
    else paths.resolve_path(base_uri, src)
}

fn style(opts) {
    let size = "display:inline-block;vertical-align:middle;" ++
        (if (opts.width != null) "width:" ++ util.css_dimension(opts.width) ++ ";" else "") ++
        (if (opts.height != null) "height:" ++ util.css_dimension(opts.height) ++ ";" else "")
    let fit = if (util.option_enabled(opts.keepaspectratio)) "object-fit:contain;" else ""
    let transform = (if (opts.scale != null) "scale(" ++ opts.scale ++ ") " else "") ++
        (if (opts.angle != null) "rotate(" ++ opts.angle ++ "deg)" else "")
    let rotated = if (trim(transform) != "") "transform:" ++ trim(transform) ++ ";" else ""
    let trimmed = if (opts.trim != null) split(trim(opts.trim), null) else []
    let clipping = if (util.option_enabled(opts.clip) and len(trimmed) == 4)
        "clip-path:inset(" ++ util.css_pixel_dimension(trimmed[3]) ++ " " ++
        util.css_pixel_dimension(trimmed[2]) ++ " " ++ util.css_pixel_dimension(trimmed[1]) ++ " " ++
        util.css_pixel_dimension(trimmed[0]) ++ ");"
        else ""
    size ++ fit ++ rotated ++ clipping
}

fn pdf_style(opts, svg) {
    let physical = if (opts.width == null and opts.height == null)
        "width:" ++ string(float(svg.width) * 96.0 / 72.0) ++ "px;height:" ++
        string(float(svg.height) * 96.0 / 72.0) ++ "px;"
        else ""
    style(opts) ++ physical
}

fn pdf_class(opts) {
    if (opts.width != null and opts.height == null) "latex-image-frame latex-image-width"
    else if (opts.height != null and opts.width == null) "latex-image-frame latex-image-height"
    else "latex-image-frame"
}

fn unsupported(message, offset) => util.unsupported_element("graphicx", message, offset)

fn has_unportable_pdf_content(node) {
    if (not (node is element)) false
    else if (string(name(node)) == "image" and node.href != null and
             starts_with(node.href, "img:")) true
    else if (node["data-pdf-form"] != null) true
    else any([for (child in node) has_unportable_pdf_content(child)])
}

fn render_pdf(src, opts, id_prefix, offset) {
    let parsed = input(src, "pdf") ^ { null }
    if (parsed == null) unsupported("Cannot read PDF graphic " ++ src, offset)
    else {
        let page = if (opts.page != null) int(opts.page) ^ { null } else 1
        if (page == null or page < 1 or page > pdf.pdf_page_count(parsed))
            unsupported("Invalid PDF page for graphic " ++ src, offset)
        else {
            let svg = pdf.pdf_to_svg(parsed, page - 1,
                {show_label: false, id_prefix: id_prefix})
            // A remaining img: handle needs the original PDF registry and cannot survive HTML serialization.
            if (has_unportable_pdf_content(svg))
                unsupported("PDF graphic contains an unresolved XObject or image handle: " ++ src, offset)
            else <span class: pdf_class(opts), style: pdf_style(opts, svg), svg>
        }
    }
}

pub fn render(el, base_uri) {
    let opts = options(el)
    let unknown = unsupported_keys(opts)
    let src = resolved_source(source(el), base_uri)
    let pdf_source = ends_with(lower(src), ".pdf")
    let trim_values = if (opts.trim != null) split(trim(opts.trim), null) else []
    if (len(unknown) > 0) unsupported("Unsupported graphicx option: " ++ join(unknown, ", "), el.source_offset)
    else if (src == "") unsupported("Missing graphicx source", el.source_offset)
    else if (opts.width != null and util.css_dimension(opts.width) == null)
        unsupported("Unsupported graphicx width: " ++ opts.width, el.source_offset)
    else if (opts.height != null and util.css_dimension(opts.height) == null)
        unsupported("Unsupported graphicx height: " ++ opts.height, el.source_offset)
    else if (opts.scale != null and (float(opts.scale) ^ { null }) == null)
        unsupported("Unsupported graphicx scale: " ++ opts.scale, el.source_offset)
    else if (opts.angle != null and (float(opts.angle) ^ { null }) == null)
        unsupported("Unsupported graphicx angle: " ++ opts.angle, el.source_offset)
    else if (opts.page != null and not pdf_source) unsupported("graphicx page requires a PDF source", el.source_offset)
    else if (opts.trim != null and (not util.option_enabled(opts.clip) or len(trim_values) != 4 or
             not all([for (dimension in trim_values) util.css_pixel_dimension(dimension) != null])))
        unsupported("graphicx trim requires four values and clip in this profile", el.source_offset)
    else if (ends_with(lower(src), ".eps")) unsupported("EPS graphics are not supported: " ++ src, el.source_offset)
    else if (pdf_source) render_pdf(src, opts,
        "latex-graphic-" ++ (if (el.source_offset != null) string(el.source_offset)
            else util.slugify(src)), el.source_offset)
    else if (not starts_with(src, "data:") and index_of(src, "://") == null and
             not exists(src)) unsupported("Cannot read graphicx image " ++ src, el.source_offset)
    else <img class: "latex-image", src: src, alt: src, style: style(opts)>
}
