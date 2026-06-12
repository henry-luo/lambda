// Js54 P3 regression: TypedArray length-tracking + OOB indexed access on
// resizable-buffer-backed views. The MIR JIT inline indexed get/set fast paths
// used to read the cached ta->data; after ArrayBuffer.prototype.resize()
// reallocs ab->data, that pointer is stale (use-after-free of the old
// allocation). Fix: route the data pointer through
// js_typed_array_current_data_ptr, which returns ab->data + byte_offset live,
// or NULL for OOB / detached.

function assertEq(actual, expected, label) {
    if (actual !== expected) throw new Error(label + ": got " + actual + " expected " + expected);
}

// length-tracking TA: length updates on grow
const rab1 = new ArrayBuffer(16, {maxByteLength: 40});
const ta1 = new Int32Array(rab1);  // length-tracking, 4 elements (16/4)
assertEq(ta1.length, 4, "ta1 initial length");
ta1[0] = 100; ta1[3] = 999;
assertEq(ta1[0], 100, "ta1[0] initial");
assertEq(ta1[3], 999, "ta1[3] initial");
rab1.resize(32);
assertEq(ta1.length, 8, "ta1 length after grow");
assertEq(ta1[0], 100, "ta1[0] survived grow");
assertEq(ta1[3], 999, "ta1[3] survived grow");
ta1[7] = 77;
assertEq(ta1[7], 77, "ta1[7] write/read after grow");

// length-tracking TA: length updates on shrink, OOB reads return undefined
rab1.resize(8);
assertEq(ta1.length, 2, "ta1 length after shrink");
assertEq(ta1[0], 100, "ta1[0] survived shrink");
assertEq(ta1[3], undefined, "ta1[3] OOB after shrink → undefined");
assertEq(ta1[7], undefined, "ta1[7] OOB after shrink → undefined");
// write past OOB silently no-ops
ta1[3] = 5;
assertEq(ta1[3], undefined, "ta1[3] OOB write is no-op");

// resize-to-zero: all indexed reads return undefined (the original SIGSEGV case)
rab1.resize(0);
assertEq(ta1.length, 0, "ta1 length after resize-to-zero");
assertEq(ta1[0], undefined, "ta1[0] after resize-to-zero");
assertEq(0 in ta1, false, "0 in ta1 after resize-to-zero");

// fixed-length TA: length stays constant, OOB after shrink returns undefined
const rab2 = new ArrayBuffer(16, {maxByteLength: 40});
const ta2 = new Int8Array(rab2, 0, 8);  // fixed length 8
assertEq(ta2.length, 8, "ta2 initial length");
ta2[5] = 50;
assertEq(ta2[5], 50, "ta2[5] initial");
rab2.resize(4);  // view end (8) > buffer end (4) — OOB
assertEq(ta2.length, 0, "ta2 OOB length === 0");
assertEq(ta2[0], undefined, "ta2[0] OOB read");
assertEq(0 in ta2, false, "0 in OOB ta2 is false");

// detached buffer: indexed reads return undefined
const ab3 = new ArrayBuffer(8);
const ta3 = new Int8Array(ab3);
ta3[0] = 42;
assertEq(ta3[0], 42, "ta3[0] before detach");
$262.detachArrayBuffer(ab3);
assertEq(ta3.length, 0, "ta3 length after detach");
assertEq(ta3[0], undefined, "ta3[0] after detach");
assertEq(0 in ta3, false, "0 in detached ta3 is false");

// Across-grow then back-to-original-size: data survives because ab->data realloc
// preserved bytes via memcpy in js_arraybuffer_resize. Important: the JIT must
// re-read the data pointer each access, not use a stale snapshot.
const rab4 = new ArrayBuffer(8, {maxByteLength: 40});
const ta4 = new Int32Array(rab4);  // 2 elements
ta4[0] = 11; ta4[1] = 22;
rab4.resize(16);  // ab->data realloc — old ta->data cache is now stale
ta4[2] = 33; ta4[3] = 44;
// If the JIT cached ta->data pointer, this read would return wrong values
assertEq(ta4[0], 11, "ta4[0] after grow (was old slot)");
assertEq(ta4[1], 22, "ta4[1] after grow");
assertEq(ta4[2], 33, "ta4[2] after grow (new slot)");
assertEq(ta4[3], 44, "ta4[3] after grow");

console.log("Js54 P3 TypedArray length-tracking regression: all assertions passed");
