// Reusable vector silhouettes for the named tikzpeople node profile.
import opts: .options
import labels: .labels
import util: lambda.latex.util

let PX_PER_CM = 96.0 / 2.54
let PROFILES = [
    {name: "alice", outfit: "dress", hat: "none", shirt: "#8aa6dd", mark: "A"},
    {name: "bob", outfit: "shirt", hat: "none", shirt: "#6d9ac1", mark: "B"},
    {name: "bride", outfit: "dress", hat: "veil", shirt: "#f4f1ee", mark: "B"},
    {name: "builder", outfit: "vest", hat: "helmet", shirt: "#e4b75d", mark: "B"},
    {name: "businessman", outfit: "suit", hat: "none", shirt: "#52647f", mark: "B"},
    {name: "charlie", outfit: "suit", hat: "bowler", shirt: "#45444b", mark: "C"},
    {name: "chef", outfit: "apron", hat: "chef", shirt: "#f2f1ee", mark: "C"},
    {name: "conductor", outfit: "uniform", hat: "cap", shirt: "#547293", mark: "C"},
    {name: "cowboy", outfit: "shirt", hat: "wide", shirt: "#b08364", mark: "C"},
    {name: "criminal", outfit: "stripes", hat: "none", shirt: "#888999", mark: "C"},
    {name: "dave", outfit: "shirt", hat: "none", shirt: "#8faf7d", mark: "D"},
    {name: "devil", outfit: "robe", hat: "horns", shirt: "#b85250", mark: "D"},
    {name: "duck", outfit: "duck", hat: "none", shirt: "#f0d978", mark: "D"},
    {name: "graduate", outfit: "robe", hat: "mortar", shirt: "#4b4c59", mark: "G"},
    {name: "groom", outfit: "suit", hat: "none", shirt: "#363b4b", mark: "G"},
    {name: "guard", outfit: "uniform", hat: "cap", shirt: "#78919e", mark: "G"},
    {name: "jester", outfit: "stripes", hat: "jester", shirt: "#a46397", mark: "J"},
    {name: "judge", outfit: "robe", hat: "wig", shirt: "#4b4b53", mark: "J"},
    {name: "maninblack", outfit: "suit", hat: "none", shirt: "#333941", mark: "M"},
    {name: "mexican", outfit: "poncho", hat: "wide", shirt: "#c68457", mark: "M"},
    {name: "nun", outfit: "robe", hat: "veil", shirt: "#454b58", mark: "N"},
    {name: "nurse", outfit: "coat", hat: "nurse", shirt: "#ebeeee", mark: "N"},
    {name: "physician", outfit: "coat", hat: "none", shirt: "#e3e9ed", mark: "P"},
    {name: "pilot", outfit: "uniform", hat: "cap", shirt: "#5a6e92", mark: "P"},
    {name: "police", outfit: "uniform", hat: "cap", shirt: "#425c83", mark: "P"},
    {name: "priest", outfit: "robe", hat: "none", shirt: "#3e4250", mark: "P"},
    {name: "sailor", outfit: "stripes", hat: "sailor", shirt: "#7092b6", mark: "S"},
    {name: "santa", outfit: "coat", hat: "santa", shirt: "#bf5759", mark: "S"},
    {name: "surgeon", outfit: "coat", hat: "surgical", shirt: "#7eb7b2", mark: "S"}
]

pub fn names() => [for (profile in PROFILES) profile.name]

fn profile_for(shape_name) any^ {
    let matches = [for (profile in PROFILES where profile.name == shape_name) profile]
    if (len(matches) != 1) raise error("unknown tikzpeople shape: " ++ shape_name)
    else matches[0]
}

fn node_profile(node) any^ {
    let matched = [for (profile in PROFILES where opts.has(node, profile.name)) profile]
    if (len(matched) != 1)
        raise error("tikzpeople node needs exactly one named shape")
    else matched[0]
}

fn node_size(node) float^ {
    let raw = opts.value(node, "minimum size", "1.5cm")
    let size = opts.dimension_px(raw)^
    if (size < 18.0 or size > 300.0)
        raise error("tikzpeople minimum size must be 18..300 CSS px")
    else size
}

fn node_color(node, key, fallback, custom_colors) string^ {
    opts.color_value(opts.value(node, key, fallback), custom_colors)^
}

fn hat_color(profile, hair, shirt) =>
    if (profile.hat == "sailor" or profile.hat == "nurse" or
        profile.hat == "chef") "#f2f1ee"
    else if (profile.hat == "helmet") "#e7bd5c"
    else if (profile.hat == "surgical" or profile.hat == "cap") shirt
    else hair

fn hat(profile, color) =>
    if (profile.hat == "none") null
    else if (profile.hat == "veil")
        <path d: "M28 18 Q50 -8 72 18 L76 58 L66 55 L65 19 Z",
            fill: "#eeeef3", stroke: color, 'stroke-width': 1.4>
    else if (profile.hat == "wide")
        <g <ellipse cx: 50, cy: 16, rx: 32, ry: 6, fill: color>
            <path d: "M31 17 L36 2 Q50 -5 64 2 L69 17 Z", fill: color>>
    else if (profile.hat == "chef")
        <g <rect x: 34, y: 8, width: 32, height: 12, fill: color>
            <path d: "M34 10 Q26 -3 39 -4 Q50 -12 60 -4 Q74 -2 66 10 Z",
                fill: color>>
    else if (profile.hat == "mortar")
        <g <path d: "M17 8 L50 -3 L83 8 L50 19 Z", fill: color>
            <path d: "M34 13 L66 13 L64 22 L36 22 Z", fill: color>>
    else if (profile.hat == "santa")
        <g <path d: "M30 16 Q40 -13 66 -6 L76 0 L63 15 Z",
                fill: "#be4549", stroke: color, 'stroke-width': 1.3>
            <path d: "M27 16 Q50 9 70 16 L70 22 Q50 17 27 22 Z", fill: "white">
            <circle cx: 76, cy: 1, r: 6, fill: "white">>
    else if (profile.hat == "horns" or profile.hat == "jester")
        <path d: "M31 21 L24 -7 Q31 2 41 10 L59 10 Q69 2 76 -7 L69 21 Z",
            fill: color, stroke: "#3d3940", 'stroke-width': 1.2>
    else if (profile.hat == "bowler")
        <g <ellipse cx: 50, cy: 17, rx: 26, ry: 5, fill: color>
            <path d: "M35 17 L35 3 Q50 -7 65 3 L65 17 Z", fill: color>>
    else if (profile.hat == "wig")
        <path d: "M30 15 Q24 6 33 0 Q42 -10 50 0 Q58 -10 67 0 Q76 7 70 15 Z",
            fill: "#e9e7e5", stroke: color, 'stroke-width': 1.0>
    else <g <path d: "M30 18 Q50 2 70 18 L70 24 L30 24 Z", fill: color>
        <path d: "M25 22 L76 22 L76 26 L25 26 Z", fill: color>>

fn body(profile, shirt, skin, hair, female) =>
    if (profile.outfit == "duck")
        <g <ellipse cx: 48, cy: 70, rx: 31, ry: 22,
                fill: skin, stroke: "#3a3a40", 'stroke-width': 1.5>
            <circle cx: 60, cy: 34, r: 20,
                fill: skin, stroke: "#3a3a40", 'stroke-width': 1.5>
            <path d: "M72 39 L94 45 L72 50 Z", fill: "#e8a64e">
            <circle cx: 67, cy: 31, r: 2.5, fill: "#333">
            <path d: "M28 70 Q45 47 65 68 Q49 87 28 70 Z",
                fill: hair, stroke: "#777", 'stroke-width': 1>
            <path d: "M33 91 L31 98 M61 91 L64 98", stroke: "#d58e43",
                'stroke-width': 5>>
    else <g
        <path d: "M42 73 L38 99 M58 73 L62 99", stroke: "#454b56",
            'stroke-width': 10, 'stroke-linecap': "round">
        <path d: "M34 54 L18 76 M66 54 L82 76", stroke: shirt,
            'stroke-width': 10, 'stroke-linecap': "round">
        if (female or profile.outfit == "dress" or profile.outfit == "robe" or
            profile.outfit == "poncho")
            <path d: "M35 48 L65 48 L76 88 L24 88 Z", fill: shirt,
                stroke: "#454b56", 'stroke-width': 1.5>
        else <path d: "M32 48 Q50 43 68 48 L65 80 L35 80 Z",
                fill: shirt, stroke: "#454b56", 'stroke-width': 1.5>;
        <ellipse cx: 50, cy: 32, rx: 16, ry: 18,
            fill: skin, stroke: "#705c56", 'stroke-width': 1.3>
        if (female or profile.outfit == "dress")
            <path d: "M32 25 Q35 7 51 9 Q69 9 69 28 L65 55 L59 52 L63 19 Q50 10 38 20 L40 52 L34 55 Z",
                fill: hair>
        else <path d: "M33 27 Q31 9 50 10 Q69 9 67 27 Q52 20 33 27 Z",
                fill: hair>;
        <circle cx: 44, cy: 33, r: 1.5, fill: "#30343b">
        <circle cx: 57, cy: 33, r: 1.5, fill: "#30343b">
        <path d: "M45 42 Q50 46 55 42", fill: "none", stroke: "#895e60",
            'stroke-width': 1.2>
        if (profile.outfit == "stripes")
            <path d: "M34 59 L66 59 M34 68 L66 68", stroke: "#edf1f3",
                'stroke-width': 4>
        if (profile.outfit == "suit")
            <path d: "M42 48 L50 67 L58 48 L54 73 L46 73 Z", fill: "#edf1f3">
        if (profile.outfit == "coat" or profile.outfit == "apron")
            <path d: "M42 51 L58 51 L60 80 L40 80 Z", fill: "#f5f2ed",
                stroke: "#777", 'stroke-width': 0.8>
        >

fn additions(node, profile, skin) =>
    <g
        if (opts.has(node, "good"))
            <ellipse cx: 50, cy: 0, rx: 21, ry: 6,
                fill: "none", stroke: "#d7b347", 'stroke-width': 4>
        if (opts.has(node, "evil") and profile.hat != "horns")
            <path d: "M34 19 L29 0 L43 15 M66 19 L71 0 L57 15",
                fill: "#b95354", stroke: "#7d3435", 'stroke-width': 1.5>
        if (opts.has(node, "monitor"))
            <g <rect x: 70, y: 63, width: 29, height: 22, rx: 2,
                    fill: "#d1d5dc", stroke: "#49586b", 'stroke-width': 2>
                <rect x: 74, y: 67, width: 21, height: 14, fill: "#5d91ad">
                <path d: "M84 85 L84 93 M76 93 L92 93", stroke: "#49586b",
                    'stroke-width': 2>>
        if (opts.has(node, "sword"))
            <g <path d: "M80 79 L93 23 L97 18 L95 27 L85 82 Z",
                    fill: "#c6ced5", stroke: "#4d5660", 'stroke-width': 1.2>
                <path d: "M74 70 L92 76 M81 77 L77 92", stroke: "#765543",
                    'stroke-width': 4>>
        if (opts.has(node, "shield"))
            <path d: "M4 55 L27 55 L25 78 Q15 91 5 78 Z",
                fill: "#7893b6", stroke: "#454f61", 'stroke-width': 2>
        if (profile.name == "santa")
            <path d: "M36 43 Q50 66 64 43 Q65 60 50 64 Q35 60 36 43 Z",
                fill: "#f4f2ee", stroke: "#b2aaa2", 'stroke-width': 1>
        if (profile.name == "sailor")
            <path d: "M35 50 L50 60 L65 50", fill: "none", stroke: "white",
                'stroke-width': 4>
        if (profile.name == "nurse" or profile.name == "physician")
            <text x: 50, y: 69, 'text-anchor': "middle", 'font-size': 17,
                fill: "#b65158", "+">
        if (profile.name == "charlie")
            <path d: "M43 39 Q47 34 50 39 Q53 34 58 39",
                fill: "none", stroke: "#303238", 'stroke-width': 2>
    >

fn person_shape(node, profile, size, x, y, custom_colors) any^ {
    let shirt = node_color(node, "shirt", profile.shirt, custom_colors)^
    let skin = node_color(node, "skin",
        if (profile.outfit == "duck") profile.shirt else "#e0b99e", custom_colors)^
    let hair = node_color(node, "hair",
        if (profile.outfit == "duck") "#f8e8a6" else "#5b4b43", custom_colors)^
    let female = opts.has(node, "female")
    let scale = size / 100.0
    let mirror = if (opts.has(node, "mirrored"))
        "translate(100 0) scale(-1 1)" else "";
    <g transform: "translate(" ++ string(x) ++ " " ++ string(y) ++
            ") scale(" ++ string(scale) ++ ")",
        <g transform: mirror,
            body(profile, shirt, skin, hair, female)
            hat(profile, hat_color(profile, hair, shirt))
            additions(node, profile, skin)
        >
    >
}

fn pin_source(node) any^ {
    let raw = opts.value(node, "pin", null)
    if (raw == null) null
    else {
        let source = trim(raw)
        let after_options = if (starts_with(source, "["))
            util.read_balanced(source, 0, "[", "]")^ else null
        let remaining = if (after_options == null) source
            else trim(slice(source, after_options.next, len(source)))
        let colon = util.top_level_separator(remaining, ":")
        let angle = if (colon == null) 45.0
            else opts.numeric_value(trim(slice(remaining, 0, colon)))^
        let text = if (colon == null) remaining
            else trim(slice(remaining, colon + 1, len(remaining)))
        let width = if (after_options == null) null else after_options.raw
        let valid_width = if (width != null and not starts_with(width, "text width="))
            raise error("unsupported tikzpeople pin option") else true
        {angle: angle, text: util.unwrap_braces(text), width: width}
    }
}

fn record(node, custom_colors) any^ {
    let profile = node_profile(node)^
    let checked = opts.check(node, [*names(), "good", "evil", "female",
        "mirrored", "monitor", "shield", "sword",
        "minimum size", "pin", "anchor", "skin", "hair", "shirt"],
        custom_colors)^
    let size = node_size(node)^
    let anchor = opts.value(node, "anchor", "center")
    let valid_anchor = if (anchor != "center" and anchor != "south")
        raise error("unsupported tikzpeople anchor: " ++ anchor) else true
    let x = float(node.x) * PX_PER_CM - size / 2.0
    let y = 0.0 - float(node.y) * PX_PER_CM
    let top = if (anchor == "south") y - size else y - size / 2.0;
    {source: node, profile: profile, size: size, x: x, top: top,
     pin: pin_source(node)^}
}

fn pin_position(entry, dx, dy, pin_distance) {
    let angle = entry.pin.angle * 3.141592653589793 / 180.0
    let distance = entry.size * 0.6 + pin_distance;
    {x: entry.x + dx + entry.size * 0.5 + math.cos(angle) * distance,
     y: entry.top + dy + entry.size * 0.35 - math.sin(angle) * distance,
     vx: math.cos(angle), vy: 0.0 - math.sin(angle)}
}

fn pin_line(entry, dx, dy, pin_distance) {
    if (entry.pin == null) null
    else {
        let pin = pin_position(entry, dx, dy, pin_distance)
        let start_x = entry.x + dx + entry.size * 0.5 + pin.vx * entry.size * 0.3
        let start_y = entry.top + dy + entry.size * 0.35 + pin.vy * entry.size * 0.3;
        <path d: "M" ++ string(start_x) ++ " " ++ string(start_y) ++
            " L" ++ string(pin.x) ++ " " ++ string(pin.y),
            fill: "none", stroke: "#666", 'stroke-width': 0.8>
    }
}

fn pin_label(entry, dx, dy, pin_distance, font_style) any^ {
    if (entry.pin == null) null
    else {
        let pin = pin_position(entry, dx, dy, pin_distance)
        let width = if (entry.pin.width == null) "160px"
            else trim(slice(entry.pin.width, len("text width="),
                len(entry.pin.width)))
        let prepared = labels.prepare(entry.pin.text)^;
        <span class: "tikzpeople-pin",
            style: "position:absolute;left:" ++ string(pin.x) ++ "px;top:" ++
                string(pin.y) ++ "px;max-width:" ++ width ++
                ";white-space:normal;font-size:13px;" ++ font_style,
            prepared.element>
    }
}

pub fn render_picture(picture, custom_colors = []) any^ {
    let checked = opts.check(picture,
        ["pin distance", "every pin/.append style"], custom_colors)^
    let font = opts.value(picture, "every pin/.append style", null)
    let valid_font = if (font != null and font != "font=\\sffamily\\itshape")
        raise error("unsupported tikzpeople pin style") else true
    let font_style = if (font == null) ""
        else "font-family:sans-serif;font-style:italic;"
    let pin_distance = opts.dimension_px(opts.value(picture,
        "pin distance", "1cm"))^
    let nodes = [for (child in picture where child is element and
        string(name(child)) == "node") child]
    let other = [for (child in picture where child is element and
        string(name(child)) != "node" and string(name(child)) != "option") child]
    let valid_nodes = if (len(nodes) == 0 or len(other) > 0)
        raise error("tikzpeople picture needs only named nodes") else true
    let entries = [for (node in nodes) record(node, custom_colors)^]
    let left = min([for (entry in entries) entry.x - 45.0]) - 20.0
    let right = max([for (entry in entries) entry.x + entry.size + 150.0])
    let top = min([for (entry in entries) entry.top - 70.0])
    let bottom = max([for (entry in entries) entry.top + entry.size + 55.0])
    let width = right - left
    let height = bottom - top
    let graphic = <svg xmlns: "http://www.w3.org/2000/svg",
        width: width, height: height,
        viewBox: "0 0 " ++ string(width) ++ " " ++ string(height),
        for (entry in entries)
            person_shape(entry.source, entry.profile, entry.size,
                entry.x - left, entry.top - top, custom_colors)^;
        for (entry in entries)
            pin_line(entry, 0.0 - left, 0.0 - top, pin_distance)>
    let style = "position:relative;display:inline-block;width:" ++
        string(width) ++ "px;height:" ++ string(height) ++ "px;";
    <span class: "tikz-picture tikzpeople-picture", style: style,
        graphic
        for (entry in entries)
            if (trim(entry.source.source) != "")
                labels.positioned(labels.prepare(entry.source.source)^,
                    entry.x - left + entry.size / 2.0,
                    entry.top - top + entry.size + 20.0)
        for (entry in entries)
            pin_label(entry, 0.0 - left, 0.0 - top,
                pin_distance, font_style)^
    >
}

fn gallery_figure(profile, size, show_name) any^ {
    let node = <node source: "">
    let shape = person_shape(node, profile, size, 0.0, 0.0, [])^;
    <div class: "tikzpeople-gallery-item",
        style: "display:flex;flex-direction:column;align-items:center;" ++
            "gap:4px;break-inside:avoid;min-width:" ++ string(size + 16.0) ++ "px;",
        <svg xmlns: "http://www.w3.org/2000/svg",
            width: size, height: size,
            viewBox: "0 -10 " ++ string(size) ++ " " ++ string(size + 10.0),
            shape>
        if (show_name) <span profile.name>
    >
}

pub fn gallery(size_source, options_source, show_names = true) any^ {
    let size = opts.numeric_value(size_source)^ * PX_PER_CM
    let valid_size = if (size < 18.0 or size > 300.0)
        raise error("tikzpeople gallery size must be 18..300 CSS px") else true
    let valid_options = if (trim(options_source) != "")
        raise error("tikzpeople gallery options are unsupported") else true;
    <div class: "tikzpeople-gallery",
        style: "display:grid;grid-template-columns:repeat(4,max-content);" ++
            "gap:20px;margin:1em 0;",
        for (profile in PROFILES) gallery_figure(profile, size, show_names)^>
}
