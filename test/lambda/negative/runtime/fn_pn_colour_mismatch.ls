// S11.1.5: when the colour is only known at run time (here, behind a map
// field), the parameter boundary still refuses a `pn` at an `fn` contract.
fn sq(x: int) => x * x
pn logsq(x: int) { x * x }
fn apply_fn(f: fn (int) int, x: int) => f(x)

pn main() {
    let table = {pure: sq, proc: logsq}
    print(apply_fn(table.pure, 3))
    print(apply_fn(table.proc, 8))
}
