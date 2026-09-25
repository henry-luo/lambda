// Convert a traversed Lambda path to a repository-relative filesystem path.
pub pn relative_path(path) {
    let source = string(path)
    var out = ""
    var quoted = false
    var i = 2
    while (i < len(source)) {
        let ch = slice(source, i, i + 1)
        if (ch == "'") { quoted = not quoted }
        else if (ch == "." and not quoted) { out = out ++ "/" }
        else { out = out ++ ch }
        i = i + 1
    }
    return out
}

// Python's Path.rglob does not descend through a symbolic-link directory.
pub pn has_link_ancestor(path) {
    var ancestor = path.parent
    while (ancestor != null) {
        if (ancestor.is_link) { return true }
        ancestor = ancestor.parent
    }
    return false
}
