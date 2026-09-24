// S11.1.5v2: a function type's return type is the type a returned value has,
// so a call through a function-type contract yields that type -- and calling
// the result of a curried signature, `mk(2)(3)`, yields the inner signature's
// return. The type-pattern parser had kept the return as a type VALUE, so the
// first call typed as `type`, calling it read as a conversion to the wrapped
// function type, and a map literal laid the int result out as a function
// pointer: a segfault on both tiers. Arrays and bare results survived only
// because they store any item.

type Maker = fn (x: int) fn (y: int) int
fn make(a: int) => (b: int) => a + b
fn times(a: int) => (b: int) => a * b
fn inc(y: int) => y + 1
fn curry2(f: fn (x: int) fn (y: int) int, a: int, b: int) => f(a)(b)
fn pair(f: fn (x: int) fn (y: int) int, a: int, b: int) => [f(a)(b), {v: f(a)(b)}]

let mk: Maker = make
let loose: fn (x: int) fn (y: int) = make
let step = mk(2)
let h: fn (y: int) int = inc

'1. calls through a curried contract'
"1.1"; mk(2)(3)
"1.2"; [mk(2)(3)]
"1.3"; {curried: mk(2)(3)}
"1.4"; {a: 1, curried: mk(2)(3), z: mk(10)(20)}
"1.5"; {v: step(3), t: type(step)}
"1.6"; {v: loose(4)(5)}
"1.7"; mk(2)(3) * 10 + 1

'2. calls through a curried parameter contract'
"2.1"; {v: curry2(make, 6, 7), w: curry2(times, 6, 7)}
"2.2"; pair(times, 6, 7)

'3. a one-level contract types its call result'
"3.1"; {v: h(3), w: h(3) * 2}
