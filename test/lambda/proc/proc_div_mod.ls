// Test div and % operators in pn mode
// Issue #2: div operator returned <error> instead of integer in pn functions

pn main() {
    // Basic div with literals
    print("D1:")
    print(97 div 4)       // 24
    print(" D2:")
    print(10 div 3)       // 3
    print(" D3:")
    print(100 div 7)      // 14
    print(" D4:")
    print(15 div 5)       // 3
    print(" D5:")
    print(0 div 5)        // 0

    // Basic mod with literals
    print(" M1:")
    print(97 % 4)         // 1
    print(" M2:")
    print(10 % 3)         // 1
    print(" M3:")
    print(100 % 7)        // 2
    print(" M4:")
    print(15 % 5)         // 0

    // div with variables
    var a = 100
    var b = 7
    print(" V1:")
    print(a div b)        // 14

    // div result assigned to variable
    var q = a div b
    print(" V2:")
    print(q)              // 14

    // mod with variables
    var r = a % b
    print(" V3:")
    print(r)              // 2

    // reassignment with div
    var x = 50
    x = x div 3
    print(" V4:")
    print(x)              // 16

    // quotient-remainder identity
    var n = 97
    var d = 4
    var quot = n div d
    var rem = n % d
    print(" V5:")
    print(quot * d + rem) // 97

    // Float modulo (Issue #7)
    print(" F1:")
    print(10.5 % 3)       // 1.5
    print(" F2:")
    print(17.0 % 5)       // 2
    print(" F3:")
    print(10 % 3.5)       // 3
    print(" F4:")
    print(7.5 % 2.5)      // 0
    var fa = 10.5
    var fb = 4
    print(" F5:")
    print(fa % fb)         // 2.5
}
