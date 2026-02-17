// R7RS Benchmark: nqueens
// Count all solutions to N-Queens problem
// Adapted from r7rs-benchmarks/src/nqueens.scm: nqueens(8) = 92 (scaled down)

pn make_array(n, val) {
    var arr = [val, val, val, val, val, val, val, val, val, val]
    var sz = 10
    while (sz * 2 <= n) {
        arr = arr ++ arr
        sz = sz * 2
    }
    if (sz < n) {
        var remain = n - sz
        var extra = [val]
        var esz = 1
        while (esz < remain) {
            extra = extra ++ [val]
            esz = esz + 1
        }
        arr = arr ++ extra
    }
    return arr
}

// Use arrays to represent lists of candidates
// board[i] = column placed in row i (or -1 if not placed)
// This is a direct translation of the Scheme version's logic

pn ok(row, dist, placed, placed_len) {
    if (dist > placed_len) {
        return 1
    }
    var idx = placed_len - dist
    var p = placed[idx]
    if (p == row + dist) {
        return 0
    }
    if (p == row - dist) {
        return 0
    }
    return ok(row, dist + 1, placed, placed_len)
}

pn solve(candidates, cand_len, rest, rest_len, placed, placed_len) {
    if (cand_len == 0) {
        if (rest_len == 0) {
            return 1
        }
        return 0
    }
    // Try placing candidates[0]
    var row = candidates[0]
    var count = 0

    if (ok(row, 1, placed, placed_len) == 1) {
        // Build new candidate list = rest of candidates + rest
        var new_cands = make_array(cand_len - 1 + rest_len, 0)
        var ni = 0
        var ci = 1
        while (ci < cand_len) {
            new_cands[ni] = candidates[ci]
            ni = ni + 1
            ci = ci + 1
        }
        var ri = 0
        while (ri < rest_len) {
            new_cands[ni] = rest[ri]
            ni = ni + 1
            ri = ri + 1
        }
        // Add row to placed
        placed[placed_len] = row
        count = count + solve(new_cands, ni, make_array(1, 0), 0, placed, placed_len + 1)
        // Undo placement (not strictly necessary since we copy, but clean)
    }

    // Try without placing candidates[0] (move to rest)
    var new_rest = make_array(rest_len + 1, 0)
    var ri2 = 0
    while (ri2 < rest_len) {
        new_rest[ri2] = rest[ri2]
        ri2 = ri2 + 1
    }
    new_rest[rest_len] = row

    var new_cands2 = make_array(cand_len - 1, 0)
    var ci2 = 1
    var ni2 = 0
    while (ci2 < cand_len) {
        new_cands2[ni2] = candidates[ci2]
        ni2 = ni2 + 1
        ci2 = ci2 + 1
    }
    count = count + solve(new_cands2, cand_len - 1, new_rest, rest_len + 1, placed, placed_len)

    return count
}

pn nqueens(n) {
    // Build initial candidate list: [1, 2, ..., n]
    var candidates = make_array(n, 0)
    var i = 0
    while (i < n) {
        candidates[i] = i + 1
        i = i + 1
    }
    var placed = make_array(n, 0)
    var empty = make_array(1, 0)
    return solve(candidates, n, empty, 0, placed, 0)
}

pn benchmark() {
    var result = nqueens(8)
    return result
}

pn main() {
    let result = benchmark()
    if (result == 92) {
        print("nqueens: PASS\n")
    } else {
        print("nqueens: FAIL result=")
        print(result)
        print("\n")
    }
}
