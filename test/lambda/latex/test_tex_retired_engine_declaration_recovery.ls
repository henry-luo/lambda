// D10.2.2v2: incomplete expl3 declarations follow the native macro scanner.
let missing = [for (source in ["\\ProvidesExplPackage", "\\ProvidesExplPackage{broken}",
    "\\ProvidesExplClass", "\\ProvidesExplFile{broken}"]) parse(source, "tex")^];
let unclosed = parse("A\\ProvidesExplPackage{broken", "tex")^;
let brace = parse("\\ProvidesExplPackage}X", "tex")^;
[
    all([for (r in missing) r.text == "" and r.messages == [] and r.diagnostics == []]),
    unclosed.text == "A" and len(unclosed.diagnostics) == 1 and
        unclosed.diagnostics[0].code == "tex-runaway-argument" and
        unclosed.diagnostics[0].message == "File ended while scanning use of \\@expl@provides@file@@Nnnnnn",
    brace.text == "}X " and [for (d in brace.diagnostics) d.code] == ["tex-extra-brace", "tex-extra-brace"]
]
