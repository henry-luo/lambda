// Guide metrics share SVG's font resolution, character placement, and glyph bounds.
import radiant
import util: .util

pub fn style(config, role = "label", size = 11, weight = 400) {
    let family = config[role ++ "_font_family"];
    let own_size = config[role ++ "_font_size"];
    let own_weight = config[role ++ "_font_weight"];
    let slant = config[role ++ "_font_style"];
    {
        font_family: if (family != null) family else if (config.font_family != null) config.font_family else "Arial",
        font_size: if (own_size != null) own_size else size,
        font_weight: if (own_weight != null) own_weight else weight,
        font_style: if (slant != null) slant else "normal",
        letter_spacing: if (config[role ++ "_letter_spacing"] != null) config[role ++ "_letter_spacing"] else 0,
        word_spacing: if (config[role ++ "_word_spacing"] != null) config[role ++ "_word_spacing"] else 0
    }
}

pub fn attributes(font) => {
    'font-family': font.font_family, 'font-size': font.font_size,
    'font-weight': font.font_weight, 'font-style': font.font_style,
    'letter-spacing': font.letter_spacing, 'word-spacing': font.word_spacing, 'xml:space': "preserve"
}

pub let empty_metric = {width: 0.0, height: 0.0, baseline: 0.0,
    left: 0.0, top: 0.0, right: 0.0, bottom: 0.0}

pub fn measure_requests(requests) {
    if (len(requests) == 0) []
    else {
        let metrics = radiant.measure_text(
            [for (request in requests) {text: request.label, font: request.font}], null);
        if (metrics == null or len(metrics) != len(requests)) error("chart: text measurement failed")
        else metrics
    }
}

pub fn measure(labels, font) => measure_requests([for (label in labels) {label: label, font: font}])

pub fn span(metric) => metric.right - metric.left

// search all pending prefixes in one batch per round, only at grapheme boundaries.
fn fit_round(states, font, limit) {
    let pending = [for (index, search in states where search.high - search.low > 1) index];
    if (len(pending) == 0) [for (search in states) {text: search.best, metric: search.metric}]
    else {
        let candidates = [for (index in pending) (
            let search = states[index], let middle = int(floor(float(search.low + search.high) / 2.0)),
            {index: index, middle: middle, label: join(slice(search.parts, 0, middle), "") ++ "…"})];
        let metrics = measure(candidates |> ~.label, font);
        if (metrics is error) metrics else fit_round([for (index, search in states) (
            let slot = index_of(pending, index),
            if (slot == null) search else (
                let candidate = candidates[slot], let metric = metrics[slot],
                if (span(metric) <= limit) {*:search, low: candidate.middle, best: candidate.label, metric: metric}
                else {*:search, high: candidate.middle}))], font, limit)
    }
}

pub fn fit(labels, font, limit = null) {
    if (len(labels) == 0) [] else {
    // an absent ellipsis is not a text request; native batches reject null labels.
    let metrics = measure([*labels, *(if (limit != null) ["…"] else [])], font);
    if (metrics is error) metrics
    else if (limit == null) [for (index, label in labels) {text: label, metric: metrics[index]}]
    else {
        let ellipsis = metrics[len(labels)];
        let states = [for (index, label in labels) (
            let fits = span(metrics[index]) <= limit,
            let parts = if (fits) [] else radiant.graphemes(label),
            {parts: parts, low: 0, high: len(parts),
                best: if (fits) label else if (span(ellipsis) <= limit) "…" else "",
                metric: if (fits) metrics[index] else if (span(ellipsis) <= limit) ellipsis else empty_metric})];
        if (len(states |: ~.parts == null) > 0) error("chart: invalid UTF-8 label")
        else fit_round(states, font, limit)
    }
    }
}

pub fn bounds(metric, anchor = "start", angle = 0, x = 0.0, y = 0.0) {
    let shift = if (anchor == "middle") metric.width / 2.0 else if (anchor == "end") metric.width else 0.0;
    let radians = util.deg_to_rad(angle);
    let c = math.cos(radians);
    let s = math.sin(radians);
    let corners = [for (px in [metric.left - shift, metric.right - shift], py in [metric.top, metric.bottom])
        {x: x + px * c - py * s, y: y + px * s + py * c}];
    {left: min(corners |> ~.x), right: max(corners |> ~.x), top: min(corners |> ~.y), bottom: max(corners |> ~.y)}
}
