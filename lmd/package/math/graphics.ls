// Inline raster boxes use KaTeX's height/totalheight baseline contract (D7.2.4).
import radiant
import bx: .svg_box
import util: .util
import latex_util: lambda.latex.util
import graphicx: lambda.latex.packages.graphicx

fn length(raw, dimension) {
    let text = trim(string(raw))
    let parsed = util.dimension_from_string(text)
    let explicit = ends_with(text, parsed.unit)
    let unit = if (explicit) parsed.unit else "bp"
    let numeric = float(if (explicit) trim(slice(text, 0, len(text) - len(unit))) else text)^;
    if (not contains(["em", "ex", "mu", "pt", "bp", "pc", "in", "cm", "mm", "px"], unit) or
        numeric < 0 or numeric - numeric != 0) error("math: invalid image dimension " ++ text)
    else dimension(if (explicit) text else text ++ "bp")
}

pub fn render(n, base_uri, dimension) {
    let opts = latex_util.parse_kv_options(n.options or "")
    let unknown = [for (key, value at opts where not contains(["width", "height", "totalheight", "alt"], string(key))) string(key)]
    if (len(unknown) > 0) error("math: unsupported image option " ++ unknown[0])
    else if (n.src == null or trim(n.src) == "") error("math: includegraphics requires a source")
    else {
        let source = graphicx.resolved_source(trim(n.src), base_uri)
        let bytes = input(source, 'binary')^
        let facts = radiant.image_metrics(bytes)
        if (facts == null) error("math: unsupported or invalid image " ++ source)
        else {
            let height = length(opts.height or "0.9em", dimension)^
            let requested_total = if (opts.totalheight != null) length(opts.totalheight, dimension)^ else 0.0
            let total = if (requested_total > 0.0) requested_total else height
            let requested_width = if (opts.width != null) length(opts.width, dimension)^ else 0.0
            let width = if (requested_width > 0.0) requested_width else total * facts.width / facts.height
            let encoded = format(bytes, 'json')^
            let href = "data:" ++ facts.mime_type ++ ";base64," ++ slice(encoded, 1, len(encoded) - 1)
            let alt = opts.alt or trim(n.src);
            bx.make(<image href: href, x: 0, y: (0.0 - height), width: width, height: total,
                preserveAspectRatio: "none", role: "img", 'aria-label': alt, <title alt>>,
                width, height, total - height)
        }
    }
}
