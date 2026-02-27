// BENG Benchmark: k-nucleotide
// Count nucleotide k-mer frequencies from FASTA >THREE section
// Uses file input instead of stdin (Lambda adaptation)
// Expected: matches benchmarksgame knucleotide-output.txt

let INPUT_PATH = @"test/benchmark/beng/input/fasta_1000.txt"

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

// count all k-mers of length k in sequence
pn count_kmers(seq, k) {
    var counts = map()
    var seq_len = len(seq)
    var i = 0
    while (i <= seq_len - k) {
        var kmer = slice(seq, i, i + k)
        let cur = counts[kmer]
        if (cur == null) {
            counts.set(kmer, 1)
        } else {
            counts.set(kmer, cur + 1)
        }
        i = i + 1
    }
    return counts
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
    return string(int_part) ++ "." ++ frac_str
}

// print frequency table for k-mers of length k, sorted by frequency desc
pn print_frequencies(seq, k) {
    let counts = count_kmers(seq, k)
    var total = len(seq) - k + 1

    // collect entries as [kmer, count] pairs
    var entries = [for (key, val at counts) [key, val]]

    // sort by count descending, then alphabetically
    entries = sort(entries, fn(a, b) {
        if (a[1] != b[1]) {
            return b[1] - a[1]
        }
        if (a[0] < b[0]) { return -1 }
        if (a[0] > b[0]) { return 1 }
        return 0
    })

    var i = 0
    while (i < len(entries)) {
        let kmer = entries[i][0]
        let count = entries[i][1]
        let freq = float(count) * 100.0 / float(total)
        print(kmer ++ " " ++ format3(freq) ++ "\n")
        i = i + 1
    }
    print("\n")
}

// print count of a specific k-mer
pn print_count(seq, kmer) {
    let k = len(kmer)
    let counts = count_kmers(seq, k)
    let count = counts[kmer]
    if (count == null) {
        print("0\t" ++ kmer ++ "\n")
    } else {
        print(string(count) ++ "\t" ++ kmer ++ "\n")
    }
}

pn main() {
    let text = input(INPUT_PATH, 'text)
    let seq = extract_three(text)

    // print frequency tables for 1-mers and 2-mers
    print_frequencies(seq, 1)
    print_frequencies(seq, 2)

    // print counts of specific sequences
    print_count(seq, "GGT")
    print_count(seq, "GGTA")
    print_count(seq, "GGTATT")
    print_count(seq, "GGTATTTTAATT")
    print_count(seq, "GGTATTTTAATTTATAGT")
}
