#!/usr/bin/env python3
# Kostya Benchmark: brainfuck (Python)
# Brainfuck interpreter — interprets a BF program 10000 times
import time


def build_jump_table(prog):
    jumps = [0] * len(prog)
    stack = []
    for i, c in enumerate(prog):
        if c == '[':
            stack.append(i)
        elif c == ']':
            j = stack.pop()
            jumps[i] = j
            jumps[j] = i
    return jumps


def run_bf(prog, jumps):
    tape = bytearray(30000)
    dp = 0
    ip = 0
    output = []
    prog_len = len(prog)
    while ip < prog_len:
        op = prog[ip]
        if op == '+':
            tape[dp] = (tape[dp] + 1) & 0xFF
        elif op == '-':
            tape[dp] = (tape[dp] - 1) & 0xFF
        elif op == '>':
            dp += 1
        elif op == '<':
            dp -= 1
        elif op == '.':
            output.append(chr(tape[dp]))
        elif op == '[':
            if tape[dp] == 0:
                ip = jumps[ip]
        elif op == ']':
            if tape[dp] != 0:
                ip = jumps[ip]
        ip += 1
    return ''.join(output)


def main():
    prog = "++++++++[>++++[>++>+++>+++>+<<<<-]>+>+>->>+[<]<-]>>.>---.+++++++..+++.>>.<-.<.+++.------.--------.>>+.>++."
    jumps = build_jump_table(prog)

    t0 = time.perf_counter_ns()
    output = ""
    for _ in range(10000):
        output = run_bf(prog, jumps)
    t1 = time.perf_counter_ns()

    print(output)
    print(f"__TIMING__:{(t1 - t0) / 1e6:.3f}")


main()
