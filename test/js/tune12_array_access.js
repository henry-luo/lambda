// T12-5: parameter/local indexed accesses select guarded physical reads while
// incompatible receivers and indexed-property overrides retain ordinary Get.
function tagged_total(values) {
    let total = 0;
    for (let index = 0; index < values.length; index++) {
        total = total + values[index];
    }
    return total;
}

function first_value(values, index) {
    return values[index];
}

function overwrite_existing(values) {
    values[0] = 7;
    values[1] = 8;
    return values[0] + values[1];
}

function local_typed_total() {
    const values = new Float64Array([1.5, 2.5]);
    return values[0] + values[1];
}

var packed = [1, "skip", 3];
var typed = new Uint8Array([4, 5]);
var hole = new Array(1);
Object.prototype[0] = "inherited";

console.log("packed:" + first_value(packed, 0) + "," +
    first_value(packed, 2));
console.log("typed:" + first_value(typed, 1));
console.log("fallback:" + first_value(hole, 0) + "," +
    first_value(packed, -1));
delete Object.prototype[0];
console.log("total:" + tagged_total([1, 2, 3]));
console.log("local-typed:" + local_typed_total());
Object.preventExtensions(packed);
console.log("overwrite:" + overwrite_existing(packed));
