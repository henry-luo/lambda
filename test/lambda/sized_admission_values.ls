// S11.4.5 (LR03-11): an in-range value crosses a sized boundary and takes the
// sized type; an explicit conversion still wraps, since sized ints are bounded
// and wrapping inside arithmetic. Routed through `dyn` to stay at run time.
fn dyn(v) => v
let a: u8 = dyn(200)
let b: i16 = dyn(-5)
let c = u8(dyn(300))
let d = i8(dyn(200))
let r = [a, b, c, d, type(a), type(b), type(c)]
r
