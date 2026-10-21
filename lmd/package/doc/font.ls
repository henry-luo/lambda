// Shared document font references; geometry and layout remain with each package.
import paths: lambda.io.path

pub fn resource_path(file) {
    // CSS paths use forward slashes, including an absolute Windows home override.
    let home = replace(sys.lambda.home#, "\\", "/")
    let absolute = if (len(home) > 2 and slice(home, 1, 2) == ":") paths.normal_path(home)
        else paths.resolve_path(replace(sys.proc.self.cwd#, "\\", "/"), home);
    absolute ++ "/package/" ++ file
}

fn source(face) {
    // D7.2.9: bundled resources keep their URLs; only bytes-only custom faces embed.
    if (face.src is string and face.src != "") "url(" ++ format(face.src, 'json')^ ++ ")"
    else {
        let encoded = format(face.data, 'json')^;
        "url(data:application/octet-stream;base64," ++ slice(encoded, 1, len(encoded) - 1) ++ ")"
    }
}

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
        "@font-face{font-family:" ++ format(face.font_family, 'json')^ ++
            ";font-weight:" ++ string(face.font_weight or 400) ++ ";font-style:" ++ (face.font_style or "normal") ++
            ";src:" ++ source(face) ++ "}")];
    join(rules, "\n")
}
