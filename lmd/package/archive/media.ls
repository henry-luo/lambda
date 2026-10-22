// The media engine (AR4v2): the medium a URL or a page exposes without a
// site-specific extractor — a direct file; a page's <video>/<audio> and their
// <source>s, og:video/og:audio, <link rel=enclosure>, a JSON-LD VideoObject or
// AudioObject — stored as the server sent it. A progressive file streams to
// disk; an HLS stream's segments are appended in order (fMP4 after its init
// map, or MPEG-TS), byte ranges and discontinuities honoured. WebVTT tracks and
// subtitle renditions become the transcript. Nothing is muxed or transcoded.
import n: lambda.doc.readability.node
import article: lambda.doc.readability.article
import planning: .plan
import platforms: .platforms
import rewrite: .rewrite
import transfer: .transfer
import page: .page
import store: .store
import hls: .hls
import vtt: .vtt

let HLS_TYPES = ["application/vnd.apple.mpegurl", "application/x-mpegurl", "audio/mpegurl", "audio/x-mpegurl"]
// what Radiant's <video> plays through the system player on macOS, preferred among a page's sources
let PLAYABLE = ["video/mp4", "video/quicktime", "audio/mp4", "audio/mpeg", "audio/aac"]
let MB = 1024 * 1024

fn ext_of_url(url) => (let u = planning.parse(url), if (u == null) null else planning.extension(u.path))
pub fn is_hls(url, declared) => (declared != null and page.media_type(declared) in HLS_TYPES) or ext_of_url(url) == "m3u8"
// "video", "audio" or null: by the declared type, then the extension
pub fn media_of(url, declared) {
    let t = page.media_type(declared)
    let ext = ext_of_url(url);
    if (t != null and starts_with(t, "video/")) "video" else if (t != null and starts_with(t, "audio/")) "audio"
    else if (ext in platforms.VIDEO_EXT) "video" else if (ext in platforms.AUDIO_EXT) "audio" else null
}
fn words(s) => [for (w in split(lower(s or ""), \("\s"+)) where w != "") w]
fn text_or_null(v) => if (v is string and trim(v) != "") trim(v) else null

// ---- discovery (fn) ----

fn metas(html) => [for (m in n.find_all(html, ["meta"]))
    {key: lower(n.attr(m, "property") or n.attr(m, "name") or n.attr(m, "itemprop") or ""), value: n.attr(m, "content")}]
fn meta_of(ms, keys) {
    let hits = [for (k in keys) for (m in ms where m.key == k and text_or_null(m.value) != null) trim(m.value)];
    if (len(hits) == 0) null else hits[0]
}
fn ld_flat(v) => if (v is array) [for (x in v) for (y in ld_flat(x)) y]
    else if (v is map) [v, *(if (v.'@graph' != null) ld_flat(v.'@graph') else [])] else []
fn ld_types(o) => (let t = o.'@type', if (t is array) [for (x in t) string(x)] else if (t == null) [] else [string(t)])
// the JSON-LD blocks of the parsed document, whose JSON the HTML reader has
// already parsed (Readability's node records keep only a script's text)
fn ld_blocks(el) => if (not (el is element)) []
    else if (string(name(el)) == "script" and lower(string(el.'type' or "")) == "application/ld+json")
        [for (c in el where c is map or c is array) c]
    else [for (c in el) for (b in ld_blocks(c)) b]
// the document's JSON-LD VideoObject and AudioObject records
pub fn ld_media(root) => [for (b in ld_blocks(root)) for (o in ld_flat(b)
    where any([for (t in ld_types(o)) t in ["VideoObject", "AudioObject"]])) o]

// one reference: a stream is video unless it says audio; a file is media by its
// type or extension, or, for a <video>/<audio> element's own src, by the element
fn candidate(url, declared, base, from, hint, owner) => (let abs = rewrite.resolve(base, url), let stream = is_hls(abs, declared),
    {url: abs, type: page.media_type(declared), from: from, stream: stream,
     media: if (stream) (if (hint == "audio") "audio" else "video") else (media_of(abs, declared) or owner)})
fn element_candidates(e, base) => [
    *(if (n.attr(e, "src") == null) [] else [candidate(n.attr(e, "src"), null, base, e.tag, e.tag, e.tag)]),
    *[for (k in e.kids where n.is_el(k) and k.tag == "source" and n.attr(k, "src") != null)
        candidate(n.attr(k, "src"), n.attr(k, "type"), base, e.tag, e.tag, e.tag)]]
let OG = [["og:video:secure_url", "og:video:type", "video"], ["og:video:url", "og:video:type", "video"],
    ["og:video", "og:video:type", "video"], ["og:audio:secure_url", "og:audio:type", "audio"],
    ["og:audio:url", "og:audio:type", "audio"], ["og:audio", "og:audio:type", "audio"]]

fn first_of(xs) => if (len(xs) == 0) null else xs[0]
fn unique_urls(cs) => [for (i, c in cs where not any([for (j, d in cs where j < i) d.url == c.url])) c]
fn usable(c) => not starts_with(c.url, "blob:") and not starts_with(c.url, "data:") and (c.stream or c.media != null)

// every media reference of the page, in the order of preference (AR4v2), each once
pub fn references(html, base, ld) {
    let ms = metas(html)
    let own = [for (e in n.find_all(html, ["video", "audio"])) for (c in element_candidates(e, base)) c]
    let og = [for (row in OG, let v = meta_of(ms, [row[0]]) where v != null) candidate(v, meta_of(ms, [row[1]]), base, "og", row[2], null)]
    let enclosures = [for (l in n.find_all(html, ["link"]) where "enclosure" in words(n.attr(l, "rel")) and n.attr(l, "href") != null)
        candidate(n.attr(l, "href"), n.attr(l, "type"), base, "enclosure", null, null)]
    let lds = [for (o in ld) for (u in [o.contentUrl, o.embedUrl] where u is string)
        candidate(u, o.encodingFormat, base, "json-ld", if ("AudioObject" in ld_types(o)) "audio" else "video", null)];
    unique_urls([*own, *og, *enclosures, *lds])
}
// the downloadable ones: a stream or a file with a media type or extension, those the system plays first
pub fn candidates(html, base, ld) {
    let ok = [for (c in references(html, base, ld) where usable(c)) c];
    [*[for (c in ok where c.stream or c.type in PLAYABLE) c], *[for (c in ok where not (c.stream or c.type in PLAYABLE)) c]]
}

// the <track> subtitles and captions
pub fn tracks(html, base) => [for (e in n.find_all(html, ["track"])
    where lower(n.attr(e, "kind") or "subtitles") in ["subtitles", "captions"] and n.attr(e, "src") != null)
    {url: rewrite.resolve(base, n.attr(e, "src")), lang: lower(n.attr(e, "srclang") or "und"), label: n.attr(e, "label")}]

// the record's fields from the page: title, description, author, date, site, poster
pub fn describe(html, base, ld) {
    let ms = metas(html)
    let o = first_of(ld) or {}
    let posters = [for (e in n.find_all(html, ["video"]) where n.attr(e, "poster") != null) n.attr(e, "poster")]
    let poster = first_of([for (v in [first_of(posters), meta_of(ms, ["og:image", "twitter:image"]), text_or_null(o.thumbnailUrl)]
        where v != null) v]);
    {title: meta_of(ms, ["og:title", "twitter:title"]) or text_or_null(o.name) or page.title_of(html),
     description: meta_of(ms, ["og:description", "description", "twitter:description"]) or text_or_null(o.description),
     author: meta_of(ms, ["author", "article:author"]), site: meta_of(ms, ["og:site_name"]),
     published: meta_of(ms, ["article:published_time", "og:video:release_date"]) or text_or_null(o.uploadDate),
     poster: if (poster == null) null else rewrite.resolve(base, poster)}
}

pub fn lang_wanted(lang, wanted) => any([for (w in wanted) lower(lang) == lower(w) or starts_with(lower(lang), lower(w) ++ "-")])

// ---- download (pn) ----

pn say(notify, message) { if (notify != null) notify(message) }

// a playlist or a track: its text, or why there is none
pn text_at(url, settings) map^ {
    let r = transfer.fetch(url, settings, {})^
    if (not r.ok) { return {ok: false, message: r.error.message} }
    if (not r.response.ok) { return {ok: false, message: "HTTP " ++ string(r.response.status) ++ " for " ++ url} }
    if (not (r.response.body is string)) { return {ok: false, message: url ++ " is not text"} }
    {ok: true, text: r.response.body, url: r.response.url}
}

// one segment (or init map), within its byte range, retried a few times; run as a child task
pn fetch_part(url, span, settings, max_bytes, retries) map^ {
    let headers = if (span == null) null
        else {Range: "bytes=" ++ string(span.start) ++ "-" ++ string(span.start + span.length - 1)}
    var outcome = null
    var attempt = 0
    while (outcome == null) {
        let r = transfer.fetch(url, settings, {headers: headers, max_bytes: max_bytes})^
        let good = r.ok and r.response.ok and r.response.body != null
        if (good) {
            let body = r.response.body
            // a server that ignored the Range header sent the whole resource
            let part = if (span != null and r.response.status == 200) slice(body, span.start, span.start + span.length) else body
            outcome = {ok: true, body: part}
        } else if (attempt >= retries) {
            outcome = {ok: false, message: if (not r.ok) r.error.message
                else if (not r.response.ok) "HTTP " ++ string(r.response.status) ++ " for " ++ url else "empty segment " ++ url}
        }
        attempt = attempt + 1
    }
    outcome
}

// Appends a media playlist's segments to `dest`, a few fetched at a time and
// written in order; each init map is written before the first segment it
// applies to. Progress counts segments (AR12v2).
pn append_segments(m, dest, settings, cfg, notify, tag) map^ {
    if (exists(dest)) io.delete(dest)^
    let segs = m.segments
    let per_batch = max(1, int(settings.page.assets_per_host))
    let max_part = int(cfg.max_segment_mb * MB)
    let max_total = int(cfg.max_mb * MB)
    let retries = int(cfg.segment_retries)
    var written_map = null
    var done = 0
    var bytes = 0
    var failure = null
    var pos = 0
    while (pos < len(segs) and failure == null) {
        let batch = take(drop(segs, pos), per_batch)
        let handles = [for (s in batch) start(fetch_part, [s.uri, s.range, settings, max_part, retries])]
        let results = [for (h in handles) wait(h)^]
        for (i, s in batch) {
            if (failure == null and s.map != null and s.map != written_map) {
                let init = fetch_part(s.map.uri, s.map.range, settings, max_part, retries)^
                if (init.ok) {
                    output(init.body, dest, {mode: 'append'})^
                    bytes = bytes + len(init.body)
                    written_map = s.map
                } else {
                    failure = "init segment: " ++ init.message
                }
            }
            if (failure == null) {
                let r = results[i]
                if (not r.ok) {
                    failure = r.message
                } else {
                    output(r.body, dest, {mode: 'append'})^
                    bytes = bytes + len(r.body)
                    done = done + 1
                    if (bytes > max_total) { failure = "over media.max_mb (" ++ string(cfg.max_mb) ++ " MB)" }
                }
            }
        }
        say(notify, {type: "progress", tag: tag, stage: "media", received: done, total: len(segs), bytes: bytes, rate: null})
        pos = pos + per_batch
    }
    {ok: failure == null, message: failure, segments: done, bytes: bytes}
}

// The stream to take from a master playlist: the first ranked variant whose
// segments are in an allowed container, muxed, or with its audio rendition in
// fMP4 alongside fMP4 video (two files); otherwise the next variant (AR4v2).
pn choose(master, url, settings, cfg) map^ {
    let rs = hls.renditions(master, url)
    for (v in hls.ranked(hls.variants(master, url), cfg.max_height)) {
        let pl = text_at(v.uri, settings)^
        if (pl.ok) {
            let video = hls.media(pl.text, pl.url)
            if (video.container in cfg.prefer) {
                let group = [for (r in rs where r.type == "AUDIO" and v.audio != null and r.group == v.audio and r.uri != null) r]
                if (len(group) == 0) { return {variant: v, video: video, audio: null, renditions: rs} }
                let chosen = first_of([*[for (r in group where r.default) r], *group])
                let apl = text_at(chosen.uri, settings)^
                if (apl.ok and video.container == "fmp4") {
                    let audio = hls.media(apl.text, apl.url)
                    if (audio.container == "fmp4") { return {variant: v, video: video, audio: audio, audio_rendition: chosen, renditions: rs} }
                }
            }
        }
    }
    {variant: null, renditions: rs}
}

// the subtitle tracks wanted by media.subtitles, written beside the medium
pn subtitles(slug, refs, renditions, staging, settings, cfg) array^ {
    let from_streams = [for (r in renditions where r.type == "SUBTITLES" and r.uri != null and lang_wanted(r.language or "und", cfg.subtitles))
        {url: r.uri, lang: lower(r.language), label: r.name, playlist: true}]
    let from_tracks = [for (t in refs where lang_wanted(t.lang, cfg.subtitles)) {*: t, playlist: false}]
    var out = []
    var langs = []
    for (t in [*from_tracks, *from_streams]) {
        if (not (t.lang in langs)) {
            // a subtitle rendition is a playlist of WebVTT segments, joined under one header
            let body = if (not t.playlist) text_at(t.url, settings)^
                else (let pl = text_at(t.url, settings)^, if (not pl.ok) pl
                    else (let parts = [for (s in hls.media(pl.text, pl.url).segments) text_at(s.uri, settings)^],
                        if (not all([for (x in parts) x.ok])) {ok: false, message: "subtitle segment failed"}
                        else {ok: true, text: join([for (i, x in parts) if (i == 0) x.text
                            else replace(x.text, \("WEBVTT" (!"\n")* "\n"), "")], "\n")}))
            if (body.ok) {
                let path = "media/" ++ slug ++ "." ++ t.lang ++ ".vtt"
                output(body.text, staging ++ "/" ++ path)^
                out = [*out, {lang: t.lang, label: t.label, path: path, source: t.url}]
                langs = [*langs, t.lang]
            }
        }
    }
    out
}

// a poster image beside the medium, or null
pn poster(url, slug, staging, settings) string?^ {
    if (url == null) { return null }
    let r = transfer.fetch(url, settings, {max_bytes: 20 * MB})^
    if (not (r.ok and r.response.ok and r.response.body != null)) { return null }
    let ext = page.file_ext(url, page.media_type(r.response.headers.'content-type')) or "img"
    let path = "media/" ++ slug ++ ".poster." ++ ext
    output(r.response.body, staging ++ "/" ++ path)^
    path
}

fn file_of(path, role, container, bytes, source, stream) => {path: path, role: role, container: container, bytes: bytes,
    source: source, stream: stream}
fn stream_info(url, variant, m) => {type: "hls", playlist: url, segments: len(m.segments), duration: m.duration,
    variant: if (variant == null) null else {width: variant.width, height: variant.height, bandwidth: variant.bandwidth, codecs: variant.codecs}}

// An HLS stream downloaded into media/: one file, or video and audio files
// for split fMP4 renditions. `{ok: false, message}` when there is none to take.
pn take_stream(url, playlist, slug, kind, staging, settings, cfg, notify, tag) map^ {
    let master = hls.is_master(playlist)
    let pick = if (master) choose(playlist, url, settings, cfg)^
        else {variant: null, video: hls.media(playlist, url), audio: null, renditions: []}
    if (pick.variant == null and master) { return {ok: false, message: "no variant in " ++ string(cfg.prefer) ++ " under " ++ string(cfg.max_height) ++ "p", renditions: pick.renditions} }
    if (pick.video.encrypted or (pick.audio != null and pick.audio.encrypted)) {
        return {ok: false, message: "encrypted stream (AES-128): needs AR-E9", renditions: pick.renditions}
    }
    if (len(pick.video.segments) == 0) { return {ok: false, message: "empty playlist", renditions: pick.renditions} }
    let ext = if (pick.video.container == "fmp4") (if (kind == "audio") "m4a" else "mp4") else pick.video.container
    let split = pick.audio != null
    let video_path = "media/" ++ slug ++ (if (split) ".video." else ".") ++ ext
    let got = append_segments(pick.video, staging ++ "/" ++ video_path, settings, cfg, notify, tag)^
    if (not got.ok) { return {ok: false, message: got.message, renditions: pick.renditions} }
    let video_file = file_of(video_path, if (split) "video" else kind, pick.video.container, got.bytes, url, stream_info(url, pick.variant, pick.video))
    if (not split) { return {ok: true, files: [video_file], renditions: pick.renditions} }
    let audio_path = "media/" ++ slug ++ ".audio.m4a"
    let got_audio = append_segments(pick.audio, staging ++ "/" ++ audio_path, settings, cfg, notify, tag)^
    if (not got_audio.ok) { return {ok: false, message: "audio rendition: " ++ got_audio.message, renditions: pick.renditions} }
    {ok: true, renditions: pick.renditions, files: [video_file,
        file_of(audio_path, "audio", "fmp4", got_audio.bytes, pick.audio_rendition.uri, stream_info(pick.audio_rendition.uri, null, pick.audio))]}
}

fn base_name(url) => (let u = planning.parse(url), let segs = if (u == null) [url] else split(u.path, "/"),
    let tail = segs[len(segs) - 1], let dot = last_index_of(tail, "."), if (dot == null or dot == 0) tail else slice(tail, 0, dot))

// Acquires the medium behind the job's first fetch `r` (written to `fetched`):
// a stream, a file, or a page naming one. The result is `{info, source}` — the
// media record (media/info.json) and the page's HTML when there was a page —
// or `{fallback: true, source, remote, reason}` when a page names no medium
// that can be downloaded: the job then archives it as a page (AR-O14).
pub pn acquire(p, staging, settings, notify, r, fetched) map^ {
    let cfg = p.media
    let ct = r.headers.'content-type'
    io.mkdir(staging ++ "/media")^
    // a direct stream or file: the first fetch is the medium, or its playlist
    if (is_hls(r.url, ct) or media_of(r.url, ct) != null) {
        let kind = if (p.kind in ["video", "audio"]) p.kind else (media_of(r.url, ct) or "video")
        let title = base_name(r.url)
        let slug = store.slug(title)
        let meta = {title: title, description: null, author: null, site: null, published: null, poster: null}
        if (is_hls(r.url, ct)) {
            let got = take_stream(r.url, input(fetched, 'text')^, slug, kind, staging, settings, cfg, notify, p.tag)^
            if (not got.ok) raise error("archive: " ++ got.message ++ " (" ++ r.url ++ ")")
            let subs = subtitles(slug, [], got.renditions, staging, settings, cfg)^
            io.delete(fetched)^
            return {info: {kind: kind, *: meta, url: p.url, page_url: null, files: got.files, subtitles: subs, remote: []}, source: null}
        }
        let ext = planning.extension(planning.parse(r.url).path) or page.EXT_OF_TYPE[page.media_type(ct)] or "bin"
        let path = "media/" ++ slug ++ "." ++ ext
        io.move(fetched, staging ++ "/" ++ path)^
        return {info: {kind: kind, *: meta, url: p.url, page_url: null, subtitles: [], remote: [],
            files: [file_of(path, kind, ext, r.bytes, r.url, null)]}, source: null}
    }
    let source = input(fetched, 'text')^
    io.delete(fetched)^
    let root = lambda.sys.parse(source, 'html')^
    let html = article.html_of(root)
    let base = page.base_of(html, r.url)
    let ld = ld_media(root)
    let refs = references(html, base, ld)
    let found = candidates(html, base, ld)
    let about = describe(html, base, ld)
    let not_taken = [for (c in refs where not usable(c)) {url: c.url, reason: "not a media file or stream (" ++ c.from ++ ")"}]
    if (len(found) == 0) { return {fallback: true, source: source, remote: not_taken, reason: "no downloadable media"} }
    let best = found[0]
    let kind = if (p.kind in ["video", "audio"]) p.kind else best.media
    let slug = store.slug(about.title or base_name(best.url))
    var files = []
    var renditions = []
    var trouble = null
    if (best.stream) {
        let pl = text_at(best.url, settings)^
        if (not pl.ok) {
            trouble = pl.message
        } else {
            let got = take_stream(pl.url, pl.text, slug, kind, staging, settings, cfg, notify, p.tag)^
            renditions = got.renditions or []
            if (got.ok) { files = got.files } else { trouble = got.message }
        }
    } else {
        let ext = ext_of_url(best.url) or page.EXT_OF_TYPE[best.type] or "bin"
        let path = "media/" ++ slug ++ "." ++ ext
        let got = transfer.fetch(best.url, settings, {to: staging ++ "/" ++ path, progress: notify, stage: "media",
            tag: p.tag, max_bytes: int(cfg.max_mb * MB)})^
        if (not got.ok) { trouble = got.error.message }
        else if (not got.response.ok) { trouble = "HTTP " ++ string(got.response.status) ++ " for " ++ best.url }
        else { files = [file_of(path, kind, ext, got.response.bytes, got.response.url, null)] }
    }
    if (trouble != null) {
        return {fallback: true, source: source, remote: [{url: best.url, reason: trouble}, *not_taken], reason: trouble}
    }
    let subs = subtitles(slug, tracks(html, base), renditions, staging, settings, cfg)^
    let poster_path = poster(about.poster, slug, staging, settings)^;
    {info: {kind: kind, title: about.title, description: about.description, author: about.author, site: about.site,
        published: about.published, url: p.url, page_url: r.url, poster: poster_path, files: files, subtitles: subs,
        remote: [for (c in found where c.url != best.url) {url: c.url, reason: "alternative source"}]}, source: source}
}
