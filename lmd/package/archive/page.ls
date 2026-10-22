// The page engine (AR1, AR10): fetch the page, collect its own resources —
// stylesheets, images, icons, fonts, never links — download each once into
// the staging folder's assets/, and record every one in the manifest.
import n: lambda.doc.readability.node
import article: lambda.doc.readability.article
import rewrite: .rewrite
import transfer: .transfer

let CSS_DEPTH = 3
// the file extension a content type implies, for names whose URL has none (pages, media, files)
pub let EXT_OF_TYPE = {'text/css': "css", 'image/png': "png", 'image/jpeg': "jpg", 'image/gif': "gif", 'image/webp': "webp",
    'image/svg+xml': "svg", 'image/x-icon': "ico", 'image/vnd.microsoft.icon': "ico", 'image/avif': "avif",
    'image/bmp': "bmp", 'font/woff2': "woff2", 'font/woff': "woff", 'font/ttf': "ttf", 'font/otf': "otf",
    'application/font-woff': "woff", 'application/font-woff2': "woff2", 'application/x-font-ttf': "ttf",
    'video/mp4': "mp4", 'video/webm': "webm", 'video/quicktime': "mov", 'video/x-matroska': "mkv", 'video/ogg': "ogv",
    'audio/mpeg': "mp3", 'audio/mp4': "m4a", 'audio/aac': "aac", 'audio/ogg': "ogg", 'audio/wav': "wav",
    'audio/flac': "flac", 'audio/webm': "weba", 'application/pdf': "pdf", 'application/zip': "zip",
    'application/epub+zip': "epub", 'text/plain': "txt", 'application/json': "json"}
let FONT_EXT = ["woff", "woff2", "ttf", "otf", "eot"]

pub fn media_type(ct) => if (ct == null) null else lower(trim(split(ct, ";")[0]))
fn words(s) => [for (w in split(lower(s or ""), \("\s"+)) where w != "") w]

fn link_kind(e) {
    let rel = words(n.attr(e, "rel"))
    let as = lower(n.attr(e, "as") or "")
    if (n.attr(e, "href") == null) null
    else if ("stylesheet" in rel) "stylesheet"
    else if (any([for (r in rel) r in ["icon", "apple-touch-icon", "apple-touch-icon-precomposed", "mask-icon"]])) "icon"
    else if ("preload" in rel and as in ["image", "style", "font"])
        (if (as == "style") "stylesheet" else if (as == "font") "font" else "image")
    else null
}
fn refs_of(e) {
    let tag = e.tag
    let own = if (tag == "link") (let k = link_kind(e), if (k == null) [] else [{ref: n.attr(e, "href"), kind: k}])
        else if (tag == "img") [*(if (n.attr(e, "src") == null) [] else [{ref: n.attr(e, "src"), kind: "image"}]),
            *[for (u in n.srcset_urls(n.attr(e, "srcset"))) {ref: u, kind: "image"}]]
        else if (tag == "source") [for (u in n.srcset_urls(n.attr(e, "srcset"))) {ref: u, kind: "image"}]
        else if (tag == "video" and n.attr(e, "poster") != null) [{ref: n.attr(e, "poster"), kind: "image"}]
        else if (tag == "style") [for (u in rewrite.css_refs(e.text)) {ref: u, kind: "image"}]
        else []
    let inline = [for (u in rewrite.css_refs(n.attr(e, "style"))) {ref: u, kind: "image"}];
    [*own, *inline]
}
// every resource reference of the document, as written, in document order
pub fn refs(html) => [for (e in n.all_elements(html)) for (r in refs_of(e)) r]

// document.baseURI: the first <base href> resolved against the final URL
pub fn base_of(html, url) {
    let bases = [for (b in n.find_all(html, ["base"]), let href = n.attr(b, "href") where href != null) href];
    if (len(bases) == 0) url else rewrite.resolve(url, bases[0])
}
pub fn title_of(html) {
    let t = n.first(html, ["title"]);
    if (t == null) null else (let s = trim(t.text), if (s == "") null else s)
}

fn strip(s) => if (starts_with(s, "-") or starts_with(s, ".")) strip(slice(s, 1))
    else if (ends_with(s, "-") or ends_with(s, ".")) strip(slice(s, 0, len(s) - 1)) else s
fn slug_name(s) {
    let t = strip(replace(lower(s), \((!(("a" to "z") | ("0" to "9") | "." | "_" | "-"))+), "-"));
    if (t == "") "asset" else slice(t, 0, 80)
}
fn extension(name) {
    let dot = last_index_of(name, ".");
    if (dot == null or dot == len(name) - 1) null else slice(name, dot + 1)
}
// the extension of a URL's last segment, else the one its content type implies
pub fn file_ext(url, content_type) => (let tail = last_segment_of(url), extension(slug_name(tail)) or EXT_OF_TYPE[content_type])
fn last_segment_of(url) => (let path = split(split(url, "?")[0], "#")[0], let segs = split(path, "/"), segs[len(segs) - 1])
// a member name from the URL's last segment, with an extension from the content type when it has none
pub fn file_name(url, content_type) {
    let tail = last_segment_of(url)
    let base = slug_name(if (tail == "") "index" else tail)
    let ext = EXT_OF_TYPE[content_type];
    if (extension(base) != null or ext == null) base else base ++ "." ++ ext
}
pub fn unique_name(name, taken, i) {
    let candidate = if (i == 1) name else (let dot = last_index_of(name, "."),
        if (dot == null) name ++ "-" ++ string(i) else slice(name, 0, dot) ++ "-" ++ string(i) ++ slice(name, dot));
    if (candidate in taken) unique_name(name, taken, i + 1) else candidate
}
fn css_kind(ref) {
    let ext = extension(split(split(ref, "?")[0], "#")[0]);
    if (ext == "css") "stylesheet" else if (ext in FONT_EXT) "font" else "image"
}

pn say(notify, message) { if (notify != null) notify(message) }
pn fetch_asset(url, settings, max_bytes) map^ { transfer.fetch(url, settings, {max_bytes: max_bytes})^ }

// Acquire a page into `staging` (AR3's pn step) from the job's first fetch:
// its response `r` and the HTML `source` read from it. The result holds the
// parsed document, the raw source, and the manifest of what was downloaded.
pub pn acquire(p, staging, settings, notify, r, source) map^ {
    let root = lambda.sys.parse(source, 'html')^
    let html = article.html_of(root)
    let base = base_of(html, r.url)
    let limits = settings.page
    let max_asset = int(limits.max_asset_mb * 1024 * 1024)
    let max_total = int(limits.max_total_mb * 1024 * 1024)
    let per_batch = max(1, int(limits.assets_per_host))
    let remote = starts_with(p.url, "http")
    io.mkdir(staging ++ "/assets")^
    // the queue holds absolute URLs, each once; stylesheets add theirs as they arrive
    var queue = []
    var seen = {}
    for (ref in refs(html)) {
        if (not rewrite.skipped(ref.ref)) {
            let abs = rewrite.resolve(base, ref.ref)
            if (seen[abs] == null) {
                seen = {*: seen, [abs]: true}
                queue = [*queue, {url: abs, kind: ref.kind, depth: 0}]
            }
        }
    }
    var entries = []
    var taken = []
    var total = 0
    var count = 0
    var skipped = []
    while (len(queue) > 0) {
        let batch = take(queue, per_batch)
        queue = drop(queue, per_batch)
        say(notify, {type: "progress", tag: p.tag, stage: "assets", received: count, total: count + len(batch) + len(queue), rate: null})
        // a few downloads at a time, each a child task joined here
        let handles = [for (item in batch) start(fetch_asset, [item.url, settings, max_asset])]
        let results = [for (h in handles) wait(h)^]
        for (i, item in batch) {
            let res = results[i]
            if (count >= limits.max_assets) {
                skipped = [*skipped, {url: item.url, reason: "max_assets"}]
            } else if (not res.ok) {
                skipped = [*skipped, {url: item.url, reason: res.error.message}]
                entries = [*entries, {url: item.url, kind: item.kind, status: "failed", message: res.error.message}]
            } else if (not res.response.ok) {
                skipped = [*skipped, {url: item.url, reason: "HTTP " ++ string(res.response.status)}]
                entries = [*entries, {url: item.url, kind: item.kind, status: "failed", message: "HTTP " ++ string(res.response.status)}]
            } else if (total + res.response.bytes > max_total) {
                skipped = [*skipped, {url: item.url, reason: "max_total_mb"}]
            } else {
                let response = res.response
                let ctype = media_type(response.headers.'content-type')
                let name = unique_name(file_name(item.url, ctype), taken, 1)
                output(response.body or "", staging ++ "/assets/" ++ name)^
                taken = [*taken, name]
                total = total + response.bytes
                count = count + 1
                entries = [*entries, {url: item.url, final_url: response.url, path: name, kind: item.kind,
                    content_type: response.headers.'content-type', bytes: response.bytes, etag: response.headers.etag,
                    last_modified: response.headers.'last-modified', integrity: null, status: "ok"}]
                if (item.kind == "stylesheet" and item.depth < CSS_DEPTH and response.body is string) {
                    for (ref in rewrite.css_refs(response.body)) {
                        if (not rewrite.skipped(ref)) {
                            let abs = rewrite.resolve(response.url, ref)
                            if (seen[abs] == null) {
                                seen = {*: seen, [abs]: true}
                                queue = [*queue, {url: abs, kind: css_kind(ref), depth: item.depth + 1}]
                            }
                        }
                    }
                }
            }
        }
        if (len(queue) > 0 and remote and settings.per_host_delay_ms > 0) sleep(int(settings.per_host_delay_ms))^
    }
    // stylesheets are rewritten in place once every member has its name
    let table = rewrite.lookup(entries)
    for (e in entries) {
        if (e.status == "ok" and e.kind == "stylesheet") {
            let text = input(staging ++ "/assets/" ++ e.path, 'text')^
            output(rewrite.css(text, e.final_url or e.url, table, ""), staging ++ "/assets/" ++ e.path)^
        }
    }
    say(notify, {type: "progress", tag: p.tag, stage: "assets", received: count, total: count, rate: null});
    {url: p.url, final_url: r.url, status: r.status, content_type: r.headers.'content-type', source: source,
     doc: root, html: html, base: base, manifest: entries, skipped: skipped, assets_bytes: total, assets_count: count}
}
