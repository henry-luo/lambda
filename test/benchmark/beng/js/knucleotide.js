// BENG Benchmark: k-nucleotide (Node.js reference)
// Count nucleotide k-mer frequencies from FASTA >THREE section

const fs = require('fs');

const INPUT_PATH = process.argv[2] || "test/benchmark/beng/input/fasta_1000.txt";

function extractThree(text) {
    const lines = text.split('\n');
    let seq = '';
    let inThree = false;
    for (const line of lines) {
        if (line.length > 0 && line[0] === '>') {
            if (inThree) return seq;
            if (line.startsWith('>THREE')) inThree = true;
        } else if (inThree && line.length > 0) {
            seq += line.toUpperCase();
        }
    }
    return seq;
}

function countKmers(seq, k) {
    const counts = new Map();
    for (let i = 0; i <= seq.length - k; i++) {
        const kmer = seq.substring(i, i + k);
        counts.set(kmer, (counts.get(kmer) || 0) + 1);
    }
    return counts;
}

function printFrequencies(seq, k) {
    const counts = countKmers(seq, k);
    const total = seq.length - k + 1;
    const entries = [...counts.entries()].sort((a, b) => {
        if (b[1] !== a[1]) return b[1] - a[1];
        return a[0] < b[0] ? -1 : a[0] > b[0] ? 1 : 0;
    });
    for (const [kmer, count] of entries) {
        const freq = (count * 100.0 / total).toFixed(3);
        console.log(`${kmer} ${freq}`);
    }
    console.log('');
}

function printCount(seq, kmer) {
    const counts = countKmers(seq, kmer.length);
    const count = counts.get(kmer) || 0;
    console.log(`${count}\t${kmer}`);
}

const text = fs.readFileSync(INPUT_PATH, 'utf-8');
const seq = extractThree(text);

printFrequencies(seq, 1);
printFrequencies(seq, 2);
printCount(seq, "GGT");
printCount(seq, "GGTA");
printCount(seq, "GGTATT");
printCount(seq, "GGTATTTTAATT");
printCount(seq, "GGTATTTTAATTTATAGT");
