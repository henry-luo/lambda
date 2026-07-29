// BENG Benchmark: spectral-norm (Typed version)
// Eigenvalue approximation via power method on matrix A
// where A(i,j) = 1.0 / ((i+j)*(i+j+1)/2 + i + 1)
// N=100 expected: "1.274219991\n"

let N = 100

// NOTE: no `string` return-type annotation on format9 — a string return type on a
// pn currently segfaults the MIR JIT when the result spans a GC
// (repro: temp/repro_string_return_segv.ls)
// format a float to exactly 9 decimal places
pn format9(x: float) {
    // neg/result stay unannotated: `neg ++ int_part` mixes a string with an int, and
    // a declared string operand routes the concat to fn_strcat, which rejects the int
    var neg = ""
    if (x < 0.0) {
        neg = "-"
        x = -x
    }
    var int_part: int = int(floor(x))
    var frac: float = x - float(int_part)
    var scaled: float = frac * 1000000000.0 + 0.5
    var fl: float = floor(scaled)
    var frac_int: int = int(fl)
    if (frac_int >= 1000000000) {
        int_part = int_part + 1
        frac_int = 0
    }
    var frac_str = string(frac_int)
    // pad stays unannotated: `var pad: int = <expr containing len(str)>` currently
    // miscompiles in the MIR JIT and corrupts the string concat below
    // (repro: temp/repro_declared_int_len_concat.ls)
    var pad = 9 - len(frac_str)
    var prefix = ""
    while (pad > 0) {
        prefix = prefix ++ "0"
        pad = pad - 1
    }
    frac_str = prefix ++ frac_str
    var result = neg ++ int_part ++ "." ++ frac_str
    return result
}

pn eval_A(i: int, j: int) float {
    return 1.0 / float((i + j) * (i + j + 1) / 2 + i + 1)
}

pn mul_Av(n: int, v: float[], av: float[]) {
    var i: int = 0
    while (i < n) {
        var s: float = 0.0
        var j: int = 0
        while (j < n) {
            s = s + eval_A(i, j) * v[j]
            j = j + 1
        }
        av[i] = s
        i = i + 1
    }
}

pn mul_Atv(n: int, v: float[], atv: float[]) {
    var i: int = 0
    while (i < n) {
        var s: float = 0.0
        var j: int = 0
        while (j < n) {
            s = s + eval_A(j, i) * v[j]
            j = j + 1
        }
        atv[i] = s
        i = i + 1
    }
}

pn mul_AtAv(n: int, v: float[], out: float[]) {
    var tmp = fill(n, 0.0)
    mul_Av(n, v, tmp)
    mul_Atv(n, tmp, out)
}

pn main() {
    var __t0 = clock()
    var u = fill(N, 1.0)
    var v = fill(N, 0.0)

    var i: int = 0
    while (i < 10) {
        mul_AtAv(N, u, v)
        mul_AtAv(N, v, u)
        i = i + 1
    }

    var vBv: float = 0.0
    var vv: float = 0.0
    i = 0
    while (i < N) {
        vBv = vBv + u[i] * v[i]
        vv = vv + v[i] * v[i]
        i = i + 1
    }

    print(format9(math.sqrt(vBv / vv)) ++ "\n")
    var __t1 = clock()
    print("__TIMING__:" ++ ((__t1 - __t0) * 1000.0) ++ "\n")
}
