// S7.6.1v9: a handler's pass-through arm has its operand's static contract minus error. A lowered
// comparison of an untyped read can decay to `any`; a bool binding of the handled value still
// takes the bool lane on every tier.
pn kind(doc) {
    let pigmented = (string(doc.format) == "pigment") ^ { false }
    let wide = (len(doc.tiles) > 2) ^ { false }
    let label = if (pigmented) "oil" else "pixel"
    return {pigmented: pigmented, wide: wide, label: label}
}

pn main() {
    let oil = kind(<layer format: "pigment", tiles: [1, 2, 3]>)^
    let pixel = kind(<layer format: "rgba">)^
    return [oil, pixel]
}
