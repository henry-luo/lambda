// BENG Benchmark: reverse-complement
// Read FASTA sequences, output reverse-complement of each
// Uses file input instead of stdin (Lambda adaptation)
// Expected: matches benchmarksgame revcomp-output.txt

let INPUT_PATH = "test/benchmark/beng/input/fasta_1000.txt"
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

pn output_reverse_complement(header, seq) {
    print(header ++ "\n")
    var comp = complement(seq)
    // fn_reverse() returns text unchanged — strings are singular and not iterable in
    // Lambda (lambda-vector.cpp), and only reverse(vec) is defined. This file used to
    // call reverse(comp) and print the COMPLEMENT ONLY, which is not the benchmark.
    // The reversal is done explicitly here, one output line at a time, so the string
    // concatenation stays bounded by LINE_WIDTH instead of the whole sequence.
    var total = len(comp)
    var pos = 0
    while (pos < total) {
        var end_pos = pos + LINE_WIDTH
        if (end_pos > total) {
            end_pos = total
        }
        var line = ""
        var k = pos
        while (k < end_pos) {
            line = line ++ comp[total - 1 - k]
            k = k + 1
        }
        print(line ++ "\n")
        pos = end_pos
    }
}

pn main() {
    var __t0 = clock()
    let text^err = input(INPUT_PATH, 'text')
    let lines = split(text, "\n")
    var num_lines = len(lines)

    var header = ""
    var seq = ""
    var i = 0
    while (i < num_lines) {
        let line = lines[i]
        if (len(line) > 0 and slice(line, 0, 1) == ">") {
            // output previous sequence if any
            if (len(seq) > 0) {
                output_reverse_complement(header, seq)
            }
            header = line
            seq = ""
        } else {
            if (len(line) > 0) {
                seq = seq ++ upper(line)
            }
        }
        i = i + 1
    }
    // output last sequence
    if (len(seq) > 0) {
        output_reverse_complement(header, seq)
    }
    var __t1 = clock()
    print("__TIMING__:" ++ ((__t1 - __t0) * 1000.0) ++ "\n")
}
