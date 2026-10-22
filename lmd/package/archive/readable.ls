// The readable rendering: lambda.doc.readability's article as a small page
// whose images point at the same assets/ members as the snapshot (AR5v2).
import n: lambda.doc.readability.node
import rewrite: .rewrite

let STYLE = "body{margin:0;background:#f6f1e7;color:#1d1a16;font:18px/1.6 Georgia,'Times New Roman',serif}" ++
    "article{max-width:42rem;margin:0 auto;padding:3rem 1.25rem 5rem}h1{font-size:2rem;line-height:1.2;margin:0 0 .5rem}" ++
    ".byline{color:#6b6257;font:14px/1.5 system-ui,sans-serif;margin:0 0 2rem}.byline a{color:inherit}" ++
    "img,video{max-width:100%;height:auto}pre{overflow:auto;background:#eee6d8;padding:1rem}" ++
    "blockquote{margin:1rem 0;padding-left:1rem;border-left:3px solid #c9bda8;color:#4a433b}" ++
    "figure{margin:1.5rem 0}figcaption{font-size:.9rem;color:#6b6257}"

pub fn esc(s) => replace(replace(replace(replace(string(s or ""), "&", "&amp;"), "<", "&lt;"), ">", "&gt;"), "\"", "&quot;")

// the article's markup with its resources pointing at the snapshot's members
pub fn body_html(art, table, base) {
    let parsed = lambda.sys.parse("<body>" ++ art.html ++ "</body>", 'html') ^ { null }
    if (parsed == null) art.html
    else (let body = n.first(n.of(parsed), ["body"]),
        if (body == null) art.html else rewrite.fragment(body.kids, base, table, "assets/"))
}

pub fn page(art, table, base, meta) {
    let parts = [for (x in [meta.byline, meta.site, meta.published] where x != null and trim(string(x)) != "") esc(x)]
    let lang = if (art.lang != null) " lang=\"" ++ esc(art.lang) ++ "\"" else ""
    let dir = if (art.dir != null) " dir=\"" ++ esc(art.dir) ++ "\"" else "";
    "<!DOCTYPE html>\n<html" ++ lang ++ dir ++ "><head><meta charset=\"utf-8\">" ++
    "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\"><title>" ++ esc(meta.title) ++ "</title>" ++
    "<link rel=\"canonical\" href=\"" ++ esc(meta.url) ++ "\"><style>" ++ STYLE ++ "</style></head><body><article>" ++
    "<h1>" ++ esc(meta.title) ++ "</h1><p class=\"byline\">" ++ join(parts, " · ") ++ (if (len(parts) > 0) " · " else "") ++
    "<a href=\"" ++ esc(meta.url) ++ "\">Original</a></p>" ++ body_html(art, table, base) ++ "</article></body></html>"
}
