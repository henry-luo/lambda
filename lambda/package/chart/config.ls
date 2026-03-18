// chart/config.ls — Configuration and theming for the chart library
// Provides theme presets and configuration resolution.

// ============================================================
// Theme presets
// ============================================================

pub light_theme = {
    background: "white",
    title_color: "#333",
    title_font_size: 16,
    axis_domain_color: "#888",
    axis_tick_color: "#888",
    axis_grid_color: "#e0e0e0",
    axis_label_color: "#333",
    axis_title_color: "#333",
    legend_label_color: "#333",
    legend_title_color: "#333",
    mark_color: "#4e79a7"
}

pub dark_theme = {
    background: "#333",
    title_color: "#eee",
    title_font_size: 16,
    axis_domain_color: "#888",
    axis_tick_color: "#888",
    axis_grid_color: "#555",
    axis_label_color: "#ccc",
    axis_title_color: "#eee",
    legend_label_color: "#ccc",
    legend_title_color: "#eee",
    mark_color: "#4e79a7"
}

// ============================================================
// Resolve theme from config element
// ============================================================

pub fn resolve_theme(config) {
    if (config and config.theme == "dark") dark_theme
    else light_theme
}

// ============================================================
// Build axis config map from theme
// ============================================================

pub fn axis_config(theme) {
    {
        domain_color: theme.axis_domain_color,
        tick_color: theme.axis_tick_color,
        grid_color: theme.axis_grid_color,
        label_color: theme.axis_label_color,
        title_color: theme.axis_title_color
    }
}

// ============================================================
// Build legend config map from theme
// ============================================================

pub fn legend_config(theme) {
    {
        label_color: theme.legend_label_color,
        title_color: theme.legend_title_color
    }
}
