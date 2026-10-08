// chart/color.ls — Color palettes and color interpolation for the chart library

// ============================================================
// Categorical color schemes
// ============================================================

// Tableau 10 — default categorical palette
pub let category10 = [
    "#4e79a7", "#f28e2b", "#e15759", "#76b7b2", "#59a14f",
    "#edc948", "#b07aa1", "#ff9da7", "#9c755f", "#bab0ac"
]

// Extended 20-color palette
pub let category20 = [
    "#4e79a7", "#a0cbe8", "#f28e2b", "#ffbe7d", "#e15759",
    "#ff9d9a", "#76b7b2", "#8cd17d", "#b6992d", "#f1ce63",
    "#499894", "#86bcb6", "#e15759", "#ff9da7", "#79706e",
    "#bab0ac", "#d37295", "#fabfd2", "#b07aa1", "#d4a6c8"
]

// Set 1 — bold colors
pub let set1 = [
    "#e41a1c", "#377eb8", "#4daf4a", "#984ea3", "#ff7f00",
    "#ffff33", "#a65628", "#f781bf", "#999999"
]

// Pastel 1
pub let pastel1 = [
    "#fbb4ae", "#b3cde3", "#ccebc5", "#decbe4", "#fed9a6",
    "#ffffcc", "#e5d8bd", "#fddaec", "#f2f2f2"
]

// Dark 2
pub let dark2 = [
    "#1b9e77", "#d95f02", "#7570b3", "#e7298a", "#66a61e",
    "#e6ab02", "#a6761d", "#666666"
]

// ============================================================
// Sequential color schemes (for quantitative data)
// ============================================================

pub let blues = ["#deebf7", "#c6dbef", "#9ecae1", "#6baed6", "#4292c6", "#2171b5", "#084594"]
pub let greens = ["#e5f5e0", "#c7e9c0", "#a1d99b", "#74c476", "#41ab5d", "#238b45", "#005a32"]
pub let reds = ["#fee0d2", "#fcbba1", "#fc9272", "#fb6a4a", "#ef3b2c", "#cb181d", "#99000d"]
pub let oranges = ["#feedde", "#fdd0a2", "#fdae6b", "#fd8d3c", "#f16913", "#d94801", "#8c2d04"]
pub let purples = ["#f2f0f7", "#dadaeb", "#bcbddc", "#9e9ac8", "#807dba", "#6a51a3", "#4a1486"]
pub let greys = ["#f7f7f7", "#d9d9d9", "#bdbdbd", "#969696", "#737373", "#525252", "#252525"]

// ============================================================
// Diverging color schemes
// ============================================================

pub let red_blue = ["#b2182b", "#d6604d", "#f4a582", "#fddbc7", "#d1e5f0", "#92c5de", "#4393c3", "#2166ac"]
pub let spectral = ["#d53e4f", "#f46d43", "#fdae61", "#fee08b", "#e6f598", "#abdda4", "#66c2a5", "#3288bd"]

// ============================================================
// Default mark color
// ============================================================
pub let default_color = "#4e79a7"

// Sampled published d3-scale-chromatic ramps; endpoints and center are retained.
// https://github.com/d3/d3-scale-chromatic/blob/main/src/sequential-multi/viridis.js
pub let viridis = [
    "#440154", "#470d60", "#48186a", "#482374", "#472d7b", "#453781", "#424086", "#3e4989", "#3b528b", "#375b8d", "#33638d", "#2f6b8e", "#2c728e", "#297a8e", "#26828e", "#23898e", "#21918c", "#1f978b", "#1f9f88", "#21a685", "#27ad81", "#31b57b", "#3dbc74", "#4cc26c", "#5cc863", "#6ece58", "#81d34d", "#95d840", "#aadc32", "#c0df25", "#d5e21a", "#eae51a", "#fde725"
]

pub let magma = [
    "#000004", "#030312", "#0a0822", "#130d34", "#1d1147", "#29115a", "#36106b", "#440f76", "#51127c", "#5d177f", "#6a1c81", "#762181", "#832681", "#902a81", "#9c2e7f", "#aa337d", "#b73779", "#c23b75", "#cf4070", "#db476a", "#e55064", "#ee5b5e", "#f4695c", "#f9785d", "#fb8761", "#fd9668", "#fea571", "#feb47b", "#fec287", "#fed194", "#fde0a1", "#fceeb0", "#fcfdbf"
]

pub let inferno = [
    "#000004", "#040312", "#0b0724", "#150b37", "#210c4a", "#2f0a5b", "#3d0965", "#4a0c6b", "#57106e", "#64156e", "#71196e", "#7d1e6d", "#8a226a", "#972766", "#a32c61", "#b0315b", "#bc3754", "#c63d4d", "#d04545", "#da4e3c", "#e35933", "#eb6429", "#f1711f", "#f67e14", "#f98c0a", "#fb9b06", "#fcaa0f", "#fbba1f", "#f9c932", "#f5d949", "#f2e865", "#f3f586", "#fcffa4"
]

pub let plasma = [
    "#0d0887", "#220690", "#310597", "#3f049c", "#4c02a1", "#5901a5", "#6600a7", "#7201a8", "#7e03a8", "#8a09a5", "#9511a1", "#a01a9c", "#aa2395", "#b32c8e", "#bc3587", "#c43e7f", "#cc4778", "#d24f71", "#d9586a", "#df6263", "#e56b5d", "#eb7556", "#f07f4f", "#f48948", "#f89441", "#fb9f3a", "#fdab33", "#feb72d", "#fdc328", "#fcd025", "#f9dd25", "#f5eb27", "#f0f921"
]

pub let set2 = ["#66c2a5", "#fc8d62", "#8da0cb", "#e78ac3", "#a6d854", "#ffd92f", "#e5c494", "#b3b3b3"]
pub let red_blue_midpoint = ["#b2182b", "#d6604d", "#f4a582", "#fddbc7", "#f7f7f7", "#d1e5f0", "#92c5de", "#4393c3", "#2166ac"]

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
    else if (scheme_name == "viridis") viridis
    else if (scheme_name == "magma") magma
    else if (scheme_name == "inferno") inferno
    else if (scheme_name == "plasma") plasma
    else if (scheme_name == "set2") set2
    else if (scheme_name == "red_blue_midpoint") red_blue_midpoint
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
