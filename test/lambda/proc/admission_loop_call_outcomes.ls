// D8.3.2/S7.6.3v3: loop handoff keeps dynamic call outcomes boxed until ^.
fn letter_code(codepoint: int) int =>
    if (codepoint == 65) 0 else if (codepoint == 67) 1 else
        if (codepoint == 71) 2 else 3

pn letter_counts(seq: string) int[]^ {
    var counts: int[] = fill(20, 0)^
    var i: int = 0
    while (i < 200) {
        let current: int = letter_code(ord(seq[i % 4]))^
        counts[current] = counts[current] + 1
        i = i + 1
    }
    return counts
}

pn main() {
    print(letter_counts("ACGT")); print("\n")
    print(letter_counts("AC") is error); print("\n")
}
