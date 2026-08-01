/* Native C2MIR port of kostya/brainfuck.ls. */
extern int printf(const char *, ...);

static const char program[] = "++++++++[>++++[>++>+++>+++>+<<<<-]>+>+>->>+[<]<-]>>.>---.+++++++..+++.>>.<-.<.+++.------.--------.>>+.>++.";

static void build_jumps(int jumps[111]) {
    int stack[111];
    int sp = 0;
    int i;
    for (i = 0; program[i] != 0; i++) {
        if (program[i] == '[') stack[sp++] = i;
        if (program[i] == ']') { int open = stack[--sp]; jumps[i] = open; jumps[open] = i; }
    }
}

static void run_bf(int jumps[111], char output[14]) {
    unsigned char tape[30000] = {0};
    int dp = 0;
    int ip = 0;
    int out = 0;
    while (program[ip] != 0) {
        char op = program[ip];
        if (op == '+') tape[dp]++;
        if (op == '-') tape[dp]--;
        if (op == '>') dp++;
        if (op == '<') dp--;
        if (op == '.') output[out++] = tape[dp];
        if (op == '[' && tape[dp] == 0) ip = jumps[ip];
        if (op == ']' && tape[dp] != 0) ip = jumps[ip];
        ip++;
    }
    output[out] = 0;
}

int main(void) {
    int jumps[111] = {0};
    char output[14];
    int i;
    build_jumps(jumps);
    for (i = 0; i < 10000; i++) run_bf(jumps, output);
    printf(output[0] == 'H' && output[11] == '!' ? "Hello World!\n" : "brainfuck: FAIL\n");
    return output[0] != 'H' || output[11] != '!';
}
