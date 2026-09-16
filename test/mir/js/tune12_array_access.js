// T12-5: a parameter receiver is a guarded packed-array candidate rather than
// literal-only provenance. The miss invokes the existing numeric Get helper.
function repeated_first(values) {
    return values[0] + values[0];
}

function local_typed_total() {
    const values = new Uint8Array([4, 5]);
    return values[0] + values[1];
}

function overwrite_existing(values) {
    values[0] = 7;
    values[1] = 8;
    return values[0] + values[1];
}

var packed = [1, "two"];
var hole = new Array(1);
Object.prototype[0] = "inherited";
if (repeated_first(packed) !== 2 || repeated_first(hole) !== "inheritedinherited" ||
        local_typed_total() !== 9) {
    throw new Error("T12 parameter array access changed semantics");
}
delete Object.prototype[0];
Object.preventExtensions(packed);
if (overwrite_existing(packed) !== 15) {
    throw new Error("T12 existing array store changed semantics");
}
console.log("tune12-array-access-ok");
