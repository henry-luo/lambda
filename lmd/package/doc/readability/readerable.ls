// isProbablyReaderable (Readability-readerable.js): a quick check, without
// parsing the article, of whether a page holds enough visible paragraph text.
import math
import n: .node

// <p>, <pre> and <article> in document order, each noting an <li> ancestor
fn text_blocks(e, in_li) => [for (k in e.kids where n.is_el(k))
    for (x in [*(if (k.tag in ["p","pre","article"]) [{node: k, in_li: in_li}] else []),
        *text_blocks(k, in_li or k.tag == "li")]) x]
// the parent <div> of each <br>, in the order of their <br> children
fn br_parents(e, path) => [for (i, k in e.kids where n.is_el(k))
    for (x in [*(if (k.tag == "br" and e.tag == "div") [{path: path, node: e}] else []),
        *br_parents(k, path ++ "." ++ string(i))]) x]
fn length_of(b, min_length) {
    let e = b.node
    let length = n.js_len(n.trim_text(e.text));
    if (not n.visible(e, false) or n.unlikely_candidate(n.match_string(e)) or (e.tag == "p" and b.in_li) or
        length < min_length) 0
    else math.sqrt(length - min_length)
}
pub fn check(html, options) {
    let o = options or {}
    let min_length = if (o.min_content_length != null) o.min_content_length else 140
    let min_score = if (o.min_score != null) o.min_score else 20
    let divs = [for (d in br_parents(html, "0") group by d.path into g) {node: g[0].node, in_li: false}];
    // every score is non-negative, so the running total passes min_score iff the sum does
    sum([for (b in [*text_blocks(html, false), *divs]) length_of(b, min_length)]) > min_score
}
