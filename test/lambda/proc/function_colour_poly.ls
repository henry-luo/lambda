// S12.1.4v2: a `function` declaration is pure iff its `function`-typed
// arguments are. Its value is `fn`; a call's colour is resolved per call, and
// an `fn`-context call whose colour is only known at run time is checked there.
// S12.1.1: an ordinary `fn` never runs a `pn` it receives dynamically.

fn sq(x: int) => x * x
pn logsq(x: int) { print("<pn>"); x * x }

// forward reference: `apply_all` is declared below
fn use_early(xs) => apply_all(sq, xs)

function apply_all(f: function, xs) => for (x in xs) f(x)
// a polymorphic parameter passed through keeps the colour its caller resolved
function twice(f: function, x) => f(f(x))
// `fn (...)` slots keep their fixed colour and do not vote
function mixed(g: fn (int) int, f: function, x) { g(f(x)) }

fn pure_caller(xs) => apply_all(sq, xs)
// the argument's colour is known only at run time here
fn table_caller(table, xs) => apply_all(table.f, xs)
// an ordinary fn calling whatever it is given
fn call_it(f, x) => f(x)
// a `function` value is `fn`, so it may cross an `fn (...)` contract
fn run(h: fn (f, xs) any, f, xs) => h(f, xs)

pn main() {
    print([use_early([1, 2]), pure_caller([3, 4])])
    print("\n")
    // `pn` context may pass a `pn`
    print([apply_all(logsq, [5]), twice(logsq, 2), mixed(sq, logsq, 3)])
    print("\n")
    print([apply_all is fn, apply_all is pn, apply_all is function])
    print("\n")
    // run-time rejections return the error value and never run the `pn`
    print([table_caller({f: sq}, [6]), table_caller({f: logsq}, [7]) is error])
    print("\n")
    print([call_it(sq, 8), call_it(logsq, 9) is error])
    print("\n")
    print([run(apply_all, sq, [10]), run(apply_all, logsq, [11]) is error])
    print("\n")
}
