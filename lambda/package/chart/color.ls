// chart/color.ls — Color palettes and color interpolation for the chart library

// ============================================================
// Categorical color schemes
// ============================================================

// Tableau 10 — default categorical palette
pub category10 = [
    "#4e79a7", "#f28e2b", "#e15759", "#76b7b2", "#59a14f",
    "#edc948", "#b07aa1", "#ff9da7", "#9c755f", "#bab0ac"
]

// Extended 20-color palette
pub category20 = [
    "#4e79a7", "#a0cbe8", "#f28e2b", "#ffbe7d", "#e15759",
    "#ff9d9a", "#76b7b2", "#8cd17d", "#b6992d", "#f1ce63",
    "#499894", "#86bcb6", "#e15759", "#ff9da7", "#79706e",
    "#bab0ac", "#d37295", "#fabfd2", "#b07aa1", "#d4a6c8"
]

// Set 1 — bold colors
pub set1 = [
    "#e41a1c", "#377eb8", "#4daf4a", "#984ea3", "#ff7f00",
    "#ffff33", "#a65628", "#f781bf", "#999999"
]

// Pastel 1
pub pastel1 = [
    "#fbb4ae", "#b3cde3", "#ccebc5", "#decbe4", "#fed9a6",
    "#ffffcc", "#e5d8bd", "#fddaec", "#f2f2f2"
]

// Dark 2
pub dark2 = [
    "#1b9e77", "#d95f02", "#7570b3", "#e7298a", "#66a61e",
    "#e6ab02", "#a6761d", "#666666"
]

// ============================================================
// Sequential color schemes (for quantitative data)
// ============================================================

pub blues = ["#deebf7", "#c6dbef", "#9ecae1", "#6baed6", "#4292c6", "#2171b5", "#084594"]
pub greens = ["#e5f5e0", "#c7e9c0", "#a1d99b", "#74c476", "#41ab5d", "#238b45", "#005a32"]
pub reds = ["#fee0d2", "#fcbba1", "#fc9272", "#fb6a4a", "#ef3b2c", "#cb181d", "#99000d"]
pub oranges = ["#feedde", "#fdd0a2", "#fdae6b", "#fd8d3c", "#f16913", "#d94801", "#8c2d04"]
pub purples = ["#f2f0f7", "#dadaeb", "#bcbddc", "#9e9ac8", "#807dba", "#6a51a3", "#4a1486"]
pub greys = ["#f7f7f7", "#d9d9d9", "#bdbdbd", "#969696", "#737373", "#525252", "#252525"]

// ============================================================
// Diverging color schemes
// ============================================================

pub red_blue = ["#b2182b", "#d6604d", "#f4a582", "#fddbc7", "#d1e5f0", "#92c5de", "#4393c3", "#2166ac"]
pub spectral = ["#d53e4f", "#f46d43", "#fdae61", "#fee08b", "#e6f598", "#abdda4", "#66c2a5", "#3288bd"]

// ============================================================
// Default mark color
// ============================================================
pub default_color = "#4e79a7"

// ============================================================
// Color scheme lookup
// ============================================================

// get a color scheme by name
pub fn get_scheme(scheme_name: string) {
    if (scheme_name == "category10") category10
    else if (scheme_name == "category20") category20
    else if (scheme_name == "set1") set1
    else if (scheme_name == "pastel1") pastel1
    else if (scheme_name == "dark2") dark2
    else if (scheme_name == "blues") blues
    else if (scheme_name == "greens") greens
    else if (scheme_name == "reds") reds
    else if (scheme_name == "oranges") oranges
    else if (scheme_name == "purples") purples
    else if (scheme_name == "greys") greys
    else if (scheme_name == "red_blue") red_blue
    else if (scheme_name == "spectral") spectral
    else category10
}

// get the nth color from a categorical scheme, cycling if needed
pub fn pick_color(scheme, index: int) string {
    let n = len(scheme);
    scheme[index % n]
}

// ============================================================
// Color for sequential scales (pick nearest from palette)
// ============================================================

// given a sequential color scheme and a t in [0, 1], pick the nearest color
pub fn sequential_color(scheme, t) string {
    let n = len(scheme);
    if (n == 0) default_color
    else if (n == 1) scheme[0]
    else (let raw_idx = float(t) * float(n - 1) + 0.5,
          let idx = int(raw_idx),
          let safe_idx = if (idx < 0) 0 else if (idx >= n) n - 1 else idx,
          scheme[safe_idx])
}
