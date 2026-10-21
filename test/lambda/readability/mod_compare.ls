// Readability.js harness semantics (test/test-readability.js) for a
// test-pages directory: <page>/source.html, expected.html and
// expected-metadata.json. Shared by _corpus.ls and pages.ls.
import rd: lambda.doc.readability
import article: lambda.doc.readability.article
import n: lambda.doc.readability.node

let URL = "http://fakehost/test/page.html"

// htmlTransform: collapse white space as HTML does; expected.html is
// pretty-printed, which the harness evens out by printing both sides
fn collapse(s) => n.trim_text(replace(s, \("\s"+), " ") ^ { s })
fn desc(e) {
    let id = n.id_of(e)
    let cls = n.class_name(e);
    e.tag ++ (if (id != "") "#" ++ id else "") ++ (if (cls != "") ".(" ++ cls ++ ")" else "")
}
// the in-order node sequence that the harness walks
fn flat(e) => [for (k in e.kids where not (k is string and n.is_blank(k)))
    for (x in if (k is string) [{text: collapse(k)}]
        else if (k.tag == "#comment") [{comment: k.data}]
        else [{desc: desc(k), node: k}, *flat(k)]) x]
fn same_attrs(a, b) => len(a.attrs) == len(b.attrs) and all([for (p in a.attrs) n.attr(b, p[0]) == p[1]])
// where two texts part ways, with some context
fn text_gap(a, b) {
    let at = len([for (i in 0 to min(len(a), len(b)) - 1 where slice(a, 0, i + 1) == slice(b, 0, i + 1)) i])
    let from = max(0, at - 30);
    "…" ++ slice(a, from, at + 40) ++ " | vs | …" ++ slice(b, from, at + 40)
}
fn label(x) => if (x == null) "(no node)" else if (x.text != null) "#text(" ++ slice(x.text, 0, 60) ++ ")"
    else if (x.comment != null) "#comment" else x.desc
// the first difference between two node sequences, or null
fn first_diff(a, b, i) {
    let x = if (i < len(a)) a[i] else null
    let y = if (i < len(b)) b[i] else null;
    if (x == null and y == null) null
    else if (x == null or y == null or label(x) != label(y)) "node " ++ string(i) ++ ": " ++ label(x) ++ " vs " ++ label(y)
    else if (x.text != null and x.text != y.text) "text " ++ string(i) ++ ": " ++ text_gap(x.text, y.text)
    else if (x.node != null and not same_attrs(x.node, y.node)) "attrs of " ++ x.desc ++ ": " ++ format(x.node.attrs) ++
        " vs " ++ format(y.node.attrs)
    else first_diff(a, b, i + 1)
}
// the jsdom run removes comment nodes, leaving their neighbours separate text nodes
fn uncommented(e) => n.with_kids(e, [for (k in e.kids where not (k is map and k.tag == "#comment"))
    if (n.is_el(k)) uncommented(k) else k])
fn nodes_of(html) {
    let doc = parse(html, 'html') ^ { null };
    if (doc == null) [] else flat(n.of(doc))
}
fn field_diffs(res, expected) => [for (f in [["title", "title"], ["byline", "byline"], ["excerpt", "excerpt"],
    ["site_name", "siteName"], ["dir", "dir"], ["lang", "lang"], ["published_time", "publishedTime"]]
    where (f[0] in ["title","byline","excerpt","site_name"] or expected[f[1]]) and res[f[0]] != expected[f[1]])
    f[1] ++ ": " ++ format(res[f[0]]) ++ " vs " ++ format(expected[f[1]])]

// one page of a test-pages directory: {page, ok, content, problems}
pub fn check(dir, page) map^ {
    let base = dir ++ "/" ++ page ++ "/"
    let source = trim(input(base ++ "source.html", 'text')^)
    let root = parse(source, 'html')^
    let expected_html = trim(input(base ++ "expected.html", 'text')^)
    let expected = input(base ++ "expected-metadata.json", 'json')^
    let res = article.extract(root, uncommented(article.html_of(root)), {url: URL, classes_to_preserve: ["caption"]})^
    let readerable = rd.readerable(source)^
    let content = if (res == null) "content: no article" else first_diff(nodes_of(res.html), nodes_of(expected_html), 0)
    let fields = if (res == null) [] else field_diffs(res, expected)
    let reader = if (readerable == expected.readerable) [] else ["readerable: " ++ string(readerable)]
    let problems = [*(if (content == null) [] else [content]), *fields, *reader];
    {page: page, ok: len(problems) == 0, content: content == null, problems: problems}
}
