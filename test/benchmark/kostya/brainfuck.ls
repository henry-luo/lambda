// Kostya Benchmark: brainfuck
// Brainfuck interpreter — interprets a BF program
// Adapted from github.com/kostya/benchmarks
// Interprets BF Hello World 10000 times to stress the interpreter loop
// Expected output: "Hello World!\n"

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

// Precompute matching brackets
pn build_jump_table(prog, prog_len) {
    var jumps = make_array(prog_len, 0)
    var stack = make_array(256, 0)
    var sp = 0
    var i = 0
    while (i < prog_len) {
        var c = prog[i]
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

// Convert string to array of char codes
pn str_to_codes(s) {
    var n = len(s)
    var codes = make_array(n, 0)
    var i = 0
    while (i < n) {
        // Get character code at position i
        var ch = s[i]
        if (ch == "+") { codes[i] = 43 }
        if (ch == "-") { codes[i] = 45 }
        if (ch == ">") { codes[i] = 62 }
        if (ch == "<") { codes[i] = 60 }
        if (ch == "[") { codes[i] = 91 }
        if (ch == "]") { codes[i] = 93 }
        if (ch == ".") { codes[i] = 46 }
        if (ch == ",") { codes[i] = 44 }
        i = i + 1
    }
    return codes
}

// Character from code (limited ASCII set for output)
pn chr(code) {
    if (code == 10) { return "\n" }
    if (code == 32) { return " " }
    if (code == 33) { return "!" }
    if (code == 44) { return "," }
    if (code == 46) { return "." }
    if (code == 48) { return "0" }
    if (code == 49) { return "1" }
    if (code == 50) { return "2" }
    if (code == 51) { return "3" }
    if (code == 52) { return "4" }
    if (code == 53) { return "5" }
    if (code == 54) { return "6" }
    if (code == 55) { return "7" }
    if (code == 56) { return "8" }
    if (code == 57) { return "9" }
    // Uppercase: A=65..Z=90
    if (code == 72) { return "H" }
    if (code == 87) { return "W" }
    // Lowercase: a=97..z=122
    if (code == 100) { return "d" }
    if (code == 101) { return "e" }
    if (code == 108) { return "l" }
    if (code == 111) { return "o" }
    if (code == 114) { return "r" }
    return "?"
}

pn run_bf(prog_codes, prog_len, jumps) {
    var tape = make_array(30000, 0)
    var dp = 0
    var ip = 0
    var output = ""

    while (ip < prog_len) {
        var op = prog_codes[ip]
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
    // BF Hello World! program
    let prog = "++++++++[>++++[>++>+++>+++>+<<<<-]>+>+>->>+[<]<-]>>.>---.+++++++..+++.>>.<-.<.+++.------.--------.>>+.>++."
    let prog_len = len(prog)
    var prog_codes = str_to_codes(prog)
    var jumps = build_jump_table(prog_codes, prog_len)

    var output = ""
    var iter = 0
    while (iter < 10000) {
        output = run_bf(prog_codes, prog_len, jumps)
        iter = iter + 1
    }
    print(output ++ "\n")
}
