// BENG Benchmark: reverse-complement (Node.js reference)
// Read FASTA, output reverse-complement of each sequence

const fs = require('fs');

const INPUT_PATH = process.argv[2] || "test/benchmark/beng/input/fasta_1000.txt";
const LINE_WIDTH = 60;

const COMPLEMENT = {
    'A': 'T', 'T': 'A', 'C': 'G', 'G': 'C',
    'M': 'K', 'K': 'M', 'R': 'Y', 'Y': 'R',
    'V': 'B', 'B': 'V', 'H': 'D', 'D': 'H',
    'W': 'W', 'S': 'S', 'N': 'N',
    'a': 'T', 't': 'A', 'c': 'G', 'g': 'C',
    'm': 'K', 'k': 'M', 'r': 'Y', 'y': 'R',
    'v': 'B', 'b': 'V', 'h': 'D', 'd': 'H',
    'w': 'W', 's': 'S', 'n': 'N'
};

function outputReverseComplement(header, seq) {
    console.log(header);
    const upper = seq.toUpperCase();
    let comp = '';
    for (let i = 0; i < upper.length; i++) {
        comp += COMPLEMENT[upper[i]] || upper[i];
    }
    const rev = comp.split('').reverse().join('');
    for (let pos = 0; pos < rev.length; pos += LINE_WIDTH) {
        console.log(rev.substring(pos, pos + LINE_WIDTH));
    }
}

const text = fs.readFileSync(INPUT_PATH, 'utf-8');
const lines = text.split('\n');

let header = '';
let seq = '';

for (const line of lines) {
    if (line.length > 0 && line[0] === '>') {
        if (seq.length > 0) {
            outputReverseComplement(header, seq);
        }
        header = line;
        seq = '';
    } else if (line.length > 0) {
        seq += line.toUpperCase();
    }
}

if (seq.length > 0) {
    outputReverseComplement(header, seq);
}
