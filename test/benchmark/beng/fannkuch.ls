// BENG Benchmark: fannkuch-redux
// For each of N! permutations of {0..N-1}, repeatedly reverse the first
// perm[0]+1 elements until perm[0]==0. Track max flips and checksum.
// N=7 expected: "228\nPfannkuchen(7) = 16\n"

let N = 7

pn fannkuch(n) {
    var perm = fill(n, 0)
    var perm1 = fill(n, 0)
    var count = fill(n, 0)
    var max_flips = 0
    var checksum = 0
    var perm_count = 0

    // initialize perm1 = [0, 1, ..., n-1]
    var i = 0
    while (i < n) {
        perm1[i] = i
        i = i + 1
    }

    var r = n
    var running = 1
    while (running == 1) {
        // set count values for current r
        while (r != 1) {
            count[r - 1] = r
            r = r - 1
        }

        // copy perm1 to perm
        i = 0
        while (i < n) {
            perm[i] = perm1[i]
            i = i + 1
        }

        // count flips
        var flips = 0
        var k = perm[0]
        while (k != 0) {
            var lo = 0
            var hi = k
            while (lo < hi) {
                var tmp = perm[lo]
                perm[lo] = perm[hi]
                perm[hi] = tmp
                lo = lo + 1
                hi = hi - 1
            }
            flips = flips + 1
            k = perm[0]
        }

        if (flips > max_flips) {
            max_flips = flips
        }
        if (perm_count % 2 == 0) {
            checksum = checksum + flips
        } else {
            checksum = checksum - flips
        }
        perm_count = perm_count + 1

        // generate next permutation
        var found = 0
        while (found == 0 and running == 1) {
            if (r == n) {
                running = 0
            } else {
                var perm0 = perm1[0]
                i = 0
                while (i < r) {
                    perm1[i] = perm1[i + 1]
                    i = i + 1
                }
                perm1[r] = perm0
                count[r] = count[r] - 1
                if (count[r] > 0) {
                    found = 1
                } else {
                    r = r + 1
                }
            }
        }
    }

    print(string(checksum) ++ "\n")
    print("Pfannkuchen(" ++ string(n) ++ ") = " ++ string(max_flips) ++ "\n")
}

pn main() {
    fannkuch(N)
}
