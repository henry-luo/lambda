// Test the built-in `splice(arr, start, count)` — remove `count` elements starting at
// `start` from a growable array, IN PLACE (shift tail down + shrink length). Together
// with push/len it gives pop / dequeue / middle-removal without a chunked + .sz wrapper.

pn join_ints(v) {
    var s = ""
    var i = 0
    while (i < len(v)) {
        if (i > 0) { s = s ++ "," }
        s = s ++ (int(v[i]))
        i = i + 1
    }
    return s
}

// In-place mutation through a function boundary (the property deltablue relies on)
pn dequeue(v) {
    if (len(v) == 0) { return null }
    var first = v[0]
    splice(v, 0, 1)
    return first
}

pn remove_by_id(v, id) {
    var i = 0
    var found = -1
    while (i < len(v)) {
        if (found == -1) {
            if ((v[i]).id == id) { found = i }
        }
        i = i + 1
    }
    if (found == -1) { return 0 }
    splice(v, found, 1)
    return 1
}

pn main() {
    // numeric (ArrayNum) array
    var a = [10, 20, 30, 40, 50]
    splice(a, 0, 1)                 // dequeue -> 20..50
    print("dequeue:" ++ join_ints(a) ++ "\n")
    splice(a, len(a) - 1, 1)        // pop -> 20,30,40
    print("pop:" ++ join_ints(a) ++ "\n")
    splice(a, 1, 1)                 // remove middle -> 20,40
    print("mid:" ++ join_ints(a) ++ "\n")

    var b = [1, 2, 3, 4, 5, 6]
    splice(b, 2, 3)                 // remove 3 at idx2 -> 1,2,6
    print("multi:" ++ join_ints(b) ++ "\n")

    var c = [1, 2, 3]
    splice(c, 1, 99)               // over-count clamp -> 1
    print("clamp:" ++ join_ints(c) ++ "\n")

    var d = [1, 2, 3, 4]
    splice(d, -1, 1)               // negative start -> 1,2,3
    print("neg:" ++ join_ints(d) ++ "\n")

    var e = [5]
    splice(e, 0, 0)                // count 0 = no-op -> 5
    print("noop:" ++ join_ints(e) ++ "\n")

    // grow with push, then drain with splice through a function
    var q = []
    push(q, 7)
    push(q, 8)
    push(q, 9)
    var f1 = dequeue(q)
    print("drain:" ++ (int(f1)) ++ "|" ++ join_ints(q) ++ "\n")

    // generic array of objects: by-id middle removal
    var v = []
    push(v, {id: 1})
    push(v, {id: 2})
    push(v, {id: 3})
    remove_by_id(v, 2)
    print("obj-remove: len=" ++ (len(v)) ++ " ids=" ++ (int((v[0]).id)) ++ "," ++ (int((v[1]).id)) ++ "\n")

    // setter whose body is a bare index-assignment (returns null, must not MIR-crash)
    var g = [0, 0, 0]
    set_at(g, 1, 42)
    print("set:" ++ join_ints(g) ++ "\n")
}

pn set_at(v, idx, item) {
    v[idx] = item
}
