// BENG Benchmark: fasta
// Generate random DNA sequences using LCG PRNG
// N=1000 expected: matches benchmarksgame fasta-output.txt

let N_COUNT = 1000
let LINE_WIDTH = 60

let IM = 139968
let IA = 3877
let IC = 29573

let ALU = "GGCCGGGCGCGGTGGCTCACGCCTGTAATCCCAGCACTTTGGGAGGCCGAGGCGGGCGGATCACCTGAGGTCAGGAGTTCGAGACCAGCCTGGCCAACATGGTGAAACCCCGTCTCTACTAAAAATACAAAAATTAGCCGGGCGTGGTGGCGCGCGCCTGTAATCCCAGCTACTCGGGAGGCTGAGGCAGGAGAATCGCTTGAACCCGGGAGGCGGAGGTTGCAGTGAGCCGAGATCGCGCCACTGCACTCCAGCCTGGGCGACAGAGCGAGACTCCGTCTCAAAAA"

// repeat ALU sequence, outputting width chars per line
pn repeat_fasta(id, desc, src, count) {
    print(">" ++ id ++ " " ++ desc ++ "\n")
    var src_len = len(src)
    var pos = 0
    var remaining = count

    while (remaining > 0) {
        var line_len = LINE_WIDTH
        if (remaining < line_len) {
            line_len = remaining
        }

        // build one line by copying from src with wraparound
        var line = ""
        var written = 0
        while (written < line_len) {
            var chunk_len = line_len - written
            var avail = src_len - pos
            if (chunk_len > avail) {
                chunk_len = avail
            }
            line = line ++ slice(src, pos, pos + chunk_len)
            pos = pos + chunk_len
            if (pos >= src_len) {
                pos = 0
            }
            written = written + chunk_len
        }
        print(line ++ "\n")
        remaining = remaining - line_len
    }
}

// IUB and HomoSapiens probability tables
// stored as parallel arrays: chars and cumulative probs
let IUB_CHARS = ["a", "c", "g", "t",
                 "B", "D", "H", "K", "M", "N", "R", "S", "V", "W", "Y"]
let IUB_PROBS = [0.27, 0.12, 0.12, 0.27,
                 0.02, 0.02, 0.02, 0.02, 0.02, 0.02, 0.02, 0.02, 0.02, 0.02, 0.02]

let HS_CHARS = ["a", "c", "g", "t"]
let HS_PROBS = [0.3029549426680, 0.1979883004921, 0.1975473066391, 0.3015094502008]

// build cumulative probability table
pn make_cumulative(probs) {
    var cum = fill(len(probs), 0.0)
    var total = 0.0
    var i = 0
    while (i < len(probs)) {
        total = total + probs[i]
        cum[i] = total
        i = i + 1
    }
    return cum
}

// seed is mutable via array trick: seed_arr[0]
pn random_fasta(id, desc, chars, probs, count, seed_arr) {
    print(">" ++ id ++ " " ++ desc ++ "\n")
    var cum = make_cumulative(probs)
    var num_chars = len(chars)
    var remaining = count

    while (remaining > 0) {
        var line_len = LINE_WIDTH
        if (remaining < line_len) {
            line_len = remaining
        }

        var line = ""
        var j = 0
        while (j < line_len) {
            // LCG random
            seed_arr[0] = (seed_arr[0] * IA + IC) % IM
            var r = float(seed_arr[0]) / float(IM)

            // lookup character by cumulative probability
            var k = 0
            while (k < num_chars - 1 and cum[k] < r) {
                k = k + 1
            }
            line = line ++ chars[k]
            j = j + 1
        }
        print(line ++ "\n")
        remaining = remaining - line_len
    }
}

pn main() {
    // seed is shared across calls, stored in mutable array
    var seed_arr = [42]

    repeat_fasta("ONE", "Homo sapiens alu", ALU, N_COUNT * 2)
    random_fasta("TWO", "IUB ambiguity codes", IUB_CHARS, IUB_PROBS, N_COUNT * 3, seed_arr)
    random_fasta("THREE", "Homo sapiens frequency", HS_CHARS, HS_PROBS, N_COUNT * 5, seed_arr)
}
