// BENG Benchmark: fasta (Node.js reference)
// Generate DNA sequences using LCG PRNG

const N = parseInt(process.argv[2] || "1000");
const LINE_WIDTH = 60;

const ALU = "GGCCGGGCGCGGTGGCTCACGCCTGTAATCCCAGCACTTTGGGAGGCCGAGGCGGGCGGATCACCTGAGGTCAGGAGTTCGAGACCAGCCTGGCCAACATGGTGAAACCCCGTCTCTACTAAAAATACAAAAATTAGCCGGGCGTGGTGGCGCGCGCCTGTAATCCCAGCTACTCGGGAGGCTGAGGCAGGAGAATCGCTTGAACCCGGGAGGCGGAGGTTGCAGTGAGCCGAGATCGCGCCACTGCACTCCAGCCTGGGCGACAGAGCGAGACTCCGTCTCAAAAAGG";

const IUB = [
    ['a', 0.27], ['c', 0.12], ['g', 0.12], ['t', 0.27],
    ['B', 0.02], ['D', 0.02], ['H', 0.02], ['K', 0.02],
    ['M', 0.02], ['N', 0.02], ['R', 0.02], ['S', 0.02],
    ['V', 0.02], ['W', 0.02], ['Y', 0.02]
];

const HOMO = [
    ['a', 0.3029549426680], ['c', 0.1979883004921],
    ['g', 0.1975473066391], ['t', 0.3015094502008]
];

function makeCumulative(table) {
    let sum = 0;
    for (const entry of table) {
        sum += entry[1];
        entry[1] = sum;
    }
}

let seed = 42;
const IM = 139968, IA = 3877, IC = 29573;

function random(max) {
    seed = (seed * IA + IC) % IM;
    return max * seed / IM;
}

function repeatFasta(id, desc, src, n) {
    console.log(`>${id} ${desc}`);
    let k = 0;
    let line = '';
    for (let i = 0; i < n; i++) {
        line += src[k];
        k = (k + 1) % src.length;
        if (line.length === LINE_WIDTH) {
            console.log(line);
            line = '';
        }
    }
    if (line.length > 0) console.log(line);
}

function randomFasta(id, desc, table, n) {
    console.log(`>${id} ${desc}`);
    makeCumulative(table);
    let line = '';
    for (let i = 0; i < n; i++) {
        const r = random(1.0);
        for (const [ch, prob] of table) {
            if (r < prob) { line += ch; break; }
        }
        if (line.length === LINE_WIDTH) {
            console.log(line);
            line = '';
        }
    }
    if (line.length > 0) console.log(line);
}

repeatFasta("ONE", "Homo sapiens alu", ALU, N * 2);
randomFasta("TWO", "IUB ambiguity codes", IUB, N * 3);
randomFasta("THREE", "Homo sapiens frequency", HOMO, N * 5);
