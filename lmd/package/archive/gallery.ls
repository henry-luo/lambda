// The gallery engine (AR4v2): a page's images — <img> (its widest srcset
// candidate), <picture> sources, <a href> to an image file (the full size
// behind a thumbnail, preferred over it), og:image — downloaded once each, a
// few at a time, and kept when at least gallery.min_pixels; captions come from
// <figcaption>, alt, title and the link's text. Each kept image gets a
// thumbnail through load/save.
import n: lambda.doc.readability.node
import article: lambda.doc.readability.article
import planning: .plan
import platforms: .platforms
import rewrite: .rewrite
import transfer: .transfer
import page: .page
import media: .media
import radiant

let MB = 1024 * 1024
let THUMB = {width: 320, height: 320}

fn is_image(url) => (let u = planning.parse(url), u != null and planning.extension(u.path) in platforms.IMAGE_EXT)
fn text_or_null(v) => if (v is string and trim(v) != "") trim(v) else null
fn first_of(xs) => if (len(xs) == 0) null else xs[0]

// the widest srcset candidate (by width, then density), else the fallback
fn widest(srcset, fallback) {
    let cs = n.srcset_candidates(srcset)
    let by_w = sort([for (c in cs where c.w != null) c], {dir: 'desc', by: (c) => c.w})
    let by_x = sort([for (c in cs where c.x != null) c], {dir: 'desc', by: (c) => c.x});
    if (len(by_w) > 0) by_w[0].url else if (len(by_x) > 0) by_x[0].url else (fallback or first_of([for (c in cs) c.url]))
}

// the caption of an image: its figure's figcaption, its alt, its title, the link's title or text
fn caption_of(img, figure, link) => first_of([for (v in [
    if (figure == null) null else (let c = n.first(figure, ["figcaption"]), if (c == null) null else n.inner_text(c)),
    n.attr(img, "alt"), n.attr(img, "title"), if (link == null) null else n.attr(link, "title"),
    if (link == null) null else n.inner_text(link)], let t = text_or_null(v) where t != null) t])

// one element's images; `figure` and `link` are its nearest enclosing ones
fn images_in(e, base, figure, link) {
    let fig = if (e.tag == "figure") e else figure
    let a = if (e.tag == "a" and n.attr(e, "href") != null and is_image(rewrite.resolve(base, n.attr(e, "href")))) e else link
    if (e.tag == "img") {
        let src = widest(n.attr(e, "srcset"), n.attr(e, "src"))
        // a link to an image file is the full size behind this thumbnail
        let url = if (a != null) n.attr(a, "href") else src;
        if (url == null) [] else [{url: rewrite.resolve(base, url), caption: caption_of(e, fig, a),
            thumb_of: if (a != null and src != null) rewrite.resolve(base, src) else null}]
    }
    else if (e.tag == "picture") (let sources = [for (k in e.kids where n.is_el(k) and k.tag == "source") widest(n.attr(k, "srcset"), null)],
        let img = n.first(e, ["img"]), let best = first_of([for (s in sources where s != null) s]),
        if (best == null) [for (k in e.kids where n.is_el(k)) for (x in images_in(k, base, fig, a)) x]
        else [{url: rewrite.resolve(base, best), caption: if (img == null) null else caption_of(img, fig, a), thumb_of: null}])
    else [for (k in e.kids where n.is_el(k)) for (x in images_in(k, base, fig, a)) x]
}

// the page's image candidates in document order, og:image last, each URL once
pub fn candidates(html, base) {
    let body = n.first(html, ["body"]) or html
    let og = [for (m in n.find_all(html, ["meta"]) where lower(n.attr(m, "property") or "") == "og:image" and n.attr(m, "content") != null)
        {url: rewrite.resolve(base, n.attr(m, "content")), caption: null, thumb_of: null}]
    let all = [*images_in(body, base, null, null), *og];
    [for (i, c in all where not any([for (j, d in all where j < i) d.url == c.url])) c]
}

pn say(notify, message) { if (notify != null) notify(message) }
pn fetch_image(url, settings, max_bytes) map^ { transfer.fetch(url, settings, {max_bytes: max_bytes})^ }

// a thumbnail of an image beside it, or null when it cannot be decoded
pn thumbnail(bytes, path) {
    let img = load({data: bytes, thumbnail: THUMB}) ^ { null }
    if (img == null) { return null }
    var failure = null
    save(img, path) ^ { failure = ^ }
    if (failure is error) null else path
}

// Acquires the gallery behind the job's first fetch (`r`, written to
// `fetched`): `{info, source}`, or `{fallback: true, source, reason}` when no
// image qualifies, in which case the job archives the page as a page.
pub pn acquire(p, staging, settings, notify, r, fetched) map^ {
    let cfg = p.gallery
    let source = input(fetched, 'text')^
    io.delete(fetched)^
    let root = lambda.sys.parse(source, 'html')^
    let html = article.html_of(root)
    let base = page.base_of(html, r.url)
    let about = media.describe(html, base, media.ld_media(root))
    let found = take(candidates(html, base), int(cfg.max_images))
    io.mkdir(staging ++ "/media")^
    io.mkdir(staging ++ "/media/thumbs")^
    let per_batch = max(1, int(settings.page.assets_per_host))
    var kept = []
    var skipped = []
    var taken = []
    var done = 0
    var pos = 0
    while (pos < len(found)) {
        let batch = take(drop(found, pos), per_batch)
        let handles = [for (c in batch) start(fetch_image, [c.url, settings, int(cfg.max_image_mb * MB)])]
        let results = [for (h in handles) wait(h)^]
        for (i, c in batch) {
            let res = results[i]
            let body = if (res.ok and res.response.ok) res.response.body else null
            let size = if (body is binary) radiant.image_metrics(body) else null
            if (not res.ok) {
                skipped = [*skipped, {url: c.url, reason: res.error.message}]
            } else if (not res.response.ok) {
                skipped = [*skipped, {url: c.url, reason: "HTTP " ++ string(res.response.status)}]
            } else if (size == null) {
                skipped = [*skipped, {url: c.url, reason: "not a raster image"}]
            } else if (size.width * size.height < cfg.min_pixels) {
                skipped = [*skipped, {url: c.url, reason: "too small (" ++ string(size.width) ++ "×" ++ string(size.height) ++ ")"}]
            } else {
                let ctype = page.media_type(res.response.headers.'content-type')
                let name = page.unique_name(page.file_name(c.url, ctype), taken, 1)
                output(body, staging ++ "/media/" ++ name)^
                let thumb = thumbnail(body, staging ++ "/media/thumbs/" ++ name ++ ".png")
                taken = [*taken, name]
                kept = [*kept, {path: "media/" ++ name, role: "image", source: res.response.url, width: size.width,
                    height: size.height, bytes: res.response.bytes, content_type: ctype, caption: c.caption,
                    thumb: if (thumb == null) null else "media/thumbs/" ++ name ++ ".png"}]
            }
            done = done + 1
        }
        say(notify, {type: "progress", tag: p.tag, stage: "media", received: done, total: len(found), rate: null})
        pos = pos + per_batch
        if (pos < len(found) and starts_with(p.url, "http") and settings.per_host_delay_ms > 0) sleep(int(settings.per_host_delay_ms))^
    }
    if (len(kept) == 0) {
        if (exists(staging ++ "/media")) io.delete(staging ++ "/media")^
        return {fallback: true, source: source, remote: [], reason: "no image of at least " ++ string(cfg.min_pixels) ++ " pixels"}
    }
    {info: {kind: "gallery", title: about.title, description: about.description, author: about.author, site: about.site,
        published: about.published, url: p.url, page_url: r.url, poster: kept[0].thumb, files: kept, subtitles: [],
        skipped: skipped, remote: []}, source: source}
}
