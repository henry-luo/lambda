// D2.6.6v2 phase 2 / S2.1.1v3: an object is a NOMINAL CONTAINER, not a
// container kind. A nominal value is an ordinary map or element whose type
// descriptor carries a nominal record, so `is object` and the structural test
// are independent axes. This fixture pins that independence, and pins nominal
// sameness as identity of the record rather than equality of the type name.

type P { x: int, fn dbl() => x * 2 }
type Q { x: int }                 // same shape, different type
type B { label: string, string* } // a content pattern makes it element-kinded

let p = <P x: 3>
let b = <B label: "t", "c">

// a: the two axes are orthogonal. P declares only attributes, so its instances
// ARE maps; B declares content, so its instances ARE elements. Both are objects.
let axes_p = [p is P, p is object, p is map, p is element]
let axes_b = [b is B, b is object, b is element, b is map]
let axes_s = [{x: 3} is object, <e "c"> is object]
'=axes='
axes_p
axes_b
axes_s

// b: nominal sameness is the RECORD, not the name — Q has P's exact shape and
// is still a different type; a structural map never equals a nominal one.
// (each literal is bound first: `== <` would otherwise read as a comparison)
let p2 = <P x: 3>
let q1 = <Q x: 3>
let plain = {x: 3}
let eqs = [p == p2, p == q1, p == plain, q1 == plain]
'=equal='
eqs

// c: methods resolve on a nominal value whatever its structural kind.
let meth = p.dbl()
'=method='
meth

// d: the declared type survives reflection.
let nm = string(name(p))
'=name='
nm

// e: len counts attributes plus content for a nominal value (S8.3.1v2), on
// both the map form and the element form.
let lens = [len(p), len(b), len(<B label: "a">)]
'=len='
lens

// f: printing round-trips as object-literal syntax — the type name is the tag
// (OB8) for the map form as well as the element form.
'=print='
p
b

// g: S6.2.1 — `object` is its own ORDER BAND between map and element, and it
// holds for nominal values of ANY structural kind. Within the band, order is by
// type name. A structural map sorts before every object; a structural element
// after. (This is exactly what the representation flip nearly lost: the band
// used to be selected by the container tag, which nominal values no longer wear.)
type A2 { b: int }
let mixed = [<P x: 1>, {b: 1}, <e "x">, <A2 b: 1>]
let sorted_mixed = sort(mixed)
'=order='
sorted_mixed
