// Weighted text layout and SVG rendering; shipped namespace follows D7.2.4.

import radiant
import color: .color
import scale: .scale
import svg: .svg
import util: .util

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
        let family = option(opts, "font_family", "sans-serif");
        let weight = option(opts, "font_weight", 400);
        let palette = option(opts, "colors", color.category10);
        // The headless layout API takes signed 32-bit viewport dimensions.
        if (width > 2147483647 or height > 2147483647 or margin * 2 >= min([width, height]))
            raise error("wordcloud: viewport must leave room inside margin")
        else if (min_font_size > max_font_size)
            raise error("wordcloud: min_font_size exceeds max_font_size")
        else if (not (max_steps is int) or max_steps <= 0)
            raise error("wordcloud: max_steps must be a positive int")
        else if (not (family is string) or len(family) == 0 or
            contains(family, ";") or contains(family, "{") or contains(family, "}"))
            raise error("wordcloud: invalid font_family")
        else if (not (weight is int) or weight < 100 or weight > 900)
            raise error("wordcloud: font_weight must be an int from 100 to 900")
        else if (not (rotations is array) or len(rotations) == 0)
            raise error("wordcloud: rotations must be a nonempty array")
        else if (len([for (r in rotations where r != 0 and r != 90 and r != -90) r]) > 0)
            raise error("wordcloud: rotations must be 0, 90 or -90")
        else if (not (palette is array) or len(palette) == 0)
            raise error("wordcloud: colors must be a nonempty array")
        else if (len([for (c in palette where not (c is string) or len(c) == 0) c]) > 0)
            raise error("wordcloud: colors must contain nonempty strings")
        else {
            width: width, height: height, margin: margin, padding: padding,
            min_font_size: min_font_size, max_font_size: max_font_size,
            font_family: family, font_weight: weight, colors: palette,
            rotations: rotations, step: step, max_steps: max_steps
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
    else {text: word.text, weight: word.weight, color: word.color, index: index}
}

fn prepare(words, opts) array^ {
    let valid = [for (i, word in words) validate_word(word, i)^];
    let weights = [for (word in valid) float(word.weight)^];
    let lo = min(weights);
    let hi = max(weights);
    let sizing = scale.sqrt_scale(lo, hi, opts.min_font_size, opts.max_font_size);
    // Keep exact integer keys; float rounding must not introduce ties (S6.2.3).
    let ordered = sort(valid, (word) => 0 - word.weight);
    [for (word in ordered) {
        *:word,
        font_size: if (lo == hi) opts.max_font_size else scale.scale_apply(sizing, word.weight),
        rotation: opts.rotations[word.index % len(opts.rotations)],
        color: if (word.color != null) word.color else color.pick_color(opts.colors, word.index)
    }]
}

fn measure(words, opts) array^ {
    let html = <html <head <meta charset: "utf-8">> <body style: "margin:0;",
        for (word in words)
            <span style: "display:inline-block;white-space:pre;line-height:normal;" ++
                "font-family:" ++ opts.font_family ++ ";font-weight:" ++ string(opts.font_weight) ++
                ";font-size:" ++ string(word.font_size) ++ "px;",
                word.text>
    >>;
    let boxes = radiant.measure_html(format(html, 'html'), int(ceil(opts.width)), int(ceil(opts.height)));
    if (boxes == null or len(boxes) != len(words)) raise error("wordcloud: text measurement failed")
    else [for (i, word in words) (
        let box = boxes[i],
        if (box == null or box.width <= 0 or box.height <= 0)
            raise error("wordcloud: word has no measurable text")
        else {
            *:word, text_width: box.width, text_height: box.height, baseline: box.baseline,
            width: if (word.rotation == 0) box.width else box.height,
            height: if (word.rotation == 0) box.height else box.width
        }
    )]
}

fn collides(word, placed, index, padding) {
    if (index >= len(placed)) false
    else {
        let other = placed[index];
        if (abs(word.x - other.x) < (word.width + other.width) / 2.0 + padding and
            abs(word.y - other.y) < (word.height + other.height) / 2.0 + padding) true
        else collides(word, placed, index + 1, padding)
    }
}

fn seek(word, placed, opts, index) {
    if (index >= opts.max_steps) null
    else {
        // An Archimedean spiral keeps successive turns one step apart.
        let angle = float(index) * 0.35;
        let radius = opts.step * angle / util.TAU;
        let x = opts.width / 2.0 + radius * math.cos(angle);
        let y = opts.height / 2.0 + radius * math.sin(angle);
        let candidate = {*:word, x: x, y: y};
        if (radius > math.sqrt(opts.width * opts.width + opts.height * opts.height) / 2.0) null
        else if (x - word.width / 2.0 >= opts.margin and
            x + word.width / 2.0 <= opts.width - opts.margin and
            y - word.height / 2.0 >= opts.margin and
            y + word.height / 2.0 <= opts.height - opts.margin and
            not collides(candidate, placed, 0, opts.padding)) candidate
        else seek(word, placed, opts, index + 1)
    }
}

fn place(words, opts, index, placed, unplaced) {
    if (index >= len(words)) {words: placed, unplaced: unplaced}
    else {
        let word = words[index];
        let oversized = word.width > opts.width - 2.0 * opts.margin or
            word.height > opts.height - 2.0 * opts.margin;
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

fn render_word(word, result) element^ {
    svg.group("translate(" ++ util.fmt_num(word.x) ++ ", " ++ util.fmt_num(word.y) ++
        ") rotate(" ++ string(word.rotation) ++ ")", [
        <title word.text ++ ": " ++ string(word.weight)>,
        <text x: 0.0 - word.text_width / 2.0, y: word.baseline - word.text_height / 2.0,
            'font-family': result.font_family, 'font-weight': result.font_weight,
            'font-size': word.font_size, fill: word.color, 'xml:space': "preserve",
            word.text>
    ])^
}

pub fn render(words, opts = null) element^ {
    let result = layout(words, opts)^;
    <svg xmlns: "http://www.w3.org/2000/svg", width: result.width, height: result.height,
        viewBox: "0 0 " ++ util.fmt_num(result.width) ++ " " ++ util.fmt_num(result.height),
        role: "img", 'aria-label': "Word cloud", 'data-unplaced': len(result.unplaced),
        svg.group_class("wordcloud", [for (word in result.words) render_word(word, result)^])>
}
