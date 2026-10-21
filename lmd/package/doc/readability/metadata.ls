// Article metadata: the document title, <meta> values and JSON-LD
// (Readability.js _getArticleTitle, _getArticleMetadata, _getJSONLD).
import n: .node
import .text

type sep = \("|" | "-" | "–" | "—" | "\\" | "/" | ">" | "»")
type sep_gap = \(ws sep ws)
type hierarchy_gap = \(ws ("\\" | "/" | ">" | "»") ws)
// property is a space-separated list; name is a single value
type property_name = \(ws* ("article" | "dc" | "dcterm" | "og" | "twitter") ws* ":" ws*
    ("author" | "creator" | "description" | "published_time" | "title" | "site_name") ws*)
type name_pattern = \(ws* (("dc" | "dcterm" | "og" | "twitter" | "parsely" | "weibo:" ("article" | "webpage"))
    ws* ("-" | "." | ":") ws*)? ("author" | "creator" | "pub-date" | "description" | "title" | "site_name") ws*)
type char_ref = \("&#" (("x" | "X") ("0" to "9" | "a" to "f" | "A" to "F")+ | "\d"+) ";")

let ARTICLE_TYPES = ["AdvertiserContentArticle","NewsArticle","AnalysisNewsArticle","AskPublicNewsArticle",
    "BackgroundNewsArticle","OpinionNewsArticle","ReportageNewsArticle","ReviewNewsArticle","Report",
    "SatiricalArticle","ScholarlyArticle","MedicalScholarlyArticle","SocialMediaPosting","BlogPosting",
    "LiveBlogPosting","DiscussionForumPosting","TechArticle"]
let SCHEMA_ORG = ["http://schema.org","http://schema.org/","https://schema.org","https://schema.org/"]
let ENTITIES = {quot: "\"", amp: "&", apos: "'", lt: "<", gt: ">"}

fn word_count(s) => len(split(s, \(ws+)))

// document.title: the first <title>, ASCII white space stripped and collapsed
pub fn doc_title(html) {
    let found = n.first(html, ["title"]);
    if (found == null) "" else n.trim_text(trim(replace(found.text, \("\s"+), " ")))
}

pub fn title(html) {
    let original = doc_title(html)
    let separators = find(original, sep_gap)
    let hierarchical = len(separators) > 0 and len(find(original, hierarchy_gap, {limit: 1})) > 0
    let picked = if (len(separators) > 0) {
        let before_last = slice(original, 0, separators[len(separators) - 1].index);
        // too short: remove the first part instead
        if (word_count(before_last) < 3) slice(original, find(original, sep, {limit: 1})[0].index + 1)
        else before_last
    } else if (contains(original, ": ")) {
        let whole = n.trim_text(original)
        let heading = any([for (h in n.find_all(html, ["h1","h2"])) n.trim_text(h.text) == whole]);
        if (heading) original else {
            let after_last = slice(original, last_index_of(original, ":") + 1)
            let colon = index_of(original, ":");
            if (word_count(after_last) < 3) slice(original, colon + 1)
            else if (word_count(slice(original, 0, colon)) > 5) original
            else after_last
        }
    } else if (n.js_len(original) > 150 or n.js_len(original) < 15) {
        let h1s = n.find_all(html, ["h1"]);
        if (len(h1s) == 1) n.inner_text(h1s[0]) else original
    } else original
    let cleaned = n.squash(n.trim_text(picked))
    let words = word_count(cleaned);
    // few words left and no hierarchical separator, or more than one word dropped
    if (words <= 4 and (not hierarchical or words != word_count(replace(original, sep_gap, "")) - 1)) original
    else cleaned
}

// ---- <meta> ----
fn meta_entry(m) {
    let value_text = n.attr(m, "content")
    let property = n.attr(m, "property")
    let meta_name = n.attr(m, "name")
    let matches = if (property) find(property, property_name, {ignore_case: true, limit: 1}) else [];
    if (not value_text) null
    else if (len(matches) > 0) [replace(lower(matches[0].value), \(ws), ""), n.trim_text(value_text)]
    else if (meta_name and lower(meta_name) is name_pattern)
        [replace(replace(lower(meta_name), \(ws), ""), ".", ":"), n.trim_text(value_text)]
    else null
}
// a later <meta> overwrites an earlier one of the same name
fn value(entries, key) {
    let hits = [for (e in entries where e[0] == key) e[1]];
    if (len(hits) == 0) null else hits[len(hits) - 1]
}
fn pick(entries, keys) {
    let found = [for (k in keys, let v = value(entries, k) where v) v];
    if (len(found) == 0) null else found[0]
}

// new URL(s) succeeds: a scheme, and a host after it for the special schemes
fn is_url(s) {
    let t = trim(s)
    let colon = index_of(t, ":");
    if (colon == null or not (slice(t, 0, colon) is \("\a" ("\a" | "\d" | "+" | "-" | ".")*))) false
    else {
        let scheme = lower(slice(t, 0, colon));
        if (scheme in ["http","https","ws","wss","ftp"]) len(replace(slice(t, colon + 1), \(("/" | "\\")+), "")) > 0
        else true
    }
}

pub fn article_metadata(html, ld, title_fn) {
    let entries = [for (m in n.find_all(html, ["meta"]), let e = meta_entry(m) where e != null) e]
    let author = value(entries, "article:author")
    let byline = ld.byline or pick(entries, ["dc:creator","dcterm:creator","author","parsely-author"]) or
        (if (author is string and not is_url(author)) author else null)
    let found_title = ld.title or pick(entries, ["dc:title","dcterm:title","og:title","weibo:article:title",
        "weibo:webpage:title","title","twitter:title","parsely-title"]);
    {title: unescape(found_title or title_fn()),
     byline: unescape(byline),
     excerpt: unescape(ld.excerpt or pick(entries, ["dc:description","dcterm:description","og:description",
        "weibo:article:description","weibo:webpage:description","description","twitter:description"])),
     site_name: unescape(ld.site_name or value(entries, "og:site_name")),
     published_time: unescape(ld.published or pick(entries, ["article:published_time","parsely-pub-date"]))}
}

// _unescapeHtmlEntities: the five named references, then numeric ones
pub fn unescape(s) => if (not s) s else n.replace_with(
    n.replace_with(s, \("&" ("quot" | "amp" | "apos" | "lt" | "gt") ";"), (m) => ENTITIES[slice(m, 1, len(m) - 1)]),
    char_ref, (m) => chr(code_point(m)) ^ { "\uFFFD" })
fn code_point(ref) {
    let hex = ref[2] == "x" or ref[2] == "X"
    let raw = slice(ref, if (hex) 3 else 2, len(ref) - 1)
    let significant = find(raw, \(!"0"), {limit: 1})
    let digits = if (len(significant) == 0) "" else slice(raw, significant[0].index)
    let value = if (len(digits) > 8) -1 else reduce([0, *[for (c in split(digits, "")) digit(c)]],
        (acc, d) => acc * (if (hex) 16 else 10) + d);
    // the replacements a conforming HTML parser makes
    if (value == 0 or value < 0 or value > 0x10FFFF or (value >= 0xD800 and value <= 0xDFFF)) 0xFFFD else value
}
fn digit(c) => if (c >= "0" and c <= "9") ord(c) - 48 else if (c >= "a" and c <= "f") ord(c) - 87 else ord(c) - 55

// ---- JSON-LD ----
fn article_type(t: string) bool => starts_with(t, "Article") or ends_with(t, "APIReference") or
    any([for (a in ARTICLE_TYPES) contains(t, a)])
// String.match on a non-string @type throws, which skips the whole script
fn type_test(t) {
    if (t is string) t != "" and article_type(t)
    else if (falsy(t)) false else 'throw'
}
fn first_typed(items, i) {
    if (i >= len(items)) null
    else {
        let it = items[i]
        let r = if (it is map) type_test(it["@type"]) else if (it == null) 'throw' else false;
        if (r == 'throw') 'throw' else if (r) it else first_typed(items, i + 1)
    }
}
fn schema_context(ctx) => (ctx is string and ctx in SCHEMA_ORG) or
    (ctx is map and ctx["@vocab"] is string and ctx["@vocab"] in SCHEMA_ORG)
fn ld_scripts(el) => [for (c in content(el) where c is element)
    for (d in [c, *ld_scripts(c)] where name(d) == 'script' and d.type == "application/ld+json") d]
// the HTML reader already parsed well-formed JSON; text left over may hold CDATA markers
fn script_data(script) {
    let body = content(script);
    if (len(body) == 0) null
    else if (body[0] is string) lambda.sys.parse(strip_cdata(n.trim_text(body[0])), 'json') ^ { null }
    else body[0]
}
fn strip_cdata(s) {
    let opened = if (starts_with(s, "<![CDATA[")) slice(s, 9) else s;
    if (ends_with(opened, "]]>")) slice(opened, 0, len(opened) - 3) else opened
}
fn falsy(v) => v == null or v == false or v == "" or v == 0
// the metadata of one script, or null when the script does not describe an article
fn ld_entry(data, title_fn) {
    let listed = if (data is array) first_typed(data, 0) else data
    let root = if (listed is map and schema_context(listed["@context"])) listed else null
    let graph = if (root != null and falsy(root["@type"]) and root["@graph"] is array) first_typed(root["@graph"], 0) else root
    let typed = if (graph is map) type_test(graph["@type"]) else false;
    if (typed != true) null else ld_fields(graph, title_fn)
}
fn trimmed(s) => if (s is string) n.trim_text(s) else null
fn ld_fields(p, title_fn) {
    let both = p.name is string and p.headline is string and p.name != p.headline
    let page_title = if (both) title_fn() else null
    let author = p.author
    let named_authors = if (author is array and len(author) > 0 and author[0] is map and author[0].name is string)
        join([for (a in author where a is map and a.name is string) n.trim_text(a.name)], ", ") else null;
    {title: if (both) (if (n.similarity(p.headline, page_title) > 0.75 and not (n.similarity(p.name, page_title) > 0.75))
            p.headline else p.name)
        else if (p.name is string) n.trim_text(p.name) else trimmed(p.headline),
     byline: if (author is map and author.name is string) n.trim_text(author.name) else named_authors,
     excerpt: trimmed(p.description),
     site_name: if (p.publisher is map) trimmed(p.publisher.name) else null,
     published: trimmed(p.datePublished)}
}
// the first application/ld+json script that describes an article
pub fn json_ld(root, title_fn) {
    let found = [for (s in ld_scripts(root), let e = ld_entry(script_data(s), title_fn) where e != null) e];
    if (len(found) == 0) {} else found[0]
}
