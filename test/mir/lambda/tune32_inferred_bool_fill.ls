// D3.3.3v3: an inferred bool fill owns a lane witness, not a bool[] contract.
pn tune32_bool_hot(n: int) {
    var flags = fill(n, true)
    var i = 0
    var count = 0
    while (i < n) {
        if (flags[i]) { count = count + 1 }
        flags[i] = false
        i = i + 1
    }
    print(count); print(" "); print(flags[n]); print("\n")
}
pn tune32_bool_fill(n: int) {
    var flags = fill(n, true)
    var i = 0
    while (i < n) {
        flags[i] = false
        i = i + 1
    }
    print(flags); print(" "); print(flags[-1]); print(" "); print(flags[n]); print("\n")
}

pn tune32_bool_plain(flags) { flags[0] = false }
pn tune32_bool_borrow(var flags) { flags[0] = false }
pn tune32_bool_oob(n: int) any^ {
    var flags = fill(n, true)
    flags[n] = false
}
pn tune32_bool_mixed() {
    var flags = fill(2, true)
    flags[0] = 7
    print(flags); print("\n")
    flags = ["x", null]
    print(flags[0]); print(" "); print(flags[1]); print("\n")
}

pn main() {
    tune32_bool_hot(4)
    tune32_bool_hot(0)
    tune32_bool_fill(4)
    tune32_bool_fill(0)
    var flags = fill(2, true)
    var snapshot = flags
    tune32_bool_plain(flags)
    print(flags); print(" ")
    tune32_bool_borrow(flags)
    print(flags); print(" "); print(snapshot); print("\n")
    var failed = false
    tune32_bool_oob(0) ^ { failed = true }
    print(failed); print(" "); print(fill(-1, true)); print("\n")
    tune32_bool_mixed()
}
