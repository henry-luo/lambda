// Test: for k:int, v and for k:symbol, v in element
// Verifies filtered iteration over element attributes and children

// construct element via XML parsing
let doc^err = parse("<item x='1' y='2'><sub>A</sub><sub>B</sub></item>", "xml")
let el = doc[0]

"=== for k:int, v in element (children only) ==="
[for (k:int, v in el) k]
[for (k:int, v in el) name(v)]

"=== for k:symbol, v in element (attrs only) ==="
[for (k:symbol, v in el) k]
[for (k:symbol, v in el) v]

"=== for k, v in element (all: attrs + children) ==="
[for (k, v in el) k]

"=== for v in element (children values) ==="
len([for (v in el) v])

"=== for k:int, v on element with text children ==="
let doc2^err2 = parse("<p>hello<b>bold</b>world</p>", "xml")
let p = doc2[0]
[for (k:int, v in p) k]
len(p)

"=== for k:symbol, v on element with no attrs ==="
let doc3^err3 = parse("<div>text</div>", "xml")
let div = doc3[0]
[for (k:symbol, v in div) [k, v]]
[for (k:int, v in div) [k, v]]

"=== for k:int, v on map (no indexed entries) ==="
let m = {a: 1, b: 2}
[for (k:int, v in m) [k, v]]

"=== for k:symbol, v on map (all entries) ==="
[for (k:symbol, v in m) [k, v]]

"=== for k:symbol, v on array (no keyed entries) ==="
[for (k:symbol, v in [10, 20]) [k, v]]

"=== for k:int, v on array (all entries) ==="
[for (k:int, v in [10, 20]) [k, v]]
