// S3.4/S3.5: lazy errors skip effects and remain local expression values.
pn observe(var count: int) { count = count + 1; 99 }
pn exercise(value: any) {
    var count = 0
    let a = value or observe(count)
    let b = value and observe(count)
    let c = if (value) observe(count) else observe(count)
    let d = if (value) { observe(count) }
    print([a.message, b.message, c.message, d.message, count])
    print("\n")
    // The failed expressions did not return from this procedure.
    count = count + 1
    print(count)
    print("\n")
}
pn main() {
    exercise(error("retained"))
    var count = 0
    print([true or observe(count), false and observe(count), count])
    print("\n")
}
