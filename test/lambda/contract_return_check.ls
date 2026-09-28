// LR03-13 (S11.4.1v3, S7.7.1): a call through a declared function-type
// contract checks its result against the contract's return type where the
// call is made. Admitting the function value tests only its colour
// (S11.1.5v2), so the JIT had read `h`'s string as the int 0.
let h: fn (y: int) int = (y) => "s"
let g: fn (y: int) int = (y) => y * 2
fn run_with(f: fn (x: int) int, x: int) => f(x) + 1
fn raw_call(f: fn (x: int) int, x: int) => f(x)

// in a literal slot, an operand, and through a parameter's contract
let a = {v: h(3)};
let b = [h(3), g(3)];
[a, b, h(3) + 1, g(3) + 1, run_with(g, 4), run_with(h, 4)]

// the failure is an ordinary contained error
let e = h(3) ^ { ^ };
[e is error, e.code, e.message]
let r = run_with(h, 4);
let q = raw_call(h, 4);
[r is error, q is error, raw_call(g, 5)]
