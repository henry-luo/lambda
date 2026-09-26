// LR03-19 (S1.6): an object type laid its fields out on a flat 8-byte stride,
// but a field whose contract names no one carrier (a union, a range, `number`,
// `integer`) is a 9-byte self-describing slot, so the next field started one
// byte inside it and the boxed field read back `inf`, `-inf` or `0`. Fields
// now stride by their storage size, as map types do. Golden written from the
// ruling, not from the runtime.

type Obj { a: int | string, b: string }
type Ranged { a: 1 to 5, b: string }
type RangedInt { a: 1 to 5, b: int }
type Last { b: string, a: int | string }
type Base { a: int | string, b: string, fn get() => [a, b] }
type Derived : Base { c: 1 to 9, d: float, e: number, f: int? }
type Num { n: integer, label: string, m: number, tag: symbol }

let o = <Obj a: 3, b: "b">;
let r = <Ranged a: 3, b: "b">;
let ri = <RangedInt a: 3, b: 7>;
let l = <Last a: 3, b: "b">;
"-- a boxed field that is not last reads back its value --";
[o.a, o.b, r.a, r.b, ri.a, ri.b, l.a, l.b];
[o, r, ri, l];
"-- inherited fields keep the base's layout --";
let x = <Derived a: "s", b: "bb", c: 4, d: 2.5, e: 7, f: 3>;
[x.a, x.b, x.c, x.d, x.e, x.f, x.get()];
let y = <Derived a: 1, b: "bb", c: 9, d: 0.5, e: 7.5, f: null>;
[y.a, y.c, y.e, y.f, y.get()];
[x is Base, x == <Derived a: "s", b: "bb", c: 4, d: 2.5, e: 7, f: 3>, x == y];
"-- number and integer fields are boxed too --";
let n = <Num n: 5, label: "five", m: 1.5, tag: 'k'>;
[n.n, n.label, n.m, n.tag];
{*: x}
