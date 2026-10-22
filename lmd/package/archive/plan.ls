// Classification and planning (AR3): pure functions over the URL and the
// configuration, no IO. A Plan names the kind, the engine, its settings and
// the outputs to derive; the response's content type may still refine the
// kind (AR2v2: rules → platforms → extension → content type).
import config: .config
import platforms: .platforms

let DEFAULT_PORTS = {http: "80", https: "443"}
// the module that acquires each kind (AR4v2: all Lambda)
pub let ENGINE_OF = {page: "page", image: "file", file: "file", video: "media", audio: "media", gallery: "gallery"}

fn cut(s, sep) {
    let i = index_of(s, sep);
    if (i == null) [s, null] else [slice(s, 0, i), slice(s, i + len(sep))]
}
fn last_segment(path) {
    let segs = split(path, "/");
    segs[len(segs) - 1]
}

// the parts of an absolute URL, or null when `url` is not one
pub fn parse(url) {
    if (not (url is string) or index_of(url, "://") == null) null
    else {
        let at = index_of(url, "://")
        let scheme = lower(slice(url, 0, at))
        let no_frag = cut(slice(url, at + 3), "#")
        let no_query = cut(no_frag[0], "?")
        let slash = index_of(no_query[0], "/")
        let authority = if (slash == null) no_query[0] else slice(no_query[0], 0, slash)
        let path = if (slash == null) "/" else slice(no_query[0], slash)
        let user_at = last_index_of(authority, "@")
        let hostport = if (user_at == null) authority else slice(authority, user_at + 1)
        let colon = last_index_of(hostport, ":")
        let host = lower(if (colon == null) hostport else slice(hostport, 0, colon))
        let port = if (colon == null) null else slice(hostport, colon + 1);
        {scheme: scheme, host: host, port: port, path: path, query: no_query[1], fragment: no_frag[1], href: url}
    }
}

// the duplicate-detection form (§7): lowercase scheme and host, default port
// and fragment dropped, tracking parameters removed
pub fn normalize(url, settings) {
    let u = parse(url)
    if (u == null) url
    else {
        let tracking = settings.tracking_params or []
        let port = if (u.port == null or u.port == DEFAULT_PORTS[u.scheme]) "" else ":" ++ u.port
        let params = if (u.query == null) []
            else [for (p in split(u.query, "&") where p != "" and not (lower(cut(p, "=")[0]) in tracking)) p]
        let query = if (len(params) == 0) "" else "?" ++ join(params, "&");
        u.scheme ++ "://" ++ u.host ++ port ++ u.path ++ query
    }
}

// `*` matches any run of characters within one label or segment
pub fn wild(pattern, text) bool => if (pattern == "") text == ""
    else if (starts_with(pattern, "*")) (wild(slice(pattern, 1), text) or (text != "" and wild(pattern, slice(text, 1))))
    else (text != "" and slice(pattern, 0, 1) == slice(text, 0, 1) and wild(slice(pattern, 1), slice(text, 1)))
fn segments_match(pattern, text, sep) {
    let ps = split(pattern, sep)
    let ts = split(text, sep);
    len(ps) == len(ts) and all([for (i, p in ps) wild(p, ts[i])])
}
// a host glob: a leading `*.` also matches the bare domain
pub fn host_glob(pattern, host) bool {
    let p = lower(pattern)
    if (p == "*") true
    else if (starts_with(p, "*.")) (host == slice(p, 2) or ends_with(host, slice(p, 1)) or segments_match(p, host, "."))
    else segments_match(p, host, ".")
}
// a path glob: without a `/` it matches the last segment
pub fn path_glob(pattern, path) bool => if (contains(pattern, "/")) segments_match(pattern, path, "/")
    else wild(pattern, last_segment(path))

fn field_ok(spec, value, matcher) => spec == null or
    (value != null and any([for (g in (if (spec is array) spec else [spec])) matcher(g, value)]))
// the first rule whose match holds on every field it names (§6, first match wins)
pub fn rule_for(u, rules) {
    let hits = [for (r in (rules or []) where r.match != null and field_ok(r.match.host, u.host, host_glob) and
        field_ok(r.match.scheme, u.scheme, wild) and field_ok(r.match.path, u.path, path_glob) and
        field_ok(r.match.query, u.query, wild)) r];
    if (len(hits) == 0) null else hits[0]
}

pub fn extension(path) {
    let seg = last_segment(path)
    let dot = last_index_of(seg, ".");
    if (dot == null or dot == len(seg) - 1) null else lower(slice(seg, dot + 1))
}
pub fn kind_of_extension(ext) => if (ext == null) null else if (ext in platforms.VIDEO_EXT) "video"
    else if (ext in platforms.AUDIO_EXT) "audio" else if (ext in platforms.IMAGE_EXT) "image"
    else if (ext in platforms.FILE_EXT) "file" else null
pub fn kind_of_host(host) {
    let hits = [for (row in platforms.TABLE where any([for (h in row.hosts) host_glob(h, host)])) row.kind];
    if (len(hits) == 0) null else hits[0]
}
pub fn kind_of_content_type(ct) => if (ct == null) null else (let t = lower(trim(cut(ct, ";")[0])),
    if (t == "text/html" or t == "application/xhtml+xml") "page" else if (starts_with(t, "image/")) "image"
    else if (contains(t, "mpegurl")) "video"
    else if (starts_with(t, "video/")) "video" else if (starts_with(t, "audio/")) "audio"
    else if (starts_with(t, "text/")) "page" else "file")

// the kind the URL alone decides, or null when the response must (AR2v2)
pub fn classify(u, rule) => if (rule != null and rule.kind != null) string(rule.kind)
    else (kind_of_host(u.host) or kind_of_extension(extension(u.path)))

pub fn plan(url, cfg, options = null) map^ {
    let u = parse(url)
    // an expression-form guard: a statement `if` here would make the body a list
    let absolute = if (u == null or not (u.scheme in ["http", "https", "file"]))
        raise error("archive: not an absolute http, https or file URL: " ++ string(url)) else true
    let o = options or {}
    let rule = rule_for(u, cfg.rules)
    let settings = config.settings(cfg, rule, o)
    let kind = if (o.kind != null) string(o.kind) else classify(u, rule)
    let engine = if (kind == null) null else ENGINE_OF[kind]
    // the media and gallery settings: the rule's over the configuration's
    let media = config.merge(cfg.media, if (rule != null) rule.media else null)
    let gallery = config.merge(cfg.gallery, if (rule != null) rule.gallery else null);
    {url: url, normalized: normalize(url, settings), parts: u, kind: kind, engine: engine, media: media,
     gallery: gallery, settings: settings, formats: [for (f in settings.formats) string(f)], rule: rule, tag: o.tag}
}

// the kind decided by the response when the URL alone could not
pub fn refine(p, content_type) => if (p.kind != null) p
    else (let kind = kind_of_content_type(content_type) or "page", {*: p, kind: kind, engine: ENGINE_OF[kind]})
