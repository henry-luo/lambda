// Kostya Benchmark: brainfuck (Node.js)
// Brainfuck interpreter — interprets a BF program 10000 times
'use strict';

function buildJumpTable(prog) {
    const len = prog.length;
    const jumps = new Int32Array(len);
    const stack = [];
    for (let i = 0; i < len; i++) {
        const c = prog.charCodeAt(i);
        if (c === 91) { // '['
            stack.push(i);
        } else if (c === 93) { // ']'
            const j = stack.pop();
            jumps[i] = j;
            jumps[j] = i;
        }
    }
    return jumps;
}

function runBf(prog, jumps) {
    const len = prog.length;
    const tape = new Uint8Array(30000);
    let dp = 0;
    let ip = 0;
    const output = [];

    while (ip < len) {
        const op = prog.charCodeAt(ip);
        if (op === 43) { // '+'
            tape[dp] = (tape[dp] + 1) & 0xFF;
        } else if (op === 45) { // '-'
            tape[dp] = (tape[dp] + 255) & 0xFF;
        } else if (op === 62) { // '>'
            dp++;
        } else if (op === 60) { // '<'
            dp--;
        } else if (op === 46) { // '.'
            output.push(String.fromCharCode(tape[dp]));
        } else if (op === 91) { // '['
            if (tape[dp] === 0) {
                ip = jumps[ip];
            }
        } else if (op === 93) { // ']'
            if (tape[dp] !== 0) {
                ip = jumps[ip];
            }
        }
        ip++;
    }
    return output.join('');
}

function main() {
    const prog = "++++++++[>++++[>++>+++>+++>+<<<<-]>+>+>->>+[<]<-]>>.>---.+++++++..+++.>>.<-.<.+++.------.--------.>>+.>++.";
    const jumps = buildJumpTable(prog);

    let output = "";
    for (let iter = 0; iter < 10000; iter++) {
        output = runBf(prog, jumps);
    }
    process.stdout.write(output + "\n");
}

main();
