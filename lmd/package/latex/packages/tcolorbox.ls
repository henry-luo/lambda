// Bounded tcolorbox color frame on the shared HTML element path.
import util: ~~.util
import color: ~~.elements.color
import boxes: ~~.elements.boxes

pub fn options(node) => util.parse_kv_options(node.options_raw)

pub fn issues(node) {
    let selected = options(node);
    [for (key, value at selected
        where string(key) != "colback" and string(key) != "colframe")
        util.diagnostic("unsupported-tcolorbox-option", "tcolorbox", string(key),
            "Unsupported tcolorbox option " ++ string(key), node.source_offset)]
}

pub fn render(node, custom_colors, children) {
    let selected = options(node)
    let invalid = issues(node)
    let background = if (selected.colback == null) "white"
        else color.resolve_color(selected.colback, custom_colors)
    let border = if (selected.colframe == null) "black"
        else color.resolve_color(selected.colframe, custom_colors)
    if (len(invalid) > 0 or background == null or border == null)
        util.unsupported_element("tcolorbox", "Unsupported tcolorbox color or option",
            node.source_offset)
    else <div class: "latex-tcolorbox",
        style: boxes.frame_css({background: background, line: border, width: "1px", padding: "0.6em 0.8em"}),
        for c in children { c }>
}
