// Arrow tip specifications (arrows.meta and the legacy tips) for TikZ paths.
// One parser serves both renderers; tips are drawn in px along the path direction.
import opts: .options
import util: lambda.latex.util
import svg: lambda.chart.svg

let PT_PX = 96.0 / 72.27

// Defaults in TeX pt from PGF 3.1 (pgflibraryarrows.meta, pgfcorearrows):
// length = base + factor * line width; width = width_ratio * length;
// inset = inset_ratio * length; a bar's width is width_base + width_factor * line width.
// The lower-case names are the core's legacy tips, built on d = 0.28pt + 0.3 lw.
// Triangle is Stealth[inset=0, angle=60:2.7pt +3.6]; To is Computer Modern Rightarrow.
let TIPS = [
    {kind: "stealth", names: ["Stealth"], base: 3.0, factor: 4.5,
     width_ratio: 0.75, inset_ratio: 0.325, filled: true},
    {kind: "stealth", names: ["stealth"], base: 2.24, factor: 2.4,
     width_ratio: 1.0, inset_ratio: 0.375, filled: true},
    {kind: "latex", names: ["Latex", "LaTeX"], base: 3.0, factor: 4.5,
     width_ratio: 0.75, inset_ratio: 0.0, filled: true},
    {kind: "latex", names: ["latex"], base: 2.8, factor: 3.0,
     width_ratio: 0.75, inset_ratio: 0.0, filled: true},
    {kind: "triangle", names: ["Triangle"], base: 2.338269, factor: 3.117691,
     width_ratio: 1.154701, inset_ratio: 0.0, filled: true},
    {kind: "to", names: ["To"], base: 1.6, factor: 2.2,
     width_ratio: 2.096774, inset_ratio: 0.0, filled: false},
    {kind: "to", names: ["to"], base: 1.05, factor: 1.125,
     width_ratio: 2.133333, inset_ratio: 0.0, filled: false},
    {kind: "circle", names: ["Circle"], base: 2.39365, factor: 3.191538,
     width_ratio: 1.0, inset_ratio: 0.0, filled: true},
    {kind: "bar", names: ["Bar"], base: 0.0, factor: 0.0,
     width_base: 3.0, width_factor: 4.0, inset_ratio: 0.0, filled: false},
    {kind: "bar", names: ["|"], base: 0.0, factor: 0.0,
     width_base: 4.0, width_factor: 3.0, inset_ratio: 0.0, filled: false}
]

fn tip_table(name) {
    let matches = [for (entry in TIPS, candidate in entry.names
        where candidate == name) entry]
    if (len(matches) == 0) null else matches[0]
}

// Index of the separating `-` outside braces and brackets, or null.
fn separator(key, at, depth, found) {
    if (at >= len(key)) found
    else {
        let ch = slice(key, at, at + 1)
        let next_depth = if (ch == "{" or ch == "[") depth + 1
            else if (ch == "}" or ch == "]") depth - 1 else depth
        if (ch == "-" and depth == 0)
            (if (found == null) separator(key, at + 1, next_depth, at) else -1)
        else separator(key, at + 1, next_depth, found)
    }
}

// One tip: `{Name[options]}`, `Name[options]`, `Name`, or a legacy `<`, `>`, `|`.
fn parse_tip(raw, side) {
    let source = trim(util.unwrap_braces(trim(raw)))
    let bracket = index_of(source, "[")
    let tip_name = trim(if (bracket == null) source else slice(source, 0, bracket))
    let options = if (bracket == null or not ends_with(source, "]")) null
        else slice(source, bracket + 1, len(source) - 1)
    let legacy_default = tip_name == ">" or tip_name == "<"
    if (bracket != null and options == null) null
    else if (legacy_default)
        // `<` opens the start and `>` closes the end; the other spelling is reversed.
        {kind: "default", name: tip_name, options: options,
         reversed: (side == "start" and tip_name == ">") or
            (side == "end" and tip_name == "<")}
    else if (tip_table(tip_name) == null) null
    else {kind: tip_table(tip_name).kind, name: tip_name, options: options, reversed: false}
}

fn parse_side(raw, side) {
    if (trim(raw) == "") {ok: true, tip: null}
    else {
        let tip = parse_tip(raw, side)
        if (tip == null) {ok: false, tip: null} else {ok: true, tip: tip}
    }
}

// `start-end` arrow keys such as `->`, `<->`, `|-|`, `latex-latex`,
// `-{Stealth[length=3mm]}` and `{Latex}-{Latex}`; null for other keys.
pub fn parse_key(key) {
    let raw = trim(key)
    let at = separator(raw, 0, 0, null)
    if (at == null or at < 0) null
    else {
        let start = parse_side(slice(raw, 0, at), "start")
        let end = parse_side(slice(raw, at + 1, len(raw)), "end")
        if (start.ok and end.ok) {start: start.tip, end: end.tip} else null
    }
}

pub fn is_arrow_key(key) => parse_key(key) != null

// Arrow keys present on a node, for option validation lists.
pub fn option_keys(node) => [for (child in node where child is element and
    string(name(child)) == "option" and child.value == "" and is_arrow_key(child.key))
    child.key] ++ (if (opts.has(node, "arrows")) ["arrows"] else [])

// The node's arrow spec: the last arrow key, or `arrows=<spec>`.
pub fn spec(node) {
    let keys = [for (child in node where child is element and
        string(name(child)) == "option" and child.value == "" and
        is_arrow_key(child.key)) child.key]
    let explicit = opts.value(node, "arrows", null)
    let chosen = if (explicit != null) explicit
        else if (len(keys) > 0) keys[len(keys) - 1] else null
    if (chosen == null) {start: null, end: null} else parse_key(chosen)
}

fn option_dimension(raw, line_width_pt) float^ {
    // `length=3pt 4.5` adds a multiple of the line width, as arrows.meta does.
    let words = [for (part in split(trim(raw), " ") where trim(part) != "") trim(part)]
    let base = opts.dimension_px(words[0])^ / PT_PX
    if (len(words) == 1) base
    else if (len(words) == 2) base + opts.numeric_value(words[1])^ * line_width_pt
    else raise error("unsupported arrow tip dimension: " ++ raw)
}

fn tip_options(raw) any^ {
    if (raw == null or trim(raw) == "") []
    else [for (part in util.split_top_level(raw, ",") where trim(part) != "") {
        let entry = trim(part)
        let eq = util.top_level_separator(entry, "=")
        if (eq == null) {key: entry, value: null}
        else {key: trim(slice(entry, 0, eq)), value: trim(slice(entry, eq + 1, len(entry)))}
    }]
}

fn option_value(entries, key) {
    let matches = [for (entry in entries where entry.key == key) entry.value]
    if (len(matches) == 0) null else matches[len(matches) - 1]
}

fn option_flag(entries, key) => len([for (entry in entries where entry.key == key) entry]) > 0

// Resolve a parsed tip to px geometry. `fallback` replaces the legacy `>` tip.
pub fn resolve(tip, line_width_px, fallback = null, custom_colors = null) any^ {
    if (tip == null) null
    else if (tip.kind == "default" and fallback == null)
        // Without a picture `>` setting the legacy default head keeps its 8px shape.
        {kind: "legacy", length: 8.0, width: 6.0, inset: 0.0, hollow: false,
         reversed: tip.reversed, color: null, line_end: 0.0}
    else if (tip.kind == "default") {
        let replaced = parse_tip(fallback, "end")
        if (replaced == null or replaced.kind == "default")
            raise error("unsupported TikZ arrow tip: " ++ fallback)
        else resolve({*:replaced, reversed: tip.reversed}, line_width_px, null,
            custom_colors)^
    } else {
        let table = tip_table(tip.name)
        let entries = tip_options(tip.options)^
        let known = ["length", "width", "scale", "scale length", "scale width", "open",
            "reversed", "fill", "color", "round", "sharp"]
        let unknown = [for (entry in entries
            where len([for (key in known where key == entry.key) key]) == 0) entry.key]
        let known_options = if (len(unknown) > 0)
            raise error("unsupported arrow tip option: " ++ unknown[0]) else true
        let line_width_pt = line_width_px / PT_PX
        let scale = if (option_value(entries, "scale") == null) 1.0
            else opts.numeric_value(option_value(entries, "scale"))^
        let scale_length = if (option_value(entries, "scale length") == null) 1.0
            else opts.numeric_value(option_value(entries, "scale length"))^
        let scale_width = if (option_value(entries, "scale width") == null) 1.0
            else opts.numeric_value(option_value(entries, "scale width"))^
        let length_pt = if (option_value(entries, "length") != null)
            option_dimension(option_value(entries, "length"), line_width_pt)^
            else table.base + table.factor * line_width_pt
        let natural_width = if (table.kind == "bar")
            table.width_base + table.width_factor * line_width_pt
            else table.width_ratio * length_pt
        let width_pt = if (option_value(entries, "width") != null)
            option_dimension(option_value(entries, "width"), line_width_pt)^
            else natural_width
        let length = length_pt * PT_PX * scale * scale_length
        let width = width_pt * PT_PX * scale * scale_width
        // Validation results are bound so the function returns only the tip map.
        let positive = if (length < 0.0 or width <= 0.0)
            raise error("arrow tip dimensions must be positive") else true
        let hollow = option_flag(entries, "open") or not table.filled
        let inset = table.inset_ratio * length
        let raw_color = option_value(entries, "color")
        let raw_fill = option_value(entries, "fill")
        let color = if (raw_color == null) null else opts.color_value(raw_color, custom_colors)^
        let tip_fill = if (raw_fill == null) null else opts.color_value(raw_fill, custom_colors)^;
        {kind: table.kind, length: length, width: width, inset: inset, hollow: hollow,
         reversed: tip.reversed or option_flag(entries, "reversed"),
         color: color, fill: tip_fill, round: option_flag(entries, "round"),
         // Filled tips cover the path end, so the stroke stops at the tip's back notch.
         line_end: if (hollow or tip.reversed or option_flag(entries, "reversed")) 0.0
            else length - inset}
    }
}

// Move `point` back toward `from` by `distance` px (never past `from`).
pub fn shortened(from, point, distance) {
    let dx = point[0] - from[0]
    let dy = point[1] - from[1]
    let length = math.sqrt(dx * dx + dy * dy)
    if (distance <= 0.0 or length == 0.0) point
    else {
        let cut = min([distance, length]);
        [point[0] - dx / length * cut, point[1] - dy / length * cut]
    }
}

fn local_point(end, ux, uy, x, y) =>
    [end[0] + x * ux - y * uy, end[1] + x * uy + y * ux]

fn local_path(end, ux, uy, points) =>
    util.str_join([for (index, point in points)
        (let mapped = local_point(end, ux, uy, point[0], point[1]),
         if (index == 0) svg.M(mapped[0], mapped[1]) else svg.L(mapped[0], mapped[1]))], " ")

// Draw `tip` at `end`, pointing away from `from` (px coordinates).
pub fn tip_svg(tip, from, end, color, stroke_width) {
    let dx = end[0] - from[0]
    let dy = end[1] - from[1]
    let distance = math.sqrt(dx * dx + dy * dy)
    let ux = dx / distance
    let uy = dy / distance
    let paint = if (tip.color == null) color else tip.color
    let body = if (tip.hollow) (if (tip.fill == null) "white" else tip.fill)
        else if (tip.fill == null) paint else tip.fill
    let length = tip.length
    let half = tip.width / 2.0
    // A reversed tip mirrors along the path: its apex sits at the back.
    let x_of = (x) => if (tip.reversed) 0.0 - length - x else x
    if (tip.kind == "legacy")
        (if (tip.reversed) svg.arrow_head(end[0], end[1],
            end[0] - ux * length, end[1] - uy * length, paint, length)
         else svg.arrow_head(from[0], from[1], end[0], end[1], paint, length))
    else if (tip.kind == "bar")
        <path d: local_path(end, ux, uy, [[0.0, half], [0.0, 0.0 - half]]),
            fill: "none", stroke: paint, 'stroke-width': stroke_width>
    else if (tip.kind == "circle") {
        let center = local_point(end, ux, uy, x_of(0.0 - length / 2.0), 0.0);
        <circle cx: center[0], cy: center[1], r: length / 2.0,
            fill: body, stroke: paint, 'stroke-width': stroke_width>
    }
    else if (tip.kind == "to")
        <path d: local_path(end, ux, uy, [[x_of(0.0 - length), half],
                [x_of(0.0), 0.0], [x_of(0.0 - length), 0.0 - half]]),
            fill: "none", stroke: paint, 'stroke-width': stroke_width,
            'stroke-linecap': "round", 'stroke-linejoin': "round">
    else if (tip.kind == "latex") {
        // Latex sides bow outward; control points follow arrows.meta's shape.
        let apex = local_point(end, ux, uy, x_of(0.0), 0.0)
        let upper_barb = local_point(end, ux, uy, x_of(0.0 - length), half)
        let lower_barb = local_point(end, ux, uy, x_of(0.0 - length), 0.0 - half)
        let c1 = local_point(end, ux, uy, x_of(0.0 - length * 0.35), half * 0.3)
        let c2 = local_point(end, ux, uy, x_of(0.0 - length * 0.8), half * 0.8)
        let c3 = local_point(end, ux, uy, x_of(0.0 - length * 0.8), 0.0 - half * 0.8)
        let c4 = local_point(end, ux, uy, x_of(0.0 - length * 0.35), 0.0 - half * 0.3);
        <path d: svg.M(apex[0], apex[1]) ++ " " ++
                svg.C(c1[0], c1[1], c2[0], c2[1], upper_barb[0], upper_barb[1]) ++ " " ++
                svg.L(lower_barb[0], lower_barb[1]) ++ " " ++
                svg.C(c3[0], c3[1], c4[0], c4[1], apex[0], apex[1]) ++ " Z",
            fill: body, stroke: if (tip.hollow) paint else "none",
            'stroke-width': stroke_width>
    }
    else {
        let apex = [x_of(0.0), 0.0]
        let upper_barb = [x_of(0.0 - length), half]
        let lower_barb = [x_of(0.0 - length), 0.0 - half]
        // Stealth adds its back notch between the two barbs.
        let outline = if (tip.kind == "stealth")
            [apex, upper_barb, [x_of(0.0 - length + tip.inset), 0.0], lower_barb]
            else [apex, upper_barb, lower_barb];
        <path d: local_path(end, ux, uy, outline) ++ " Z", fill: body,
            stroke: if (tip.hollow) paint else "none", 'stroke-width': stroke_width,
            'stroke-linejoin': if (tip.round) "round" else null>
    }
}
