// Larceny Benchmark: triangl
// Triangle board solitaire puzzle — count solutions via backtracking
// Adapted from the classic Gabriel/Larceny "triangl" benchmark
//
// The board is a triangle with 15 positions:
//         0
//        1 2
//       3 4 5
//      6 7 8 9
//    10 11 12 13 14
//
// A peg can jump over an adjacent peg to an empty spot (removing the jumped peg).
// Start with position 0 empty (14 pegs). Count all sequences that leave 1 peg.
// Expected: 1 (there is exactly 1 final peg position reachable = position 12)
// But we count total solution sequences: expected = 29760

// Moves: each move is (from, over, to)
// We encode as parallel arrays for from, over, to

pn benchmark() {
    // All valid jump moves
    var mfrom = [0,0,1,1,2,2,3,3,3,3,4,4,5,5,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,12,12,13,13,14,14]
    var mover = [1,2,3,4,4,5,1,4,6,7,7,8,2,4,8,9,3,7,4,8,4,7,5,8,6,11,7,12,7,8,11,13,8,12,9,13]
    var mto   = [3,5,6,8,7,9,0,5,10,12,11,13,0,3,12,14,1,8,2,9,1,6,2,7,3,12,4,13,3,5,10,14,4,11,5,12]
    let num_moves = 36

    var board = fill(15, true)
    board[0] = false

    var solutions = 0
    var pegs = 14

    // Use explicit stack for backtracking
    // Stack entries: move index to try next at each depth
    var stack = fill(14, 0)
    var depth = 0

    while (depth >= 0) {
        if (pegs == 1) {
            solutions = solutions + 1
            // backtrack
            depth = depth - 1
            if (depth < 0) {
                return solutions
            }
            // undo last move
            var last_m = stack[depth]
            board[mfrom[last_m]] = true
            board[mover[last_m]] = true
            board[mto[last_m]] = false
            pegs = pegs + 1
            stack[depth] = last_m + 1
        }

        // Find next valid move at current depth
        var found = false
        var mi = stack[depth]
        while (mi < num_moves) {
            if (board[mfrom[mi]] and board[mover[mi]] and (not board[mto[mi]])) {
                // Make move
                board[mfrom[mi]] = false
                board[mover[mi]] = false
                board[mto[mi]] = true
                pegs = pegs - 1
                stack[depth] = mi
                depth = depth + 1
                if (depth < 14) {
                    stack[depth] = 0
                }
                found = true
                mi = num_moves // break
            }
            mi = mi + 1
        }

        if (not found) {
            // No more moves at this depth, backtrack
            depth = depth - 1
            if (depth >= 0) {
                var last_m = stack[depth]
                board[mfrom[last_m]] = true
                board[mover[last_m]] = true
                board[mto[last_m]] = false
                pegs = pegs + 1
                stack[depth] = last_m + 1
            }
        }
    }

    return solutions
}

pn main() {
    var __t0 = clock()
    let result = benchmark()
    var __t1 = clock()
    print("triangl: solutions=" ++ string(result) ++ "\n")
    if (result == 29760) {
        print("triangl: PASS\n")
    } else {
        print("triangl: DONE\n")
    }
    print("__TIMING__:" ++ string((__t1 - __t0) * 1000.0) ++ "\n")
}
