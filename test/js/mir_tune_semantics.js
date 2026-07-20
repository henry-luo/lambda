// Semantic guards for MIR tuning transformations. This fixture is deliberately
// separate from the fixed-shape structural profiling corpus.
function tuneBindings(a) {
    let first = a;
    const second = first;
    let third = second;
    third = first;
    return third;
}
if (tuneBindings({value: 2}).value !== 2) throw new Error("binding assignment");

let tdzCaught = false;
try {
    switch (1) {
    case 1: tdzLater = 1; break;
    case 0: let tdzLater;
    }
} catch (error) {
    tdzCaught = error instanceof ReferenceError;
}
if (!tdzCaught) throw new Error("switch TDZ guard");

const staticRecord = {alpha: 1, beta: "two", gamma: {ok: true}};
if (staticRecord.alpha !== 1 || staticRecord.beta !== "two" ||
    staticRecord.gamma.ok !== true) throw new Error("static object shape");
const duplicateRecord = {value: 1, value: 2};
if (duplicateRecord.value !== 2) throw new Error("duplicate object fallback");
const proto = {inherited: 7};
const protoRecord = {__proto__: proto, own: 8};
if (protoRecord.inherited !== 7 || protoRecord.own !== 8 ||
    Object.prototype.hasOwnProperty.call(protoRecord, "__proto__")) {
    throw new Error("object prototype fallback");
}
let keyOrder = 0;
const computedRecord = {[++keyOrder]: ++keyOrder};
if (keyOrder !== 2 || computedRecord[1] !== 2) throw new Error("computed key order");

const tuneBuffer = Buffer.allocUnsafe(10);
const tuneNestedSlices = [2].map(function(size) {
    return tuneBuffer.slice(0, size);
});
if (!Buffer.isBuffer(tuneNestedSlices[0]) || tuneNestedSlices[0].length !== 2) {
    throw new Error("nested Array builtin must not change Buffer species");
}

function tuneMetadata(first, second = 2) { return first + second; }
if (tuneMetadata.length !== 1 || tuneMetadata.name !== "tuneMetadata" ||
    !Function.prototype.toString.call(tuneMetadata).includes("tuneMetadata")) {
    throw new Error("function metadata");
}
function tuneSloppyThis() { return this; }
if (tuneSloppyThis() !== globalThis) throw new Error("direct sloppy this");
function tuneNewTarget() { return new.target; }
if (tuneNewTarget() !== undefined) throw new Error("direct new.target");

function tuneUndefinedShadow(undefined) { return undefined; }
if (tuneUndefinedShadow(17) !== 17) throw new Error("undefined parameter shadow");
with ({undefined: 23}) {
    if (undefined !== 23) throw new Error("undefined with resolution");
}
