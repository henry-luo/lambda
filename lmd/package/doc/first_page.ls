// A visitor returns null until the first boundary and supplies the retained part
// of the boundary node in keep. Copy only the prefixes on the path to that node.
fn locate(children, visit, start, end) any^ {
    if (end <= start) null
    else if (end == start + 1) {
        let boundary = visit(children[start], start)^;
        if (boundary == null) null else {*:boundary, index:start}
    } else {
        // Search in source order with logarithmic call depth on long documents.
        let middle = start + (end - start) div 2
        let left = locate(children, visit, start, middle)^;
        if (left != null) left else locate(children, visit, middle, end)^
    }
}

pub fn cut(children, visit) any^ {
    let boundary = locate(children, visit, 0, len(children))^;
    if (boundary == null) null
    else {*:boundary, children:slice(children, 0, boundary.index) ++ boundary.keep}
}
