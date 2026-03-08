#!/usr/bin/env python3
# Larceny Benchmark: triangl (Python)
# Triangle solitaire board puzzle — count solutions via backtracking
import time
import array

MFROM = [0,0,1,1,2,2,3,3,3,3,4,4,5,5,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,12,12,13,13,14,14]
MOVER = [1,2,3,4,4,5,1,4,6,7,7,8,2,4,8,9,3,7,4,8,4,7,5,8,6,11,7,12,7,8,11,13,8,12,9,13]
MTO   = [3,5,6,8,7,9,0,5,10,12,11,13,0,3,12,14,1,8,2,9,1,6,2,7,3,12,4,13,3,5,10,14,4,11,5,12]
NUM_MOVES = 36


def main():
    t0 = time.perf_counter_ns()
    board = array.array('b', [1] * 15)
    board[0] = 0

    solutions = 0
    pegs = 14
    stack = array.array('i', [0] * 14)
    depth = 0

    while depth >= 0:
        if pegs == 1:
            solutions += 1
            depth -= 1
            if depth < 0:
                break
            last_m = stack[depth]
            board[MFROM[last_m]] = 1
            board[MOVER[last_m]] = 1
            board[MTO[last_m]] = 0
            pegs += 1
            stack[depth] = last_m + 1

        found = False
        mi = stack[depth]
        while mi < NUM_MOVES:
            if board[MFROM[mi]] and board[MOVER[mi]] and not board[MTO[mi]]:
                board[MFROM[mi]] = 0
                board[MOVER[mi]] = 0
                board[MTO[mi]] = 1
                pegs -= 1
                stack[depth] = mi
                depth += 1
                if depth < 14:
                    stack[depth] = 0
                found = True
                break
            mi += 1

        if not found:
            depth -= 1
            if depth >= 0:
                last_m = stack[depth]
                board[MFROM[last_m]] = 1
                board[MOVER[last_m]] = 1
                board[MTO[last_m]] = 0
                pegs += 1
                stack[depth] = last_m + 1

    t1 = time.perf_counter_ns()
    print(f"triangl: solutions={solutions}")
    if solutions == 29760:
        print("triangl: PASS")
    else:
        print("triangl: DONE")
    print(f"__TIMING__:{(t1 - t0) / 1e6:.3f}")


main()
