// Regression for nested pn calls returning records with non-default fields.

pn font_info(name: string) {
    let weight = if (name == "Helvetica-Bold") { "bold" } else { "normal" }
    let style = if (name == "Helvetica-Oblique") { "italic" } else { "normal" }
    {family: "Helvetica", weight: weight, style: style}
}

pn resolve_info(name: string) {
    let info = font_info(name)
    {family: info.family, weight: info.weight, style: info.style, info: info}
}

pn main() {
    print({direct: font_info("Helvetica-Bold")})
    print({nested: resolve_info("Helvetica-Bold")})
}
