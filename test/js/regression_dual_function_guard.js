// An inferred native JS function must retain source behavior outside its
// numeric fast path, for direct calls and calls through a Function value.
function plusOne(value) { return value + 1; }

const indirect = plusOne;
console.log(plusOne(2.5));
console.log(plusOne("x"));
console.log(indirect(2.5));
console.log(indirect("x"));
