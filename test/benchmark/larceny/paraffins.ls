// Larceny Benchmark: paraffins
// Count distinct paraffin isomers C_n H_{2n+2} using radical counting
// Adapted from the classic Larceny/Gabriel "paraffins" benchmark
//
// Uses a counting-only approach: instead of building explicit tree structures,
// counts radicals at each size and computes multiset combinations.
//
// A paraffin (alkane) is either:
//   BCP (bond-centered): 2 radicals of size n/2 joined at a bond (even n only)
//   CCP (carbon-centered): 4 sub-radicals joined at a central carbon
//
// A radical of size k: central carbon + 3 sub-radicals summing to k-1
//
// Computes nb(n) for n=1..23, repeated 10 times for timing
// Expected: matches OEIS A000602
//           nb(17) = 24894, nb(23) = 5731580

// Multiset coefficients: choose k items from n with repetition
pn ms2(r) {
    return r * (r + 1) div 2
}

pn ms3(r) {
    return r * (r + 1) * (r + 2) div 6
}

pn ms4(r) {
    return r * (r + 1) * (r + 2) * (r + 3) div 24
}

// Count radicals of size k given rcount[] (radical counts for smaller sizes)
// A radical: central C + 3 sub-radicals of sizes s1 <= s2 <= s3, s1+s2+s3 = k-1
pn count_radicals(rcount, k) {
    var count = 0
    var target = k - 1
    var nc1 = 0
    while (nc1 * 3 <= target) {
        var nc2 = nc1
        while (nc1 + nc2 * 2 <= target) {
            var nc3 = target - nc1 - nc2
            if (nc3 >= nc2) {
                var r1 = rcount[nc1]
                var r2 = rcount[nc2]
                var r3 = rcount[nc3]

                if (nc1 == nc2 and nc2 == nc3) {
                    count = count + ms3(r1)
                }
                if (nc1 == nc2 and nc2 != nc3) {
                    count = count + ms2(r1) * r3
                }
                if (nc1 != nc2 and nc2 == nc3) {
                    count = count + r1 * ms2(r2)
                }
                if (nc1 != nc2 and nc2 != nc3) {
                    count = count + r1 * r2 * r3
                }
            }
            nc2 = nc2 + 1
        }
        nc1 = nc1 + 1
    }
    return count
}

// BCP: bond-centered paraffins (even n only)
// Two radicals of size n/2, unordered pair => ms2(rcount[n/2])
pn count_bcp(rcount, n) {
    if (n % 2 != 0) {
        return 0
    }
    var half = shr(n, 1)
    var r = rcount[half]
    return ms2(r)
}

// CCP: carbon-centered paraffins
// 4 sub-radicals of sizes s1 <= s2 <= s3 <= s4, sum = n-1
// max_rad = floor((n-1)/2) ensures no overlap with BCP and no out-of-bounds
pn count_ccp(rcount, n) {
    var m = n - 1
    var max_rad = shr(m, 1)
    var count = 0
    var nc1 = 0
    while (nc1 * 4 <= m) {
        var nc2 = nc1
        while (nc1 + nc2 * 3 <= m) {
            var remain = m - nc1 - nc2
            var nc3 = nc2
            while (nc3 * 2 <= remain) {
                var nc4 = remain - nc3
                if (nc4 >= nc3 and nc4 <= max_rad) {
                    var r1 = rcount[nc1]
                    var r2 = rcount[nc2]
                    var r3 = rcount[nc3]
                    var r4 = rcount[nc4]

                    if (nc1 == nc2 and nc2 == nc3 and nc3 == nc4) {
                        count = count + ms4(r1)
                    }
                    if (nc1 == nc2 and nc2 == nc3 and nc3 != nc4) {
                        count = count + ms3(r1) * r4
                    }
                    if (nc1 != nc2 and nc2 == nc3 and nc3 == nc4) {
                        count = count + r1 * ms3(r2)
                    }
                    if (nc1 == nc2 and nc2 != nc3 and nc3 == nc4) {
                        count = count + ms2(r1) * ms2(r3)
                    }
                    if (nc1 == nc2 and nc2 != nc3 and nc3 != nc4) {
                        count = count + ms2(r1) * r3 * r4
                    }
                    if (nc1 != nc2 and nc2 == nc3 and nc3 != nc4) {
                        count = count + r1 * ms2(r2) * r4
                    }
                    if (nc1 != nc2 and nc2 != nc3 and nc3 == nc4) {
                        count = count + r1 * r2 * ms2(r3)
                    }
                    if (nc1 != nc2 and nc2 != nc3 and nc3 != nc4) {
                        count = count + r1 * r2 * r3 * r4
                    }
                }
                nc3 = nc3 + 1
            }
            nc2 = nc2 + 1
        }
        nc1 = nc1 + 1
    }
    return count
}

pn nb(n) {
    if (n < 1) {
        return 0
    }
    var half = shr(n, 1)
    var rcount = fill(half + 1, 0)
    rcount[0] = 1

    var k = 1
    while (k <= half) {
        rcount[k] = count_radicals(rcount, k)
        k = k + 1
    }

    var bcp = count_bcp(rcount, n)
    var ccp = count_ccp(rcount, n)
    return bcp + ccp
}

pn main() {
    var __t0 = clock()
    var result = 0
    var iter = 0
    while (iter < 10) {
        var n = 1
        while (n <= 23) {
            result = nb(n)
            n = n + 1
        }
        iter = iter + 1
    }
    var __t1 = clock()

    // result = nb(23)
    print("paraffins: nb(23) = " ++ string(result) ++ "\n")
    if (result == 5731580) {
        print("paraffins: PASS\n")
    } else {
        print("paraffins: FAIL (expected 5731580)\n")
    }
    print("__TIMING__:" ++ string((__t1 - __t0) * 1000.0) ++ "\n")
}
