// BENG Benchmark: regex-redux (Node.js reference)
// Read FASTA, count regex matches, perform IUPAC substitutions

const fs = require('fs');

const INPUT_PATH = process.argv[2] || "test/benchmark/beng/input/fasta_1000.txt";

let text = fs.readFileSync(INPUT_PATH, 'utf-8');
const originalLen = text.length;

const __t0 = process.hrtime.bigint();
// Remove FASTA headers and newlines
text = text.replace(/>[^\n]*\n/g, '');
text = text.replace(/\n/g, '');
const cleanLen = text.length;

// 9 regex patterns to count
const patterns = [
    /agggtaaa|tttaccct/g,
    /[cgt]gggtaaa|tttaccc[acg]/g,
    /a[act]ggtaaa|tttacc[agt]t/g,
    /ag[act]gtaaa|tttac[agt]ct/g,
    /agg[act]taaa|ttta[agt]cct/g,
    /aggg[acg]aaa|ttt[cgt]ccct/g,
    /agggt[cgt]aa|tt[acg]taccct/g,
    /agggta[cgt]a|t[acg]ataccct/g,
    /agggtaa[cgt]|[acg]aataccct/g,
];

const patternLabels = [
    'agggtaaa|tttaccct',
    '[cgt]gggtaaa|tttaccc[acg]',
    'a[act]ggtaaa|tttacc[agt]t',
    'ag[act]gtaaa|tttac[agt]ct',
    'agg[act]taaa|ttta[agt]cct',
    'aggg[acg]aaa|ttt[cgt]ccct',
    'agggt[cgt]aa|tt[acg]taccct',
    'agggta[cgt]a|t[acg]ataccct',
    'agggtaa[cgt]|[acg]aataccct',
];

for (let i = 0; i < patterns.length; i++) {
    const matches = text.match(patterns[i]);
    const count = matches ? matches.length : 0;
    console.log(`${patternLabels[i]} ${count}`);
}

// IUPAC substitutions
const substitutions = [
    [/B/g, '(c|g|t)'],
    [/D/g, '(a|g|t)'],
    [/H/g, '(a|c|t)'],
    [/K/g, '(g|t)'],
    [/M/g, '(a|c)'],
    [/N/g, '(a|c|g|t)'],
    [/R/g, '(a|g)'],
    [/S/g, '(c|g)'],
    [/V/g, '(a|c|g)'],
    [/W/g, '(a|t)'],
    [/Y/g, '(c|t)'],
];

let result = text;
for (const [re, repl] of substitutions) {
    result = result.replace(re, repl);
}

const __t1 = process.hrtime.bigint();
console.log('');
console.log(originalLen);
console.log(cleanLen);
console.log(result.length);
process.stdout.write("__TIMING__:" + Number(__t1 - __t0) / 1e6 + "\n");
