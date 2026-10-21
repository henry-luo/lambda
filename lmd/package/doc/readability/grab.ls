// _grabArticle (Readability.js): node prepping, paragraph scoring, the top
// candidate and its siblings, and the flag-dropping retries.
import n: .node
import clean: .clean

type byline = \("byline" | "author" | "dateline" | "writtenby" | "p-author")
type commas = \("," | "\u060C" | "\uFE50" | "\uFE10" | "\uFE11" | "\u2E41" | "\u2E34" | "\u2E32" | "\uFF0C")

let TAGS_TO_SCORE = ["section","h2","h3","h4","h5","h6","p","td","pre"]
let UNLIKELY_ROLES = ["menu","menubar","complementary","navigation","alert","alertdialog","dialog"]
let DIV_TO_P = ["blockquote","dl","div","img","ol","p","pre","table","ul"]
let ALTER_TO_DIV_EXCEPTIONS = ["div","article","section","p","ol","ul"]
let REMOVE_IF_EMPTY = ["div","section","header","h1","h2","h3","h4","h5","h6"]
// strip unlikely candidates, weigh classes, clean conditionally; each retry drops one more
let FLAGS = [{strip: true, weigh: true, clean: true}, {strip: false, weigh: true, clean: true},
    {strip: false, weigh: false, clean: true}, {strip: false, weigh: false, clean: false}]

// ---- node prepping: one pre-order walk that removes and rewrites ----
fn valid_byline(e, ms) {
    let length = n.js_len(n.trim_text(e.text));
    (n.attr(e, "rel") == "author" or contains(n.attr(e, "itemprop") or "", "author") or n.test(ms, byline)) and
        length > 0 and length < 100
}
// a descendant marked itemprop="name" names the author more exactly
fn byline_text(e) {
    let named = [for (d in drop(n.all_elements(e), 1) where contains(n.attr(d, "itemprop") or "", "name")) d];
    n.trim_text((if (len(named) > 0) named[0] else e).text)
}
fn unlikely_node(e, ms, anc) => (n.unlikely_candidate(ms) and
    not n.has_ancestor(anc, "table") and not n.has_ancestor(anc, "code") and e.tag != "body" and e.tag != "a") or
    n.attr(e, "role") in UNLIKELY_ROLES
fn whitespace_trimmed(run) {
    let keep = [for (i, k in run where not n.is_whitespace(k)) i];
    if (len(keep) == 0) [] else slice(run, keep[0], keep[len(keep) - 1] + 1)
}
fn phrasing_end(kids, i) => if (i < len(kids) and n.is_phrasing(kids[i])) phrasing_end(kids, i + 1) else i
// runs of phrasing content, trimmed of white space, move into new paragraphs
fn wrap_phrasing(kids, i, out) {
    if (i >= len(kids)) out
    else if (not n.is_phrasing(kids[i])) wrap_phrasing(kids, i + 1, [*out, kids[i]])
    else {
        let end = phrasing_end(kids, i)
        let run = whitespace_trimmed(slice(kids, i, end));
        wrap_phrasing(kids, end, if (len(run) > 0) [*out, n.mk("p", [], run)] else out)
    }
}
// a <div> holding one <p> becomes that <p>; one without block children becomes a <p>
fn div_to_p(e) {
    let d = n.with_kids(e, wrap_phrasing(e.kids, 0, []));
    if (n.has_single_tag(d, "p") and n.link_density(d) < 0.25) n.elems(d)[0]
    else if (n.count_tags(d, DIV_TO_P) == 0) n.retag(d, "p")
    else d
}
// returns {node (null when removed), st}; st threads the byline, lang and title-header state
fn walk(e, anc, st, cx) {
    let s = if (e.tag == "html") {*: st, lang: n.attr(e, "lang")} else st
    let ms = n.match_string(e);
    if (not n.visible(e) or (n.attr(e, "aria-modal") == "true" and n.attr(e, "role") == "dialog")) {node: null, st: s}
    else if (not s.byline and not cx.meta_byline and valid_byline(e, ms)) {node: null, st: {*: s, byline: byline_text(e)}}
    else if (s.title_header and (e.tag == "h1" or e.tag == "h2") and n.similarity(cx.title, n.inner_text(e, false)) > 0.75)
        {node: null, st: {*: s, title_header: false}}
    else if (cx.strip and unlikely_node(e, ms, anc)) {node: null, st: s}
    else if (e.tag in REMOVE_IF_EMPTY and n.without_content(e)) {node: null, st: s}
    else {
        let m = if (e.tag == "div") div_to_p(e) else e
        let r = walk_kids(m.kids, 0, [m, *anc], s, cx, []);
        {node: n.with_kids(m, r.kids), st: r.st}
    }
}
fn walk_kids(kids, i, anc, st, cx, out) {
    if (i >= len(kids)) {kids: out, st: st}
    else if (not n.is_el(kids[i])) walk_kids(kids, i + 1, anc, st, cx, [*out, kids[i]])
    else {
        let r = walk(kids[i], anc, st, cx);
        walk_kids(kids, i + 1, anc, r.st, cx, if (r.node == null) out else [*out, r.node])
    }
}

// ---- scoring over a path index of the walked tree ----
fn index(e, path, parent) => [{path: path, parent: parent, node: e},
    *[for (i, k in e.kids where n.is_el(k)) for (x in index(k, path ++ "." ++ string(i), path)) x]]
fn up(at, it) => if (it.parent == null) null else at[it.parent]
fn ancestors(at, it, limit) {
    let p = if (limit == 0) null else up(at, it);
    if (p == null) [] else [p, *ancestors(at, p, limit - 1)]
}
fn tag_score(tag) {
    if (tag == "div") 5
    else if (tag in ["pre","td","blockquote"]) 3
    else if (tag in ["address","ol","ul","dl","dd","dt","li","form"]) -3
    else if (tag in ["h1","h2","h3","h4","h5","h6","th"]) -5
    else 0
}
// _initializeNode
fn initial(e, cx) => tag_score(e.tag) + n.class_weight(e, cx.weigh)
// each paragraph adds to five ancestors: parent fully, grandparent half, then 1/(level*3)
fn contributions(items, at) => [for (it in items, let text = n.inner_text(it.node), let length = n.js_len(text)
    where it.node.tag in TAGS_TO_SCORE and it.parent != null and length >= 25)
    for (level, a in ancestors(at, it, 5) where a.parent != null)
        {path: a.path, amount: points(text, length) / (if (level == 0) 1 else if (level == 1) 2 else level * 3)}]
fn points(text, length) => 1 + len(find(text, commas)) + 1 + min(floor(length / 100), 3)
// candidates in first-scored order with link-density-scaled scores
fn candidates(items, at, cx) => [for (c in contributions(items, at) group by c.path into g)
    {path: path, score: scaled(at[path].node, g |> ~.amount, cx)}]
fn scaled(node, amounts, cx) => reduce([initial(node, cx), *amounts], (a, b) => a + b) * (1 - n.link_density(node))

fn score_of(scores, path) {
    let hits = [for (s in scores where s[0] == path) s[1]];
    if (len(hits) == 0) null else hits[0]
}
// readability is assigned on first need, with the unscaled initial score
fn ensure(scores, it, cx) => if (score_of(scores, it.path) != null) scores else [*scores, [it.path, initial(it.node, cx)]]
fn is_body(it) => it == null or it.node.tag == "body"
fn shared_climb(at, p, lists) => if (is_body(p)) null
    else if (len([for (l in lists where p.path in l) 1]) >= 3) p else shared_climb(at, up(at, p), lists)
fn score_climb(at, scores, top, p, previous, low) {
    let s = if (is_body(p)) null else score_of(scores, p.path);
    if (is_body(p)) top
    else if (s == null) score_climb(at, scores, top, up(at, p), previous, low)
    else if (s < low) top
    else if (s > previous) p
    else score_climb(at, scores, top, up(at, p), s, low)
}
fn only_child_climb(at, top) {
    let p = up(at, top);
    if (not is_body(p) and len(n.elems(p.node)) == 1) only_child_climb(at, p) else top
}
// the top candidate, moved up when its parents carry the competing content
fn select_top(at, scores, top, cx) {
    let first = top[0]
    let alternatives = [for (t in drop(top, 1) where score_of(scores, t.path) / score_of(scores, first.path) >= 0.75)
        [for (a in ancestors(at, t, -1)) a.path]]
    let shared = if (len(alternatives) >= 3) shared_climb(at, up(at, first), alternatives) else null
    let picked = if (shared != null) shared else first
    let s1 = ensure(scores, picked, cx)
    let picked_score = score_of(s1, picked.path)
    let climbed = score_climb(at, s1, picked, up(at, picked), picked_score, picked_score / 3)
    let final = only_child_climb(at, climbed);
    {top: final, scores: ensure(s1, final, cx)}
}
fn keep_sibling(s, top, scores, bar) {
    let top_class = n.class_name(top.node)
    let bonus = if (top_class != "" and n.class_name(s.node) == top_class) score_of(scores, top.path) * 0.2 else 0
    let own = score_of(scores, s.path);
    if (own != null and own + bonus >= bar) true
    else if (s.node.tag == "p") {
        let density = n.link_density(s.node)
        let inner = n.inner_text(s.node)
        let length = n.js_len(inner);
        (length > 80 and density < 0.25) or
            (length < 80 and length > 0 and density == 0 and (contains(inner, ". ") or ends_with(inner, ".")))
    } else false
}
fn dir_of(chain) {
    let dirs = [for (it in chain where it != null) for (d in [n.attr(it.node, "dir")] where d) d];
    if (len(dirs) == 0) null else dirs[0]
}

// one pass of the article grab under one flag set
fn attempt(html, cx) {
    let walked = walk(html, [], {byline: cx.byline, title_header: true, lang: null}, cx)
    let root = if (walked.node == null) n.mk("html", [], []) else walked.node
    let items = index(root, "0", null)
    let at = map([for (it in items) for (x in [it.path, it]) x])
    let body = [for (it in items where it.node.tag == "body" and it.parent == "0") it]
    let body_path = if (len(body) > 0) body[0].path else null
    let all_scored = candidates(items, at, cx)
    // retries restore body's children only, so a body scored earlier keeps its
    // readability and is never a candidate again
    let scored = sort([for (c in all_scored where not (cx.body_scored and c.path == body_path)) c],
        {dir: 'desc', by: (c) => c.score})
    let top = [for (c in take(scored, cx.top_candidates)) at[c.path]]
    let scores = [for (c in scored) [c.path, c.score]]
    let created = len(top) == 0 or top[0].node.tag == "body"
    let chosen = if (created) null else select_top(at, scores, top, cx)
    let parent = if (created) (if (len(body) > 0) body[0] else null) else up(at, chosen.top)
    let picked = if (created) [n.mk("div", [], if (parent != null) parent.node.kids else [])]
        else {
            let top_score = score_of(chosen.scores, chosen.top.path)
            let bar = max(10, top_score * 0.2)
            let siblings = if (parent == null) [chosen.top] else
                [for (i, k in parent.node.kids where n.is_el(k)) at[parent.path ++ "." ++ string(i)]];
            [for (s in siblings where s.path == chosen.top.path or keep_sibling(s, chosen.top, chosen.scores, bar))
                if (s.node.tag in ALTER_TO_DIV_EXCEPTIONS) s.node else n.retag(s.node, "div")]
        }
    let prepped = clean.prep_article(n.mk("div", [], picked), cx)
    let page = [["id", "readability-page-1"], ["class", "page"]]
    // a created container is the page itself; otherwise a page <div> wraps the content
    let article = if (created) n.with_kids(prepped, [for (k in prepped.kids) if (n.is_el(k)) {*: k, attrs: page} else k])
        else n.mk("div", [], [n.mk("div", page, prepped.kids)])
    let chain = if (created) [parent, null, *ancestors(at, parent, -1)]
        else [parent, chosen.top, *ancestors(at, parent, -1)];
    {article: article, length: n.text_len(article), dir: if (parent == null) null else dir_of(chain),
     byline: walked.st.byline, lang: walked.st.lang, walked: root,
     body_scored: cx.body_scored or any([for (c in all_scored) c.path == body_path])}
}
// innerHTML round trip of the cached page for the next attempt
fn restore_body(walked, body) {
    let reparsed = lambda.sys.parse("<body>" ++ n.to_html(body.kids) ++ "</body>", 'html') ^ { null }
    let fresh = if (reparsed == null) null else n.first(n.of(reparsed), ["body"]);
    n.with_kids(walked, [for (k in walked.kids)
        if (n.is_el(k) and k.tag == "body") n.with_kids(k, if (fresh != null) fresh.kids else body.kids) else k])
}
// retries drop one flag at a time; failing all, the longest attempt wins
pub fn grab(html, cx) => retry(html, n.first(html, ["body"]), {*: cx, body_scored: false}, 0, [])
fn retry(html, cached, cx, pass, tried) {
    let r = attempt(html, {*: cx, *: FLAGS[pass]});
    if (r.length >= cx.char_threshold) r
    else if (pass < 3)
        retry(if (cached == null) r.walked else restore_body(r.walked, cached), cached,
            {*: cx, byline: r.byline, body_scored: r.body_scored}, pass + 1, [*tried, r])
    else {
        let best = sort([*tried, r], {dir: 'desc', by: (a) => a.length})[0];
        if (best.length == 0) null else {*: best, dir: r.dir, byline: r.byline, lang: r.lang}
    }
}
