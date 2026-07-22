// MT5 fixture: a binding initialized from a user function call must stay boxed.
// Statically inferring INT/FLOAT for a CALL_EXPRESSION result and emitting an
// unchecked item-to-double unbox on it read an object as a double and produced
// 0 (Stage 4C LambdaJS bug #3). The binding below is initialized from a user
// function that returns an object, so no it2d/d2i may be emitted on the result.
// Checked by numeric_inference_call.mir-check.

function makeBox(v) { return { value: v }; }

const boxed = makeBox(21);
console.log(boxed.value * 2);
