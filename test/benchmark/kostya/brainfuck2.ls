// Kostya Benchmark: brainfuck (Typed version)
// Brainfuck interpreter — interprets a BF program
// Adapted from github.com/kostya/benchmarks
// Interprets BF Hello World 10000 times to stress the interpreter loop
// Expected output: "Hello World!\n"

// The local buffers use packed ArrayNum storage; main records the jump table's
// int[] contract before it crosses run_bf's checked parameter boundary.
// Precompute matching brackets
pn build_jump_table(prog: string, prog_len: int) {
    var jumps = fill(prog_len, 0)
    var stack = fill(256, 0)
    var sp: int = 0
    var i: int = 0
    while (i < prog_len) {
        var c: int = ord(prog[i])
        if (c == 91) {
            // '[' = 91
            stack[sp] = i
            sp = sp + 1
        }
        if (c == 93) {
            // ']' = 93
            sp = sp - 1
            var j: int = stack[sp]
            jumps[i] = j
            jumps[j] = i
        }
        i = i + 1
    }
    return jumps
}

pn run_bf(prog: string, prog_len: int, jumps: int[]) string {
    var tape = fill(30000, 0)
    var dp: int = 0
    var ip: int = 0
    var output: string = ""

    while (ip < prog_len) {
        var op: int = ord(prog[ip])
        if (op == 43) {
            // '+'
            tape[dp] = (tape[dp] + 1) % 256
        }
        if (op == 45) {
            // '-'
            tape[dp] = (tape[dp] + 255) % 256
        }
        if (op == 62) {
            // '>'
            dp = dp + 1
        }
        if (op == 60) {
            // '<'
            dp = dp - 1
        }
        if (op == 46) {
            // '.'
            output = output ++ chr(tape[dp])
        }
        if (op == 91) {
            // '['
            if (tape[dp] == 0) {
                ip = jumps[ip]
            }
        }
        if (op == 93) {
            // ']'
            if (tape[dp] != 0) {
                ip = jumps[ip]
            }
        }
        ip = ip + 1
    }
    return output
}

pn main() {
    var __t0 = clock()
    // BF Hello World! program
    let prog: string = "++++++++[>++++[>++>+++>+++>+<<<<-]>+>+>->>+[<]<-]>>.>---.+++++++..+++.>>.<-.<.+++.------.--------.>>+.>++."
    let prog_len: int = len(prog)
    var jumps: int[] = build_jump_table(prog, prog_len)

    var output: string = ""
    var iter: int = 0
    while (iter < 10000) {
        output = run_bf(prog, prog_len, jumps)
        iter = iter + 1
    }
    var __t1 = clock()
    print(output ++ "\n")
    print("__TIMING__:" ++ ((__t1 - __t0) * 1000.0) ++ "\n")
}
