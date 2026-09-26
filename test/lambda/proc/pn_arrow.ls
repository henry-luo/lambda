// S16.6.7v2: the procedure arrow `pn (x) => { ... }` is the anonymous
// procedure -- a nested `pn` without its name. Its body is the braced statement
// block every `pn` takes; its value is a `pn`; it captures by snapshot (S9.1.4)
// and may be created in any context, while only `pn` context may call it.

function apply_all(f: function, xs) => for (x in xs) f(x)
pn apply_pn(f: pn (x: int), v: int) { f(v) }
fn apply_fn(f, v) => f(v)
fn make_greeter(greeting) => pn (who) => { print(greeting ++ ", " ++ who ++ "\n") }

// created at the top level, which is `fn` context
let top = pn (x) => { print("top " ++ string(x) ++ "\n") }

pn main() {
    // colour and the empty body
    let empty = pn () => {}
    print([empty is pn, empty is fn, empty is function, empty()])
    print("\n")

    // the body is a procedure's statement block
    let k = 5
    let first_three = pn (n: int) int => {
        var acc = 0
        var i = 0
        while (i < n) {
            if (i == 3) { break }
            acc = acc + i + k
            i = i + 1
        }
        return acc
    }
    print(first_three(10))
    print("\n")

    // a rest parameter, and one closure per loop iteration
    let count = pn (...) => { len(varg()) }
    var total = 0
    for (x in [1, 2, 3]) {
        let tenfold = pn (y) => { y * 10 }
        total = total + tenfold(x)
    }
    print([count(1, 2, 3), count(), total])
    print("\n")

    // a `function` parameter takes it, and so does a `pn (...)` contract
    apply_all(pn (x) => { print(x) }, [1, 2, 3])
    print("\n")
    apply_pn(pn (x: int) => { print(x * 2) }, 21)
    print("\n")

    // an `fn` refuses to call it: the call returns the error value
    let refused = apply_fn(pn (x) => { print("never") }, 1)
    print(refused is error)
    print("\n")

    // made by an `fn`, nested, and created at the top level
    let greet = make_greeter("hello")
    greet("arrow")
    let outer = pn (a) => {
        let inner = pn (b) => { print(a + b) }
        inner(100)
    }
    outer(1)
    print("\n")
    top(7)

    // a task target, and a raised error through the arrow
    let task = start(pn (n) => { n * 2 }, [21])
    print(wait(task))
    print("\n")
    let check = pn (x: int) int^ => {
        if (x < 0) { raise error("negative") }
        return x + 1
    }
    let ok = check(1)^
    print(ok)
    print("\n")
    check(-5) ^ { print("caught " ++ ^.message ++ "\n") }

    // hot enough to promote under auto
    let square = pn (x) => { x * x }
    var sum = 0
    var j = 0
    while (j < 50) {
        sum = sum + square(j)
        j = j + 1
    }
    print(sum)
    print("\n")
}
