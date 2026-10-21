// Document preparation before the article is grabbed (Readability.js
// _unwrapNoscriptImages, _removeScripts and _prepDocument).
import n: .node

// ---- _unwrapNoscriptImages ----
fn image_attr(a) => a[0] in ["src","srcset","data-src","data-srcset"] or n.image_ext(a[1])
// placeholder <img>s carry no image source at all
fn drop_placeholders(e) => n.with_kids(e, [for (k in e.kids where not (n.is_el(k) and k.tag == "img" and
    not any([for (a in k.attrs) image_attr(a)]))) if (n.is_el(k)) drop_placeholders(k) else k])
// _isSingleImage: an <img>, possibly wrapped in elements holding nothing else
fn single_image(e) {
    let kids = n.elems(e);
    e.tag == "img" or (len(kids) == 1 and n.is_blank(e.text) and single_image(kids[0]))
}
// the noscript image takes over the previous image's sources that it lacks
fn adopt(image, a) {
    let current = n.attr(image, a[0]);
    if (a[1] == "" or not (a[0] in ["src","srcset"] or n.image_ext(a[1])) or current == a[1]) image
    else n.set_attr(image, if (current != null) "data-old-" ++ a[0] else a[0], a[1])
}
// apply f to the first <img> of a subtree, in document order
fn first_image(e, f) {
    if (e.tag == "img") f(e)
    else {
        let at = [for (i, k in e.kids where n.is_el(k) and (k.tag == "img" or n.count_tags(k, ["img"]) > 0)) i][0];
        n.with_kids(e, [for (i, k in e.kids) if (i == at) first_image(k, f) else k])
    }
}
// one sibling list, left to right: a single-image <noscript> replaces the
// single-image element before it (the noscript itself stays until scripts go)
fn unwrap_kids(kids, i, out: array) array {
    if (i >= len(kids)) out
    else {
        let k = if (n.is_el(kids[i])) unwrap_noscript(kids[i]) else kids[i]
        let prev = last_element(out, len(out) - 1);
        if (n.is_el(k) and k.tag == "noscript" and single_image(k) and prev != null and single_image(out[prev])) {
            let old = out[prev]
            let source = if (old.tag == "img") old else n.first(old, ["img"])
            let holder = n.elems(k)[0]
            let replaced = first_image(holder, (image) => reduce([image, *source.attrs], adopt));
            unwrap_kids(kids, i + 1, [*[for (j, o in out) if (j == prev) replaced else o], k])
        } else unwrap_kids(kids, i + 1, [*out, k])
    }
}
fn last_element(out, i) => if (i < 0) null else if (n.is_el(out[i])) i else last_element(out, i - 1)
fn unwrap_noscript(e) => n.with_kids(e, unwrap_kids(e.kids, 0, []))
pub fn unwrap_noscript_images(html) => unwrap_noscript(drop_placeholders(html))

// ---- _replaceBrs ----
fn is_br(kids, i) => i != null and n.is_el(kids[i]) and kids[i].tag == "br"
// the <br>s of a chain after its first one, which are removed
fn chain(kids, i, found) {
    let next = n.next_node(kids, i + 1);
    if (is_br(kids, next)) chain(kids, next, [*found, next]) else found
}
// the siblings after a replaced chain that move into its <p>
fn absorb(kids, i, skip, taken) {
    let k = if (i < len(kids)) kids[i] else null;
    if (k == null) {taken: taken, next: i}
    else if (i in skip) absorb(kids, i + 1, skip, taken)
    else if ((is_br(kids, i) and is_br(kids, n.next_node(kids, i + 1, skip))) or not n.is_phrasing(k)) {taken: taken, next: i}
    else absorb(kids, i + 1, skip, [*taken, if (n.is_el(k)) replace_brs(k) else k])
}
fn drop_trailing_whitespace(kids) => if (len(kids) > 0 and n.is_whitespace(kids[len(kids) - 1]))
    drop_trailing_whitespace(take(kids, len(kids) - 1)) else kids
fn br_kids(kids, i, out, made) {
    if (i >= len(kids)) {kids: out, made: made}
    else {
        let removed = if (is_br(kids, i)) chain(kids, i, []) else [];
        if (len(removed) > 0) {
            let moved = absorb(kids, i + 1, removed, []);
            br_kids(kids, moved.next, [*out, n.mk("p", [], drop_trailing_whitespace(moved.taken))], true)
        } else br_kids(kids, i + 1, [*out, if (n.is_el(kids[i])) replace_brs(kids[i]) else kids[i]], made)
    }
}
// 2+ successive <br>s become one <p> holding the phrasing content after them
pub fn replace_brs(e) {
    let r = br_kids(e.kids, 0, [], false)
    let rebuilt = n.with_kids(e, r.kids);
    // a <p> cannot sit inside a <p>
    if (r.made and e.tag == "p") n.retag(rebuilt, "div") else rebuilt
}

// _prepDocument: styles go, <br> chains become paragraphs, <font> becomes <span>
pub fn prep_document(html) {
    let unstyled = n.strip_tags(html, ["style"])
    let body = n.with_kids(unstyled, [for (k in unstyled.kids) if (n.is_el(k) and k.tag == "body") replace_brs(k) else k]);
    n.retag_all(body, "font", "span")
}
