// latex/floats.ls — float kinds: one table for numbering, caption labels and lists
// Each kind names its counter, the caption label, and the \listof… command and
// title that list its captions. Packages that add floats extend this table.

import util: .util

pub let KINDS = {
    figure: {counter: "figure", name: "Figure", list: "listoffigures", list_title: "List of Figures"},
    table: {counter: "table", name: "Table", list: "listoftables", list_title: "List of Tables"}
}

// the float kind a \listof… command lists, or null
pub fn kind_for_list(command) {
    let kinds = [for (k, v in KINDS where v.list == command) string(k)]
    if (len(kinds) == 0) null else kinds[0]
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
