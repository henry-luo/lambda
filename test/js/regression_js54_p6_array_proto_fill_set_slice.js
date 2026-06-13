// Js54 P6 regression: extends P5 dispatch-mode gating in three directions.
//   1. Runtime helpers js_typed_array_fill / set_from / slice gated on
//      js_dispatch_as_array_method (P5 only covered js_map_method).
//   2. Length-tracking views over resizable buffers no longer reject when the
//      buffer byteLength isn't a multiple of BYTES_PER_ELEMENT — per ES2024
//      §10.4.5.5 the alignment check is only required for non-resizable
//      buffers; resizable+auto-tracking floors.
//   3. Array-mode iteration methods (forEach/reduce/find/every/some/findLast/
//      findLastIndex/reduceRight/filter/map) skip indices >= current length
//      (post-resize OOB) — spec HasProperty returns false there.
//   4. ArrayBuffer.prototype.resize moves the detached-buffer check to AFTER
//      ToIntegerOrInfinity coercion.

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
const fill_result = Array.prototype.fill.call(ta_oob2, 99);
assertEq(fill_result, ta_oob2, "Array.fill returns receiver");
const slice_result = Array.prototype.slice.call(ta_oob2);
assertEq(slice_result.length, 0, "Array.slice on OOB-TA → empty");

// === Length-tracking view over non-aligned resizable buffer ===
const rab_unaligned = new ArrayBuffer(10, {maxByteLength: 20});
const ta_unaligned = new Float64Array(rab_unaligned);  // 10 / 8 = 1 element (floor)
assertEq(ta_unaligned.length, 1, "Float64Array auto-length over 10-byte buffer");

// === Healthy TA still works (sanity) ===
const ta_ok = new Int32Array([1, 2, 3, 4]);
const sliced = Array.prototype.slice.call(ta_ok, 1, 3);
assertEq(sliced.length, 2, "Array.slice healthy length");
assertEq(sliced[0], 2, "Array.slice healthy elem 0");
assertEq(sliced[1], 3, "Array.slice healthy elem 1");

// === ArrayBuffer.prototype.resize coerces BEFORE detach check ===
// valueOf must be called even when the buffer was already detached at entry.
const rab_pre_detached = new ArrayBuffer(64, {maxByteLength: 1024});
$262.detachArrayBuffer(rab_pre_detached);
let valueofCalled = false;
assertThrows(() => rab_pre_detached.resize({ valueOf() { valueofCalled = true; return 32; } }),
    "TypeError", "resize on already-detached throws");
assertEq(valueofCalled, true, "resize calls valueOf BEFORE detach check");

console.log("Js54 P6 OOB-gating + length-tracking + resize regression: all assertions passed");
