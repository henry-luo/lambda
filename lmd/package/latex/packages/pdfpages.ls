// latex/packages/pdfpages.ls — \includepdf[keys]{file}: the selected pages of a PDF,
// each a page-wide block through graphicx's PDF path (vibe/impl/Lambda_Impl_Latex_Packages.md
// §9). nup, pagecommand and landscape, and keys outside the profile, are reported.

import util: ~~.util
import graphicx: .graphicx

// page layouts each page is inserted without: reported (registry.target_diagnostics)
pub let LAYOUT_KEYS = ["nup", "pagecommand", "landscape"]

// fitpaper sizes the paper to the inserted page, and HTML has no paper
pub let KEYS = ["pages", "scale", "angle", "frame", "width", "height", "keepaspectratio", "fitpaper"] ++ LAYOUT_KEYS

pub fn unknown_keys(opts) => [for (key, value at opts where not (contains(KEYS, string(key)) or false)) string(key)]

// the pages of a pages= list: n, a-b (either way round), open ranges a- and -b, -
// for all, last, and {} for a blank page (null)
pub fn page_list(spec, count) {
    let items = util.split_top_level(trim(spec or "1"), ",");
    [for (item in items, page in pages_of(trim(item), count)) page]
}

fn page_number(text, count) {
    let raw = trim(text)
    if (raw == "last") count else int(raw) ^ { 0 }
}

fn pages_of(item, count) {
    let dash = index_of(item, "-")
    if (item == "{}") [null]
    else if (dash == null) [page_number(item, count)]
    else {
        let first = if (dash == 0) 1 else page_number(slice(item, 0, dash), count)
        let final = if (dash == len(item) - 1) count else page_number(slice(item, dash + 1, len(item)), count)
        if (first <= final) [for (p in first to final) p] else [for (k in 0 to first - final) first - k]
    }
}

// the graphicx keys of one inserted page: by default it fills the line, and scale
// is a fraction of that
pub fn page_keys(opts) {
    let scale = float(trim(opts.scale or "1")) ^ { 1.0 }
    if (opts.width != null or opts.height != null)
        {width: opts.width, height: opts.height, keepaspectratio: opts.keepaspectratio, angle: opts.angle}
    else {width: string(scale) ++ "\\linewidth", angle: opts.angle}
}
