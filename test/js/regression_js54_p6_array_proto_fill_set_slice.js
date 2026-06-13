// Js54 P6 regression: Array.prototype.{fill,slice,set} on OOB TA receivers.
// The runtime helpers js_typed_array_fill / js_typed_array_set_from /
// js_typed_array_slice contained their own ValidateTypedArray-style OOB
// throws. P5 gated the dispatcher-level checks on js_dispatch_as_array_method,
// but these runtime helpers still threw. P6 gates them too so that
// Array.prototype.X.call(ta_oob, ...) correctly silently no-ops.

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

function makeOobTA() {
    const rab = new ArrayBuffer(16, {maxByteLength: 40});
    const ta = new Int32Array(rab, 0, 4);
    rab.resize(8);  // view end=16 > buffer end=8 → OOB
    return ta;
}

// === TypedArray.prototype.X on OOB TA still throws (P4/P5 preserved) ===
const ta_oob = makeOobTA();
assertThrows(() => ta_oob.fill(1), "TypeError", "ta.fill direct");
assertThrows(() => ta_oob.slice(), "TypeError", "ta.slice direct");
const src = new Int32Array([1, 2]);
assertThrows(() => ta_oob.set(src), "TypeError", "ta.set direct");

// === Array.prototype.X.call(ta_oob, ...) does NOT throw — silently no-ops ===
const ta_oob2 = makeOobTA();

// fill: spec uses LengthOfArrayLike=0, no iteration, returns the receiver
const fill_result = Array.prototype.fill.call(ta_oob2, 99);
assertEq(fill_result, ta_oob2, "Array.fill returns receiver");

// slice: spec creates empty result
const slice_result = Array.prototype.slice.call(ta_oob2);
assertEq(slice_result.length, 0, "Array.slice on OOB-TA → empty");

// === Healthy TA Array.prototype path still works ===
const ta_ok = new Int32Array([1, 2, 3, 4]);
const sliced = Array.prototype.slice.call(ta_ok, 1, 3);
assertEq(sliced.length, 2, "Array.slice healthy length");
assertEq(sliced[0], 2, "Array.slice healthy elem 0");
assertEq(sliced[1], 3, "Array.slice healthy elem 1");

console.log("Js54 P6 Array.prototype fill/set/slice regression: all assertions passed");
