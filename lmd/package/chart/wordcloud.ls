// Weighted text layout and SVG rendering; shipped namespace follows D7.2.4.

import radiant
import color: .color
import scale: .scale
import svg: .svg
import util: .util

// Each square-spiral edge has a start corner and a unit travel direction.
let square_edges = [[1, -1, -1, 0], [-1, -1, 0, 1], [-1, 1, 1, 0], [1, 1, 0, -1]]

fn finite_number(value) => (value is int or value is i64 or value is float) and
    not (value is nan) and value != inf and value != -inf

fn option(opts, key, fallback) =>
    if (opts != null and opts[key] != null) opts[key] else fallback

fn number_option(opts, key, fallback, positive) float^ {
    let value = option(opts, key, fallback);
    if (not finite_number(value) or (if (positive) value <= 0 else value < 0))
        raise error("wordcloud: invalid " ++ key)
    else float(value)^
}

fn font_family(value) string^ {
    if (not (value is string) or len(trim(value)) == 0 or
        contains(value, ";") or contains(value, "{") or contains(value, "}"))
        raise error("wordcloud: invalid font_family")
    else value
}

fn font_weight(value) int^ {
    if (not (value is int) or value < 100 or value > 900)
        raise error("wordcloud: font_weight must be an int from 100 to 900")
    else value
}

fn valid_rotation(value) => finite_number(value) and value >= -180 and value <= 180

fn rotation(value) float^ {
    if (not valid_rotation(value)) raise error("wordcloud: rotation must be finite and from -180 to 180")
    else float(value)^
}

fn options(opts) map^ {
    if (opts != null and not (opts is map)) raise error("wordcloud: options must be a map")
    else {
        let width = number_option(opts, "width", 600, true)^;
        let height = number_option(opts, "height", 400, true)^;
        let margin = number_option(opts, "margin", 8, false)^;
        let padding = number_option(opts, "padding", 2, false)^;
        let min_font_size = number_option(opts, "min_font_size", 12, true)^;
        let max_font_size = number_option(opts, "max_font_size", 64, true)^;
        let step = number_option(opts, "step", 4, true)^;
        let max_steps = option(opts, "max_steps", 4000);
        let rotations = option(opts, "rotations", [0]);
        let family = font_family(option(opts, "font_family", "sans-serif"))^;
        let weight = font_weight(option(opts, "font_weight", 400))^;
        let palette = option(opts, "colors", color.category10);
        let shape = option(opts, "shape", "rectangle");
        let spiral = option(opts, "spiral", "archimedean");
        let size_scale = option(opts, "size_scale", "sqrt");
        let seed = option(opts, "seed", 0);
        // The headless layout API takes signed 32-bit viewport dimensions.
        if (width > 2147483647 or height > 2147483647 or margin * 2 >= min([width, height]))
            raise error("wordcloud: viewport must leave room inside margin")
        else if (min_font_size > max_font_size)
            raise error("wordcloud: min_font_size exceeds max_font_size")
        else if (not (max_steps is int) or max_steps <= 0)
            raise error("wordcloud: max_steps must be a positive int")
        else if (not (rotations is array) or len(rotations) == 0)
            raise error("wordcloud: rotations must be a nonempty array")
        else if (len([for (r in rotations where not valid_rotation(r)) r]) > 0)
            raise error("wordcloud: rotations must be finite and from -180 to 180")
        else if (not (palette is array) or len(palette) == 0)
            raise error("wordcloud: colors must be a nonempty array")
        else if (len([for (c in palette where not (c is string) or len(c) == 0) c]) > 0)
            raise error("wordcloud: colors must contain nonempty strings")
        else if (not contains(["rectangle", "ellipse", "circle", "diamond"], shape))
            raise error("wordcloud: invalid shape")
        else if (not contains(["archimedean", "rectangular"], spiral))
            raise error("wordcloud: invalid spiral")
        else if (not contains(["sqrt", "linear", "log"], size_scale))
            raise error("wordcloud: invalid size_scale")
        else if (not (seed is int) or seed < 0 or seed >= 2147483647)
            raise error("wordcloud: seed must be an int from 0 to 2147483646")
        else {
            width: width, height: height, margin: margin, padding: padding,
            min_font_size: min_font_size, max_font_size: max_font_size,
            font_family: family, font_weight: weight, colors: palette,
            rotations: rotations, step: step, max_steps: max_steps,
            shape: shape, spiral: spiral, size_scale: size_scale, seed: seed,
            _rx: if (shape == "circle") min([width, height]) / 2.0 - margin else width / 2.0 - margin,
            _ry: if (shape == "circle") min([width, height]) / 2.0 - margin else height / 2.0 - margin,
            _limit: math.sqrt(width * width + height * height) / 2.0
        }
    }
}

fn validate_word(word, index) map^ {
    if (not (word is map) or not (word.text is string) or len(trim(word.text)) == 0)
        raise error("wordcloud: each word needs nonempty text")
    else if (contains(word.text, "\n") or contains(word.text, "\r") or contains(word.text, "\t"))
        raise error("wordcloud: text must be a single line without tabs")
    else if (not finite_number(word.weight) or word.weight <= 0)
        raise error("wordcloud: weight must be finite and positive")
    else if (word.color != null and (not (word.color is string) or len(word.color) == 0))
        raise error("wordcloud: color must be a nonempty string")
    else {*:word, index: index}
}

fn prepare(words, opts) array^ {
    let valid = [for (i, word in words) validate_word(word, i)^];
    let weights = [for (word in valid) float(word.weight)^];
    let lo = min(weights);
    let hi = max(weights);
    let sizing = if (opts.size_scale == "linear") scale.linear_scale(lo, hi, opts.min_font_size, opts.max_font_size)
        else if (opts.size_scale == "log") scale.log_scale(lo, hi, opts.min_font_size, opts.max_font_size, 10)
        else scale.sqrt_scale(lo, hi, opts.min_font_size, opts.max_font_size);
    // Keep exact integer keys; float rounding must not introduce ties (S6.2.3).
    let ordered = sort(valid, (word) => 0 - word.weight);
    [for (word in ordered) {
        *:word,
        font_size: number_option(word, "font_size",
            if (lo == hi) opts.max_font_size else scale.scale_apply(sizing, word.weight), true)^,
        font_family: font_family(option(word, "font_family", opts.font_family))^,
        font_weight: font_weight(option(word, "font_weight", opts.font_weight))^,
        rotation: rotation(option(word, "rotation", opts.rotations[word.index % len(opts.rotations)]))^,
        color: if (word.color != null) word.color else color.pick_color(opts.colors, word.index)
    }]
}

fn axes(angle) {
    // Orthogonal rotations have exact axes, avoiding roundoff in legacy box dimensions.
    let c = if (angle == 0) 1.0 else if (abs(angle) == 90) 0.0
        else if (abs(angle) == 180) -1.0 else math.cos(util.deg_to_rad(angle));
    let s = if (angle == 0 or abs(angle) == 180) 0.0 else if (angle == 90) 1.0
        else if (angle == -90) -1.0 else math.sin(util.deg_to_rad(angle));
    {c: c, s: s}
}

fn measure(words, opts) array^ {
    let html = <html <head <meta charset: "utf-8">> <body style: "margin:0;",
        for (word in words)
            <span style: "display:inline-block;white-space:pre;line-height:normal;" ++
                "font-family:" ++ word.font_family ++ ";font-weight:" ++ string(word.font_weight) ++
                ";font-size:" ++ string(word.font_size) ++ "px;",
                word.text>
    >>;
    let boxes = radiant.measure_html(format(html, 'html'), int(ceil(opts.width)), int(ceil(opts.height)));
    if (boxes == null or len(boxes) != len(words)) raise error("wordcloud: text measurement failed")
    else [for (i, word in words) (
        let box = boxes[i],
        if (box == null or box.width <= 0 or box.height <= 0)
            raise error("wordcloud: word has no measurable text")
        else (
            let axis = axes(word.rotation),
            {
                *:word, text_width: box.width, text_height: box.height, baseline: box.baseline,
                width: abs(axis.c) * box.width + abs(axis.s) * box.height,
                height: abs(axis.s) * box.width + abs(axis.c) * box.height,
                _c: axis.c, _s: axis.s, _span: abs(axis.c) + abs(axis.s),
                _corners: [for (sx in [-1, 1], sy in [-1, 1]) {
                    x: (sx * box.width * axis.c - sy * box.height * axis.s) / 2.0,
                    y: (sx * box.width * axis.s + sy * box.height * axis.c) / 2.0
                }]
            }
        )
    )]
}

fn projection_radius(word, nx, ny, padding) =>
    ((word.text_width + padding) * abs(word._c * nx + word._s * ny) +
        (word.text_height + padding) * abs(0.0 - word._s * nx + word._c * ny)) / 2.0

fn separated(a, b, dx, dy, nx, ny, padding) =>
    abs(dx * nx + dy * ny) >= projection_radius(a, nx, ny, padding) + projection_radius(b, nx, ny, padding)

fn collides(word, x, y, placed, index, padding) {
    if (index >= len(placed)) false
    else {
        let other = placed[index];
        let dx = x - other.x;
        let dy = y - other.y;
        let extra = padding * (word._span + other._span);
        // AABB rejection precedes the four separating axes of the padded text rectangles.
        if (abs(dx) < (word.width + other.width + extra) / 2.0 and
            abs(dy) < (word.height + other.height + extra) / 2.0 and
            not separated(word, other, dx, dy, word._c, word._s, padding) and
            not separated(word, other, dx, dy, 0.0 - word._s, word._c, padding) and
            not separated(word, other, dx, dy, other._c, other._s, padding) and
            not separated(word, other, dx, dy, 0.0 - other._s, other._c, padding)) true
        else collides(word, x, y, placed, index + 1, padding)
    }
}

fn corners_fit(word, x, y, opts, index) {
    if (index >= len(word._corners)) true
    else {
        let corner = word._corners[index];
        let nx = abs(x + corner.x - opts.width / 2.0) / opts._rx;
        let ny = abs(y + corner.y - opts.height / 2.0) / opts._ry;
        let inside = if (opts.shape == "diamond") nx + ny <= 1.0 else nx * nx + ny * ny <= 1.0;
        inside and corners_fit(word, x, y, opts, index + 1)
    }
}

fn fits(word, x, y, opts) =>
    x - word.width / 2.0 >= opts.margin and x + word.width / 2.0 <= opts.width - opts.margin and
    y - word.height / 2.0 >= opts.margin and y + word.height / 2.0 <= opts.height - opts.margin and
    (opts.shape == "rectangle" or corners_fit(word, x, y, opts, 0))

fn next_seed(seed) => (seed * 48271) % 2147483647

fn phase(seed, index) {
    // The 31-bit recurrence is local to this call; zero preserves the original path.
    if (seed == 0) 0.0
    else util.TAU * float(next_seed(next_seed((seed + index) % 2147483646 + 1))) / 2147483647.0
}

fn spiral_point(index, opts) {
    if (opts.spiral == "archimedean") {
        // Successive Archimedean turns stay one step apart.
        let angle = float(index) * 0.35;
        let radius = opts.step * angle / util.TAU;
        {x: radius * math.cos(angle), y: radius * math.sin(angle), limit: radius}
    } else if (index == 0) {x: 0.0, y: 0.0, limit: 0.0}
    else {
        let ring = int(ceil((math.sqrt(float(index) + 1.0) - 1.0) / 2.0));
        let side = 2 * ring;
        let offset = (2 * ring + 1) * (2 * ring + 1) - 1 - index;
        let edge = square_edges[int(floor(float(offset) / float(side)))];
        let along = offset % side;
        {x: opts.step * (edge[0] * ring + edge[2] * along),
            y: opts.step * (edge[1] * ring + edge[3] * along), limit: opts.step * float(ring)}
    }
}

fn seek(word, placed, opts, index) {
    if (index >= opts.max_steps) null
    else {
        let point = spiral_point(index, opts);
        let x = opts.width / 2.0 + point.x * word._phase_c - point.y * word._phase_s;
        let y = opts.height / 2.0 + point.x * word._phase_s + point.y * word._phase_c;
        // A rectangular ring's closest point bounds the whole ring, not its corners.
        if (point.limit > opts._limit) null
        else if (fits(word, x, y, opts) and
            not collides(word, x, y, placed, 0, opts.padding)) {*:word, x: x, y: y}
        else seek(word, placed, opts, index + 1)
    }
}

fn place(words, opts, index, placed, unplaced) {
    if (index >= len(words)) {words: placed, unplaced: unplaced}
    else {
        let start = phase(opts.seed, index);
        let word = {*:words[index], _phase_c: math.cos(start), _phase_s: math.sin(start)};
        // These shapes are convex and centrally symmetric: a box fits somewhere iff it fits centered.
        let oversized = not fits(word, opts.width / 2.0, opts.height / 2.0, opts);
        let found = if (oversized) null else seek(word, placed, opts, 0);
        place(words, opts, index + 1,
            if (found != null) [*placed, found] else placed,
            if (found == null) [*unplaced, {*:word,
                reason: if (oversized) "too_large" else "no_space"}] else unplaced)
    }
}

// Coordinates are word-box centers; no mutable document handles escape.
pub fn layout(words, opts = null) map^ {
    let resolved = options(opts)^;
    if (not (words is array)) raise error("wordcloud: words must be an array")
    else {
        let result = if (len(words) == 0) {words: [], unplaced: []}
            else place(measure(prepare(words, resolved)^, resolved)^, resolved, 0, [], []);
        {*:resolved, *:result}
    }
}

fn render_word(word) element^ {
    svg.group("translate(" ++ util.fmt_num(word.x) ++ ", " ++ util.fmt_num(word.y) ++
        ") rotate(" ++ util.fmt_num(word.rotation) ++ ")", [
        <title word.text ++ ": " ++ string(word.weight)>,
        <text *:(if (word._chart_attrs != null) word._chart_attrs else {}),
            x: 0.0 - word.text_width / 2.0, y: word.baseline - word.text_height / 2.0,
            'font-family': word.font_family, 'font-weight': word.font_weight,
            'font-size': word.font_size, fill: word.color, 'xml:space': "preserve",
            word.text>
    ])^
}

pub fn render(words, opts = null) element^ {
    let result = layout(words, opts)^;
    <svg xmlns: "http://www.w3.org/2000/svg", width: result.width, height: result.height,
        viewBox: "0 0 " ++ util.fmt_num(result.width) ++ " " ++ util.fmt_num(result.height),
        role: "img", 'aria-label': "Word cloud", 'data-unplaced': len(result.unplaced),
        svg.group_class("wordcloud", [for (word in result.words) render_word(word)^])>
}
