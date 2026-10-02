// D8.1.1v14 loop-head handoff. Every procedure below owns a top-level
// `while` that runs past LAMBDA_JIT_BACKEDGE (10000), so under AUTO the
// running activation may move to its compiled continuation at a later head
// test. Whether and where it moves depends on worker timing; the output must
// not. Each case is shaped so a wrong handoff changes what it prints. Under
// `LAMBDA_JIT_BACKEDGE=1 LAMBDA_SATELLITE_SYNC=1` every case hands off at
// its second head test, deterministically.

// (a) a container aliased before the loop and written inside it: the alias
// keeps its snapshot (S9.1.2), whichever tier performs the writes
pn alias_write() {
    var a = fill(64, 0)
    var b = a
    var i = 0
    while (i < 30000) {
        b[i % 64] = b[i % 64] + 1
        i = i + 1
    }
    print(a[0]); print(" "); print(b[0]); print(" ")
}

// (b) `return` from inside the loop, `break` and `continue` after the handoff
pn find_first(limit: int) int {
    var i = 0
    while (i < limit) {
        if (i * i > 400000000) { return i }
        i = i + 1
    }
    return -1
}

pn skip_and_stop() int {
    var total = 0
    var i = 0
    while (true) {
        i = i + 1
        if (i % 3 == 0) { continue }
        if (i > 40000) { break }
        total = total + i
    }
    total
}

// (c) a `var` parameter written after the handoff reaches the caller
pn fill_squares(var out: int[], n: int) {
    var i = 0
    while (i < n) {
        out[i % 16] = i * i % 1000
        i = i + 1
    }
}

// (d) the block value is the last value expression, after the loop
pn block_value() {
    var s = 0.0
    var i = 0
    while (i < 25000) {
        s = s + 0.5
        i = i + 1
    }
    s * 2.0
}

// (e) a loop nested in an `if` is not a handoff loop; it still runs
pn nested_in_if(flag: bool) int {
    var c = 0
    if (flag) {
        var i = 0
        while (i < 20000) { c = c + 2; i = i + 1 }
    }
    c
}

// (f) later activations take the published continuation at their first test
pn count_to(n: int) int {
    var k = 0
    var acc = 0
    while (k < n) {
        acc = acc + k % 7
        k = k + 1
    }
    acc
}

pn main() {
    alias_write()
    print(find_first(100000)); print(" ")
    print(skip_and_stop()); print(" ")
    var squares: int[] = fill(16, 0)
    fill_squares(squares, 30000)
    print(squares[3]); print(" "); print(squares[15]); print(" ")
    print(block_value()); print(" ")
    print(nested_in_if(true)); print(" ")
    var j = 0
    var sum = 0
    while (j < 3) {
        sum = sum + count_to(20000)
        j = j + 1
    }
    print(sum)
    print("\n")
}
