// S16.9.5: absence, null and defaults are independent, including nominal spreads.
type Base {
    name: string, opt?: int, tail: string, def?: int = 4,
    string*
    fn summary() => [name, opt, tail, def]
}
type Child : Base { more?: string, count: int = 8 }
fn dynamic(v) => v
let a = <Base name: "a", tail: "z">
let b = <Base *: dynamic({name: "b", tail: "y"})>
let c = <Base *: dynamic({name: "c", opt: 7, tail: "x", def: 9})>
let d = <Child name: "d", tail: "w", "content">;
[a.summary(), b.summary(), c.summary(), d.summary(), d.count,
 'opt' at a, 'opt' at b, 'opt' at c, 'more' at d,
 a is Base, d is Base, d is Child, len(a), len(c), content(d)]
