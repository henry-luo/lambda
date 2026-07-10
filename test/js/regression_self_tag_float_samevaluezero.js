// Self-tagged float regression: Map and Set number keys use SameValueZero.
// This pins NaN collapse and +0/-0 collapse across inline and boxed float Items.

function assertEq(actual, expected, label) {
    if (actual !== expected) {
        throw new Error(label + ": got " + actual + " expected " + expected);
    }
}

const nanBitsA = [1, 2, 3, 4, 5, 6, 248, 127];
const nanBitsB = [9, 8, 7, 6, 5, 4, 240, 127];

function f64FromBytes(bytes) {
    const arr = new Float64Array(1);
    new Uint8Array(arr.buffer).set(bytes);
    return arr[0];
}

const nanA = f64FromBytes(nanBitsA);
const nanB = f64FromBytes(nanBitsB);

const map = new Map();
map.set(nanA, "nan-a");
map.set(nanB, "nan-b");
assertEq(map.size, 1, "Map collapses NaN payload keys");
assertEq(map.get(NaN), "nan-b", "Map gets canonical NaN key");

map.set(+0, "positive-zero");
map.set(-0, "negative-zero");
assertEq(map.size, 2, "Map collapses signed zero separately from NaN");
assertEq(map.get(+0), "negative-zero", "Map gets +0 after -0 set");
assertEq(map.get(-0), "negative-zero", "Map gets -0 after -0 set");

const set = new Set();
set.add(nanA);
set.add(nanB);
set.add(+0);
set.add(-0);
assertEq(set.size, 2, "Set collapses NaN and signed zero");
assertEq(set.has(NaN), true, "Set has NaN");
assertEq(set.has(+0), true, "Set has +0");
assertEq(set.has(-0), true, "Set has -0");

console.log("self-tag float SameValueZero ok");
