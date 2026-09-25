// Code-point offsets compatible with slice(); find() currently reports byte offsets.
pub pn text_positions(source: string, needle: string) {
    var positions = []
    if (needle == "") { return positions }
    var offset = 0
    while (offset < len(source)) {
        let found = index_of(slice(source, offset), needle)
        if (found == null) { break }
        let position = offset + found
        positions = positions ++ [position]
        offset = position + len(needle)
    }
    return positions
}
