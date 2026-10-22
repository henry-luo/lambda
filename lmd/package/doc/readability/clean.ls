// Article cleanup (Readability.js _prepArticle and _postProcessContent).
// Each pass rebuilds the article; the pass order is Readability's.
import n: .node
import .text

// Readability.js REGEXPS.videos, matched case-insensitively
type videos = \("//" "www."? (("dailymotion" | "youtube" | "youtube-nocookie" | "player.vimeo" | "v.qq" |
    "bilibili" | "live" . "bilibili") ".com" | ("archive" | "upload.wikimedia") ".org" | "player.twitch.tv"))
// (\b|_)(share|sharedaddy)(\b|_) on a space-padded match string
type share = \(!("\a" | "\d") ("share" | "sharedaddy") !("\a" | "\d"))
type image_list = \(... "." ("jpg" | "jpeg" | "png" | "webp") ws+ "\d" ...)
type image_url = \(ws* (!ws)+ "." ("jpg" | "jpeg" | "png" | "webp") (!ws)* ws*)
type base64_head = \("data:" ws* (!(ws | ";" | ","))+ ws* ";" ws* "base64" ws* ",")

let PRESENTATIONAL = ["align","background","bgcolor","border","cellpadding","cellspacing","frame","hspace",
    "rules","style","valign","vspace"]
let SIZED = ["table","th","td","hr","pre"]
let HEADINGS = ["h1","h2","h3","h4","h5","h6"]
let TEXTISH = ["span","li","td","blockquote","dl","div","img","ol","p","pre","table","ul"]
let EMBEDS = ["object","embed","iframe"]
let MEDIA = ["img","picture","figure","video","audio","source"]
let AD_WORDS = ["ad","advertising","advertisement","pub","publicité","werb","werbung","广告","реклама","anuncio"]
let LOADING_WORDS = [for (w in ["loading","正在加载","загрузка","chargement","cargando"]) for (s in ["","…","..."]) w ++ s]
let SHARE_THRESHOLD = 500

fn kids_map(e, f) => n.with_kids(e, [for (k in e.kids) if (n.is_el(k)) f(k) else k])
// options.allowed_video replaces REGEXPS.videos
fn allowed_video(e, cx) => any([for (a in e.attrs) if (cx.video != null) n.test(a[1], cx.video) else n.test(a[1], videos)])

// ---- _cleanStyles ----
fn clean_styles(e) => if (e.tag == "svg") e else
    kids_map(n.drop_attrs(e, if (e.tag in SIZED) [*PRESENTATIONAL, "width", "height"] else PRESENTATIONAL), clean_styles)

// ---- _markDataTables: the verdict travels on the table record ----
fn is_data_table(t) => t.data_table == true
fn mark_tables(e) {
    let marked = kids_map(e, mark_tables);
    if (e.tag == "table") {*: marked, data_table: data_table(e)} else marked
}
// parseInt(value, 10) || 1
fn span_of(e, key) {
    let found = find(n.trim_text(n.attr(e, key) or ""), \(("+" | "-")? "\d"+), {limit: 1})
    let v = if (len(found) == 0 or found[0].index != 0) 0 else int(replace(found[0].value, "+", ""));
    if (v == 0) 1 else v
}
fn data_table(t) {
    let caption = n.first(t, ["caption"]);
    if (n.attr(t, "role") == "presentation" or n.attr(t, "datatable") == "0") false
    else if (n.attr(t, "summary")) true
    else if (caption != null and len(caption.kids) > 0) true
    else if (n.count_tags(t, ["col","colgroup","tfoot","thead","th"]) > 0) true
    else if (n.count_tags(t, ["table"]) > 0) false
    else {
        let trs = n.find_all(t, ["tr"])
        let rows = sum([for (tr in trs) span_of(tr, "rowspan")])
        let columns = reduce([0, *[for (tr in trs) sum([for (td in n.find_all(tr, ["td"])) span_of(td, "colspan")])]],
            (a, b) => if (b > a) b else a);
        // single row/column tables are layout; large ones hold data
        if (columns == 1 or rows == 1) false
        else if (rows >= 10 or columns > 4) true
        else rows * columns > 10
    }
}

// ---- _fixLazyImages ----
fn base64_prefix(src) {
    let head = find(src, base64_head, {ignore_case: true, limit: 1});
    if (len(head) == 0 or head[0].index != 0) null
    else {length: len(head[0].value), mime: find(slice(head[0].value, 5), \((!(ws | ";" | ","))+), {limit: 1})[0].value}
}
// a tiny base64 placeholder goes when another attribute names an image
fn drop_placeholder_src(e) {
    let src = if (e.tag == "img") n.attr(e, "src") or "" else ""
    let b64 = base64_prefix(src);
    if (b64 != null and b64.mime != "image/svg+xml" and any([for (a in e.attrs where a[0] != "src") n.image_ext(a[1])])
        and n.js_len(src) - b64.length < 133) n.drop_attrs(e, ["src"]) else e
}
fn copy_lazy(e, a) {
    let target = if (a[1] is image_list) "srcset" else if (a[1] is image_url) "src" else null;
    if (target == null) e
    else if (e.tag in ["img","picture"]) n.set_attr(e, target, a[1])
    else if (e.tag == "figure" and n.count_tags(e, ["img","picture"]) == 0) n.with_kids(e, [*e.kids, n.mk("img", [[target, a[1]]], [])])
    else e
}
fn lazy_image(e) {
    let cleaned = drop_placeholder_src(e)
    let src = if (e.tag == "img") n.attr(cleaned, "src") or "" else ""
    let srcset = if (e.tag == "img") n.attr(cleaned, "srcset") or "" else "";
    if ((src != "" or (srcset != "" and srcset != "null")) and not contains(lower(n.class_name(cleaned)), "lazy")) cleaned
    else reduce([cleaned, *[for (a in cleaned.attrs where not (a[0] in ["src","srcset","alt"])) a]], copy_lazy)
}
// document order; an <img> a figure gains is not revisited
fn fix_lazy(e) {
    let own = if (e.tag in ["img","picture","figure"]) lazy_image(e) else e
    let fresh = len(own.kids) - len(e.kids);
    n.with_kids(own, [for (i, k in own.kids) if (n.is_el(k) and i < len(own.kids) - fresh) fix_lazy(k) else k])
}

// ---- _cleanConditionally ----
fn text_density(e, tags) {
    let total = n.text_len(e);
    if (total == 0) 0 else sum([for (c in n.find_all(e, tags)) n.text_len(c)]) / total
}
fn shady(node, text, length, in_list, weight, anc, cx) {
    let p = n.count_tags(node, ["p"])
    let img = n.count_tags(node, ["img"])
    let li = n.count_tags(node, ["li"]) - 100
    let inputs = n.count_tags(node, ["input"])
    let embeds = n.find_all(node, EMBEDS)
    let words = lower(text);
    if (any([for (m in embeds) allowed_video(m, cx)])) false
    else if (words in AD_WORDS or words in LOADING_WORDS) true
    else {
        let link_density = n.link_density(node)
        let figure = n.has_ancestor(anc, "figure")
        let remove = (not figure and img > 1 and p / img < 0.5) or (not in_list and li > p) or inputs > floor(p / 3) or
            (not in_list and not figure and text_density(node, HEADINGS) < 0.9 and length < 25 and (img == 0 or img > 2) and
                link_density > 0) or
            (not in_list and weight < 25 and link_density > 0.2 + cx.link_density_modifier) or
            (weight >= 25 and link_density > 0.5 + cx.link_density_modifier) or
            (len(embeds) == 1 and length < 75) or len(embeds) > 1 or
            (img == 0 and text_density(node, TEXTISH) == 0);
        // simple lists of images stay
        if (in_list and remove and not any([for (c in n.elems(node)) len(n.elems(c)) > 1])) img != n.count_tags(node, ["li"])
        else remove
    }
}
fn removed_conditionally(node, tag, anc, cx) {
    let text = n.inner_text(node)
    let length = n.js_len(text)
    let list_length = sum([for (l in n.find_all(node, ["ul","ol"])) n.text_len(l)])
    let in_list = tag == "ul" or tag == "ol" or (if (length == 0) list_length > 0 else list_length / length > 0.9)
    let weight = n.class_weight(node, cx.weigh);
    if ((tag == "table" and is_data_table(node)) or n.has_ancestor(anc, "table", -1, is_data_table) or
        n.has_ancestor(anc, "code") or any([for (t in n.find_all(node, ["table"])) is_data_table(t)])) false
    else if (weight < 0) true
    else if (len(split(text, ",")) - 1 < 10) shady(node, text, length, in_list, weight, anc, cx)
    else false
}
fn clean_conditionally(e, tag, cx) => if (not cx.clean) e
    else n.prune(e, [tag], (k, anc) => removed_conditionally(k, tag, anc, cx))
// _clean: every <tag>, except allowed videos among embeds
fn clean(e, tag, cx) => n.prune(e, [tag], (k, anc) => not (tag in EMBEDS and allowed_video(k, cx)))

// ---- share links, empty paragraphs, stray <br>s and single-cell tables ----
fn shared(k) => n.test(" " ++ n.match_string(k) ++ " ", share) and n.js_len(n.content_text(k)) < SHARE_THRESHOLD
// _cleanMatchedNodes: a removed node's subtree is not visited
fn clean_shares(e) => n.with_kids(e, [for (k in e.kids where not (n.is_el(k) and shared(k)))
    if (n.is_el(k)) clean_shares(k) else k])
fn br_before_p(e) => n.with_kids(e, [for (i, k in e.kids where not (n.is_el(k) and k.tag == "br" and
    (let next = n.next_node(e.kids, i + 1), next != null and n.is_el(e.kids[next]) and e.kids[next].tag == "p")))
    if (n.is_el(k)) br_before_p(k) else k])
fn unwrap_cell(t) {
    let body = if (t.tag == "table" and n.has_single_tag(t, "tbody")) n.elems(t)[0] else t
    let row = if (t.tag == "table" and n.has_single_tag(body, "tr")) n.elems(body)[0] else null
    let cell = if (row != null and n.has_single_tag(row, "td")) n.elems(row)[0] else null;
    if (cell == null) t else n.retag(cell, if (all([for (c in cell.kids) n.is_phrasing(c)])) "p" else "div")
}
fn single_cells(e) => kids_map(e, (k) => single_cells(unwrap_cell(k)))

pub fn prep_article(article, cx) {
    let styled = mark_tables(clean_styles(article))
    let lazy = fix_lazy(styled)
    let forms = clean_conditionally(clean_conditionally(lazy, "form", cx), "fieldset", cx)
    let junk = reduce([forms, "object", "embed", "footer", "link", "aside"], (e, tag) => clean(e, tag, cx))
    let shares = kids_map(junk, clean_shares)
    let controls = reduce([shares, "iframe", "input", "textarea", "select", "button"], (e, tag) => clean(e, tag, cx))
    let headers = n.prune(controls, ["h1","h2"], (k, anc) => n.class_weight(k, cx.weigh) < 0)
    let blocks = reduce([headers, "table", "ul", "div"], (e, tag) => clean_conditionally(e, tag, cx))
    let titled = n.retag_all(blocks, "h1", "h2")
    let paragraphs = n.prune(titled, ["p"], (k, anc) => n.count_tags(k, ["img","embed","object","iframe"]) == 0 and
        n.inner_text(k, false) == "");
    single_cells(br_before_p(paragraphs))
}

// ---- _postProcessContent ----
fn absolute(uri, cx) {
    if (cx.base == null or (cx.base == cx.url and starts_with(uri, "#"))) uri
    else (let r = url_resolve(cx.base, uri), if (r is string) r else uri)
}
fn srcset_part(m, cx) {
    let body = slice(m, 0, len(m) - 1)
    let core = slice(body, 0, find(body, \(!ws), {last: 1})[0].index + 1)
    let gap = find(core, \(ws+), {last: 1})
    let url_end = if (len(gap) == 0) len(core) else gap[0].index;
    absolute(slice(core, 0, url_end), cx) ++ slice(body, url_end) ++ slice(m, len(m) - 1)
}
fn fix_srcset(srcset, cx) {
    let fixed = n.replace_with(srcset ++ "\u0000", n.srcset_item, (m) => srcset_part(m, cx));
    slice(fixed, 0, len(fixed) - 1)
}
fn fix_media(e, cx) {
    let src = n.attr(e, "src")
    let poster = n.attr(e, "poster")
    let srcset = n.attr(e, "srcset")
    let a = if (src) n.set_attr(e, "src", absolute(src, cx)) else e
    let b = if (poster) n.set_attr(a, "poster", absolute(poster, cx)) else a;
    if (srcset) n.set_attr(b, "srcset", fix_srcset(srcset, cx)) else b
}
// _fixRelativeUris; javascript: links become their text or a <span>
fn fix_uri(k, cx) {
    let href = if (k.tag == "a") n.attr(k, "href") else null
    let fixed = if (href and starts_with(href, "javascript:"))
            (if (len(k.kids) == 1 and k.kids[0] is string) k.kids[0] else n.mk("span", [], k.kids))
        else if (href) n.set_attr(k, "href", absolute(href, cx))
        else if (k.tag in MEDIA) fix_media(k, cx) else k;
    if (fixed is string) fixed else kids_map(fixed, (c) => fix_uri(c, cx))
}
// a <div>/<section> holding one <div>/<section> gives way to it, passing on its attributes
fn simplify(k) {
    if (k.tag in ["div","section"] and not starts_with(n.id_of(k), "readability")) {
        if (n.without_content(k)) null
        else if (n.has_single_tag(k, "div") or n.has_single_tag(k, "section"))
            simplify(reduce([n.elems(k)[0], *k.attrs], (c, a) => n.set_attr(c, a[0], a[1])))
        else simplify_kids(k)
    } else simplify_kids(k)
}
fn simplify_kids(e) => n.with_kids(e, [for (k in e.kids, let r = if (n.is_el(k)) simplify(k) else k where r != null) r])
fn clean_classes(e, keep) {
    let kept = join([for (c in split(n.class_name(e), \(ws+)) where c in keep) c], " ");
    kids_map(if (kept != "") n.set_attr(e, "class", kept) else n.drop_attrs(e, ["class"]), (k) => clean_classes(k, keep))
}
pub fn post_process(article, cx) {
    let linked = kids_map(article, (k) => fix_uri(k, cx))
    let simple = simplify_kids(linked);
    if (cx.keep_classes) simple else clean_classes(simple, ["page", *cx.classes_to_preserve])
}
