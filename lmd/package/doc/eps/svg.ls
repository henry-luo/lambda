import util: lambda.pdf.util
import svg: lambda.pdf.svg

fn clip_id(prefix, id) => prefix ++ "-clip-" ++ string(id)

fn definition(item, prefix) {
    <clipPath id: clip_id(prefix, item.id), clipPathUnits: "userSpaceOnUse",
        <path d: item.d, 'clip-rule': item.rule>
    >
}

fn clipped(node, clips, prefix, index) {
    if (index >= len(clips)) node
    else <g 'clip-path': "url(#" ++ clip_id(prefix, clips[index]) ++ ")",
        clipped(node, clips, prefix, index + 1)
    >
}

pub fn render(display, options) element^ {
    let b = display.bbox
    let geometry_checked = if (not (b is array) or len(b) != 4 or b[2] <= b[0] or b[3] <= b[1]) raise error("EPS invalid SVG bounding box") else true
    let width = b[2] - b[0]
    let height = b[3] - b[1]
    let prefix = if (options != null and options.id_prefix != null) string(options.id_prefix) else "eps"
    let scale = if (options != null and options.scale != null) options.scale else 1.0
    let scale_checked = if (not (scale is int or scale is float) or scale <= 0 or scale is nan or scale is inf) raise error("EPS invalid output scale") else true
    let definitions = [for (item in display.definitions) definition(item, prefix)]
    let nodes = [for (draw in display.draws)
        clipped(svg.group(util.fmt_matrix(draw.matrix), [draw.node]), draw.clips, prefix, 0)]
    let children = [<defs for (item in definitions) item>,
        if (options != null and options.background != null)
            <rect width: util.fmt_num(width), height: util.fmt_num(height), fill: string(options.background)>,
        <svg x: "0", y: "0", width: util.fmt_num(width), height: util.fmt_num(height), viewBox: "0 0 " ++ util.fmt_num(width) ++ " " ++ util.fmt_num(height), overflow: "hidden",
            svg.group(util.fmt_matrix([1.0, 0.0, 0.0, -1.0, -b[0], b[3]]), nodes)
        >];
    // EPS points remain physical points; the nested viewport clips artwork at its bounding box.
    <svg xmlns: "http://www.w3.org/2000/svg", viewBox: "0 0 " ++ util.fmt_num(width) ++ " " ++ util.fmt_num(height),
        width: util.fmt_num(width * scale) ++ "pt", height: util.fmt_num(height * scale) ++ "pt",
        for (child in children) child
    >
}
