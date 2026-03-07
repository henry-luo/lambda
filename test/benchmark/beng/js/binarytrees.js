// BENG Benchmark: binary-trees (Node.js reference)
// Allocate and deallocate binary trees to stress GC

const N = parseInt(process.argv[2] || "10");

function make(depth) {
    if (depth === 0) return { left: null, right: null };
    return { left: make(depth - 1), right: make(depth - 1) };
}

function check(node) {
    if (node.left === null) return 1;
    return 1 + check(node.left) + check(node.right);
}

const __t0 = process.hrtime.bigint();

const maxDepth = Math.max(6, N);
const stretchDepth = maxDepth + 1;
console.log(`stretch tree of depth ${stretchDepth}\t check: ${check(make(stretchDepth))}`);

const longLivedTree = make(maxDepth);

for (let depth = 4; depth <= maxDepth; depth += 2) {
    const iterations = 1 << (maxDepth - depth + 4);
    let sum = 0;
    for (let i = 0; i < iterations; i++) {
        sum += check(make(depth));
    }
    console.log(`${iterations}\t trees of depth ${depth}\t check: ${sum}`);
}

console.log(`long lived tree of depth ${maxDepth}\t check: ${check(longLivedTree)}`);

const __t1 = process.hrtime.bigint();
process.stdout.write("__TIMING__:" + Number(__t1 - __t0) / 1e6 + "\n");
