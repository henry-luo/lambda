// Js54 P4 regression: TypedArray prototype methods over resizable buffers.
// Spec §23.2.3.* methods call ValidateTypedArray at entry and throw TypeError
// on OOB / detached. Several methods (slice, forEach, reduce, reduceRight,
// join, toLocaleString, sort, with, toReversed, toSorted) were missing this
// check. Also: indexOf/lastIndexOf must NOT re-fetch `len` after coercion
// callbacks per spec, and the raw_index_of fast path returned -1 for
// non-numeric search values, falsely shortcutting includes(undefined, ...).

function assertEq(actual, expected, label) {
    if (actual !== expected) throw new Error(label + ": got " + actual + " expected " + expected);
}
function assertThrows(fn, kind, label) {
    let threw = false;
    try { fn(); } catch (e) {
        threw = true;
        if (e.constructor.name !== kind) throw new Error(label + ": threw " + e.constructor.name + " expected " + kind);
    }
    if (!threw) throw new Error(label + ": did not throw " + kind);
}

// === slice / forEach / reduce / reduceRight / join / toLocaleString /
//     sort / with / toReversed / toSorted: throw TypeError on OOB ===
for (const method of ["slice", "forEach", "reduce", "reduceRight", "join",
                      "toLocaleString", "sort", "with", "toReversed", "toSorted"]) {
    const rab = new ArrayBuffer(16, {maxByteLength: 40});
    const ta = new Int32Array(rab, 0, 4);  // fixed-length view
    rab.resize(8);  // shrinks below view end → OOB
    let cb;
    if (method === "forEach" || method === "reduce" || method === "reduceRight") {
        cb = () => 0;
    } else if (method === "sort" || method === "toSorted") {
        cb = (a, b) => a - b;
    }
    assertThrows(() => {
        switch (method) {
            case "slice": ta.slice(); break;
            case "forEach": ta.forEach(cb); break;
            case "reduce": ta.reduce(cb, 0); break;
            case "reduceRight": ta.reduceRight(cb, 0); break;
            case "join": ta.join(","); break;
            case "toLocaleString": ta.toLocaleString(); break;
            case "sort": ta.sort(cb); break;
            case "with": ta.with(0, 1); break;
            case "toReversed": ta.toReversed(); break;
            case "toSorted": ta.toSorted(cb); break;
        }
    }, "TypeError", method + " on OOB");
}

// === indexOf: do NOT re-fetch len after coercion callback ===
{
    const rab = new ArrayBuffer(16, {maxByteLength: 40});
    const ta = new Int32Array(rab);  // length-tracking, 4 elements
    for (let i = 0; i < 4; i++) ta[i] = 1;
    const evil = { valueOf() { rab.resize(24); return 0; } };  // grows to 6
    // Per spec, len is captured BEFORE valueOf. New elements (indices 4-5) are
    // zeroed, but the search range stays at the original [0..3], so 0 is NOT
    // found.
    assertEq(ta.indexOf(0, evil), -1, "indexOf preserves original len after grow");
}

// === lastIndexOf: do NOT re-fetch len after coercion callback ===
{
    const rab = new ArrayBuffer(16, {maxByteLength: 40});
    const ta = new Int32Array(rab);
    for (let i = 0; i < 4; i++) ta[i] = 1;
    const evil = { valueOf() { rab.resize(24); return -4; } };
    assertEq(ta.lastIndexOf(0, evil), -1, "lastIndexOf preserves original len after grow");
}

// === includes(undefined, ...) finds OOB positions ===
// raw_index_of's fast path used to return -1 for non-numeric search values,
// shortcutting includes to false. After fix it returns -2 → slow path runs →
// finds undefined at post-shrink OOB indices.
{
    const rab = new ArrayBuffer(4, {maxByteLength: 4});
    const ta = new Int32Array(rab);
    const evil = { valueOf() { rab.resize(0); return 0; } };
    // After valueOf, the TA is OOB → index 0 reads return undefined.
    // Per spec, includes uses the original len=1; Get(0) is undefined; matches.
    assertEq(ta.includes(undefined, evil), true, "includes(undefined) finds OOB");
}

// === Methods stay correct on valid (non-OOB) TAs ===
{
    const ta = new Int32Array([1, 2, 3, 4]);
    assertEq(ta.slice(1, 3).length, 2, "slice on valid TA");
    assertEq(ta.indexOf(3), 2, "indexOf on valid TA");
    assertEq(ta.lastIndexOf(3), 2, "lastIndexOf on valid TA");
    assertEq(ta.includes(3), true, "includes on valid TA");
    assertEq(ta.join("-"), "1-2-3-4", "join on valid TA");
    let sum = 0; ta.forEach(v => sum += v);
    assertEq(sum, 10, "forEach on valid TA");
}

console.log("Js54 P4 TypedArray prototype OOB regression: all assertions passed");
