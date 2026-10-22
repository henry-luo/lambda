// The derived renderings (AR3: fn). From a stored capture — the raw source,
// the manifest and the final URL — come the static snapshot page, the
// readable article as a page and as Markdown, and the search text. The
// worker process (AR-O12) runs them for add and refresh alike.
import readability: lambda.doc.readability
import article: lambda.doc.readability.article
import page: .page
import rewrite: .rewrite
import readable: .readable
import markdown: .markdown
import text: .text

// the parsed capture: the document, its <html>, the base URL and the manifest's lookup table
pub fn capture(source, manifest, final_url) map^ {
    let doc = lambda.sys.parse(source, 'html')^
    let html = article.html_of(doc);
    {doc: doc, html: html, base: page.base_of(html, final_url), table: rewrite.lookup(manifest), final_url: final_url}
}

// the article when the page is probably readerable, as the Reader View demo decides
pub fn article_of(c) => if (readability.readerable(c.doc) ^ { false })
    (readability.parse(c.doc, {url: c.final_url}) ^ { null }) else null

fn field(m, key) => if (m == null) null else m[key]
// the record's descriptive fields: the article's when there is one, the page's title otherwise
pub fn meta_of(c, art, url: any, archived: any, fallback_title: any) => {title: field(art, 'title') or page.title_of(c.html) or fallback_title,
    byline: field(art, 'byline'), site: field(art, 'site_name'), published: field(art, 'published_time'),
    excerpt: field(art, 'excerpt'), url: url, final_url: c.final_url, archived: archived}

pub fn snapshot_html(c) => rewrite.snapshot(c.html, c.base, c.table)
pub fn readable_html(c, art, meta) => readable.page(art, c.table, c.base, meta)
pub fn markdown_text(c, art, meta) string^ => markdown.article(art, c.table, c.base, meta)^
pub fn search_text(c, art, meta) => text.page_text(meta, art, c.html)

// ---- the renderings of a media, gallery, image or file record (AR4v2) ----
// The record is media/info.json; its files are paths under media/, beside
// snapshot.zip, so the pages refer to them relatively.

let STYLE = "body{margin:0;background:#11161c;color:#e8edf2;font:16px/1.55 system-ui,-apple-system,sans-serif}" ++
    "main{max-width:60rem;margin:0 auto;padding:2rem 1.25rem 4rem}h1{font-size:1.8rem;line-height:1.2;margin:0 0 .4rem}" ++
    ".byline{color:#9aa7b4;font-size:.9rem;margin:0 0 1.5rem}.byline a{color:inherit}video,audio{width:100%;background:#000;" ++
    "border-radius:6px}.grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(14rem,1fr));gap:1rem}" ++
    "figure{margin:0}figure img{width:100%;height:auto;border-radius:4px}figcaption{font-size:.85rem;color:#9aa7b4;margin-top:.3rem}" ++
    ".transcript p{margin:.2rem 0}.card{border:1px solid #2a3440;border-radius:8px;padding:1.25rem}a{color:#7cc4ff}" ++
    ".remote li{word-break:break-all}"

fn esc(s) => readable.esc(s)
fn parts(info) => [for (x in [info.author, info.site, info.published] where x != null and trim(string(x)) != "") esc(x)]
fn byline_html(info) => (let ps = parts(info), "<p class=\"byline\">" ++ join(ps, " · ") ++ (if (len(ps) > 0) " · " else "") ++
    "<a href=\"" ++ esc(info.url) ++ "\">Original</a></p>")
fn kb(bytes) => if (bytes == null) "" else if (bytes >= 1048576) string(round(bytes / 104857.6) / 10) ++ " MB"
    else string(round(bytes / 102.4) / 10) ++ " KB"
fn track_html(s) => "<track kind=\"subtitles\" srclang=\"" ++ esc(s.lang) ++ "\" label=\"" ++ esc(s.label or s.lang) ++
    "\" src=\"" ++ esc(s.path) ++ "\">"
fn player_html(info) {
    let video = [for (f in info.files where f.role == "video") f]
    let audio = [for (f in info.files where f.role == "audio") f]
    let poster = if (info.poster != null) " poster=\"" ++ esc(info.poster) ++ "\"" else ""
    let tracks = join([for (s in info.subtitles) track_html(s)], "");
    // split renditions are two players: the snapshot has no script to keep them in step
    (if (len(video) > 0) "<video controls preload=\"metadata\"" ++ poster ++ " src=\"" ++ esc(video[0].path) ++ "\">" ++ tracks ++ "</video>" else "") ++
    (if (len(audio) > 0) "<audio controls preload=\"metadata\" src=\"" ++ esc(audio[0].path) ++ "\"></audio>" else "")
}
fn transcript_html(transcript) => if (transcript == null or transcript == "") ""
    else "<section class=\"transcript\"><h2>Transcript</h2>" ++ join([for (l in split(transcript, "\n")) "<p>" ++ esc(l) ++ "</p>"], "") ++ "</section>"
fn remote_html(info) => if (len(info.remote or []) == 0) ""
    else "<section class=\"remote\"><h2>Not downloaded</h2><ul>" ++ join([for (x in info.remote)
        "<li><a href=\"" ++ esc(x.url) ++ "\">" ++ esc(x.url) ++ "</a> — " ++ esc(x.reason) ++ "</li>"], "") ++ "</ul></section>"
fn gallery_html(info) => "<div class=\"grid\">" ++ join([for (f in info.files)
    "<figure><a href=\"" ++ esc(f.path) ++ "\"><img src=\"" ++ esc(f.thumb or f.path) ++ "\" alt=\"" ++ esc(f.caption or "") ++
    "\" width=\"" ++ string(f.width) ++ "\" height=\"" ++ string(f.height) ++ "\"></a>" ++
    (if (f.caption != null) "<figcaption>" ++ esc(f.caption) ++ "</figcaption>" else "") ++ "</figure>"], "") ++ "</div>"
fn file_html(info) => (let f = info.files[0], if (info.kind == "image")
    "<figure><img src=\"" ++ esc(f.path) ++ "\" alt=\"" ++ esc(info.title) ++ "\" style=\"max-width:100%;height:auto\">" ++
    "<figcaption>" ++ (if (f.width != null) string(f.width) ++ "×" ++ string(f.height) ++ " · " else "") ++ kb(f.bytes) ++ "</figcaption></figure>"
    else "<div class=\"card\"><p><a href=\"" ++ esc(f.path) ++ "\">" ++ esc(f.path) ++ "</a></p><p>" ++ esc(f.content_type or "file") ++
    " · " ++ kb(f.bytes) ++ "</p></div>")

// the page a record shows as index.html
pub fn info_page(info, transcript) {
    let body = if (info.kind == "video" or info.kind == "audio") player_html(info) ++
            (if (info.description != null) "<p>" ++ esc(info.description) ++ "</p>" else "") ++ transcript_html(transcript)
        else if (info.kind == "gallery") (if (info.description != null) "<p>" ++ esc(info.description) ++ "</p>" else "") ++ gallery_html(info)
        else file_html(info);
    "<!DOCTYPE html>\n<html><head><meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">" ++
    "<title>" ++ esc(info.title) ++ "</title><style>" ++ STYLE ++ "</style></head><body><main><h1>" ++ esc(info.title) ++ "</h1>" ++
    byline_html(info) ++ body ++ remote_html(info) ++ "</main></body></html>"
}
// what a media record reads as an article: its description and transcript (null when it has neither)
pub fn info_article(info, transcript) {
    let has_text = (info.description != null and trim(info.description) != "") or (transcript != null and transcript != "")
    if (not (info.kind in ["video", "audio"]) or not has_text) null
    else {html: (if (info.description != null) "<p>" ++ esc(info.description) ++ "</p>" else "") ++
            (if (transcript != null and transcript != "") "<h2>Transcript</h2>" ++ join([for (l in split(transcript, "\n")) "<p>" ++ esc(l) ++ "</p>"], "") else ""),
        text: join([for (t in [info.description, transcript] where t != null and t != "") t], "\n\n"), lang: null, dir: null}
}
pub fn info_meta(info, archived) => {title: info.title, byline: info.author, site: info.site, published: info.published,
    excerpt: info.description, url: info.url, final_url: info.page_url or info.url, archived: archived}
// text.txt: the record's fields, then the transcript or the gallery's captions
pub fn info_text(info, transcript, meta) {
    let captions = join([for (f in info.files where f.caption != null) f.caption], "\n")
    let body = if (transcript != null and transcript != "") transcript else captions;
    text.page_text(meta, {text: body}, null)
}
// the readable page and Markdown of a media record's article, as for a page
pub fn info_readable(art, meta) => readable.page(art, {}, meta.final_url, meta)
pub fn info_markdown(art, meta) string^ => markdown.article(art, {}, meta.final_url, meta)^
