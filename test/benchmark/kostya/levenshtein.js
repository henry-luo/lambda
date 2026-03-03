// Kostya Benchmark: levenshtein (Node.js)
// Levenshtein edit distance using dynamic programming
'use strict';

function levenshtein(s1, s2) {
    const n = s1.length;
    const m = s2.length;
    let prev = new Int32Array(m + 1);
    let curr = new Int32Array(m + 1);
    for (let j = 0; j <= m; j++) prev[j] = j;

    for (let i = 1; i <= n; i++) {
        curr[0] = i;
        const c1 = s1.charCodeAt(i - 1);
        for (let j = 1; j <= m; j++) {
            const cost = c1 === s2.charCodeAt(j - 1) ? 0 : 1;
            const del = prev[j] + 1;
            const ins = curr[j - 1] + 1;
            const sub = prev[j - 1] + cost;
            curr[j] = del < ins ? (del < sub ? del : sub) : (ins < sub ? ins : sub);
        }
        [prev, curr] = [curr, prev];
    }
    return prev[m];
}

function makeString(ch, n) {
    return ch.repeat(n);
}

function main() {
    const __t0 = process.hrtime.bigint();
    const d1 = levenshtein("kitten", "sitting");
    const d2 = levenshtein("saturday", "sunday");

    const s1 = makeString("a", 500);
    const s2 = makeString("b", 500);
    const d3 = levenshtein(s1, s2);

    const s3 = makeString("ab", 200);
    const s4 = makeString("ba", 200);
    const d4 = levenshtein(s3, s4);
    const __t1 = process.hrtime.bigint();

    process.stdout.write("levenshtein: d(kitten,sitting)=" + d1 + "\n");
    process.stdout.write("levenshtein: d(saturday,sunday)=" + d2 + "\n");
    process.stdout.write("levenshtein: d(aaa...,bbb...)=" + d3 + "\n");
    process.stdout.write("levenshtein: d(ababab...,babab...)=" + d4 + "\n");

    if (d1 === 3 && d2 === 3) {
        process.stdout.write("levenshtein: PASS\n");
    } else {
        process.stdout.write("levenshtein: FAIL\n");
    }
    process.stdout.write("__TIMING__:" + Number(__t1 - __t0) / 1e6 + "\n");
}

main();
