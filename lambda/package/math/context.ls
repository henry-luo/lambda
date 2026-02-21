// math/context.ls — Rendering context for math typesetting
// The context carries math style, font, color, and scale information
// down through the rendering tree. It is immutable — child contexts
// are new maps with overridden fields.

import met: .lambda.package.math.metrics

// ============================================================
// Math styles and their relationships
// ============================================================

// create the default (root) context for display math
pub fn display_context() {
    {
        style: "display",
        size: 1.0,
        color: null,
        font: "mathit",
        cramped: false,
        phantom: false
    }
}

// create the default context for inline math
pub fn text_context() {
    {
        style: "text",
        size: 1.0,
        color: null,
        font: "mathit",
        cramped: false,
        phantom: false
    }
}

// ============================================================
// Context derivation — create child contexts
// ============================================================

// derive a new context with overrides
pub fn derive(ctx, overrides) {
    {
        style: if (overrides.style != null) overrides.style else ctx.style,
        size: if (overrides.size != null) overrides.size else ctx.size,
        color: if (overrides.color != null) overrides.color else ctx.color,
        font: if (overrides.font != null) overrides.font else ctx.font,
        cramped: if (overrides.cramped != null) overrides.cramped else ctx.cramped,
        phantom: if (overrides.phantom != null) overrides.phantom else ctx.phantom
    }
}

// derive context for a fraction numerator
pub fn numer_context(ctx) {
    let new_style = match ctx.style {
        case "display": "text"
        case "text": "script"
        case "script": "scriptscript"
        default: "scriptscript"
    }
    derive(ctx, {style: new_style})
}

// derive context for a fraction denominator
pub fn denom_context(ctx) {
    let new_style = match ctx.style {
        case "display": "text"
        case "text": "script"
        case "script": "scriptscript"
        default: "scriptscript"
    }
    derive(ctx, {style: new_style, cramped: true})
}

// derive context for superscript
pub fn sup_context(ctx) {
    let new_style = match ctx.style {
        case "display": "script"
        case "text": "script"
        case "script": "scriptscript"
        default: "scriptscript"
    }
    derive(ctx, {style: new_style})
}

// derive context for subscript
pub fn sub_context(ctx) {
    let new_style = match ctx.style {
        case "display": "script"
        case "text": "script"
        case "script": "scriptscript"
        default: "scriptscript"
    }
    derive(ctx, {style: new_style, cramped: true})
}

// ============================================================
// Context queries
// ============================================================

// is this a display-style context?
pub fn is_display(ctx) => ctx.style == "display"

// is this a script or scriptscript context?
pub fn is_script(ctx) => ctx.style == "script" or ctx.style == "scriptscript"

// get the scale for the current style relative to text size
pub fn context_scale(ctx) => met.style_scale(ctx.style) * ctx.size

// get the metric index for the current style
pub fn metric_index(ctx) => met.style_index(ctx.style)

// get a font metric value for the current style
pub fn get_metric(ctx, metric_arr) => metric_arr[met.style_index(ctx.style)]

// get rule thickness for current context
pub fn rule_thickness(ctx) => get_metric(ctx, met.defaultRuleThickness)

// CSS font-size percentage string if scaling needed
pub fn font_size_css(ctx) {
    let s = context_scale(ctx)
    if (s == 1.0) null
    else string(round(s * 1000.0) / 10.0) ++ "%"
}
