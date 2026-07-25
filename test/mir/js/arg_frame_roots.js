// MT5 fixture: a dynamic call must use fixed slots in its generated side-root
// frame without reintroducing per-call argument-stack helpers.
// Checked by arg_frame_roots.mir-check.

function callDynamic(fn, value) {
    return fn(value);
}

function identity(value) {
    return value;
}

console.log(callDynamic(identity, {value: 42}).value);
