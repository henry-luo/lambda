// Test `for i in start to end { ... }` statement loops in procedural (pn) code.
// Regression: a for-statement whose body is a pure statement (no value), e.g.
// `arr[i] = v`, returned the invalid-reg sentinel which transpile_for then tried
// to box → "undeclared reg 0" MIR error. Also, a range loop variable used as an
// ANY-typed index against a *generic* array routed to fn_index_assign, which only
// accepted typed numeric arrays. Both are fixed; for-statements with array-index
// assignment now work for typed and generic arrays alike.

// Plain accumulation (value-producing body)
pn t_sum() {
    var s = 0
    for i in 1 to 5 { s = s + i }
    print("sum=" ++ string(s) ++ "\n")
}

// Typed numeric array (fill → ArrayNum), loop var as assignment index
pn t_num_array() {
    var arr = fill(5, 0)
    for i in 0 to 4 { arr[i] = i * i }
    print("num=")
    for i in 0 to 4 {
        if (i > 0) { print(" ") }
        print(string(int(arr[i])))
    }
    print("\n")
}

// Variable loop bound
pn t_var_bound(n) {
    var arr = fill(n, 0)
    for i in 0 to n - 1 { arr[i] = i + 10 }
    print("vbound=")
    for i in 0 to n - 1 {
        if (i > 0) { print(" ") }
        print(string(int(arr[i])))
    }
    print("\n")
}

// Typed float array
pn t_float_array() {
    var arr:float[] = fill(3, 0.0)
    for i in 0 to 2 { arr[i] = i * 1.5 }
    print("float=" ++ string(arr[0]) ++ " " ++ string(arr[1]) ++ " " ++ string(arr[2]) ++ "\n")
}

// Generic array ([null,...]) with range loop var as index (fn_index_assign path)
pn t_generic_array() {
    var arr = [null, null, null]
    for i in 0 to 2 { arr[i] = i * 100 }
    print("generic=" ++ string(int(arr[0])) ++ " " ++ string(int(arr[1])) ++ " " ++ string(int(arr[2])) ++ "\n")
}

// Nested for-statements, flattened index into a typed array
pn t_nested() {
    var arr = fill(9, 0)
    for i in 0 to 2 {
        for j in 0 to 2 { arr[i * 3 + j] = i * 3 + j }
    }
    print("nested=")
    for k in 0 to 8 {
        if (k > 0) { print(" ") }
        print(string(int(arr[k])))
    }
    print("\n")
}

// for-statement body with no array write at all (pure side-effecting print)
pn t_void_body() {
    print("void=")
    for i in 1 to 3 { print(string(i)) }
    print("\n")
}

pn main() {
    t_sum()
    t_num_array()
    t_var_bound(5)
    t_float_array()
    t_generic_array()
    t_nested()
    t_void_body()
}
