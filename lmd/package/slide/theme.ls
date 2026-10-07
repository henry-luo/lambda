import c: .common

pub let names = ['light', 'dark', 'corporate', 'midnight', 'paper']
pub let layouts = ['blank', 'title', 'title-body', 'two-column', 'section', 'quote', 'three-column', 'image-left', 'image-right']
pub let roles = ['title', 'body', 'left', 'right', 'center', 'footer']

// named palettes and authored overrides share one resolved representation.
pub fn resolve(spec, path = "theme") map^ {
    let custom = spec is map
    let base = symbol(c.as_text(c.value(if (custom) spec.base else spec, 'light')))
    let checked_name = if (not contains(names, base)) raise c.fail(path, "unsupported theme")
    let checked_attrs = if (custom) c.attributes(spec,
        ["base", "background", "foreground", "accent", "font_family", "title_size", "body_size"], path)^;
    let checked_numbers = if (custom) c.numbers(spec, ["title_size", "body_size"], true, path)^;
    let checked_strings = if (custom and any([for (key in ["background", "foreground", "accent", "font_family"] where spec[key] != null)
        not (spec[key] is string) or len(spec[key]) == 0])) raise c.fail(path, "theme colors and font_family must be nonempty strings")
    let dark = base == 'dark' or base == 'midnight'
    let defaults = {base: base,
        background: if (base == 'midnight') "#0b132b" else if (base == 'paper') "#faf7ef" else if (dark) "#111827" else "#ffffff",
        foreground: if (base == 'paper') "#302b24" else if (dark) "#f9fafb" else "#111827",
        accent: if (base == 'midnight') "#5bc0be" else if (base == 'paper') "#a44a3f" else if (base == 'corporate') "#0057b8" else "#3b82f6",
        font_family: if (base == 'paper') "Georgia, serif" else "Arial, sans-serif", title_size: 56.0, body_size: 32.0}
    {*: defaults, *: if (custom) map([for (key, value in spec where value != null) (string(key), value)]) else {}}
}

pub fn background(scene) => c.value(scene.source.background, scene.palette.background)

// layouts supply bounds only; explicitly authored coordinates always win.
fn box(x, y, width, height) => {x: x, y: y, width: width, height: height}

pub fn bounds(layout, role, width, height, index) {
    let margin = width * 0.0625
    let top = height * 0.0833333333
    let inner = width - margin * 2.0
    let heading = height * 0.1666666667
    let body_y = top + heading + height * 0.05
    if (layout is map) {
        let slot = layout[c.as_text(role)]
        // unnamed built-in roles retain their usual bounds in a custom layout.
        if (slot != null) slot else bounds('title-body', role, width, height, index)
    }
    else if (role == 'footer') box(margin, height - top, inner, top * 0.7)
    else if (layout == 'blank') box(0.0, 0.0, width, height)
    else if (layout == 'section' or layout == 'quote')
        box(margin, if (role == 'title') height * 0.3 else height * 0.55, inner,
            if (role == 'title') height * 0.25 else height * 0.3)
    else if (role == 'title' or (role == null and index == 0)) box(margin, top, inner, heading)
    else if (layout == 'title') box(margin, height * 0.45, inner, height * 0.25)
    else if (contains(['two-column', 'three-column', 'image-left', 'image-right'], layout)) {
        let columns = if (layout == 'three-column') 3.0 else 2.0
        let column = (inner - margin * (columns - 1.0)) / columns
        let slot = if (role == 'body' and (layout == 'image-left' or layout == 'image-right'))
                (if (layout == 'image-left') 1.0 else 0.0)
            else if (role == 'right') columns - 1.0 else if (role == 'center') 1.0
            else if (role == null) max(0.0, min(columns - 1.0, float(index - 1))) else 0.0
        box(margin + slot * (column + margin),
            body_y, column, height - body_y - top)
    } else box(margin, body_y, inner, height - body_y - top)
}
