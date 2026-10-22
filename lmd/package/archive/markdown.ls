// The Markdown rendering: front matter from the record, then the article
// through Lambda's Markdown writer, images pointing at assets/ (AR10).
import readable: .readable

fn yaml(v) {
    let s = string(v);
    if (contains(s, ":") or contains(s, "\"") or contains(s, "#") or starts_with(s, " ")) "\"" ++ replace(s, "\"", "\\\"") ++ "\"" else s
}

pub fn article(art, table, base, meta) string^ {
    let fields = [["title", meta.title], ["byline", meta.byline], ["site", meta.site], ["published", meta.published],
        ["url", meta.url], ["archived", meta.archived]]
    let front = join([for (kv in fields where kv[1] != null and trim(string(kv[1])) != "") kv[0] ++ ": " ++ yaml(kv[1])], "\n")
    let html = readable.body_html(art, table, base)
    let doc = lambda.sys.parse("<html><body>" ++ html ++ "</body></html>", 'html')^
    "---\n" ++ front ++ "\n---\n\n# " ++ string(meta.title) ++ "\n\n" ++ format(doc, 'markdown')
}
