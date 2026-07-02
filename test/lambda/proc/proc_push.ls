// Test the built-in `push(arr, val)` — append to a growable array in place
// (amortized O(1) via doubling). Replaces the chunked-vector + `.sz` workaround:
// `push` grows the array, `len` gives the logical size, `arr[i]` indexes directly.

// Grow an empty array and read back
pn t_grow() {
    var v = []
    for i in 0 to 9 { push(v, i * i) }
    print("grow: len=" ++ (len(v)) ++ " v0=" ++ (int(v[0])) ++ " v9=" ++ (int(v[9])) ++ "\n")
}

// push maps/objects, read fields back
pn t_maps() {
    var w = []
    push(w, {name: "a", val: 10})
    push(w, {name: "b", val: 20})
    push(w, {name: "c", val: 30})
    print("maps: len=" ++ (len(w)) ++ " w1.name=" ++ (w[1]).name ++ " w2.val=" ++ (int((w[2]).val)) ++ "\n")
}

// push an array value — appended as a single element (no flattening)
pn t_nested() {
    var z = []
    push(z, [1, 2, 3])
    push(z, [4, 5])
    print("nested: len=" ++ (len(z)) ++ " z0.len=" ++ (len(z[0])) ++ " z1.len=" ++ (len(z[1])) ++ "\n")
}

// growable array stored in a map field (chunked-vector replacement pattern)
pn vadd(v, x) {
    push(v.data, x)
    return 0
}
pn t_field() {
    var vec = { data: [], first: 0 }
    for i in 0 to 4 { vadd(vec, i + 100) }
    print("field: len=" ++ (len(vec.data)) ++ " d0=" ++ (int((vec.data)[0])) ++ " d4=" ++ (int((vec.data)[4])) ++ "\n")
}

// many appends (stress the doubling growth)
pn t_stress() {
    var v = []
    for i in 0 to 999 { push(v, i) }
    var sum = 0
    for i in 0 to 999 { sum = sum + int(v[i]) }
    print("stress: len=" ++ (len(v)) ++ " sum=" ++ (sum) ++ "\n")
}

pn main() {
    t_grow()
    t_maps()
    t_nested()
    t_field()
    t_stress()
}
