// Js55 P0: Array.prototype.slice allocates its species result before reading
// source entries. The source, result, and accessor value must all survive the
// intervening GC safepoints.

function assertEq(actual, expected, label) {
    if (actual !== expected) {
        throw new Error(label + ": got " + actual + " expected " + expected);
    }
}

const source = [];
for (let index = 0; index < 512; index += 1) {
    source.push({ index });
}

source.constructor = {
    [Symbol.species]: function(length) {
        gc();
        return new Array(length);
    },
};

const sliced = source.slice(1, 511);
assertEq(sliced.length, 510, "slice length after species GC");
assertEq(sliced[0].index, 1, "slice first entry after species GC");
assertEq(sliced[509].index, 510, "slice last entry after species GC");

const appended = [];
for (let index = 0; index < 4096; index += 1) {
    appended.push(index);
}
assertEq(appended.length, 4096, "dense append length");
assertEq(appended[0], 0, "dense append first entry");
assertEq(appended[4095], 4095, "dense append last entry");

console.log("Js55 P0 Array slice GC roots and dense append: all assertions passed");
