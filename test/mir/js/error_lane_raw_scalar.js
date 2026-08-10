// D8.4.3: native boolean/comparison helpers return raw scalars, not Items.
// Their I64 MIR registers must therefore stay outside the merged error lane.
function rawScalarBranch(value, other) {
    if (value === other) return !value;
    return value === null;
}

console.log(rawScalarBranch(1, 1));
