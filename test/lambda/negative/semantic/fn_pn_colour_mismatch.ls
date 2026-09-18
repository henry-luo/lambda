// S11.1.5 / S12.1.4v2(3): a statically-known colour that does not match an
// `fn`/`pn` parameter contract is a compile error, not a runtime surprise.
fn sq(x: int) => x * x
pn logsq(x: int) { x * x }
fn apply_fn(f: fn (int) int, x: int) => f(x)
pn apply_pn(f: pn (int) int, x: int) { f(x) }

pn main() {
    print(apply_fn(logsq, 8))
    print(apply_pn(sq, 7))
}
