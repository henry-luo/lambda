// Test: pn function parameters are mutable (can be reassigned)
// fn function parameters remain immutable

// Case 1: simple scalar param reassignment
pn swap_and_print(x, y) {
    let temp = x
    x = y
    y = temp
    print(string(x) ++ " " ++ string(y) ++ "\n")
}

// Case 2: counter pattern - decrement param
pn countdown(n) {
    while (n > 0) {
        print(string(n) ++ "\n")
        n = n - 1
    }
    print("done\n")
}

// Case 3: accumulator pattern
pn accumulate(acc, items) {
    var i = 0
    while (i < len(items)) {
        acc = acc + items[i]
        i = i + 1
    }
    print(string(acc) ++ "\n")
}

// Case 4: string param mutation
pn build_greeting(name) {
    name = "Hello, " ++ name ++ "!"
    print(name ++ "\n")
}

// Case 5: param used as loop variable
pn find_first_positive(arr, idx) {
    while (idx < len(arr) and arr[idx] <= 0) {
        idx = idx + 1
    }
    if (idx < len(arr)) { print(string(arr[idx]) ++ "\n") }
    else { print("none\n") }
}

pn main() {
    swap_and_print(1, 2)
    countdown(3)
    accumulate(0, [10, 20, 30])
    build_greeting("World")
    find_first_positive([-1, -2, 3, 4], 0)
}
