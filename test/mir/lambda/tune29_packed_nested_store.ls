// T29-5: a nested index store tries the packed N-D coordinate store before
// the per-link walk that would build a row view per write.
pn stamp(var m: (int*)*, n: int) any {
    var i: int = 0
    while (i < n) {
        m[i][n - 1 - i] = i + 1
        i = i + 1
    }
}

pn main() {
    var m: (int*)* = [for (k in 0 to 2) fill(3, 0)]
    stamp(m, 3)
    print(m[0][2] ++ " " ++ m[1][1] ++ " " ++ m[2][0] ++ "\n")
}
