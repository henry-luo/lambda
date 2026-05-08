var ab = new ArrayBuffer(8);
ab.label = "buffer";
console.log("ab label: " + ab.label);
console.log("ab byteLength: " + ab.byteLength);
console.log("ab has label: " + Object.prototype.hasOwnProperty.call(ab, "label"));

var ta = new Uint8Array(3);
ta[0] = 7;
ta.extra = "typed";
console.log("ta extra: " + ta.extra);
console.log("ta length: " + ta.length);
console.log("ta byteLength: " + ta.byteLength);
console.log("ta first: " + ta[0]);
console.log("ta has extra: " + Object.prototype.hasOwnProperty.call(ta, "extra"));

var dv = new DataView(ab);
dv.note = "view";
dv.setUint8(0, 33);
console.log("dv note: " + dv.note);
console.log("dv byteLength: " + dv.byteLength);
console.log("dv get: " + dv.getUint8(0));
console.log("ab through dv: " + new Uint8Array(ab)[0]);
