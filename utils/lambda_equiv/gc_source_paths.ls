// Source traversal shared by native GC audits.
import .path_utils

fn gc_source(name: string) bool =>
    ends_with(name, ".c") or ends_with(name, ".cc") or
    ends_with(name, ".cpp") or ends_with(name, ".h") or
    ends_with(name, ".hh") or ends_with(name, ".hpp")

pn gc_collect_source(path, roots, skipped, var targets) {
    if (not path.is_file or not gc_source(path.name) or has_link_ancestor(path)) { return }
    let label = relative_path(path)
    let parts = split(label, "/")
    if (not contains(roots, parts[0])) { return }
    for (part in parts) { if (contains(skipped, part)) { return } }
    targets = targets ++ [path]
}

pub pn gc_source_paths(roots, skipped) {
    var targets = []
    for (path in \.lambda.**) { gc_collect_source(path, roots, skipped, targets) }
    for (path in \.lib.**) { gc_collect_source(path, roots, skipped, targets) }
    return targets
}
