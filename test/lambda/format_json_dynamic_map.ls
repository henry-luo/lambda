// Dynamic maps have the same JSON object face as shaped maps (D7.4.5v2).
let dynamic = map(["name", "Ada", "nested", map(["x", 2]), "", 7, 12, "integer", "skip", (x)=>x])
let decoded = parse(format({dynamic:dynamic},'json'),'json')^;
[decoded.dynamic.name,decoded.dynamic.nested.x,decoded.dynamic[""],decoded.dynamic["12"],len(decoded.dynamic)]
let escaped = map(["line\nbreak", "✓"])
let roundtrip = parse(format(escaped,'json'),'json')^
roundtrip["line\nbreak"] == "✓"
