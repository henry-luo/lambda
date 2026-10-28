// S1.6, S12.2.1: inferred lanes must survive representation-changing writes.
fn get(xs, i) => xs[i]
fn dyn(x) => x
pn main() {
    var d = [1.5, 2.5]
    d[0] = (7, 8)
    print([d[0], d[1], len(d)])
    print("\n")
    var r: 1 to 5 = 3
    r = 4.0
    print([r, type(r)])
    print("\n")
    var flags = fill(3, true)
    flags[2] = "x"
    print([get(flags, 2), [for (v in flags) v]])
    print("\n")
    var nums = [1, 2, 3]
    nums[2] = "x"
    print([for (k in nums) k])
    print("\n")
    print(get(nums, 2))
    print("\n")
    var n = fill(3, 1)
    n[2] = dyn("y")
    print(n[2])
    print("\n")
    let data = parse("[1.5, 2.25]", 'json')^
    var total = 0
    for (j in 0 to 1) { total = total + data[j] }
    print(total)
    print("\n")
    var t = 0
    for (i in 1 to 2) { let c = t; t = [c, c] }
    print(t)
    print("\n")
    var nested = 0
    for (i in 1 to 2) { nested = [[nested, nested]] }
    print(nested)
    print("\n")
    var z = 0
    for (i in 1 to 2) { z = [z + 1] }
    print(z)
    print("\n")
}
