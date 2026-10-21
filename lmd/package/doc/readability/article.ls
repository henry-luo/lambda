// The Readability.parse() pipeline over one document (Readability.js parse()):
// noscript images, JSON-LD, script removal, document prep, metadata, the
// article grab and post-processing. The facade owns source handling.
import n: .node
import meta: .metadata
import prep: .prep
import grab: .grab
import clean: .clean

// the <html> record of a document, a bare element wrapped as body content
pub fn html_of(root) {
    let rec = n.of(root)
    let found = if (rec.tag == "html") rec else n.first(rec, ["html"]);
    if (found != null) found else n.mk("html", [], [n.mk("head", [], []), n.mk("body", [], [rec])])
}
fn lambda_child(el, tag) {
    let found = if (el is element) [for (c in content(el) where c is element and name(c) == tag) c] else [];
    if (len(found) == 0) null else found[0]
}
fn is_absolute(url) => url is \("\a" ("\a" | "\d" | "+" | "-" | ".")* ":" ...)
// document.baseURI: the first <base href>, resolved against the document URL
fn base_uri(html, url) {
    let bases = [for (b in n.find_all(html, ["base"]), let href = n.attr(b, "href") where href != null) href];
    if (len(bases) == 0) url
    else if (url != null) (let r = url_resolve(url, bases[0]), if (r is string) r else url)
    else if (is_absolute(bases[0])) bases[0] else null
}
fn settings(o, html) => {
    url: o.url, base: base_uri(html, o.url),
    char_threshold: if (o.char_threshold != null) o.char_threshold else 500,
    top_candidates: if (o.top_candidates != null) o.top_candidates else 5,
    max_elems: if (o.max_elems != null) o.max_elems else 0,
    classes_to_preserve: o.classes_to_preserve or [],
    keep_classes: o.keep_classes == true,
    json_ld: o.disable_json_ld != true,
    link_density_modifier: if (o.link_density_modifier != null) o.link_density_modifier else 0,
    video: o.allowed_video}

// the article of a parsed Lambda document `root` whose <html> record is `html`, or null
pub fn extract(root, html, options) map?^ {
    let cx = settings(options or {}, html)
    let elems = len(n.all_elements(html))
    let sized = if (cx.max_elems > 0 and elems > cx.max_elems)
        raise error("readability: aborting parsing document; " ++ string(elems) ++ " elements found") else true
    let unwrapped = prep.unwrap_noscript_images(html)
    // JSON-LD is read from the parsed document, before scripts are removed
    let ld = if (cx.json_ld) meta.json_ld(root, () => meta.title(unwrapped)) else {}
    let prepped = prep.prep_document(n.strip_tags(unwrapped, ["script","noscript"]))
    let info = meta.article_metadata(prepped, ld, () => meta.title(prepped))
    let found = grab.grab(prepped, {*: cx, title: info.title, meta_byline: info.byline, byline: null});
    if (found == null) null else {
        let article = clean.post_process(found.article, cx)
        let markup = n.to_html(article.kids)
        let paragraphs = n.find_all(article, ["p"])
        let parsed = lambda.sys.parse("<body>" ++ markup ++ "</body>", 'html') ^ { null }
        let body = lambda_child(lambda_child(parsed, 'html'), 'body')
        let page = if (body == null) null else [for (c in content(body) where c is element) c];
        {title: info.title, byline: info.byline or found.byline, dir: found.dir, lang: found.lang,
         content: if (page == null or len(page) == 0) null else page[0], html: markup,
         text: article.text, length: n.js_len(article.text),
         excerpt: info.excerpt or (if (len(paragraphs) > 0) n.trim_text(paragraphs[0].text) else null),
         site_name: info.site_name, published_time: info.published_time}
    }
}
