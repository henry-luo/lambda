// BENG Benchmark: fasta (Typed version)
// Generate random DNA sequences using LCG PRNG
// N=1000 expected: matches benchmarksgame fasta-output.txt

let N_COUNT = 1000
let LINE_WIDTH = 60

let IM = 139968
let IA = 3877
let IC = 29573

let ALU = "GGCCGGGCGCGGTGGCTCACGCCTGTAATCCCAGCACTTTGGGAGGCCGAGGCGGGCGGATCACCTGAGGTCAGGAGTTCGAGACCAGCCTGGCCAACATGGTGAAACCCCGTCTCTACTAAAAATACAAAAATTAGCCGGGCGTGGTGGCGCGCGCCTGTAATCCCAGCTACTCGGGAGGCTGAGGCAGGAGAATCGCTTGAACCCGGGAGGCGGAGGTTGCAGTGAGCCGAGATCGCGCCACTGCACTCCAGCCTGGGCGACAGAGCGAGACTCCGTCTCAAAAA"

// repeat ALU sequence, outputting width chars per line
pn repeat_fasta(id: string, desc: string, src: string, count: int) any {
    print(">" ++ id ++ " " ++ desc ++ "\n")
    var src_len: int = len(src)
    var pos: int = 0

    while (count > 0) {
        var line_len: int = LINE_WIDTH
        if (count < line_len) {
            line_len = count
        }

        // build one line by copying from src with wraparound
        var line: string = ""
        var written: int = 0
        while (written < line_len) {
            var chunk_len: int = line_len - written
            var avail: int = src_len - pos
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
        count = count - line_len
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
pn make_cumulative(probs: float[]) float[] {
    var cum: float[] = fill(len(probs), 0.0)
    var total: float = 0.0
    var i: int = 0
    while (i < len(probs)) {
        total = total + probs[i]
        cum[i] = total
        i = i + 1
    }
    return cum
}

// seed is mutable via array trick: seed_arr[0]
pn random_fasta(id: string, desc: string, chars, probs: float[],
                count: int, var seed_arr: int[]) {
    print(">" ++ id ++ " " ++ desc ++ "\n")
    var cum = make_cumulative(probs)
    var num_chars: int = len(chars)

    while (count > 0) {
        var line_len: int = LINE_WIDTH
        if (count < line_len) {
            line_len = count
        }

        var line: string = ""
        var j: int = 0
        while (j < line_len) {
            // `%` has a flexible numeric result; convert it at the int[]
            // boundary so the PRNG state keeps its declared element contract.
            seed_arr[0] = int((seed_arr[0] * IA + IC) % IM)
            var r: float = float(seed_arr[0]) / float(IM)

            // lookup character by cumulative probability
            var k: int = 0
            while (k < num_chars - 1 and cum[k] < r) {
                k = k + 1
            }
            line = line ++ chars[k]
            j = j + 1
        }
        print(line ++ "\n")
        count = count - line_len
    }
}

pn main() {
    var __t0 = clock()
    // seed is shared across calls, stored in mutable array
    var seed_arr: int[] = [42]

    repeat_fasta("ONE", "Homo sapiens alu", ALU, N_COUNT * 2)
    random_fasta("TWO", "IUB ambiguity codes", IUB_CHARS, IUB_PROBS, N_COUNT * 3, seed_arr)
    random_fasta("THREE", "Homo sapiens frequency", HS_CHARS, HS_PROBS, N_COUNT * 5, seed_arr)
    var __t1 = clock()
    print("__TIMING__:" ++ ((__t1 - __t0) * 1000.0) ++ "\n")
}
