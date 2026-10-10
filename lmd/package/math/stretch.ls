// Prefer font variants and assemblies. Without construction data, transform the
// measured outline and its box together; no particular Size1–4 font is assumed.
import bx: .svg_box

fn expanded(parts, repeats) => [
    for (part in parts, i in 1 to (if (part.extender) repeats else 1)) part
]

fn overlaps(parts) => [for (i in 1 to (len(parts) - 1))
    min(parts[i - 1].end_connector, parts[i].start_connector)]

// Ordinary fonts have no arrow assembly: extend the shaft without distorting the head.
pub fn arrow(g, target, scale, right, axis, thickness) map | error {
    if (len(g.horizontal.parts) > 0 or len(g.horizontal.variants) > 0)
        glyph(g, target, false, scale, "mrel")^
    else {
        let natural = bx.glyph(g, scale, "mrel")
        let width = max(target, natural.width)
        let extra = width - natural.width
        let start = if (right) 0.0 else g.ink.right * scale - thickness
        let length = extra + (if (right) g.ink.left * scale else natural.width - g.ink.right * scale) + thickness;
        if (extra <= 0.0) natural
        else bx.compose([{box: bx.rule(length, thickness, 0.0 - axis - thickness / 2.0), x: start, y: 0.0},
            {box: natural, x: if (right) extra else 0.0, y: 0.0}], width, "mrel")
    }
}

// TeX accents use the largest designed variant that fits over the nucleus.
pub fn accent(g, target, scale) map | error {
    let variants = [for (v in g.horizontal.variants where v.extent * scale <= target) v];
    if (len(variants) > 0) bx.glyph(variants[len(variants) - 1], scale)
    else if (len(g.horizontal.variants) > 0) bx.glyph(g, scale)
    else glyph(g, target, false, scale, "mord")^
}

fn recipe(construction, target, repeats) map | error {
    let parts = expanded(construction.parts, repeats)
    let joins = overlaps(parts)
    let maximum = sum([for (part in parts) part.extent]) - construction.min_overlap * max(0, len(parts) - 1)
    let gain = sum([for (part in construction.parts where part.extender) part.extent - construction.min_overlap])
    if (len(parts) > 0 and maximum >= target) {
        let minimum = sum([for (part in parts) part.extent]) - sum(joins)
        let extent = max(target, minimum)
        let room = maximum - minimum
        let ratio = if (room > 0.0) (maximum - extent) / room else 0.0
        let overlap = [for (join in joins) construction.min_overlap + ratio * (join - construction.min_overlap)];
        {parts: parts, overlap: overlap, extent: extent}
    } else if (gain <= 0.0 or repeats >= 4096)
        error("math: font has no usable assembly for requested stretch extent")
    else recipe(construction, target, max(repeats + 1, repeats + int(ceil((target - maximum) / gain))))
}

pub fn glyph(g, target, vertical, scale, atom) map | error {
    let construction = if (vertical) g.vertical else g.horizontal
    let variants = [for (v in construction.variants where v.extent * scale >= target) v]
    let natural = bx.glyph(g, scale, atom)
    // Combining accents have no advance. Stretch their visible outline, including its origin.
    let outline = if (not vertical and g.has_math == false and g.ink != null)
        bx.shifted(natural, 0.0 - g.ink.left * scale, 0.0, (g.ink.right - g.ink.left) * scale)
        else natural
    let natural_extent = if (vertical) natural.height + natural.depth else outline.width
    if (natural_extent >= target) natural
    else if (len(variants) > 0) bx.glyph(variants[0], scale, atom)
    else if (len(construction.parts) > 0) {
        let spec = recipe(construction, target / scale, 0)^
        let entries = [for (i, part in spec.parts) (
            let position = (sum([for (j in 0 to (i - 1)) spec.parts[j].extent]) - sum(slice(spec.overlap, 0, i))) * scale,
            {box: bx.glyph(part, scale, atom), x: if (vertical) 0.0 else position, y: if (vertical) 0.0 - position else 0.0})]
        let width = if (vertical) max([for (part in spec.parts) part.advance]) * scale else spec.extent * scale
        let result = bx.compose(entries, width, atom);
        {*:result, italic: construction.italic * scale, accent: width / 2.0}
    } else if (len(construction.variants) > 0)
        bx.glyph(construction.variants[len(construction.variants) - 1], scale, atom)
    else if (g.has_math == false and natural_extent > 0.0) bx.stretched(outline,
        if (vertical) 1.0 else target / natural_extent, if (vertical) target / natural_extent else 1.0)
    else natural
}
