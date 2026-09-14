// Tune27 T27-7: a float literal whose members may be null (out-of-bounds typed
// reads) builds one packed array with direct stores; a member that IS null
// republishes the literal as generic storage so a later boundary still sees
// the null (D3.2.2, S7.1.3v2).
pn tune27_take(v: float[]) float { return v[0] + v[1] }

pn tune27_float_literal_nullable(q: float[], i: int, j: int) any {
    let packed = [q[i], q[j]]
    return packed
}

// a NaN member is not the null payload: the literal stays packed
pn tune27_float_literal_nan(q: float[], i: int) any {
    let zero: float = q[i] - q[i]
    let packed = [q[i], zero / zero]
    return packed
}

// a non-float member beside a nullable float keeps generic storage: the
// packed path would coerce the bool through it2d
pn tune27_float_literal_mixed(q: float[], i: int) any {
    let mixed = [q[i], q[i] == null, i]
    return mixed
}

pn main() {
    var q: float[] = [1.5, 2.5, 3.5]
    var total: float = 0.0
    var k: int = 0
    while (k < 2) {
        total = total + tune27_take([q[k], q[k + 1]])
        k = k + 1
    }
    print(string(total) ++ "\n")
    print(tune27_float_literal_nullable(q, 0, 2))
    print("\n")
    print(tune27_float_literal_nullable(q, 1, 7))
    print("\n")
    let with_nan = tune27_float_literal_nan(q, 2)
    print([type(with_nan), with_nan[0], with_nan[1]])
    print("\n")
    // a nullable member that holds a real NaN also keeps the packed literal
    var qn: float[] = [1.0, 2.0]
    qn[0] = with_nan[1]
    let lane_nan = tune27_float_literal_nullable(qn, 0, 1)
    print([type(lane_nan), lane_nan[0], lane_nan[1]])
    print("\n")
    print([tune27_float_literal_mixed(q, 0), tune27_float_literal_mixed(q, 7)])
    print("\n")
}
