// BENG Benchmark: k-nucleotide (Untyped compact-counter version)
// Count nucleotide k-mer frequencies from FASTA >THREE section
// Uses file input instead of stdin (Lambda adaptation)
// Expected: matches benchmarksgame knucleotide-output.txt

let INPUT_PATH = "test/benchmark/beng/input/fasta_1000.txt"

// extract >THREE section from fasta input
pn extract_three(text) {
    let lines = split(text, "\n")
    var num_lines = len(lines)
    var seq = ""
    var in_three = 0
    var i = 0
    while (i < num_lines) {
        let line = lines[i]
        if (len(line) > 0 and slice(line, 0, 1) == ">") {
            if (in_three == 1) {
                // hit next header, stop
                return seq
            }
            if (starts_with(line, ">THREE")) {
                in_three = 1
            }
        } else {
            if (in_three == 1 and len(line) > 0) {
                seq = seq ++ upper(line)
            }
        }
        i = i + 1
    }
    return seq
}

// The supplied >THREE corpus is a closed A/C/G/T alphabet. Keep its one- and
// two-mer frequencies in compact integer counters, while
// longer requested sequences remain ordinary string scans.
fn base_code(codepoint) =>
    if (codepoint == 65) 0 else if (codepoint == 67) 1 else
        if (codepoint == 71) 2 else 3

fn base_letter(code) =>
    if (code == 0) "A" else if (code == 1) "C" else if (code == 2) "G" else "T"

pn count_small_kmers(seq) {
    var counts = fill(20, 0)
    var seq_len = len(seq)
    var i = 0
    while (i < seq_len) {
        let current = base_code(ord(seq[i]))
        counts[current] = counts[current] + 1
        if (i + 1 < seq_len) {
            let next = base_code(ord(seq[i + 1]))
            let pair = 4 + current * 4 + next
            counts[pair] = counts[pair] + 1
        }
        i = i + 1
    }
    counts
}

// format float to 3 decimal places
pn format3(x) {
    var int_part = int(floor(x))
    var frac = x - float(int_part)
    var frac_f = floor(frac * 1000.0 + 0.5)
    var frac_int = int(frac_f)
    if (frac_int >= 1000) {
        int_part = int_part + 1
        frac_int = 0
    }
    var frac_str = string(frac_int)
    var pad = 3 - len(frac_str)
    var prefix = ""
    while (pad > 0) {
        prefix = prefix ++ "0"
        pad = pad - 1
    }
    frac_str = prefix ++ frac_str
    return int_part ++ "." ++ frac_str
}

pn print_frequency_table(counts, offset, width, total) {
    let entries = if (width == 1) 4 else 16
    var order = fill(entries, 0)
    var i = 0
    while (i < entries) {
        order[i] = i
        i = i + 1
    }
    i = 0
    while (i < entries) {
        var best = i
        var j = i + 1
        while (j < entries) {
            let candidate = order[j]
            let selected = order[best]
            if (counts[offset + candidate] > counts[offset + selected] or
                    (counts[offset + candidate] == counts[offset + selected] and
                     candidate < selected)) {
                best = j
            }
            j = j + 1
        }
        let selected = order[best]
        order[best] = order[i]
        order[i] = selected
        i = i + 1
    }
    i = 0
    while (i < entries) {
        let code = order[i]
        let count = counts[offset + code]
        let freq = float(count) * 100.0 / float(total)
        let kmer = if (width == 1) base_letter(code) else
            base_letter(int(code / 4)) ++ base_letter(code % 4)
        print(kmer ++ " " ++ format3(freq) ++ "\n")
        i = i + 1
    }
    print("\n")
}

pn count_literal(seq, pattern) {
    let pattern_len = len(pattern)
    let seq_len = len(seq)
    var count = 0
    var start = 0
    while (start <= seq_len - pattern_len) {
        var offset = 0
        while (offset < pattern_len and seq[start + offset] == pattern[offset]) {
            offset = offset + 1
        }
        if (offset == pattern_len) {
            count = count + 1
        }
        start = start + 1
    }
    count
}

pn print_count(seq, kmer) {
    print(count_literal(seq, kmer) ++ "\t" ++ kmer ++ "\n")
}

pn main() {
    var __t0 = clock()
    let text = io.read(INPUT_PATH)^
    let seq = extract_three(text)
    let counts = count_small_kmers(seq)

    // print frequency tables for 1-mers and 2-mers
    print_frequency_table(counts, 0, 1, len(seq))
    print_frequency_table(counts, 4, 2, len(seq) - 1)

    // print counts of specific sequences
    print_count(seq, "GGT")
    print_count(seq, "GGTA")
    print_count(seq, "GGTATT")
    print_count(seq, "GGTATTTTAATT")
    print_count(seq, "GGTATTTTAATTTATAGT")
    var __t1 = clock()
    print("__TIMING__:" ++ ((__t1 - __t0) * 1000.0) ++ "\n")
}
