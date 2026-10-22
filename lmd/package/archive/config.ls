// Configuration (AR2v2): the package's complete defaults, a deep merge of
// the caller's configuration over them, and a check that names every path
// that fails, before anything is fetched (S17.8.1 in spirit).

pub let FORMATS = ["html", "readable", "markdown", "pdf"]
pub let KINDS = ["page", "video", "audio", "image", "gallery", "file"]
let DUPLICATES = ["skip", "new", "replace"]

pub let DEFAULTS = {
    defaults: {
        formats: ["html", "readable", "markdown"],
        user_agent: "LambdaArchive/0.1 (+personal archive)",
        timeout_s: 60,
        per_host_delay_ms: 500,
        on_duplicate: "skip",
        progress_ms: 250,
        jobs: 2,
        // the worker that derives the renderings (AR-O12)
        render_timeout_s: 300,
        page: {max_assets: 400, max_asset_mb: 25, max_total_mb: 200, assets_per_host: 4},
        // page size in CSS pixels: A4 at 96 dpi
        pdf: {source: "readable", pagination: true, width: 794, height: 1123},
        thumbnail: true,
        tracking_params: ["utm_source", "utm_medium", "utm_campaign", "utm_term", "utm_content", "utm_id",
            "fbclid", "gclid", "mc_cid", "mc_eid", "ref_src"]
    },
    // the native media and gallery engines (AR4v2)
    media: {max_height: 1080, max_mb: 2048, max_segment_mb: 64, subtitles: ["en"], prefer: ["fmp4", "ts"],
        segment_retries: 2, timeout_s: 3600},
    gallery: {min_pixels: 40000, max_images: 500, max_image_mb: 50, timeout_s: 1800},
    rules: []
}

// the option schema: a leaf names a checker, a map nests
let DEFAULT_SCHEMA = {formats: "formats", user_agent: "string", timeout_s: "number", per_host_delay_ms: "number",
    on_duplicate: "duplicate", progress_ms: "number", jobs: "number", render_timeout_s: "number",
    page: {max_assets: "number", max_asset_mb: "number", max_total_mb: "number", assets_per_host: "number"},
    pdf: {source: "pdf_source", pagination: "bool", width: "number", height: "number"}, thumbnail: "bool", tracking_params: "strings"}
let MEDIA_SCHEMA = {max_height: "number", max_mb: "number", max_segment_mb: "number", subtitles: "strings",
    prefer: "strings", segment_retries: "number", timeout_s: "number"}
let GALLERY_SCHEMA = {min_pixels: "number", max_images: "number", max_image_mb: "number", timeout_s: "number"}
let SCHEMA = {defaults: DEFAULT_SCHEMA, media: MEDIA_SCHEMA, gallery: GALLERY_SCHEMA, rules: "rules"}
// a rule carries a match, an optional kind, media and gallery settings, and any default
let RULE_SCHEMA = {*: DEFAULT_SCHEMA, match: {host: "globs", path: "globs", scheme: "globs", query: "globs"},
    kind: "kind", media: MEDIA_SCHEMA, gallery: GALLERY_SCHEMA}
pub let SETTING_KEYS = ["formats", "user_agent", "timeout_s", "per_host_delay_ms", "on_duplicate", "progress_ms",
    "jobs", "render_timeout_s", "page", "pdf", "thumbnail", "tracking_params"]

fn assign(acc, key: any, value: any) => {*: acc, [key]: value}

// a deep merge: maps merge by key, anything else is replaced by `over`
pub fn merge(base: any, over: any) {
    if (not (base is map and over is map)) (if (over == null) base else over)
    else reduce([base, *[for (k, v at over) [k, v]]], (acc, kv) => assign(acc, kv[0], merge(acc[kv[0]], kv[1])))
}

// a shallow merge: every field of `top` replaces the same field of `base`
pub fn overlay(base: any, top: any) => if (not (top is map)) base
    else reduce([base or {}, *[for (k, v at top) [k, v]]], (acc, kv) => assign(acc, kv[0], kv[1]))

// the fields of `m` among `keys`
pub fn pick(m: any, keys) => if (not (m is map)) {}
    else reduce([{}, *[for (k, v at m where string(k) in keys) [k, v]]], (acc, kv) => assign(acc, kv[0], kv[1]))

// the effective settings of one job: per-call options over the matched rule
// over the caller's defaults over the package defaults
pub fn settings(cfg, rule: any, options: any) => merge(merge(cfg.defaults, pick(rule, SETTING_KEYS)), pick(options, SETTING_KEYS))

fn is_strings(v: any) => v is array and all([for (x in v) x is string])
fn is_globs(v: any) => v is string or is_strings(v)

fn check_leaf(kind, value: any, path) {
    if (kind == "string") (if (value is string) [] else [path ++ ": expected text"])
    else if (kind == "string?") (if (value is string or value == null) [] else [path ++ ": expected text or null"])
    else if (kind == "number") (if (value is number and value >= 0) [] else [path ++ ": expected a non-negative number"])
    else if (kind == "bool") (if (value is bool) [] else [path ++ ": expected true or false"])
    else if (kind == "strings") (if (is_strings(value)) [] else [path ++ ": expected a list of text"])
    else if (kind == "globs") (if (is_globs(value)) [] else [path ++ ": expected a glob or a list of globs"])
    else if (kind == "map") (if (value is map) [] else [path ++ ": expected a map"])
    else if (kind == "formats") (if (value is array and all([for (x in value) string(x) in FORMATS])) []
        else [path ++ ": expected formats among " ++ join(FORMATS, ", ")])
    else if (kind == "kind") (if (string(value) in KINDS) [] else [path ++ ": expected a kind among " ++ join(KINDS, ", ")])
    else if (kind == "duplicate") (if (string(value) in DUPLICATES) [] else [path ++ ": expected skip, new or replace"])
    else if (kind == "pdf_source") (if (string(value) in ["readable", "snapshot"]) [] else [path ++ ": expected readable or snapshot"])
    else if (kind == "rules") check_rules(value, path)
    else [path ++ ": unknown option"]
}

fn check_rule(rule: any, path) => if (not (rule is map)) [path ++ ": expected a rule map"]
    else if (rule.match == null) [path ++ ".match: a rule needs a match"]
    else check(rule, RULE_SCHEMA, path)
fn check_rules(value: any, path) => if (not (value is array)) [path ++ ": expected a list of rules"]
    else [for (i, rule in value) for (p in check_rule(rule, path ++ "[" ++ string(i) ++ "]")) p]

// every problem of `value` against `schema`, as "path: message" lines
pub fn check(value: any, schema: any, path) {
    if (schema is map) {
        if (not (value is map)) [path ++ ": expected a map"]
        else [for (k, v at value) for (p in (if (schema[k] == null) [path ++ "." ++ string(k) ++ ": unknown option"]
            else check(v, schema[k], path ++ "." ++ string(k)))) p]
    } else check_leaf(schema, value, path)
}

// the merged configuration and its problems; an empty list means usable
pub fn resolve(given: any) {
    let merged = merge(DEFAULTS, given or {});
    {config: merged, problems: check(merged, SCHEMA, "config")}
}
