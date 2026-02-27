// BENG Benchmark: reverse-complement
// Read FASTA sequences, output reverse-complement of each
// Uses file input instead of stdin (Lambda adaptation)
// Expected: matches benchmarksgame revcomp-output.txt

let INPUT_PATH = @"test/benchmark/beng/input/fasta_1000.txt"
let LINE_WIDTH = 60

// complement mapping using two-pass swap to avoid overwriting
// handles IUPAC codes: A<->T, C<->G, M<->K, R<->Y, V<->B, H<->D, W<->W, S<->S, N<->N
pn complement(seq) {
    var s = upper(seq)
    // first pass: replace one side of each pair with temp digits
    s = replace(s, "A", "1")
    s = replace(s, "T", "A")
    s = replace(s, "1", "T")
    s = replace(s, "C", "2")
    s = replace(s, "G", "C")
    s = replace(s, "2", "G")
    // IUPAC ambiguity codes
    s = replace(s, "M", "3")
    s = replace(s, "K", "M")
    s = replace(s, "3", "K")
    s = replace(s, "R", "4")
    s = replace(s, "Y", "R")
    s = replace(s, "4", "Y")
    s = replace(s, "V", "5")
    s = replace(s, "B", "V")
    s = replace(s, "5", "B")
    s = replace(s, "H", "6")
    s = replace(s, "D", "H")
    s = replace(s, "6", "D")
    // W, S, N are self-complementary — no change needed
    return s
}

pn output_reverse(header, seq_chars) {
    print(header ++ "\n")
    // reverse the sequence
    var rev = reverse(seq_chars)
    // complement each character and output in LINE_WIDTH chunks
    var total = len(rev)
    var pos = 0
    while (pos < total) {
        var end = pos + LINE_WIDTH
        if (end > total) {
            end = total
        }
        // build line from complemented chars
        var chunk = str_join(slice(rev, pos, end), "")
        print(complement(chunk) ++ "\n")
        pos = end
    }
}

pn main() {
    let text = input(INPUT_PATH, 'text)
    let lines = split(text, "\n")
    var num_lines = len(lines)

    var header = ""
    var seq_chars = []
    var i = 0
    while (i < num_lines) {
        let line = lines[i]
        if (len(line) > 0 and slice(line, 0, 1) == ">") {
            // output previous sequence if any
            if (len(seq_chars) > 0) {
                output_reverse(header, seq_chars)
            }
            header = line
            seq_chars = []
        } else {
            if (len(line) > 0) {
                // append characters of this line
                seq_chars = seq_chars ++ chars(line)
            }
        }
        i = i + 1
    }
    // output last sequence
    if (len(seq_chars) > 0) {
        output_reverse(header, seq_chars)
    }
}
