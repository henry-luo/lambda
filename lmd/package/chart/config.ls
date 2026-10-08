// chart/config.ls — Configuration and theming for the chart library
// Provides theme presets and configuration resolution.

import parse: .parse

pub fn settings(value) => map([for (key, field in parse.attributes(value) where field != null)
    for (part in [string(key), field]) part])

pub fn inherit(parent, child) {
    let merged = {*:settings(parent), *:settings(child)};
    map([for (key, field in merged) for (part in [string(key),
        if (parent[key] is map and child[key] is map) inherit(parent[key], child[key]) else field]) part])
}

fn prefixed(value, prefix) => map([for (key, field in value where starts_with(string(key), prefix))
    for (part in [slice(string(key), len(prefix)), field]) part])

fn chart_settings(value) {
    let own = settings(value);
    let sections = map([for (section, fields in own where fields is map and string(section) != "theme")
        for (key, field in fields) for (part in [string(section) ++ "_" ++ string(key), field]) part]);
    {*:sections, *:own}
}

// ============================================================
// Theme presets
// ============================================================

pub let light_theme = {
    background: "white",
    title_color: "#333",
    title_font_size: 16,
    axis_domain_color: "#888",
    axis_tick_color: "#888",
    axis_grid_color: "#e0e0e0",
    axis_label_color: "#333",
    axis_title_color: "#333",
    legend_label_color: "#333",
    legend_title_color: "#333"
}

pub let dark_theme = {
    background: "#333",
    title_color: "#eee",
    title_font_size: 16,
    axis_domain_color: "#888",
    axis_tick_color: "#888",
    axis_grid_color: "#555",
    axis_label_color: "#ccc",
    axis_title_color: "#eee",
    legend_label_color: "#ccc",
    legend_title_color: "#eee"
}

pub let minimal_theme = {*:light_theme, axis_domain_color: "#bbb", axis_tick_color: "#bbb", axis_grid: false}
pub let presentation_theme = {*:light_theme, title_font_size: 24,
    axis_label_font_size: 16, axis_title_font_size: 18, legend_label_font_size: 16,
    legend_title_font_size: 18, mark_size: 80, line_stroke_width: 3}

// ============================================================
// Resolve theme from config element
// ============================================================

pub fn resolve_theme(config) {
    let own = chart_settings(config);
    let preset = if (own.theme == "dark") dark_theme
        else if (own.theme == "minimal") minimal_theme
        else if (own.theme == "presentation") presentation_theme
        else if (own.theme is map) own.theme else light_theme;
    // Explicit chart settings override the preset, including false and zero values.
    {*:light_theme, *:chart_settings(preset), *:own}
}

pub fn mark_config(theme, mark) {
    let own = settings(mark);
    let kind = if (own.kind != null) own.kind else "point";
    let combined = {*:prefixed(theme, "mark_"), *:prefixed(theme, kind ++ "_"), *:own, kind: kind};
    {*:combined, font_family: if (combined.font_family != null) combined.font_family else theme.font}
}

pub fn svg_attributes(theme) => if (theme.font != null) {'font-family': theme.font} else {}

// ============================================================
// Build axis config map from theme
// ============================================================

pub fn axis_config(theme, channel = null) {
    {*:prefixed(theme, "axis_"), *:settings(channel.axis),
        format: if (channel.axis and channel.axis.format != null) channel.axis.format else channel.format,
        dtype: if (channel._temporal) "temporal" else channel.dtype,
        timezone: channel.scale.timezone,
        title_enabled: not parse.has_attribute(channel.axis, "title") or channel.axis.title != null,
        enabled: channel == null or parse.option_enabled(channel, "axis")}
}

// ============================================================
// Build legend config map from theme
// ============================================================

pub fn legend_config(theme, channel = null) {
    {*:prefixed(theme, "legend_"), *:settings(channel.legend),
        title_enabled: not parse.has_attribute(channel.legend, "title") or channel.legend.title != null,
        enabled: channel == null or parse.option_enabled(channel, "legend")}
}
