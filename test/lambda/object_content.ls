// S2.1.3: an object is a nominally-typed element — attributes, content and
// methods, each optional. The literal is the element form under S16.9.3's
// two-regime commas: a strict comma list for attributes, juxtaposed content,
// and one boundary comma exactly when both are present.

type Point { x: int, y: int }
type Btn {
    label: string,
    string*
    fn shout() => label ++ "!"
}
type Wrap { string* }

let p = <Point x: 3, y: 4>
let b = <Btn label: "ok", "click" <b>>
let w = <Wrap "solo">

// a: printing round-trips as object-literal syntax — the type name is the tag
// (OB8), not map braces.
'=print='
p
w

// b: len is attributes + content (S8.3.1v2). `<Point x: 1, "t">` is 2.
'=len='
len(p)
len(b)
len(w)
len(<Btn label: "a">)

// c: `in` walks attribute values then children; `at` walks attribute names
// only (S8.1.2v2). Methods are members of the type, never of the value, so
// neither axis ever shows one (S8.2.3).
let b_in = [for (v in b) v]
let b_at = [for (k at b) k]
let w_at = [for (k at w) k]
'=iterate='
b_in
b_at
w_at

// d: an IntKey selects a content child, a NameKey an attribute (S8.2.1v3).
let i0 = b[0]
let i1 = b[1]
let i2 = b.label
let i3 = b["label"]
let i4 = w[0]
// (grouped in one array: adjacent string statements would merge under the
// top-level content normalization of S16.7)
let idx = [i0, i1, i2, i3, i4]
'=index='
idx

// e: methods still resolve on an object that has content.
let m0 = b.shout()
'=method='
m0

// f: equality is nominal type + attributes unordered + content ordered
// (S5.4.2v2); a plain map never equals an object.
let e1 = <Btn label: "a", "c"> == <Btn label: "a", "c">
let e2 = <Btn label: "a", "c"> == <Btn label: "a", "d">
let e3 = <Btn label: "a"> == <Btn label: "a", "c">
let e4 = p == {x: 3, y: 4}
'=equal='
e1
e2
e3
e4

// g: content normalization is the element rule (S16.7) — adjacent strings
// merge, so two string children become one.
let merged = <Wrap "a" "b">
let n0 = len(merged)
let n1 = merged[0]
'=normalize='
n0
n1

// h: serialization. OB8 — the type name is the tag in markup formats; JSON is
// not a markup format, so it keeps the "@" nominal-type key and gains the same
// "_" content key an element uses.
let fx = format(b, 'xml')
let fm = format(b, 'mark')
let fj = format(b, 'json')
let fmts = [fx, fm, fj]
'=format='
fmts

// i: `in` walks attribute values then children, so membership must reach both
// (S8.1.1 — whatever `for…in` walks, `in` tests).
let mem = ["click" in b, "ok" in b, "nope" in b]
'=member='
mem
