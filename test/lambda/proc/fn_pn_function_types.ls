// S11.1.5: `fn` and `pn` types are disjoint by effect bit; `function` is their
// union. Regression guards, all of which failed before 2026-09-18:
//  * a `pn` value satisfied `is fn` and `fn (...)` contracts (S12.1.1 promise);
//  * `pn` was rejected in type position ("expected a type pattern");
//  * a system procedure reference (`cancel`) answered `is fn`;
//  * calling through a `function`-typed parameter crashed the compiler, which
//    read the signature-less TYPE_FUNC singleton as a full TypeFunc.

fn sq(x: int) => x * x
pn logsq(x: int) { x * x }

fn colour(f) => match f {
    case fn: "fn"
    case pn: "pn"
    default: "other"
}

type PureUnary = fn (int) int
type ProcUnary = pn (int) int

fn apply_fn(f: fn (int) int, x: int) => f(x)
pn apply_pn(f: pn (int) int, x: int) { f(x) }
pn apply_any(f: function, x: int) { f(x) }

pn main() {
    let anon = (x) => x
    print([sq is fn, sq is pn, sq is function])
    print("\n")
    print([logsq is fn, logsq is pn, logsq is function])
    print("\n")
    print([anon is fn, anon is pn])
    print("\n")
    print([sq is fn (int) int, logsq is fn (int) int, logsq is pn (int) int])
    print("\n")
    print([sq is PureUnary, logsq is PureUnary, logsq is ProcUnary])
    print("\n")
    print([cancel is pn, cancel is fn, len is fn, len is pn])
    print("\n")
    print([colour(sq), colour(logsq), colour(1)])
    print("\n")
    print([1 is fn, 1 is pn, 1 is function])
    print("\n")
    // matching colours are admitted; `function` admits both
    print([apply_fn(sq, 3), apply_pn(logsq, 4), apply_any(sq, 5), apply_any(logsq, 6)])
    print("\n")
}
