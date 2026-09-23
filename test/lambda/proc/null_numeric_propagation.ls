// S7.1.1v3 / S7.10.5v3: null propagates through scalar arithmetic and the
// numeric functions -- `math.sqrt(null)`, `abs(null)`, `-null` and
// `min(null, 1)` are null, as `null + 1` is (both tiers returned error for the
// functions). The JIT's native libm and unary lowerings also computed on the
// null sentinel's bits: NaN from a `float[]` read out of range, and from an
// `int[]` one the routine of 0.0 (`math.sqrt` gave 0, `-a[i]` gave -inf,
// `a[i] / 2` gave 0).

fn dyn(v) => v

fn floats(v: float[], i: int) {
    [math.sqrt(v[i]), math.sin(v[i]), math.hypot(v[i], 1.0), math.atan2(1.0, v[i]),
     abs(v[i]), round(v[i]), floor(v[i]), ceil(v[i]), trunc(v[i]), sign(v[i]),
     -v[i], +v[i], math.pow(v[i], 2.0), v[i] ** 2.0, min(v[i], 1.0), max(1.0, v[i]),
     v[i] + 1.0, 1.5 - v[i], v[i] / 2.0]
}

fn ints(a: int[], i: int) {
    [math.sqrt(a[i]), math.hypot(a[i], 1), abs(a[i]), floor(a[i]), -a[i],
     min(a[i], 1), a[i] + 1, a[i] * 2, a[i] / 2, a[i] div 2, a[i] % 2]
}

fn opt(x: float?) { [math.sqrt(x), abs(x), floor(x), -x, x + 1.0] }
fn opti(x: int?) { [math.sqrt(x), abs(x), floor(x), -x, x + 1, x / 2] }

// the Tune30 libm probe: a float accumulator over a float[] row
pn norms(v: float[], n: int) float {
    var total: float = 0.0
    var i: int = 0
    while (i < n) {
        total = total + math.sqrt(v[i]) + math.sin(v[i])
        i = i + 1
    }
    return total
}

pn main() {
    // boxed calls on a dynamic null
    let z = dyn(null)
    print([math.sqrt(z), math.log1p(z), abs(z), round(z), sign(z), math.pow(z, 2),
        math.pow(2, z), z ** 2, math.hypot(z, 1), math.atan2(1, z), -z, +z,
        min(z, 3), max(3, z), clip(z, 0, 1)])
    print("\n")

    // native lanes: in range, then a read out of range
    print(floats([4.0, 9.0], 0))
    print("\n")
    print(floats([4.0, 9.0], 5))
    print("\n")
    print(ints([4, 9], 1))
    print("\n")
    print(ints([4, 9], 5))
    print("\n")
    print([opt(4.0), opt(null), opti(4), opti(null)])
    print("\n")

    // a null result keeps its null where it is printed, tested or passed on
    let v: float[] = [4.0]
    print([v[3] + 1.0, type(v[3] + 1.0), (v[3] + 1.0) == null, dyn(math.sqrt(v[3]))])
    print("\n")

    // in range the probe sums; out of range the sum is null, which the
    // `float` accumulator rejects (S7.7.4), so the call yields that error
    print(norms([1.0, 4.0, 9.0, 16.0], 4))
    print("\n")
    print(norms([1.0, 4.0, 9.0, 16.0], 5) is error)
    print("\n")
}
