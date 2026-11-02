fn invoke_fn(f) {
    var i = 0
    while (i < 2) { i = i + 1 }
    return f()
}
pn forbidden() { print("must not execute"); return 1 }
pn main() { invoke_fn(forbidden) }
