// lambda.mail.sanitize — an HTML body made safe to show (vibe/Lambda_Demo_Mail.md
// ML5v2, §5.8). The message frame already loads script-free (MQ11); this is
// the second layer: no script, no embedded documents or plugins, no forms
// that submit, no navigation from inside the frame, and no remote loads
// unless the reader allowed them. It runs on readability's node records,
// which serialize themselves (lambda.doc.readability.node).
import n: lambda.doc.readability.node

// subtrees that never render in a message
let DROPPED = ["script", "iframe", "frame", "frameset", "object", "embed", "applet", "base", "link", "meta",
    "noembed", "noframes", "portal", "title", "template", "webview"]
// kept for their content only: a form must not submit
let UNWRAPPED = ["form"]
// attributes that load what they name
let LOADS = ["src", "background", "poster", "lowsrc", "dynsrc", "data", "href", "xlink:href"]
let SRCSETS = ["srcset", "imagesrcset"]
// attributes that act rather than describe
let REFUSED = ["action", "formaction", "ping", "manifest", "srcdoc", "target", "http-equiv"]
let LINKS = ["a", "area"]
// a link the frame cannot follow still reads as one
let LINK_STYLE = "a[data-href]{color:#0b57d0;text-decoration:underline;cursor:pointer}"

let CSS_URL = \("url(" (!")")* ")")
let CSS_IMPORT = \("@import" (!";")* ";"?)

// "java\tscript:" is "javascript:" to a URL parser: drop controls and spaces
fn squeezed(url) => lower(join([for (ch in split(url, "") where ord(ch) > 32) ch], ""))

fn is_remote(url) {
    let s = squeezed(url);
    starts_with(s, "http:") or starts_with(s, "https:") or starts_with(s, "//") or starts_with(s, "ftp:")
}

// What a loading attribute may keep: a part of this message by cid: URL
// (RFC 2392), an inline image, or a network URL once remote content is
// allowed; null drops it. A relative URL would resolve against the window's
// own folder, never the sender's, so it goes too.
fn resource(url, cx) {
    let s = squeezed(url);
    if (starts_with(s, "cid:")) cx.cids[slice(trim(url), 4)]
    else if (starts_with(s, "data:image/")) url
    else if (is_remote(url) and cx.remote) url
    else null
}

fn descriptor(c) => if (c.w != null) " " ++ string(int(c.w)) ++ "w" else if (c.x != null) " " ++ string(c.x) ++ "x" else ""

fn srcset(value, cx) {
    let kept = [for (c in n.srcset_candidates(value), let r = resource(c.url, cx) where r != null) r ++ descriptor(c)];
    if (len(kept) == 0) null else join(kept, ", ")
}

// Links never navigate the frame. One to the outside moves to data-href and
// shows its target as the title until the window can open it (§5.8).
fn link(value, titled) {
    let s = squeezed(value);
    if (starts_with(s, "#")) [["href", value]]
    else if (starts_with(s, "http:") or starts_with(s, "https:") or starts_with(s, "mailto:"))
        [["data-href", value], *(if (titled) [] else [["title", value]])]
    else []
}

// each match replaced by f(match), letters in any case
fn replace_all(s, pattern, f) {
    let found = find(s, pattern, {ignore_case: true});
    if (len(found) == 0) s else {
        let ends = [0, *[for (m in found) m.index + len(m.value)]];
        join([*[for (i, m in found) slice(s, ends[i], m.index) ++ f(m.value)], slice(s, ends[len(found)])], "")
    }
}

fn css_url_value(token) {
    let inner = trim(slice(token, 4, len(token) - 1));
    if (len(inner) >= 2 and (starts_with(inner, "\"") or starts_with(inner, "'"))) slice(inner, 1, len(inner) - 1) else inner
}

// Style text without @import rules, and with each url() it may not load
// replaced by `none`, which leaves its declaration valid.
pub fn clean_css(text, cx) => replace_all(replace_all(text, CSS_IMPORT, (m) => ""), CSS_URL, (m) => {
    let r = resource(css_url_value(m), cx);
    if (r == null) "none" else "url(\"" ++ replace(r, "\"", "%22") ++ "\")"
})

fn clean_attr(tag, a, titled, cx) {
    let key = lower(a[0]);
    if (starts_with(key, "on") or key in REFUSED) []
    else if (key == "style") [["style", clean_css(a[1], cx)]]
    else if (key in ["href", "xlink:href"] and tag in LINKS) link(a[1], titled)
    else if (key in SRCSETS) (let kept = srcset(a[1], cx), if (kept == null) [] else [[a[0], kept]])
    else if (key in LOADS) (let kept = resource(a[1], cx), if (kept == null) [] else [[a[0], kept]])
    else [a]
}

fn clean_attrs(e, cx) {
    let titled = n.attr(e, "title") != null;
    [for (a in e.attrs, c in clean_attr(e.tag, a, titled, cx)) c]
}

// <noscript> holds raw text while scripting is on, as the parser assumed;
// the frame renders it, so it is parsed and cleaned like the rest
fn noscript_kids(e) {
    let raw = join([for (k in e.kids where k is string) k], "")
    let doc = parse(raw, 'html') ^ { null };
    let parts = if (doc == null) [] else [for (top in n.elems(n.of(doc)), part in n.elems(top)) part];
    [for (part in parts, k in part.kids) k]
}

fn clean_node(k, cx) {
    if (k is string) [k]
    // comments carry Outlook's conditional markup ([if mso]), nothing to show
    else if (k.tag == "#comment" or k.tag in DROPPED) []
    else if (k.tag == "noscript") clean_kids(noscript_kids(k), cx)
    else if (k.tag in UNWRAPPED) clean_kids(k.kids, cx)
    else if (k.tag == "style") [n.mk("style", [], [clean_css(join([for (c in k.kids where c is string) c], ""), cx)])]
    else [n.mk(k.tag, clean_attrs(k, cx), clean_kids(k.kids, cx))]
}

fn clean_kids(kids, cx) => [for (k in kids, c in clean_node(k, cx)) c]

fn css_remote(text) => len([for (m in find(text, CSS_URL, {ignore_case: true}) where is_remote(css_url_value(m.value))) m])

fn attr_remote(tag, a) {
    let key = lower(a[0]);
    if (key == "style") css_remote(a[1])
    else if (key in SRCSETS) len([for (c in n.srcset_candidates(a[1]) where is_remote(c.url)) c])
    else if (key in LOADS and not (key in ["href", "xlink:href"] and tag in LINKS) and is_remote(a[1])) 1
    else 0
}

// remote loads the document would make with remote content allowed
fn remote_loads(root) => sum([for (e in n.all_elements(root))
    if (e.tag == "style") css_remote(e.text) else sum([for (a in e.attrs) attr_remote(e.tag, a)])])

// Content-ID → data: URL for the parts an HTML body names by cid: (RFC 2392)
// a part's content as a data: URL
pub fn data_url(p) {
    let json = format(p.content, 'json');
    "data:" ++ p.type ++ ";base64," ++ slice(json, 1, len(json) - 1)
}
pub fn cid_urls(parts) => reduce([{}, *[for (p in parts where p.cid != null and p.content is binary) p]],
    (m, p) => {*: m, [p.cid]: data_url(p)})

// An HTML body as a standalone document for a message frame's srcdoc.
// options.remote allows network loads; options.cids maps Content-IDs to data:
// URLs (cid_urls). `blocked` counts the remote loads left out.
pub fn clean(html, options) {
    let cx = {remote: options.remote == true, cids: if (options.cids == null) {} else options.cids}
    let doc = parse(html, 'html') ^ { null }
    let root = if (doc == null) null else [for (k in n.of(doc).kids where n.is_el(k) and k.tag == "html") k][0]
    let head = if (root == null) null else [for (k in root.kids where n.is_el(k) and k.tag == "head") k][0]
    let body = if (root == null) null else [for (k in root.kids where n.is_el(k) and k.tag == "body") k][0]
    let styles = if (head == null) [] else [for (k in clean_kids(head.kids, cx) where n.is_el(k) and k.tag == "style") k]
    let body_node = if (body == null) n.mk("body", [], []) else n.mk("body", clean_attrs(body, cx), clean_kids(body.kids, cx));
    {html: "<!DOCTYPE html><html><head><meta charset=\"utf-8\"><style>" ++ LINK_STYLE ++ "</style>" ++
        n.to_html(styles) ++ "</head>" ++ n.to_html([body_node]) ++ "</html>",
     blocked: if (root == null or cx.remote) 0 else remote_loads(root)}
}
