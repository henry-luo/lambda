// BENG Benchmark: spectral-norm
// Eigenvalue approximation via power method on matrix A
// where A(i,j) = 1.0 / ((i+j)*(i+j+1)/2 + i + 1)
// N=100 expected: "1.274219991\n"

let N = 100

// format a float to exactly 9 decimal places
pn format9(x) {
    var v = x
    var neg = ""
    if (v < 0.0) {
        neg = "-"
        v = 0.0 - v
    }
    var int_part = int(floor(v))
    var frac = v - float(int_part)
    var scaled = frac * 1000000000.0 + 0.5
    var fl = floor(scaled)
    var frac_int = int(fl)
    if (frac_int >= 1000000000) {
        int_part = int_part + 1
        frac_int = 0
    }
    var frac_str = string(frac_int)
    var pad = 9 - len(frac_str)
    var prefix = ""
    while (pad > 0) {
        prefix = prefix ++ "0"
        pad = pad - 1
    }
    frac_str = prefix ++ frac_str
    var result = neg ++ string(int_part) ++ "." ++ frac_str
    return result
}

pn eval_A(i, j) {
    return 1.0 / float((i + j) * (i + j + 1) / 2 + i + 1)
}

pn mul_Av(n, v, av) {
    var i = 0
    while (i < n) {
        var s = 0.0
        var j = 0
        while (j < n) {
            s = s + eval_A(i, j) * v[j]
            j = j + 1
        }
        av[i] = s
        i = i + 1
    }
}

pn mul_Atv(n, v, atv) {
    var i = 0
    while (i < n) {
        var s = 0.0
        var j = 0
        while (j < n) {
            s = s + eval_A(j, i) * v[j]
            j = j + 1
        }
        atv[i] = s
        i = i + 1
    }
}

pn mul_AtAv(n, v, out) {
    var tmp = fill(n, 0.0)
    mul_Av(n, v, tmp)
    mul_Atv(n, tmp, out)
}

pn main() {
    var __t0 = clock()
    var u = fill(N, 1.0)
    var v = fill(N, 0.0)

    var i = 0
    while (i < 10) {
        mul_AtAv(N, u, v)
        mul_AtAv(N, v, u)
        i = i + 1
    }

    var vBv = 0.0
    var vv = 0.0
    i = 0
    while (i < N) {
        vBv = vBv + u[i] * v[i]
        vv = vv + v[i] * v[i]
        i = i + 1
    }

    print(format9(math.sqrt(vBv / vv)) ++ "\n")
    var __t1 = clock()
    print("__TIMING__:" ++ string((__t1 - __t0) * 1000.0) ++ "\n")
}
