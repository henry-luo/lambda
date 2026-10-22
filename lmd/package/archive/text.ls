// text.txt (AR8): what io.text_search reads — title, byline, site and
// description first, then the body text.
import n: lambda.doc.readability.node

fn line(v) => if (v == null) null else (let s = trim(string(v)), if (s == "") null else s)

pub fn page_text(meta, art, html) {
    let head = [for (v in [meta.title, meta.byline, meta.site, meta.excerpt], let l = line(v) where l != null) l]
    let body = if (art != null) art.text
        else (let b = n.first(html, ["body"]), n.strip_tags(if (b == null) html else b, ["script", "style", "noscript"]).text);
    join([*head, "", body], "\n")
}
