// LR07-15: implicit receiver-field access inside object method bodies.
// A method prologue loads the receiver's fields into locals; bare field names
// in the body must resolve to those locals. They did not on the eager JIT tier
// until the object field's scope helper carried its binding back-pointer, so
// every implicit read evaluated as 0 there while T0 returned the right value.
// Tier parity is the point of this fixture: run it under LAMBDA_TIER=interp
// and LAMBDA_TIER=jit and the output must be identical.

type Counter {
    value: int,
    fn double() => value * 2
    fn add(n: int) => value + n
    fn explicit() => ~.value
    fn mixed(k: int) => value + k * 2
}

let c = <Counter value: 5>
'=read='
c.double()
c.add(3)
c.explicit()
c.mixed(4)
c.value

// inherited fields are copied into the child shape and take the same path
type Tagged : Counter {
    tag: string,
    fn describe() => tag ++ ":" ++ string(value)
}
let t = <Tagged value: 7, tag: "n">
'=inherit='
t.describe()
t.double()

// a float field exercises the prologue's unboxing arm
type Box { w: float, fn area() => w * 2.0 }
let b = <Box w: 2.5>
'=float='
b.area()
