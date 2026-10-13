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
        (if (opts.angle != null) css_rotation(opts.angle) else "")
    let rotated = if (trim(transform) != "") "transform:" ++ trim(transform) ++ ";" else ""
    let trimmed = if (opts.trim != null) split(trim(opts.trim), null) else []
    let clipping = if (util.option_enabled(opts.clip) and len(trimmed) == 4)
        "clip-path:inset(" ++ util.css_pixel_dimension(trimmed[3]) ++ " " ++
        util.css_pixel_dimension(trimmed[2]) ++ " " ++ util.css_pixel_dimension(trimmed[1]) ++ " " ++
        util.css_pixel_dimension(trimmed[0]) ++ ");"
        else ""
    size ++ fit ++ rotated ++ clipping
}

// graphicx angles turn counterclockwise; CSS rotate() turns clockwise
pub fn css_rotation(angle) => "rotate(" ++ string(0.0 - float(trim(angle))) ++ "deg)"

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
    let parsed = pdf_document(src)
    if (parsed == null) unsupported("Cannot read PDF graphic " ++ src, offset)
    else render_pdf_page(parsed, src, if (opts.page != null) int(opts.page) ^ { null } else 1,
        opts, id_prefix, offset)
}

// a PDF read once for its pages (pdfpages inserts several), or null
pub fn pdf_document(src) => input(src, "pdf") ^ { null }

pub fn pdf_page_count(parsed) => pdf.pdf_page_count(parsed)

// page (from 1) of a read PDF as an inline SVG sized by the graphicx keys in opts
pub fn render_pdf_page(parsed, src, page, opts, id_prefix, offset) {
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

// `sizing` overrides the image's own keys when a box command sizes a lone image
pub fn render(el, base_uri, sizing = null) {
    let opts = if (sizing == null) options(el) else {*:options(el), *:sizing}
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

// \resizebox, \scalebox, \rotatebox and \reflectbox. A lone image is sized through
// its own keys; other content gets a CSS-transformed box whose layout size stays
// untransformed, which registry.target_diagnostics reports.
// the \includegraphics a box holds as its whole content, or null: a box command's
// last argument, or an environment's body nodes; space and paragraph wrappers aside
pub fn lone_image(el) {
    let args = util.command_args(el)
    sole_image(slice(args, util.last_argument_start(el), len(args)))
}

pub fn sole_image(nodes) {
    let solid = [for (node in nodes where not (node is string and trim(node) == "")) node]
    if (len(solid) != 1 or not (solid[0] is element)) null
    else if (string(name(solid[0])) == "includegraphics") solid[0]
    else if (string(name(solid[0])) == "paragraph") sole_image([for (c in content(solid[0])) c])
    else null
}

pub fn render_box(el, items, image, base_uri) {
    let tag = string(name(el))
    if (tag == "resizebox") render_resizebox(el, items, image, base_uri)
    else if (tag == "scalebox") render_scalebox(el, items, image, base_uri)
    else if (tag == "rotatebox") render_rotatebox(el, items, image, base_uri)
    else transformed_box("latex-reflectbox", "transform:scaleX(-1)", items)
}

pub fn transformed_box(css_class, style, items) {
    <span class: css_class, style: "display:inline-block;" ++ style, for c in items { c }>
}

// a \resizebox length; "!" keeps the aspect ratio and is null here
fn box_length(raw) => if (raw == null or trim(raw) == "!") null else trim(raw)

fn render_resizebox(el, items, image, base_uri) {
    let width = box_length(util.raw_argument(el, "required", 0))
    let height = box_length(util.raw_argument(el, "required", 1))
    if ((width != null and util.css_dimension(width) == null) or
        (height != null and util.css_dimension(height) == null) or (width == null and height == null))
        unsupported("Unsupported resizebox dimensions", el.source_offset)
    else if (image != null) render(image, base_uri, {width: width, height: height})
    else transformed_box("latex-resizebox", "max-width:100%;" ++
        (if (width == null) "" else "width:" ++ util.css_dimension(width) ++ ";") ++
        (if (height == null) "" else "height:" ++ util.css_dimension(height) ++ ";"), items)
}

fn render_scalebox(el, items, image, base_uri) {
    let horizontal = float(trim(util.raw_argument(el, "required", 0) or "")) ^ { null }
    let raw_vertical = util.optional_raw(el)
    let vertical = if (raw_vertical == null) horizontal else float(trim(raw_vertical)) ^ { null }
    if (horizontal == null or vertical == null)
        unsupported("Unsupported scalebox factor", el.source_offset)
    else if (image != null and vertical == horizontal) render(image, base_uri, {scale: string(horizontal)})
    else transformed_box("latex-scalebox", "transform:scale(" ++ string(horizontal) ++ "," ++
        string(vertical) ++ ");transform-origin:left bottom", items)
}

// \rotatebox[origin=…]{angle}: LaTeX's default origin is the left baseline
fn render_rotatebox(el, items, image, base_uri) {
    let angle = util.raw_argument(el, "required", 0)
    let opts = util.parse_kv_options(util.optional_raw(el))
    let unknown = [for (key, value at opts where string(key) != "origin") string(key)]
    let origin = rotation_origin(opts.origin)
    if (angle == null or (float(trim(angle)) ^ { null }) == null)
        unsupported("Unsupported rotatebox angle", el.source_offset)
    else if (len(unknown) > 0 or origin == null)
        unsupported("Unsupported rotatebox option: " ++ join(unknown ++ [string(opts.origin)], ", "),
            el.source_offset)
    else if (image != null and opts.origin == null) render(image, base_uri, {angle: trim(angle)})
    else transformed_box("latex-rotatebox", "transform:" ++ css_rotation(angle) ++
        ";transform-origin:" ++ origin, items)
}

fn rotation_origin(spec) {
    if (spec == null) "left bottom"
    else {
        let letters = trim(spec)
        let horizontal = if (contains(letters, "l")) "left" else if (contains(letters, "r")) "right" else "center"
        let vertical = if (contains(letters, "t")) "top"
            else if (contains(letters, "b") or contains(letters, "B")) "bottom" else "center"
        let valid = all([for (i in 0 to len(letters) - 1) contains("lrtbBc", slice(letters, i, i + 1))])
        if (letters == "" or not valid) null else horizontal ++ " " ++ vertical
    }
}
