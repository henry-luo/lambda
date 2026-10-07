// Unannotated port: push appends where JS writes at the current array length.
pn work() {
    var a = []
    var i = 0
    while (i < 20000) {
        push(a, i)
        i = i + 1
    }
    var sum = 0
    var pass = 0
    while (pass < 50) {
        i = 0
        while (i < len(a)) {
            sum = sum + a[i]
            i = i + 1
        }
        pass = pass + 1
    }
    return sum
}
pn main() {
    let start = clock()
    let result = work()
    let elapsed = (clock() - start) * 1000.0
    if (result == 9999500000) {
        print("integer_dense: PASS\n")
    } else {
        print("integer_dense: FAIL\n")
    }
    print("__TIMING__:" ++ elapsed ++ "\n")
}
