// Larceny Benchmark: gcbench (Node.js)
// GC stress test — binary tree allocation and traversal
'use strict';

function makeTree(depth) {
    if (depth === 0) return { left: null, right: null };
    return { left: makeTree(depth - 1), right: makeTree(depth - 1) };
}

function checkTree(node) {
    if (node.left === null) return 1;
    return 1 + checkTree(node.left) + checkTree(node.right);
}

function main() {
    const minDepth = 4;
    const maxDepth = 14;
    const stretchDepth = maxDepth + 1;

    const stretch = makeTree(stretchDepth);
    process.stdout.write("stretch tree of depth " + stretchDepth + " check: " + checkTree(stretch) + "\n");

    const longLived = makeTree(maxDepth);

    for (let depth = minDepth; depth <= maxDepth; depth += 2) {
        const iterations = 1 << (maxDepth - depth + minDepth);
        let totalCheck = 0;
        for (let i = 0; i < iterations; i++) {
            totalCheck += checkTree(makeTree(depth));
        }
        process.stdout.write(iterations + " trees of depth " + depth + " check: " + totalCheck + "\n");
    }

    process.stdout.write("long lived tree of depth " + maxDepth + " check: " + checkTree(longLived) + "\n");
}

main();
