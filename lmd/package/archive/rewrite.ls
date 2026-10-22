// One rewrite for every format (AR10): the manifest maps each resource URL to
// its member; scripts, handler attributes and javascript: URLs are dropped so
// the snapshot is static (AR5v2); hyperlinks stay absolute to the live web.
import n: lambda.doc.readability.node

type css_url = \("url(" "\s"* ("'" | "\"")? (!(")" | "'" | "\""))* ("'" | "\"")? "\s"* ")")
type css_import = \("@import" "\s"+ ("'" | "\"") (!("'" | "\""))+ ("'" | "\""))
let SKIP = ["data:", "javascript:", "blob:", "about:", "mailto:", "tel:"]

// a reference that is not a resource to fetch
pub fn skipped(ref) => not (ref is string) or trim(ref) == "" or starts_with(trim(ref), "#") or
    any([for (s in SKIP) starts_with(lower(trim(ref)), s)])
pub fn resolve(base, ref) {
    if (base == null) trim(ref)
    else (let r = url_resolve(base, trim(ref)), if (r is string) r else trim(ref))
}
fn unquote(s) {
    let t = trim(s);
    if (len(t) >= 2 and (starts_with(t, "'") or starts_with(t, "\"")) and slice(t, 0, 1) == slice(t, len(t) - 1))
        slice(t, 1, len(t) - 1) else t
}
fn url_inner(m) => unquote(slice(m, 4, len(m) - 1))
fn import_inner(m) => unquote(trim(slice(m, 7)))

// the references a stylesheet makes, as written
pub fn css_refs(text) => if (not (text is string)) []
    else unique([*[for (m in find(text, css_url)) url_inner(m.value)], *[for (m in find(text, css_import)) import_inner(m.value)]])

// url → member for every stored resource, final URLs included
pub fn lookup(manifest) => reduce([{}, *[for (e in manifest where e.status == "ok")
    for (u in unique([e.url, e.final_url or e.url])) [u, e.path]]], (acc, kv) => {*: acc, [kv[0]]: kv[1]})

// a reference rewritten: a stored resource becomes `prefix ++ member`, anything else its absolute URL
pub fn target(ref, base, table, prefix) {
    if (skipped(ref)) ref
    else (let abs = resolve(base, ref), let member = table[abs], if (member != null) prefix ++ member else abs)
}
// a stylesheet's text with its references rewritten
pub fn css(text, base, table, prefix) {
    let imports = n.replace_with(text, css_import, (m) => "@import \"" ++ target(import_inner(m), base, table, prefix) ++ "\"");
    n.replace_with(imports, css_url, (m) => "url(\"" ++ target(url_inner(m), base, table, prefix) ++ "\")")
}
fn srcset(value, base, table, prefix) => join([for (part in split(value, ","), let piece = trim(part) where piece != "")
    (let gap = find(piece, \("\s"+), {limit: 1}), if (len(gap) == 0) target(piece, base, table, prefix)
     else target(slice(piece, 0, gap[0].index), base, table, prefix) ++ slice(piece, gap[0].index))], ", ")

fn link(base, v) => if (starts_with(lower(trim(v)), "javascript:")) "#" else if (skipped(v)) v else resolve(base, v)
fn set(el, key, f) {
    let v = n.attr(el, key);
    if (v == null) el else n.set_attr(el, key, f(v))
}
fn rewrite_attrs(e, base, table, prefix) {
    let a1 = n.drop_attrs(e, [for (a in e.attrs where starts_with(lower(a[0]), "on")) a[0]])
    let tag = e.tag
    let res = (v) => target(v, base, table, prefix)
    let abs = (v) => link(base, v)
    let set_srcset = (el) => set(el, "srcset", (v) => srcset(v, base, table, prefix))
    let a2 = if (tag == "a" or tag == "area") set(a1, "href", abs)
        else if (tag == "form") set(a1, "action", abs)
        else if (tag == "link") set(a1, "href", res)
        else if (tag == "img") set_srcset(set(a1, "src", res))
        else if (tag == "source") set_srcset(set(a1, "src", abs))
        else if (tag == "video" or tag == "audio") set(set(a1, "poster", res), "src", abs)
        else if (tag == "iframe" or tag == "embed" or tag == "object") set(set(a1, "src", abs), "data", abs)
        else a1;
    set(a2, "style", (v) => css(v, base, table, prefix))
}
fn dropped(e) => e.tag == "script" or e.tag == "base" or
    (e.tag == "meta" and lower(n.attr(e, "http-equiv") or "") == "refresh") or
    (e.tag == "link" and contains(lower(n.attr(e, "rel") or ""), "preload") and lower(n.attr(e, "as") or "") == "script")
fn rewrite_tree(e, base, table, prefix) {
    let r = rewrite_attrs(e, base, table, prefix)
    if (r.tag == "style") n.with_kids(r, [css(r.text, base, table, prefix)])
    else n.with_kids(r, [for (k in r.kids where not (n.is_el(k) and dropped(k)))
        if (n.is_el(k)) rewrite_tree(k, base, table, prefix) else k])
}

// a fragment's children, rewritten and serialized
pub fn fragment(kids, base, table, prefix) => n.to_html([for (k in kids where not (n.is_el(k) and dropped(k)))
    if (n.is_el(k)) rewrite_tree(k, base, table, prefix) else k])
// the snapshot's index.html: the whole document, static, its resources under assets/
pub fn snapshot(html, base, table) => "<!DOCTYPE html>\n" ++ n.to_html([rewrite_tree(html, base, table, "assets/")])
