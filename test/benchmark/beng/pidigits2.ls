// BENG Benchmark: pidigits (Typed version)
// Compute digits of Pi using the unbounded spigot algorithm (Gibbons 2004)
// Uses integer values with n suffix for unlimited precision arithmetic
// n=30 expected: "3141592653\t:10\n5897932384\t:20\n6264338327\t:30\n"

let NUM_DIGITS = 30

// integer division for decimals: floor(a/b) = (a - a % b) / b
pn idiv(a: decimal, b: decimal) decimal {
    return (a - a % b) / b
}

pn main() {
    var __t0 = clock()
    // LFT state: (q, r, s, t) as arbitrary-precision decimals
    var q: decimal = 1n
    var r: decimal = 0n
    var s: decimal = 0n
    var t: decimal = 1n
    var k: decimal = 0n
    var i: int = 0
    var digits: string = ""

    while (i < NUM_DIGITS) {
        k = k + 1n
        var k2: decimal = k * 2n + 1n

        // compose: multiply LFT by next term
        var new_q: decimal = q * k
        var new_r: decimal = (2n * q + r) * k2
        var new_s: decimal = s * k
        var new_t: decimal = (2n * s + t) * k2
        q = new_q
        r = new_r
        s = new_s
        t = new_t

        // can we extract a digit?
        if (q <= r) {
            var fd3: decimal = idiv(3n * q + r, 3n * s + t)
            var fd4: decimal = idiv(4n * q + r, 4n * s + t)
            if (fd3 == fd4) {
                digits = digits ++ fd3
                i = i + 1

                // output 10 digits per line
                if (i % 10 == 0) {
                    print(digits ++ "\t:" ++ i ++ "\n")
                    digits = ""
                }

                // reduce: eliminate the extracted digit
                r = (r - fd3 * t) * 10n
                q = q * 10n
            }
        }
    }

    // handle last partial line (fewer than 10 digits)
    if (len(digits) > 0) {
        // pad with spaces to 10 characters
        while (len(digits) < 10) {
            digits = digits ++ " "
        }
        print(digits ++ "\t:" ++ i ++ "\n")
    }
    var __t1 = clock()
    print("__TIMING__:" ++ ((__t1 - __t0) * 1000.0) ++ "\n")
}
