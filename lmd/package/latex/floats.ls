// latex/floats.ls — float kinds: one table for numbering, caption labels and lists
// Each kind names its counter, the caption label, the \listof… command and title
// that list its captions, and its float style (plain, plaintop, ruled, boxed).
// Packages add kinds here; \newfloat adds them during analysis.

import util: .util

pub let KINDS = {
    figure: {counter: "figure", name: "Figure", list: "listoffigures", list_title: "List of Figures",
        style: "plain"},
    table: {counter: "table", name: "Table", list: "listoftables", list_title: "List of Tables",
        style: "plain"}
}

let ALGORITHM_KIND = {counter: "algorithm", name: "Algorithm", list: "listofalgorithms",
    list_title: "List of Algorithms"}

// packages that define the algorithm float, with the style their options choose
let ALGORITHM_STYLES = {algorithm: "ruled", algorithm2e: "plain"}

fn package_options(packages, name) {
    let found = [for (entry in packages where entry.key == name) entry.val]
    if (len(found) == 0) null else found[0]
}

fn style_option(opts, fallback) =>
    if (opts == null) fallback
    else if (opts.boxed != null or opts.boxruled != null) "boxed"
    else if (opts.ruled != null) "ruled"
    else if (opts.plain != null) "plain"
    else fallback

// minted's listing float, listed by \listoflistings
let LISTING_KIND = {counter: "listing", name: "Listing", list: "listoflistings",
    list_title: "List of Listings", style: "plain"}

// the float kinds of a document's active packages
pub fn kinds_for(packages) {
    let algorithm = [for (pkg, style in ALGORITHM_STYLES,
        let opts = package_options(packages, string(pkg)) where opts != null)
        {*:ALGORITHM_KIND, style: style_option(opts, style),
         within: if (opts.section != null) "section" else if (opts.chapter != null) "chapter" else null}]
    let with_algorithm = if (len(algorithm) == 0) KINDS else {*:KINDS, algorithm: algorithm[0]};
    (if (package_options(packages, "minted") == null) with_algorithm
     else {*:with_algorithm, listing: LISTING_KIND})
}

// \newfloat{type}{placement}{ext}: named after its type until \floatname renames it
pub fn new_kind(float_type, style) =>
    {counter: float_type, name: float_type, list: null, list_title: null, style: style}

// a float environment's kind: its own name, or the unstarred name of a spanning variant
pub fn kind_of(kinds, tag) {
    let base = if (ends_with(tag, "*")) slice(tag, 0, len(tag) - 1) else tag
    if (kinds[base] != null) base else null
}

pub fn kind_name(kinds, kind) => if (kinds != null and kinds[kind] != null) kinds[kind].name else kind

// the float kind a \listof… command lists, or null
pub fn kind_for_list(kinds, command) {
    let found = [for (k, v in kinds where v.list == command) string(k)]
    if (len(found) == 0) null else found[0]
}

// The float's own caption and label: subfloats keep theirs, so the search stops there.
pub fn own_child(el, tag) {
    if (not (el is element)) null
    else {
        let found = [for (child in el, let hit = own_match(child, tag) where hit != null) hit]
        if (len(found) == 0) null else found[0]
    }
}

fn own_match(node, tag) {
    if (not (node is element)) null
    else {
        let node_tag = string(name(node))
        if (node_tag == tag) node
        else if (node_tag == "subfigure" or node_tag == "subtable") null
        else own_child(node, tag)
    }
}

// list text of a caption: the optional short form when given; starred captions are not listed
pub fn list_caption(caption) {
    if (caption == null or caption.starred == true or string(name(caption)) == "caption*") null
    else {
        let short = util.optional_raw(caption)
        if (short != null) trim(short) else trim(util.text_of_skip_brack(caption))
    }
}
