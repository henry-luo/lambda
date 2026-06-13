// Js54 P8 — Gate B arraybuffer-transfer regression coverage.
// Exercises ArrayBuffer.prototype.transfer and transferToFixedLength:
//   1. transfer() with default newLength copies bytes, detaches source.
//   2. transfer(newLength) truncates/zero-pads correctly.
//   3. transferToFixedLength produces non-resizable buffer regardless of source.
//   4. transfer of resizable source preserves resizable + maxByteLength.
//   5. Operations on detached source throw TypeError.
//   6. Double-transfer throws TypeError.

function assertEq(a, b, msg) {
    if (a !== b) throw new Error("assert: " + msg + " expected " + b + " got " + a);
}
function assertThrows(name, fn, msg) {
    try { fn(); } catch (e) {
        if (e && e.name === name) return;
        throw new Error("assert: " + msg + " expected " + name + " got " + (e && e.name));
    }
    throw new Error("assert: " + msg + " expected " + name + " to be thrown");
}

// Case 1 — transfer() default newLength
var src1 = new ArrayBuffer(4);
var v1 = new Uint8Array(src1);
v1[0] = 10; v1[1] = 20; v1[2] = 30; v1[3] = 40;
var dst1 = src1.transfer();
assertEq(dst1.byteLength, 4, "case1: dst byteLength");
assertEq(src1.byteLength, 0, "case1: src detached byteLength");
assertEq(dst1.resizable, false, "case1: dst non-resizable");
var dv1 = new Uint8Array(dst1);
assertEq(dv1[0], 10, "case1: byte 0");
assertEq(dv1[3], 40, "case1: byte 3");

// Case 2 — transfer(newLength) truncate
var src2 = new ArrayBuffer(8);
var v2 = new Uint8Array(src2);
v2[0] = 1; v2[1] = 2; v2[2] = 3; v2[3] = 4;
v2[4] = 5; v2[5] = 6; v2[6] = 7; v2[7] = 8;
var dst2 = src2.transfer(4);
assertEq(dst2.byteLength, 4, "case2: truncated byteLength");
var dv2 = new Uint8Array(dst2);
assertEq(dv2[0], 1, "case2: byte 0");
assertEq(dv2[3], 4, "case2: byte 3");

// Case 3 — transferToFixedLength flattens resizable
var src3 = new ArrayBuffer(4, {maxByteLength: 16});
var v3 = new Uint8Array(src3);
v3[0] = 100;
var dst3 = src3.transferToFixedLength(8);
assertEq(dst3.byteLength, 8, "case3: dst byteLength");
assertEq(dst3.resizable, false, "case3: dst non-resizable");
assertEq(dst3.maxByteLength, 8, "case3: dst maxByteLength == byteLength");
var dv3 = new Uint8Array(dst3);
assertEq(dv3[0], 100, "case3: byte 0 preserved");
assertEq(dv3[4], 0, "case3: zero-extended");

// Case 4 — transfer preserves resizable + maxByteLength
var src4 = new ArrayBuffer(4, {maxByteLength: 32});
var dst4 = src4.transfer(8);
assertEq(dst4.byteLength, 8, "case4: dst byteLength");
assertEq(dst4.resizable, true, "case4: dst resizable");
assertEq(dst4.maxByteLength, 32, "case4: dst maxByteLength preserved");

// Case 5 — operations on detached source throw TypeError
var src5 = new ArrayBuffer(4);
src5.transfer();
assertThrows("TypeError", function() { src5.slice(); }, "case5: detached.slice");
assertThrows("TypeError", function() { src5.transfer(); }, "case5: double transfer");

// Case 6 — transfer of zero-length
var src6 = new ArrayBuffer(0);
var dst6 = src6.transfer();
assertEq(dst6.byteLength, 0, "case6: zero-length");

console.log("regression_js54_p8_arraybuffer_transfer: OK");
