// Js54 P5 regression: Array.prototype methods called on TA receivers.
// Several methods (every, fill, slice, forEach, ...) share JS_BUILTIN_ARR_*
// between Array.prototype and TypedArray.prototype but spec-diverge on OOB:
// - TypedArray.prototype.X.call(ta_oob): throws TypeError via ValidateTypedArray
// - Array.prototype.X.call(ta_oob): uses LengthOfArrayLike → 0 → silently no-op
// js_call_function / js_invoke_fn set js_dispatch_as_array_method when the
// calling fn lacks JS_FUNC_FLAG_TYPED_ARRAY_METHOD; the per-method OOB-throw
// blocks check the flag and skip the throw in Array-mode.

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

function makeDetachedTA() {
    const ab = new ArrayBuffer(8);
    const ta = new Int32Array(ab);
    $262.detachArrayBuffer(ab);
    return ta;
}

// === TypedArray.prototype.X on OOB TA still throws (P4 behaviour preserved) ===
const ta_oob = makeOobTA();
assertThrows(() => ta_oob.every(() => true), "TypeError", "ta.every direct");
assertThrows(() => ta_oob.forEach(() => {}), "TypeError", "ta.forEach direct");
assertThrows(() => ta_oob.slice(), "TypeError", "ta.slice direct");
assertThrows(() => ta_oob.indexOf(0), "TypeError", "ta.indexOf direct");

// === Array.prototype.X.call(ta_oob, ...) does NOT throw — vacuous result ===
const ta_oob2 = makeOobTA();
assertEq(Array.prototype.every.call(ta_oob2, () => false), true, "Array.every on OOB-TA → vacuous true");
assertEq(Array.prototype.some.call(ta_oob2, () => true), false, "Array.some on OOB-TA → vacuous false");
assertEq(Array.prototype.find.call(ta_oob2, () => true), undefined, "Array.find on OOB-TA → undefined");
assertEq(Array.prototype.findIndex.call(ta_oob2, () => true), -1, "Array.findIndex on OOB-TA → -1");
assertEq(Array.prototype.indexOf.call(ta_oob2, 0), -1, "Array.indexOf on OOB-TA → -1");
assertEq(Array.prototype.lastIndexOf.call(ta_oob2, 0), -1, "Array.lastIndexOf on OOB-TA → -1");
assertEq(Array.prototype.includes.call(ta_oob2, undefined), false, "Array.includes(undefined) on OOB-TA → false");
assertEq(Array.prototype.join.call(ta_oob2, ","), "", "Array.join on OOB-TA → empty");

// === Array.prototype.X.call(ta_detached, ...) does NOT throw ===
const ta_det = makeDetachedTA();
assertEq(Array.prototype.every.call(ta_det, () => false), true, "Array.every on detached → vacuous true");
assertEq(Array.prototype.indexOf.call(ta_det, 0), -1, "Array.indexOf on detached → -1");
assertEq(Array.prototype.join.call(ta_det, ","), "", "Array.join on detached → empty");

// === Healthy TA: Array.prototype methods produce correct results ===
const ta_ok = new Int32Array([1, 2, 3, 4]);
assertEq(ta_ok.every(x => x > 0), true, "TA.every healthy");
assertEq(Array.prototype.every.call(ta_ok, x => x > 0), true, "Array.every.call(TA) healthy");
assertEq(Array.prototype.indexOf.call(ta_ok, 3), 2, "Array.indexOf.call(TA) healthy");
assertEq(Array.prototype.includes.call(ta_ok, 3), true, "Array.includes.call(TA) healthy");

console.log("Js54 P5 Array.prototype on TA regression: all assertions passed");
