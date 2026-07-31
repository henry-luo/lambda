// ER-S4: equality-depth exhaustion is a static C14 fault in a local pn handler.
fn deep(n) => if (n == 0) [] else [deep(n - 1)]

pn main() {
    let value^err = deep(300) == deep(300)
    print([value, ^err, err.code, err.message])
}
