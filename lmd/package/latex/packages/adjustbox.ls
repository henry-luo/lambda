// latex/packages/adjustbox.ls — \adjustbox{keys}{content} and the adjustbox
// environment (vibe/impl/Lambda_Impl_Latex_Packages.md §9). A lone image is sized
// through graphicx's own keys; other content gets graphicx's CSS-transformed box,
// whose layout size stays untransformed (registry.target_diagnostics). Frames and
// backgrounds use the frame-box model (boxes.frame_css). adjustbox applies its keys
// in order; here they act together.

import util: ~~.util
import color: ~~.elements.color
import boxes: ~~.elements.boxes
import graphicx: .graphicx

pub let KEYS = ["width", "height", "totalheight", "max width", "max height", "scale", "angle",
    "rotate", "trim", "clip", "keepaspectratio", "frame", "fbox", "bgcolor", "center", "valign"]

// keys that change the box's size or orientation, which CSS cannot do to the layout
pub let TRANSFORMS = ["width", "height", "totalheight", "scale", "angle", "rotate"]

pub fn keys(raw) => util.parse_kv_options(raw)

// the keys (as source) and content of \adjustbox{keys}{content}, whose content starts
// at `start`, or of the environment, whose keys are its header group and whose content
// is `body`; `image` is a lone \includegraphics
pub fn parts(node) {
    if (node.argument_groups != null)
        {raw: util.raw_argument(node, "required", 0), start: util.last_argument_start(node),
         body: null, image: graphicx.lone_image(node)}
    else {
        let header = util.environment_header(node, 1)
        {raw: if (len(header.groups) > 0) util.group_source(header.groups[0]) else null,
         start: null, body: header.body, image: graphicx.sole_image(header.body)}
    }
}

// a box that resizes or turns content other than a lone image
pub fn transforms_content(node) {
    let box = parts(node)
    let opts = keys(box.raw)
    box.image == null and any([for (key in TRANSFORMS) opts[key] != null])
}

pub fn unknown_keys(opts) => [for (key, value at opts where not (contains(KEYS, string(key)) or false)) string(key)]

// a length key, or null when absent or "!" (keep the aspect ratio)
fn length(value) => if (value == null or trim(value) == "!") null else util.css_dimension(trim(value))

let VALIGN = {t: "top", T: "top", m: "middle", M: "middle", b: "bottom", B: "bottom"}

// the box: an image sized by graphicx, or the content in a transformed, framed span
// `block` content (the environment's paragraphs) gets a block box set inline
pub fn render(opts, items, image, base_uri, custom_colors, offset, block = false) {
    let unknown = unknown_keys(opts)
    let angle = opts.angle or opts.rotate
    let height = opts.height or opts.totalheight
    let background = if (opts.bgcolor == null) null else color.spec_color(null, opts.bgcolor, custom_colors)
    let image_keys = {width: if (length(opts.width) == null) null else trim(opts.width),
        height: if (length(height) == null) null else trim(height),
        scale: opts.scale, angle: angle, trim: opts.trim, clip: opts.clip,
        keepaspectratio: opts.keepaspectratio}
    if (len(unknown) > 0)
        util.unsupported_element("adjustbox", "Unsupported adjustbox key: " ++ join(unknown, ", "), offset)
    else if (opts.bgcolor != null and background == null)
        util.unsupported_element("adjustbox", "Invalid adjustbox bgcolor: " ++ opts.bgcolor, offset)
    else if (image == null and opts.trim != null)
        util.unsupported_element("adjustbox", "adjustbox trims only a lone image here", offset)
    else {
        let inner = if (image != null)
            [graphicx.render(image, base_uri, map([for (k, v at image_keys, x in [string(k), v] where v != null) x]))]
            else items
        let transform = (if (image != null or opts.scale == null) "" else "scale(" ++ trim(opts.scale) ++ ") ") ++
            (if (image != null or angle == null) "" else graphicx.css_rotation(angle))
        let frame = if (util.option_enabled(opts.fbox)) {width: "0.4pt", padding: "3pt", background: background}
            else if (opts.frame != null) {width: if (util.option_enabled(opts.frame)) "0.4pt"
                else util.css_dimension(opts.frame) or "0.4pt", background: background}
            else if (background != null) {width: "0", background: background} else null
        let style = "display:inline-block;" ++
            (if (image != null) "" else
                (if (length(opts.width) == null) "" else "width:" ++ length(opts.width) ++ ";") ++
                (if (length(height) == null) "" else "height:" ++ length(height) ++ ";")) ++
            (if (length(opts["max width"]) == null) "" else "max-width:" ++ length(opts["max width"]) ++ ";") ++
            (if (length(opts["max height"]) == null) "" else "max-height:" ++ length(opts["max height"]) ++ ";") ++
            (if (trim(transform) == "") "" else "transform:" ++ trim(transform) ++ ";") ++
            (if (opts.valign == null or VALIGN[trim(opts.valign)] == null) ""
                else "vertical-align:" ++ VALIGN[trim(opts.valign)] ++ ";") ++
            (if (frame == null) "" else boxes.frame_css(frame))
        let box = if (block) <div class: "latex-adjustbox", style: style, for c in inner { c }>
            else <span class: "latex-adjustbox", style: style, for c in inner { c }>
        // center: the box centered on a line of its own
        if (opts.center != null) <div class: "latex-adjustbox-center", style: "text-align:center", box>
        else box
    }
}
