// Shared standalone SVG font embedding; geometry and layout remain with each document package.
fn text_faces(body) {
    if (not (body is element)) []
    else [*if (name(body) == 'text') [{family: body["font-family"], weight: body["font-weight"], style: body["font-style"]}] else [],
        *[for (child in content(body), face in text_faces(child)) face]]
}

pub fn stylesheet(faces, body) {
    let used = unique(text_faces(body))
    let rules = [for (face in faces where len([for (use in used where
        use.family == face.font_family and use.weight == (face.font_weight or 400) and
        use.style == (face.font_style or "normal")) use]) > 0) (
        let encoded = format(face.data, 'json')^,
        "@font-face{font-family:" ++ format(face.font_family, 'json')^ ++
            ";font-weight:" ++ string(face.font_weight or 400) ++ ";font-style:" ++ (face.font_style or "normal") ++
            ";src:url(data:application/octet-stream;base64," ++ slice(encoded, 1, len(encoded) - 1) ++ ")}")];
    join(rules, "\n")
}
