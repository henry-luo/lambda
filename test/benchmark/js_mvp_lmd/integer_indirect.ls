// Unannotated port of the same indirect call and accumulation workload.
pn increment(x) { return x + 1 }
pn work() {
    let callback = increment
    var sum = 0
    var i = 0
    while (i < 200000) {
        sum = sum + callback(i)
        i = i + 1
    }
    return sum
}
pn main() {
    let start = clock()
    let result = work()
    let elapsed = (clock() - start) * 1000.0
    if (result == 20000100000) {
        print("integer_indirect: PASS\n")
    } else {
        print("integer_indirect: FAIL\n")
    }
    print("__TIMING__:" ++ elapsed ++ "\n")
}
