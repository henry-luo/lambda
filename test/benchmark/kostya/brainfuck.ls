// Kostya Benchmark: brainfuck
// Brainfuck interpreter — interprets a BF program
// Adapted from github.com/kostya/benchmarks
// Interprets BF Hello World 10000 times to stress the interpreter loop
// Expected output: "Hello World!\n"

// Precompute matching brackets
pn build_jump_table(prog, prog_len) {
    var jumps = fill(prog_len, 0)
    var stack = fill(256, 0)
    var sp = 0
    var i = 0
    while (i < prog_len) {
        var c = ord(prog[i])
        if (c == 91) {
            // '[' = 91
            stack[sp] = i
            sp = sp + 1
        }
        if (c == 93) {
            // ']' = 93
            sp = sp - 1
            var j = stack[sp]
            jumps[i] = j
            jumps[j] = i
        }
        i = i + 1
    }
    return jumps
}

pn run_bf(prog, prog_len, jumps) {
    var tape = fill(30000, 0)
    var dp = 0
    var ip = 0
    var output = ""

    while (ip < prog_len) {
        var op = ord(prog[ip])
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
    let prog = "++++++++[>++++[>++>+++>+++>+<<<<-]>+>+>->>+[<]<-]>>.>---.+++++++..+++.>>.<-.<.+++.------.--------.>>+.>++."
    let prog_len = len(prog)
    var jumps = build_jump_table(prog, prog_len)

    var output = ""
    var iter = 0
    while (iter < 10000) {
        output = run_bf(prog, prog_len, jumps)
        iter = iter + 1
    }
    var __t1 = clock()
    print(output ++ "\n")
    print("__TIMING__:" ++ string((__t1 - __t0) * 1000.0) ++ "\n")
}
